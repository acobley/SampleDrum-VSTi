//------------------------------------------------------------------------
// SampleDrum - the audio line, implementation
//
// SDK-FREE. If a #include of anything under pluginterfaces/ or
// public.sdk/ ever appears in this file, the standalone test build stops
// compiling and the only executable verification on this project goes
// with it. See the banner in SampleDrumDsp.h.
//------------------------------------------------------------------------

#include "SampleDrumDsp.h"

#include <algorithm>

namespace SampleDrum {

//------------------------------------------------------------------------
void Smoother::setSampleRate (double sampleRate, double seconds)
{
	if (sampleRate < 1000.0)
		sampleRate = 44100.0;

	if (seconds <= 0.0)
	{
		// No glide asked for: follow the target exactly. Not a
		// degenerate case to guard against elsewhere - it is how a
		// parameter that must NOT be smoothed is expressed.
		mCoeff = 1.f;
		return;
	}

	// 1 - exp(-1/(t*fs)): the one-pole coefficient for a time constant of
	// t seconds. Computed in double and stored in float, because the
	// numbers at 192 k are small enough that the difference shows.
	mCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
	mCoeff = std::min (1.f, std::max (0.f, mCoeff));
}

//------------------------------------------------------------------------
void Smoother::snap (float target)
{
	mTarget = target;
	mValue  = target;
}

//------------------------------------------------------------------------
float Smoother::next ()
{
	// The order matters. The value returned is the one AFTER this
	// sample's step, so a target set and then immediately rendered with
	// coefficient 1 gives the new value on the very first sample rather
	// than one sample late.
	mValue += (mTarget - mValue) * mCoeff;

	// Denormal flush on the recursive state only - see kDenormalFloor.
	// The test that unity is bit-identical depends on this NOT touching
	// the signal.
	if (mValue < kDenormalFloor && mValue > -kDenormalFloor)
		mValue = 0.f;

	return mValue;
}

//------------------------------------------------------------------------
SampleDrumDsp::SampleDrumDsp ()
{
	// The constructor must leave the object usable, because a host may
	// call process() before setupProcessing in the rare restart. 44.1 k
	// is a guess; setSampleRate replaces it with the truth.
	setSampleRate (44100.0);
}

//------------------------------------------------------------------------
void SampleDrumDsp::setSampleRate (double sampleRate)
{
	if (sampleRate < 1000.0)
		sampleRate = 44100.0;

	mSampleRate = sampleRate;

	// EVERY rate-dependent coefficient, recomputed here. There is one so
	// far. Add to this function and nowhere else, and the 96 k bug never
	// gets written.
	mTrim.setSampleRate (mSampleRate, kTrimSmoothingSeconds);

	reset ();
}

//------------------------------------------------------------------------
void SampleDrumDsp::setMaxBlockSize (int maxSamples)
{
	mMaxBlockSize = (maxSamples > 0) ? maxSamples : 0;

	// THE ONLY ALLOCATION IN THIS CLASS, and it is not on the audio
	// thread. assign() rather than resize() so the contents are known.
	mScratch.assign (static_cast<size_t> (mMaxBlockSize) * kChannelCount, 0.f);
}

//------------------------------------------------------------------------
void SampleDrumDsp::reset ()
{
	// SNAP, not glide. A reset that left the trim smoother at zero would
	// fade the instrument in over 20 ms every time the host stopped and
	// started, which reads as a missing first hit.
	mTrim.snap (static_cast<float> (dbToGain (mTrimDb)));

	std::fill (mScratch.begin (), mScratch.end (), 0.f);
}

//------------------------------------------------------------------------
void SampleDrumDsp::setOutputTrimDb (double db)
{
	mTrimDb = db;
	mTrim.setTarget (static_cast<float> (dbToGain (db)));
}

//------------------------------------------------------------------------
void SampleDrumDsp::renderVoices (float* /*left*/, float* /*right*/, int /*numSamples*/)
{
	// EMPTY ON PURPOSE. The block arrives zeroed; silence is what
	// summing no voices into it gives, so there is nothing to write.
	//
	// SAMPLE DATA IS NEVER LOADED HERE. Reading a file, decoding it or
	// resizing a buffer on the audio thread is an allocation and a disk
	// wait inside the host's deadline. Load on the UI thread into a
	// buffer that is then handed over whole; this function only reads.
	//
	// This is the function the voices go in. It is called with
	// the block already cleared and with numSamples > 0 and both
	// pointers non-null - render() has checked - so a voice loop here
	// needs no guards of its own.
}

//------------------------------------------------------------------------
void SampleDrumDsp::render (float* left, float* right, int numSamples)
{
	// A PARAMETER-ONLY BLOCK is legal and arrives in practice: hosts send
	// numSamples == 0 to deliver automation between audible blocks. The
	// guard belongs here, once, rather than in every voice.
	if (numSamples <= 0 || left == nullptr || right == nullptr)
		return;

	std::fill (left, left + numSamples, 0.f);
	std::fill (right, right + numSamples, 0.f);

	renderVoices (left, right, numSamples);

	applyTrim (left, right, numSamples);
}

//------------------------------------------------------------------------
void SampleDrumDsp::applyTrim (float* left, float* right, int numSamples)
{
	if (numSamples <= 0 || left == nullptr || right == nullptr)
		return;

	// PER SAMPLE, and both channels off the SAME gain value - a stereo
	// trim that advanced its smoother once per channel would walk the
	// image while it glided.
	for (int i = 0; i < numSamples; ++i)
	{
		const float gain = mTrim.next ();
		left[i]  *= gain;
		right[i] *= gain;
	}
}

//------------------------------------------------------------------------
} // namespace SampleDrum
