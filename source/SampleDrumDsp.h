//------------------------------------------------------------------------
// SampleDrum - the audio line
//
// NO SDK HEADER MAY ENTER THIS FILE OR ITS .cpp. That is deliberate, and
// there are two reasons for it.
//
//   1. It compiles and runs standalone with plain `c++ -std=c++17`, so
//      its numbers can be tested before anything is built - which
//      matters on this project, because the session that writes the code
//      reaches the Mac through a Linux VM and cannot run cmake, Xcode,
//      the SDK validator or auval at all. tests/DspTests.cpp is the only
//      thing here that actually executes during development.
//
//   2. VST3 splits the processor from the controller, and they do not
//      share memory. Anything the panel displays that the DSP computes
//      must come from ONE SHARED FUNCTION that both call. Those live here
//      (dbToGain) and in SampleDrumKit.h (which cell a note plays).
//
// WHAT IT DOES: 32 cells of samples, 8 drums by 4 velocity bands. A note
// arrives with a sample offset; the kit's rules pick a cell; a voice
// plays that cell's sample from the start to the end, once, at unity
// gain, resampled linearly to the host's rate. Voices sum, pass through
// the smoothed output trim, and that is the instrument.
//
// THE THREADING, which is the part to understand before changing
// anything here:
//
//   * The UI thread OWNS every SampleBuffer (the processor holds them in
//     shared_ptrs) and publishes each cell's to this class with
//     setCellSample(), a RELEASE store of a bare pointer.
//   * The audio thread reads that pointer with an ACQUIRE load when a
//     voice STARTS, and the voice then keeps it.
//   * A replaced buffer is retired by the processor and freed only once
//     two further blocks have begun. A voice still holding it would be
//     reading freed memory, so at the top of EVERY process() call -
//     including the parameter-only ones - syncCells() finds each voice
//     whose cell has changed under it and ends it inside that block.
//     That is the whole contract; see syncCells().
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumKit.h"
#include "SampleDrumSample.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SampleDrum {

//------------------------------------------------------------------------
/** Stereo, and only stereo. It agrees with setBusArrangements in the
    processor and with the single 0-in / 2-out entry in
    resource/au-info.plist; all three have to say the same thing or auval
    rejects the Audio Unit. */
constexpr int kChannelCount = 2;

/** How long the output trim takes to reach a new value, in seconds.

    IT IS NOT A TASTE SETTING. It is what keeps a parameter move from
    clicking, and the tests assert that no single sample steps the gain
    by more than 1 %. Shorten it and re-run the suite. */
constexpr double kTrimSmoothingSeconds = 0.020;

/** The floor below which the smoother's own state is flushed to zero.
    Only the smoother, never the signal - see Smoother::next. */
constexpr float kDenormalFloor = 1e-25f;

/** How many samples can sound at once, across the whole kit.

    32 is a cymbal's tail under a fast hat pattern with room to spare. A
    hit that finds the pool full STEALS THE OLDEST voice, with a
    kChokeFadeSeconds fade rather than a hard cut. */
constexpr int kMaxVoices = 32;

/** The voice pool is LARGER than kMaxVoices by this many, and the spare
    slots are what let a stolen voice FADE rather than cut: the hit that
    steals takes a spare slot while the victim spends 5 ms leaving. Only
    when even the spares are full is anything cut hard. */
constexpr int kSpareVoices = 8;

/** How many note-ons one block may carry. A bound, so the queue is a
    fixed array and the audio thread never allocates; a block with more
    than this - a MIDI flood - drops the excess rather than growing. */
constexpr int kMaxEventsPerBlock = 256;

/** The fade applied when a sound has to stop early, in seconds.

    THREE CASES, one length: the closed hat CHOKING the open one, a voice
    STOLEN when the pool is full, and a voice whose cell was REPLACED
    while it played. Short enough to read as "stopped", long enough not
    to click. The choke is the one that is heard as a musical event, and
    5 ms is roughly what a real hat's pedal takes. */
constexpr double kChokeFadeSeconds = 0.005;

//------------------------------------------------------------------------
// The shared functions
//------------------------------------------------------------------------

/** Decibels to a linear gain. Exactly 1.0 at 0 dB, which is what makes
    the default patch bit-identical rather than nearly so. */
inline double dbToGain (double db)
{
	return (db == 0.0) ? 1.0 : std::pow (10.0, db / 20.0);
}

//------------------------------------------------------------------------
// A cell's own level
//
// -40 to +12 dB, Project6's slot-level range, and for its reason: this is
// a COMPONENT level inside a kit, not the master. A quiet sample needs a
// way up, and the stage that answers clipping is the Output Trim.
//
// THE BOTTOM OF TRAVEL IS SILENCE, not -40 dB of leakage: a bar pulled
// all the way down is off. cellLevelGain() is the one place that says so;
// the DSP scales by it and the panel reads "off" from the same test.
//------------------------------------------------------------------------
constexpr double kCellLevelMinDb     = -40.0;
constexpr double kCellLevelMaxDb     =  12.0;
constexpr double kCellLevelDefaultDb =   0.0;

/** How long a cell's level takes to follow its bar - the same as the
    trim, and for the same reason: no zipper noise on a sample that is
    ringing while the bar moves. */
constexpr double kCellLevelSmoothingSeconds = kTrimSmoothingSeconds;

/** And back, with a floor so silence does not return -inf. */
inline double gainToDb (double gain)
{
	return (gain <= 1e-9) ? -180.0 : 20.0 * std::log10 (gain);
}

/** A cell level in dB to the gain applied: dbToGain, clamped to the
    range, and EXACTLY ZERO at the bottom of it. */
inline double cellLevelGain (double db)
{
	if (!(db > kCellLevelMinDb))
		return 0.0;
	return dbToGain (std::min (kCellLevelMaxDb, db));
}

//------------------------------------------------------------------------
/** A one-pole glide towards a target, its coefficient recomputed from the
    sample rate so the glide takes the same MILLISECONDS at every rate. */
class Smoother
{
public:
	void setSampleRate (double sampleRate, double seconds);
	void snap (float target);
	void setTarget (float target) { mTarget = target; }
	float target () const { return mTarget; }
	float value () const { return mValue; }
	float next ();
	float coefficient () const { return mCoeff; }

private:
	float mValue  = 0.f;
	float mTarget = 0.f;
	float mCoeff  = 1.f;
};

//------------------------------------------------------------------------
class SampleDrumDsp
{
public:
	SampleDrumDsp ();

	/** Recomputes EVERY rate-dependent value and stops every voice. From
	    setupProcessing, never from process(). */
	void setSampleRate (double sampleRate);
	double sampleRate () const { return mSampleRate; }

	/** The largest block the host will ask for. From setupProcessing
	    only; kept for symmetry with the other projects - the voices
	    render straight into the caller's buffer and need no scratch. */
	void setMaxBlockSize (int maxSamples);
	int maxBlockSize () const { return mMaxBlockSize; }

	/** Stop every voice, forget queued hits, snap the trim. */
	void reset ();

	/** The output trim, in dB. Smoothed. */
	void setOutputTrimDb (double db);
	double outputTrimDb () const { return mTrimDb; }

	/** A cell's level, in dB (the parameter's plain value). Applies at
	    once to anything that cell is playing, smoothed. */
	void setCellLevelDb (int cell, double db);
	double cellLevelDb (int cell) const;

	/** The gain the cell is at right now, after smoothing - for tests. */
	float currentCellGain (int cell) const;

	/** The MIDI note that plays a column. Defaults to kDrumMap. Several
	    columns may share a note, and then ONE HIT PLAYS ALL OF THEM -
	    layering two drums is a thing people do on purpose, and the
	    alternative (only the leftmost plays) would be a column that
	    silently stops working when its note is duplicated. Out-of-range
	    notes are clamped to 0..127. */
	void setColumnNote (int column, int note);
	int columnNote (int column) const;

	//--------------------------------------------------------------------
	// Samples - UI THREAD
	//--------------------------------------------------------------------

	/** Publish a cell's sample, or nullptr to empty it.

	    UI THREAD. The buffer is owned by the caller and must stay alive
	    until two further blocks have begun after it is replaced - the
	    processor's retire list is what guarantees that. */
	void setCellSample (int cell, const SampleBuffer* sample);

	//--------------------------------------------------------------------
	// Playing - AUDIO THREAD
	//--------------------------------------------------------------------

	/** CALL FIRST IN EVERY process(), including when numSamples is 0.

	    Every voice whose cell now holds a DIFFERENT buffer from the one
	    it started with is ended INSIDE THIS BLOCK: faded over the block
	    if there is one, stopped outright if numSamples is 0. By the time
	    the processor frees the old buffer, two blocks later, nothing can
	    be holding it. Skip this on a parameter-only block and a voice
	    carries a dangling pointer into the next. */
	void syncCells (int numSamples);

	/** A MIDI note-on, `sampleOffset` samples into the coming block.

	    Only QUEUED; render() starts it at exactly that sample. A voice
	    started at offset 0 regardless is the classic timing bug, audible
	    as every hit quantised to the block size. Notes not in the drum
	    map and velocity 0 are ignored here. */
	void noteOn (int note, float velocity01, int sampleOffset);

	/** Play one cell directly, at the start of the coming block, for the
	    panel's audition click. Ignores the velocity bands and fallback -
	    the user clicked THIS cell. */
	void auditionCell (int cell);

	/** Render the block: clear it, then the queued hits and every voice,
	    then the trim. Tolerates numSamples <= 0 and null pointers. */
	void render (float* left, float* right, int numSamples);

	/** The trim stage on its own, in place - exposed for the tests. At
	    0 dB with the smoother settled it is bit-identical. */
	void applyTrim (float* left, float* right, int numSamples);

	float currentTrimGain () const { return mTrim.value (); }

	//--------------------------------------------------------------------
	// For the panel's lamps and for the tests
	//--------------------------------------------------------------------

	/** How many times this cell has been STRUCK, ever. Written by the
	    audio thread, read by the UI thread with a relaxed load: the panel
	    lights a cell when the count moves, so a hit shorter than one
	    timer tick still flashes. */
	std::uint32_t cellHits (int cell) const;

	/** True while a voice is playing this cell, as of the last block. */
	bool cellSounding (int cell) const;

	int activeVoices () const;

	/** Which cell the kit's rules would play for this note and velocity
	    RIGHT NOW in the LEFTMOST column with that note, or -1. The same
	    rules noteOn uses, exposed so the tests assert the rule rather
	    than a copy of it. */
	int cellForNote (int note, float velocity01) const;

	/** Which columns a note plays right now, one bit per column. */
	unsigned int columnsForNote (int note) const;

private:
	struct Voice
	{
		const SampleBuffer* sample = nullptr;   // null = free
		int    cell     = -1;
		double position = 0.0;                  // in source frames
		double step     = 1.0;                  // source frames per output sample
		std::uint64_t age = 0;                  // start order, for stealing

		// Early stop: when fadeLeft > 0 the voice ramps to silence over
		// fadeLeft samples and is freed at the end of it.
		int   fadeLeft  = 0;
		float fadeGain  = 1.f;
		float fadeStep  = 0.f;

		bool active () const { return sample != nullptr; }
	};

	struct PendingHit
	{
		int offset = 0;
		int cell   = -1;
		int column = -1;
	};

	void startVoice (int cell, int column);
	void queueHit (int offset, int cell, int column);
	void beginFade (Voice& voice, int samples);
	void renderVoices (float* left, float* right, int from, int to);
	void advanceCellGains (int numSamples);
	unsigned int loadedMask () const;

	double mSampleRate   = 44100.0;
	int    mMaxBlockSize = 0;
	double mTrimDb       = 0.0;
	int    mFadeSamples  = 220;

	Smoother mTrim;

	/** Per-cell level. Advanced ONCE PER BLOCK in closed form, and every
	    voice on the cell ramps linearly from mCellGainStart to
	    mCellGainEnd across the block - so two voices on one cell see the
	    same gain, and the smoother is not stepped once per voice. */
	double mCellLevelDb[kCellCount];
	float  mCellTarget[kCellCount];
	float  mCellGain[kCellCount];
	float  mCellGainStart[kCellCount];
	float  mCellGainEnd[kCellCount];
	double mCellLevelCoeff = 1.0;   // per-sample one-pole coefficient
	int    mBlockLength = 0;        // of the block render() is in

	/** Each column's note. Plain ints: written by applyParam on the audio
	    thread (and by setState, whose worst case is one hit read against
	    the old note). */
	int mColumnNote[kColumns];

	/** What the audio thread reads. Release-stored by setCellSample,
	    acquire-loaded when a voice starts. */
	std::atomic<const SampleBuffer*> mCells[kCellCount];

	Voice mVoices[kMaxVoices + kSpareVoices];
	std::uint64_t mNextAge = 0;

	PendingHit mPending[kMaxEventsPerBlock];
	int mPendingCount = 0;

	std::atomic<std::uint32_t> mHits[kCellCount];
	std::atomic<bool>          mSounding[kCellCount];
};

//------------------------------------------------------------------------
} // namespace SampleDrum
