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
// the output isolator (bass and treble, see Isolator) and the smoothed
// output trim, and that is the instrument.
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
/** THE BAND-LIMITED READ, for a sample whose rate is ABOVE the session's.

    A voice reads its file at step = sourceRate / session rate, and linear
    interpolation does nothing against aliasing when that is above 1: a
    96 kHz file in a 48 kHz session folded everything between 24 and
    48 kHz in the recording straight down at nearly full level - a 30 kHz
    component came out at 18 kHz at 0 dB (AUDIT-denormals-aliasing.md).

    So above a step of 1 a voice reads through a Kaiser-windowed sinc
    whose cutoff is kSincCutoff x the SESSION's Nyquist, from a polyphase
    bank: kSincPhases + 1 rows of `taps` weights, row p the kernel at a
    read position p / kSincPhases past an integer frame, each row
    normalised to unity DC gain. The same design as Project6's tempo-fit
    read, with a longer kernel (10 zero-crossings, beta 9, against 6 and
    7): a file's rate is fixed, so the bank is built once, and the extra
    taps buy the transition band. Measured, 96 kHz file at 48 kHz: what
    would fold to 18 kHz is at -98 dB, to 16 kHz -93; 19 kHz passes at
    -0.4 dB, 20 kHz -1.1. At a step of 1 or below nothing changes: the
    linear read stays, and a file at the session's rate is still
    bit-identical to itself. */
struct SincBank
{
	static constexpr int    kZeroCrossings = 10;     ///< either side, at the cutoff
	static constexpr double kCutoff        = 0.94;   ///< of the session's Nyquist
	static constexpr double kBeta          = 9.0;    ///< Kaiser
	static constexpr int    kPhases        = 128;
	static constexpr int    kMaxHalfWidth  = 32;     ///< source frames either side, at most
	static constexpr int    kMaxTaps       = 2 * kMaxHalfWidth + 2;

	std::vector<float> weights;   ///< (kPhases + 1) x kMaxTaps, sized off the audio thread
	double ratio = 0.0;           ///< the step this bank was built for; 0 = empty
	int    taps  = 0;
	int    lead  = 0;             ///< taps before the integer read position

	/** Fill the bank for `ratio` (> 1). Allocation-free once weights is
	    sized; about 8 k table lookups. */
	void build (double ratio);

	/** One stereo frame of interleaved `data` at `position`. Taps past
	    either end of the file read as silence: a drum hit is a one-shot. */
	void read (const float* data, int frames, double position, float& left, float& right) const;
};

//------------------------------------------------------------------------
// THE OUTPUT ISOLATOR - a DJ mixer's isolator on the main output
//
// Not a shelving EQ: a THREE-WAY BAND SPLIT whose bands add back to the
// input's magnitude exactly, with a gain on the low and the high band and
// the mid band fixed at unity. The bottom of either gain is a KILL - the
// band is gone, not 40 dB down - so the kick can be pulled out of a kit
// and the hats taken off the top. Kill both and the snare's body is left.
//
//   voices ─► LR4 @ bass Hz ─┬─ low ─► allpass @ treble Hz ─► × bass ───┐
//                            └─ rest ─► LR4 @ treble Hz ─┬─ mid ─► × 1 ─┼─► Σ ─► trim
//                                                        └─ high ─► × treble
//
// LINKWITZ-RILEY 4th ORDER (two Butterworth 2nd-order sections in series)
// because its low and high outputs are IN PHASE and sum to a 2nd-order
// allpass: no bump or hole at the crossover. The low band goes through
// that same allpass at the treble frequency so it stays in phase with the
// mid and high bands, which have been through the second split. The
// whole thing at unity is then (allpass @ bass) x (allpass @ treble) -
// flat in level, whatever the two frequencies are, even crossed.
//
// THE FILTERS ARE TPT STATE-VARIABLE FILTERS, in double. One SVF gives
// low-pass, high-pass and allpass from the same state, and it stays
// stable and click-free while its frequency is swept - the frequencies
// are automatable. Seven SVFs per channel.
//
// AT 0 dB / 0 dB THE OUTPUT IS THE INPUT, BIT FOR BIT. The flat sum is
// still an allpass - right in level, shifted in phase - and this
// project's promise is that a hit at default settings is the file's own
// samples. So while both gains sit at exactly 0 dB the dry signal is
// passed through untouched, and a gain leaving 0 dB crossfades to the
// filtered path over kIsolatorSmoothingSeconds (and back). The filters
// run all the time, so the path faded in is already settled.
//------------------------------------------------------------------------

/** Bass and treble gain: -40 to +6 dB, and THE BOTTOM IS A KILL. +6 is
    what DJ mixers' isolators boost; a boosted bass on a kit that already
    peaks at 0 dBFS will clip, and the trim after it is what answers that. */
constexpr double kIsolatorMinDb     = -40.0;
constexpr double kIsolatorMaxDb     =   6.0;
constexpr double kIsolatorDefaultDb =   0.0;

/** How close to 0 dB counts as 0 dB - see isolatorGain. Far below
    anything audible, far above a float normalised value's rounding. */
constexpr double kIsolatorUnitySnapDb = 1e-3;

/** The two crossover frequencies, in Hz. THE RANGES DO NOT OVERLAP, so
    neither parameter ever has to be clamped against the other - an
    automation lane that silently moved a second parameter would disagree
    with what is heard. (Crossed frequencies would still sum flat; the mid
    band would just vanish.) */
constexpr double kBassHzMin     =    50.0;
constexpr double kBassHzMax     =   500.0;
constexpr double kBassHzDefault =   200.0;   // a kick's body vs. the rest
constexpr double kTrebleHzMin     =  1500.0;
constexpr double kTrebleHzMax     = 12000.0;
constexpr double kTrebleHzDefault =  3500.0; // a snare's body vs. hats and cymbals

/** Gains, frequencies and the dry/filtered crossfade all take this long. */
constexpr double kIsolatorSmoothingSeconds = kTrimSmoothingSeconds;

/** A crossover is never set above this fraction of the session's rate,
    where tan() in the SVF heads for infinity. Only reachable at rates far
    below 44.1 k: 12 kHz is 0.27 of 44.1 k. */
constexpr double kIsolatorMaxHzFraction = 0.45;

/** An isolator gain in dB to the gain applied: EXACTLY ZERO at the bottom
    of the range (the kill), exactly 1 at 0 dB. The DSP, the host text and
    the panel's "kill" readout all ask this one function. */
inline double isolatorGain (double db)
{
	if (!(db > kIsolatorMinDb))
		return 0.0;
	// WITHIN kIsolatorUnitySnapDb OF 0 dB IS 0 dB. The default does not
	// survive the trip through a normalised value exactly - 0 dB on a
	// -40..+6 range comes back as about 7e-15 dB, and a host that stores
	// automation as float as 3e-6 dB - and anything but EXACTLY unity
	// takes the filtered path, which is not bit-identical.
	if (std::fabs (db) < kIsolatorUnitySnapDb)
		return 1.0;
	return dbToGain (std::min (kIsolatorMaxDb, db));
}

/** A LOGARITHMIC range: normalised 0..1 to a frequency, so the middle of
    a frequency knob is the geometric middle of its range, as heard. The
    parameter table and the controller both call these, so the host text,
    the panel and the DSP agree. */
inline double logRangeToPlain (double normalized, double lo, double hi)
{
	const double n = (normalized < 0.0) ? 0.0 : (normalized > 1.0 ? 1.0 : normalized);
	return lo * std::pow (hi / lo, n);
}

inline double logRangeToNormalized (double plain, double lo, double hi)
{
	const double p = (plain < lo) ? lo : (plain > hi ? hi : plain);
	return std::log (p / lo) / std::log (hi / lo);
}

//------------------------------------------------------------------------
class Isolator
{
public:
	Isolator ();

	/** Off the audio thread. Snaps everything. */
	void setSampleRate (double sampleRate);

	/** Snap every smoother to its target and clear the filters. */
	void reset ();

	void setBassDb (double db);
	void setTrebleDb (double db);
	void setBassHz (double hz);
	void setTrebleHz (double hz);

	/** In place. At 0 dB / 0 dB, settled, the block is not touched. */
	void process (float* left, float* right, int numSamples);

	//--------------------------------------------------------------------
	// For the tests
	//--------------------------------------------------------------------
	double bassGain () const { return mBass.value; }
	double trebleGain () const { return mTreble.value; }
	double bassHz () const { return std::exp2 (mBassOct.value); }
	double trebleHz () const { return std::exp2 (mTrebleOct.value); }
	/** 0 = dry, 1 = filtered. */
	double mix () const { return mMix.value; }

	/** TESTS ONLY: take the filtered path even at 0 dB / 0 dB, so the
	    flatness of the band sum itself can be measured. Takes effect
	    with a snap, like reset(). */
	void forceFilteredForTest (bool on) { mForceFiltered = on; reset (); }

private:
	/** One TPT SVF's coefficients (k = sqrt 2: Butterworth). */
	struct Coeffs
	{
		double a1 = 1.0, a2 = 0.0, a3 = 0.0;
	};

	struct SvfState
	{
		double ic1 = 0.0, ic2 = 0.0;
	};

	struct SvfOut
	{
		double lp, hp, ap;
	};

	/** A one-pole glide that ARRIVES: within kSnap of the target it is
	    the target, so 0 dB lands on exactly 1 and a kill on exactly 0. */
	struct Glide
	{
		double value = 0.0, target = 0.0;
		void snap () { value = target; }
		bool settled () const { return value == target; }
		void next (double coeff, double snapWithin);
	};

	static SvfOut tick (const Coeffs& c, SvfState& s, double x);
	Coeffs coeffsFor (double hz) const;
	double clampHz (double hz) const;
	void flushStates ();

	double mSampleRate = 44100.0;
	double mCoeff = 1.0;      // the glides' per-sample one-pole coefficient
	double mMixStep = 1.0;    // the crossfade's per-sample LINEAR step

	Glide mBass, mTreble;     // linear gain
	Glide mBassOct, mTrebleOct;   // log2 of the frequency
	Glide mMix;               // 0 dry .. 1 filtered, linear ramp

	Coeffs mBassCoeffs, mTrebleCoeffs;

	/** Seven SVFs per channel - see the diagram. */
	SvfState mState[2][7];

	bool mForceFiltered = false;
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

	/** The output isolator - see Isolator. Gains in dB (the bottom of
	    the range is a kill), frequencies in Hz. All smoothed. */
	void setBassDb (double db)     { mIsolator.setBassDb (db); }
	void setTrebleDb (double db)   { mIsolator.setTrebleDb (db); }
	void setBassHz (double hz)     { mIsolator.setBassHz (hz); }
	void setTrebleHz (double hz)   { mIsolator.setTrebleHz (hz); }
	const Isolator& isolator () const { return mIsolator; }

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
		int    bank     = -1;                   // mBanks slot when step > 1, else -1
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

	/** Between the voices and the trim. */
	Isolator mIsolator;

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

	/** The band-limited read's banks, one per distinct step above 1. The
	    usual ones (88.2, 96, 176.4 and 192 kHz files) are built by
	    setSampleRate, off the audio thread; any other rate is built when
	    its first voice starts, into a slot no playing voice is using. */
	static constexpr int kBankSlots = 8;
	SincBank mBanks[kBankSlots];
	int bankFor (double step);
	std::uint64_t mNextAge = 0;

	PendingHit mPending[kMaxEventsPerBlock];
	int mPendingCount = 0;

	std::atomic<std::uint32_t> mHits[kCellCount];
	std::atomic<bool>          mSounding[kCellCount];
};

//------------------------------------------------------------------------
} // namespace SampleDrum
