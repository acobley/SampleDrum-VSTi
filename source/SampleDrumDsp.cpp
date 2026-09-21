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
// Smoother
//------------------------------------------------------------------------
void Smoother::setSampleRate (double sampleRate, double seconds)
{
	if (sampleRate < 1000.0)
		sampleRate = 44100.0;

	if (seconds <= 0.0)
	{
		mCoeff = 1.f;
		return;
	}

	// 1 - exp(-1/(t*fs)): the one-pole coefficient for a time constant
	// of t seconds, computed in double because at 192 k it is small.
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
	mValue += (mTarget - mValue) * mCoeff;

	// Denormal flush on the recursive state only. The test that unity is
	// bit-identical depends on this NOT touching the signal.
	if (mValue < kDenormalFloor && mValue > -kDenormalFloor)
		mValue = 0.f;

	return mValue;
}

//------------------------------------------------------------------------
// SampleDrumDsp
//------------------------------------------------------------------------
SampleDrumDsp::SampleDrumDsp ()
{
	for (int cell = 0; cell < kCellCount; ++cell)
	{
		mCells[cell].store (nullptr, std::memory_order_relaxed);
		mHits[cell].store (0u, std::memory_order_relaxed);
		mSounding[cell].store (false, std::memory_order_relaxed);

		mCellLevelDb[cell] = kCellLevelDefaultDb;
		mCellTarget[cell] = static_cast<float> (cellLevelGain (kCellLevelDefaultDb));
		mCellGain[cell] = mCellGainStart[cell] = mCellGainEnd[cell] = mCellTarget[cell];
	}

	// Usable before setupProcessing, because a host may call process()
	// first in the rare restart. setSampleRate replaces the guess.
	setSampleRate (44100.0);
}

//------------------------------------------------------------------------
void SampleDrumDsp::setSampleRate (double sampleRate)
{
	if (!(sampleRate >= 1000.0))
		sampleRate = 44100.0;

	mSampleRate = sampleRate;

	// EVERY rate-dependent value, here and nowhere else. The resampling
	// step is not one of them: it is worked out per voice when the voice
	// starts, from this rate, so it cannot go stale.
	mTrim.setSampleRate (mSampleRate, kTrimSmoothingSeconds);
	mFadeSamples = std::max (1, static_cast<int> (kChokeFadeSeconds * mSampleRate + 0.5));
	mCellLevelCoeff = 1.0 - std::exp (-1.0 / (kCellLevelSmoothingSeconds * mSampleRate));

	reset ();
}

//------------------------------------------------------------------------
void SampleDrumDsp::setMaxBlockSize (int maxSamples)
{
	mMaxBlockSize = (maxSamples > 0) ? maxSamples : 0;
}

//------------------------------------------------------------------------
void SampleDrumDsp::reset ()
{
	// SNAP, not glide: a reset that left the trim at zero would fade the
	// kit in over 20 ms every time the host restarted it, which reads as
	// a missing first hit.
	mTrim.snap (static_cast<float> (dbToGain (mTrimDb)));

	for (Voice& voice : mVoices)
		voice = Voice ();
	mPendingCount = 0;

	// Cell levels SNAP too, for the trim's reason.
	for (int cell = 0; cell < kCellCount; ++cell)
		mCellGain[cell] = mCellGainStart[cell] = mCellGainEnd[cell] = mCellTarget[cell];

	for (auto& sounding : mSounding)
		sounding.store (false, std::memory_order_relaxed);
}

//------------------------------------------------------------------------
void SampleDrumDsp::setOutputTrimDb (double db)
{
	mTrimDb = db;
	mTrim.setTarget (static_cast<float> (dbToGain (db)));
}

//------------------------------------------------------------------------
void SampleDrumDsp::setCellLevelDb (int cell, double db)
{
	if (!isCellIndex (cell))
		return;
	mCellLevelDb[cell] = db;
	mCellTarget[cell]  = static_cast<float> (cellLevelGain (db));
}

//------------------------------------------------------------------------
double SampleDrumDsp::cellLevelDb (int cell) const
{
	return isCellIndex (cell) ? mCellLevelDb[cell] : kCellLevelDefaultDb;
}

//------------------------------------------------------------------------
float SampleDrumDsp::currentCellGain (int cell) const
{
	return isCellIndex (cell) ? mCellGain[cell] : 0.f;
}

//------------------------------------------------------------------------
void SampleDrumDsp::advanceCellGains (int numSamples)
{
	// n samples of the one-pole in closed form: the distance left shrinks
	// by (1 - c)^n. Voices then ramp LINEARLY between the two ends, which
	// never steps further per sample than the one-pole itself would.
	const double keep = std::pow (1.0 - mCellLevelCoeff, static_cast<double> (numSamples));

	for (int cell = 0; cell < kCellCount; ++cell)
	{
		const float start  = mCellGain[cell];
		const float target = mCellTarget[cell];
		float end = static_cast<float> (target + (start - target) * keep);

		// ARRIVE EXACTLY. A gain left at 0.9999999 instead of 1 would end
		// the bit-identity of a hit at 0 dB, and one hovering just above
		// zero would never let a bar at the bottom be silent.
		if (std::fabs (end - target) < 1e-6f)
			end = target;

		mCellGainStart[cell] = start;
		mCellGainEnd[cell]   = end;
		mCellGain[cell]      = end;
	}
}

//------------------------------------------------------------------------
void SampleDrumDsp::setCellSample (int cell, const SampleBuffer* sample)
{
	if (!isCellIndex (cell))
		return;

	// An empty buffer is published as NO buffer, so the audio thread has
	// one test to make rather than two.
	if (sample != nullptr && sample->empty ())
		sample = nullptr;

	// RELEASE: a thread that acquires this pointer also sees every sample
	// written into the buffer before it was published.
	mCells[cell].store (sample, std::memory_order_release);
}

//------------------------------------------------------------------------
unsigned int SampleDrumDsp::loadedMask () const
{
	unsigned int mask = 0u;
	for (int cell = 0; cell < kCellCount; ++cell)
		if (mCells[cell].load (std::memory_order_acquire) != nullptr)
			mask |= 1u << cell;
	return mask;
}

//------------------------------------------------------------------------
int SampleDrumDsp::cellForNote (int note, float velocity01) const
{
	const int column = columnForNote (note);
	const int row    = rowForVelocity (velocityTo127 (velocity01));
	if (column < 0 || row < 0)
		return -1;
	return chooseCell (column, row, loadedMask ());
}

//------------------------------------------------------------------------
void SampleDrumDsp::beginFade (Voice& voice, int samples)
{
	if (!voice.active ())
		return;

	samples = std::max (1, samples);

	// Already leaving, and sooner than asked: leave it alone. A voice is
	// only ever made to go FASTER - syncCells depends on that, because
	// its deadline is the end of this block.
	if (voice.fadeLeft > 0 && voice.fadeLeft <= samples)
		return;

	voice.fadeLeft = samples;
	voice.fadeStep = voice.fadeGain / static_cast<float> (samples);
}

//------------------------------------------------------------------------
void SampleDrumDsp::syncCells (int numSamples)
{
	for (Voice& voice : mVoices)
	{
		if (!voice.active ())
			continue;

		const SampleBuffer* current = mCells[voice.cell].load (std::memory_order_acquire);
		if (current == voice.sample)
			continue;

		// THE CELL WAS REPLACED OR EMPTIED under a playing voice. The old
		// buffer is on the processor's retire list and will be freed once
		// two more blocks have begun, so this voice must be gone by the
		// end of THIS one. With samples to render, fade across them (as
		// much of kChokeFadeSeconds as fits); with none - a parameter-only
		// block - there is no time to fade in, and it stops now.
		if (numSamples <= 0)
			voice = Voice ();
		else
			beginFade (voice, std::min (mFadeSamples, numSamples));
	}
}

//------------------------------------------------------------------------
void SampleDrumDsp::noteOn (int note, float velocity01, int sampleOffset)
{
	const int column = columnForNote (note);
	const int row    = rowForVelocity (velocityTo127 (velocity01));
	if (column < 0 || row < 0)
		return;

	const int cell = chooseCell (column, row, loadedMask ());
	if (cell < 0)
		return;

	if (mPendingCount >= kMaxEventsPerBlock)
		return;   // a flood: drop it rather than allocate

	// KEPT IN OFFSET ORDER. Hosts are meant to deliver events sorted, and
	// most do; an insertion here costs nothing for the ones that do and
	// makes the ones that do not play correctly rather than late.
	int at = mPendingCount;
	while (at > 0 && mPending[at - 1].offset > sampleOffset)
	{
		mPending[at] = mPending[at - 1];
		--at;
	}
	mPending[at] = { std::max (0, sampleOffset), cell, column };
	++mPendingCount;
}

//------------------------------------------------------------------------
void SampleDrumDsp::auditionCell (int cell)
{
	if (!isCellIndex (cell) || mPendingCount >= kMaxEventsPerBlock)
		return;

	// Offset 0 sorts to the front of anything already queued.
	for (int i = mPendingCount; i > 0; --i)
		mPending[i] = mPending[i - 1];
	mPending[0] = { 0, cell, columnOfCell (cell) };
	++mPendingCount;
}

//------------------------------------------------------------------------
void SampleDrumDsp::startVoice (int cell, int column)
{
	const SampleBuffer* sample = mCells[cell].load (std::memory_order_acquire);
	if (sample == nullptr)
		return;   // emptied between the note and the block

	constexpr int kPool = kMaxVoices + kSpareVoices;
	static_assert (sizeof (mVoices) / sizeof (mVoices[0]) >= kPool,
	               "the voice array must hold the spares too");

	// THE CHOKE first, so a closed hat that also has to steal a voice
	// frees the open hat's slot rather than someone else's.
	int playing = 0;
	for (int i = 0; i < kPool; ++i)
	{
		Voice& voice = mVoices[i];
		if (!voice.active ())
			continue;
		if (chokes (column, columnOfCell (voice.cell)))
			beginFade (voice, mFadeSamples);
		if (voice.fadeLeft == 0)
			++playing;
	}

	// THE LIMIT is on voices that are PLAYING. One already fading out
	// does not count against it - it is leaving.
	if (playing >= kMaxVoices)
	{
		Voice* oldest = nullptr;
		for (int i = 0; i < kPool; ++i)
		{
			Voice& voice = mVoices[i];
			if (voice.active () && voice.fadeLeft == 0
			    && (oldest == nullptr || voice.age < oldest->age))
				oldest = &voice;
		}
		if (oldest != nullptr)
			beginFade (*oldest, mFadeSamples);
	}

	// A free slot, or failing that the fading voice nearest its end, cut.
	Voice* slot = nullptr;
	for (int i = 0; i < kPool && slot == nullptr; ++i)
		if (!mVoices[i].active ())
			slot = &mVoices[i];

	if (slot == nullptr)
	{
		for (int i = 0; i < kPool; ++i)
		{
			Voice& voice = mVoices[i];
			if (voice.fadeLeft > 0 && (slot == nullptr || voice.fadeLeft < slot->fadeLeft))
				slot = &voice;
		}
	}
	if (slot == nullptr)
		slot = &mVoices[0];   // unreachable while kSpareVoices > 0

	Voice voice;
	voice.sample   = sample;
	voice.cell     = cell;
	voice.position = 0.0;
	voice.step     = sample->sourceRate / mSampleRate;
	voice.age      = mNextAge++;
	*slot = voice;

	mHits[cell].fetch_add (1u, std::memory_order_relaxed);
}

//------------------------------------------------------------------------
void SampleDrumDsp::renderVoices (float* left, float* right, int from, int to)
{
	if (to <= from)
		return;

	for (Voice& voice : mVoices)
	{
		if (!voice.active ())
			continue;

		const float* data   = voice.sample->samples.data ();
		const int    frames = voice.sample->frameCount;

		// THE CELL'S LEVEL, ramped across the whole block (not this
		// segment of it), so a voice started half way through the block
		// picks the ramp up where it has got to.
		const float gainStart = mCellGainStart[voice.cell];
		const float gainStep  = (mBlockLength > 0)
			? (mCellGainEnd[voice.cell] - gainStart) / static_cast<float> (mBlockLength)
			: 0.f;

		for (int i = from; i < to; ++i)
		{
			const int frame = static_cast<int> (voice.position);
			if (frame >= frames)
			{
				voice = Voice ();   // played to the end
				break;
			}

			// LINEAR INTERPOLATION between this frame and the next, the
			// last frame leaning towards silence. At a step of exactly 1
			// the fraction is exactly 0 and the output is the file's own
			// samples, bit for bit - the tests hold it to that.
			const float frac = static_cast<float> (voice.position - frame);
			const float* a = data + static_cast<std::size_t> (frame) * kSampleChannels;
			const bool   last = (frame + 1 >= frames);
			const float  nextL = last ? 0.f : a[kSampleChannels];
			const float  nextR = last ? 0.f : a[kSampleChannels + 1];

			// At a settled level gainStep is exactly 0 and the gain is the
			// target itself - 1.0 at 0 dB, so unity stays bit-identical.
			const float gain = gainStart + gainStep * static_cast<float> (i + 1);

			float l = (a[0] + (nextL - a[0]) * frac) * gain;
			float r = (a[1] + (nextR - a[1]) * frac) * gain;

			if (voice.fadeLeft > 0)
			{
				voice.fadeGain -= voice.fadeStep;
				if (voice.fadeGain < 0.f)
					voice.fadeGain = 0.f;
				l *= voice.fadeGain;
				r *= voice.fadeGain;

				if (--voice.fadeLeft == 0)
				{
					left[i]  += l;
					right[i] += r;
					voice = Voice ();   // faded out
					break;
				}
			}

			left[i]  += l;
			right[i] += r;
			voice.position += voice.step;
		}
	}
}

//------------------------------------------------------------------------
void SampleDrumDsp::render (float* left, float* right, int numSamples)
{
	if (numSamples <= 0 || left == nullptr || right == nullptr)
	{
		// NOTHING RENDERED means no fade got to finish, so any voice
		// syncCells condemned is ended here instead. Queued hits belong
		// to this block and go with it.
		syncCells (0);
		mPendingCount = 0;
		return;
	}

	std::fill (left, left + numSamples, 0.f);
	std::fill (right, right + numSamples, 0.f);

	mBlockLength = numSamples;
	advanceCellGains (numSamples);

	// SAMPLE-ACCURATE: render up to each hit's offset, start it, carry on.
	// An offset past the end of this block is a host bug; it plays on the
	// last sample rather than being lost.
	int cursor = 0;
	for (int h = 0; h < mPendingCount; ++h)
	{
		const PendingHit& hit = mPending[h];
		const int at = std::min (hit.offset, numSamples - 1);
		renderVoices (left, right, cursor, at);
		cursor = std::max (cursor, at);
		startVoice (hit.cell, hit.column);
	}
	mPendingCount = 0;

	renderVoices (left, right, cursor, numSamples);

	// What the panel's lamps read. A voice that is fading still counts:
	// it is still making a sound.
	bool sounding[kCellCount] = {};
	for (const Voice& voice : mVoices)
		if (voice.active ())
			sounding[voice.cell] = true;
	for (int cell = 0; cell < kCellCount; ++cell)
		mSounding[cell].store (sounding[cell], std::memory_order_relaxed);

	applyTrim (left, right, numSamples);
}

//------------------------------------------------------------------------
void SampleDrumDsp::applyTrim (float* left, float* right, int numSamples)
{
	if (numSamples <= 0 || left == nullptr || right == nullptr)
		return;

	// Both channels off the SAME gain value - a trim that advanced its
	// smoother once per channel would walk the stereo image as it glided.
	for (int i = 0; i < numSamples; ++i)
	{
		const float gain = mTrim.next ();
		left[i]  *= gain;
		right[i] *= gain;
	}
}

//------------------------------------------------------------------------
std::uint32_t SampleDrumDsp::cellHits (int cell) const
{
	return isCellIndex (cell) ? mHits[cell].load (std::memory_order_relaxed) : 0u;
}

//------------------------------------------------------------------------
bool SampleDrumDsp::cellSounding (int cell) const
{
	return isCellIndex (cell) && mSounding[cell].load (std::memory_order_relaxed);
}

//------------------------------------------------------------------------
int SampleDrumDsp::activeVoices () const
{
	int count = 0;
	for (const Voice& voice : mVoices)
		if (voice.active ())
			++count;
	return count;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
