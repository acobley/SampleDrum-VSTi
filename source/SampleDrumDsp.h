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
//      share memory. Anything the EDITOR displays that the DSP COMPUTES
//      must therefore come from ONE SHARED FUNCTION that both call - not
//      from two copies of the arithmetic that will drift apart. This
//      header is where those functions live: see dbToGain() below, which
//      the DSP uses to set its gain and the panel uses to label its
//      readout.
//
// WHAT IT DOES TODAY: nothing but render silence and apply a smoothed
// output trim. SampleDrum is an instrument, so there is no input signal
// to pass through - render() fills the block from the voice mix, and the
// voice mix is currently empty. applyTrim() is the trim stage on its
// own, exposed so the tests can push a known block through it; that is
// the "pass-through" the blank scaffold is tested as.
//
// WHERE THE SAMPLER GOES: renderVoices() is the one empty function.
// Everything else is the frame around it. Before writing a real
// processing loop in there, read ../PORTING-GUIDE.md section 5 - the
// rules about not allocating on the audio thread, denormals, 64-bit
// hosts and resisting the urge to improve the original's gain staging
// all apply to new DSP exactly as they do to a port.
//------------------------------------------------------------------------

#pragma once

#include <cmath>
#include <cstddef>
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
    by more than 1 % - which at the lowest sample rate anyone runs is the
    binding constraint on how short this may be. Shorten it and re-run
    the suite. */
constexpr double kTrimSmoothingSeconds = 0.020;

/** The floor below which the smoother's own state is flushed to zero.

    Only the SMOOTHER, never the signal: the recursive element is the one
    that can decay into denormals and cost a hundred times the cycles to
    keep multiplying, and flushing the signal instead would break
    applyTrim()'s bit-identity at unity for no benefit. */
constexpr float kDenormalFloor = 1e-25f;

//------------------------------------------------------------------------
// The shared functions
//
// Called by the DSP, by the parameter table's text formatting, and by the
// editor's readouts. One copy of the arithmetic, so the number on screen
// and the number in the audio path cannot disagree.
//------------------------------------------------------------------------

/** Decibels to a linear gain. Exactly 1.0 at 0 dB, which is what makes
    the default patch bit-identical rather than nearly so. */
inline double dbToGain (double db)
{
	return (db == 0.0) ? 1.0 : std::pow (10.0, db / 20.0);
}

/** And back, with a floor so silence does not return -inf. */
inline double gainToDb (double gain)
{
	return (gain <= 1e-9) ? -180.0 : 20.0 * std::log10 (gain);
}

//------------------------------------------------------------------------
/** A one-pole glide towards a target.

    Deliberately the simplest thing that cannot click. The coefficient is
    recomputed from the sample rate, so the glide takes the same number
    of MILLISECONDS at 44.1 k and at 192 k rather than the same number of
    samples - a smoother whose coefficient is a constant gets four times
    faster when somebody switches to 192 k, and the click comes back. */
class Smoother
{
public:
	void setSampleRate (double sampleRate, double seconds);

	/** Jump straight to the target, no glide. Used at reset and at every
	    sample-rate change: a smoother left at zero would fade the whole
	    instrument in over 20 ms every time the host restarted it. */
	void snap (float target);

	void setTarget (float target) { mTarget = target; }
	float target () const { return mTarget; }
	float value () const { return mValue; }

	/** One sample of glide, returning the value to use for it. */
	float next ();

	/** The coefficient in use, for the tests. */
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

	/** Recomputes EVERY rate-dependent coefficient and resets state.

	    Every one, with no exceptions and no "it is close enough at 48 k":
	    a coefficient computed once at 44.1 k and then used at 96 k is the
	    bug that presents as "it sounds wrong on his machine only". Call
	    it from setupProcessing, never from process(). */
	void setSampleRate (double sampleRate);
	double sampleRate () const { return mSampleRate; }

	/** The largest block the host will ask for. Sizes the scratch, so
	    that render() never allocates on the audio thread. Also called
	    from setupProcessing only. */
	void setMaxBlockSize (int maxSamples);
	int maxBlockSize () const { return mMaxBlockSize; }

	/** Stop everything and glide from wherever the trim already is. */
	void reset ();

	/** The output trim, in dB - the parameter table's plain value, which
	    for this parameter is also its internal value. Smoothed. */
	void setOutputTrimDb (double db);
	double outputTrimDb () const { return mTrimDb; }

	/** Render one block of the instrument's output into two channels.

	    Tolerates numSamples <= 0 and null pointers, because a host is
	    entitled to hand over a parameter-only block and the processor
	    passes it straight through. */
	void render (float* left, float* right, int numSamples);

	/** The trim stage on its own, applied in place to a block that is
	    already there.

	    Exposed for the tests: at 0 dB with the smoother settled this is
	    bit-identical to its input, and that is the assertion the first
	    real DSP has to keep passing. */
	void applyTrim (float* left, float* right, int numSamples);

	/** The trim's current linear gain, after smoothing. */
	float currentTrimGain () const { return mTrim.value (); }

private:
	/** THE ONE EMPTY FUNCTION. Sum every voice into the block, which is
	    handed over already zeroed. Silence until there is a drum
	    machine in here. */
	void renderVoices (float* left, float* right, int numSamples);

	double mSampleRate   = 44100.0;
	int    mMaxBlockSize = 0;
	double mTrimDb       = 0.0;

	Smoother mTrim;

	/** Sized in setMaxBlockSize and never resized anywhere else. Unused
	    by the silent scaffold; it is here so the first voice that needs
	    somewhere to mix has it, rather than reaching for new[] on the
	    audio thread. */
	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
