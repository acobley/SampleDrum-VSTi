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
#include <complex>
#include <cmath>

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
// Isolator
//------------------------------------------------------------------------
namespace {

/** Butterworth damping, 1/Q with Q = 1/sqrt 2. The value that makes two
    sections in series a Linkwitz-Riley crossover, and LP^2 + HP^2 the
    allpass (s^2 - k s + 1) / (s^2 + k s + 1) the SVF's own `ap` output is. */
constexpr double kSvfK = 1.4142135623730951;

/** The recursive state is flushed below this. Double, so far above the
    denormal range; it is what lets a tail end in exact zeros, which is
    what the processor's silence flags look for. */
constexpr double kIsolatorStateFloor = 1e-30;

/** A gain glide counts as arrived within this of its target. */
constexpr double kGainSnap = 1e-6;

/** A frequency glide, in octaves. */
constexpr double kOctaveSnap = 1e-6;

} // anonymous namespace

//------------------------------------------------------------------------
void Isolator::Glide::next (double coeff, double snapWithin)
{
	value += (target - value) * coeff;
	if (std::fabs (target - value) < snapWithin)
		value = target;
}

//------------------------------------------------------------------------
Isolator::Isolator ()
{
	mBass.target      = isolatorGain (kIsolatorDefaultDb);
	mTreble.target    = isolatorGain (kIsolatorDefaultDb);
	mBassOct.target   = std::log2 (kBassHzDefault);
	mTrebleOct.target = std::log2 (kTrebleHzDefault);
	setSampleRate (44100.0);
}

//------------------------------------------------------------------------
void Isolator::setSampleRate (double sampleRate)
{
	if (!(sampleRate >= 1000.0))
		sampleRate = 44100.0;
	mSampleRate = sampleRate;
	mCoeff   = 1.0 - std::exp (-1.0 / (kIsolatorSmoothingSeconds * mSampleRate));
	mMixStep = 1.0 / std::max (1.0, kIsolatorSmoothingSeconds * mSampleRate);
	reset ();
}

//------------------------------------------------------------------------
void Isolator::reset ()
{
	mBass.snap ();
	mTreble.snap ();
	mBassOct.snap ();
	mTrebleOct.snap ();

	// Dry exactly when both gains are exactly unity - the same rule
	// process() applies, without the fade.
	mMix.target = (mBass.target == 1.0 && mTreble.target == 1.0 && !mForceFiltered) ? 0.0 : 1.0;
	mMix.snap ();

	mBassCoeffs   = coeffsFor (std::exp2 (mBassOct.value));
	mTrebleCoeffs = coeffsFor (std::exp2 (mTrebleOct.value));

	for (auto& channel : mState)
		for (SvfState& state : channel)
			state = SvfState ();
}

//------------------------------------------------------------------------
void Isolator::setBassDb (double db)   { mBass.target   = isolatorGain (db); }
void Isolator::setTrebleDb (double db) { mTreble.target = isolatorGain (db); }

void Isolator::setBassHz (double hz)
{
	mBassOct.target = std::log2 (std::min (kBassHzMax, std::max (kBassHzMin, hz)));
}

void Isolator::setTrebleHz (double hz)
{
	mTrebleOct.target = std::log2 (std::min (kTrebleHzMax, std::max (kTrebleHzMin, hz)));
}

//------------------------------------------------------------------------
double Isolator::clampHz (double hz) const
{
	return std::max (1.0, std::min (hz, kIsolatorMaxHzFraction * mSampleRate));
}

//------------------------------------------------------------------------
Isolator::Coeffs Isolator::coeffsFor (double hz) const
{
	// The TPT (bilinear, prewarped) SVF: the cutoff lands on `hz` exactly
	// at every rate.
	const double g = std::tan (3.14159265358979323846 * clampHz (hz) / mSampleRate);
	Coeffs c;
	c.a1 = 1.0 / (1.0 + g * (g + kSvfK));
	c.a2 = g * c.a1;
	c.a3 = g * c.a2;
	return c;
}

//------------------------------------------------------------------------
Isolator::SvfOut Isolator::tick (const Coeffs& c, SvfState& s, double x)
{
	const double v3 = x - s.ic2;
	const double v1 = c.a1 * s.ic1 + c.a2 * v3;
	const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
	s.ic1 = 2.0 * v1 - s.ic1;
	s.ic2 = 2.0 * v2 - s.ic2;

	SvfOut out;
	out.lp = v2;
	out.hp = x - kSvfK * v1 - v2;
	out.ap = x - 2.0 * kSvfK * v1;
	return out;
}

//------------------------------------------------------------------------
void Isolator::flushStates ()
{
	for (auto& channel : mState)
		for (SvfState& state : channel)
		{
			if (std::fabs (state.ic1) < kIsolatorStateFloor)
				state.ic1 = 0.0;
			if (std::fabs (state.ic2) < kIsolatorStateFloor)
				state.ic2 = 0.0;
		}
}

//------------------------------------------------------------------------
void Isolator::process (float* left, float* right, int numSamples)
{
	if (numSamples <= 0 || left == nullptr || right == nullptr)
		return;

	float* const channels[2] = { left, right };

	for (int i = 0; i < numSamples; ++i)
	{
		// ONE set of glides per sample, shared by both channels, so the
		// stereo image cannot walk while a knob moves.
		if (!mBass.settled ())
			mBass.next (mCoeff, kGainSnap);
		if (!mTreble.settled ())
			mTreble.next (mCoeff, kGainSnap);

		// The coefficients are recomputed only while a frequency is
		// actually moving. The SVF tolerates that per sample.
		if (!mBassOct.settled ())
		{
			mBassOct.next (mCoeff, kOctaveSnap);
			mBassCoeffs = coeffsFor (std::exp2 (mBassOct.value));
		}
		if (!mTrebleOct.settled ())
		{
			mTrebleOct.next (mCoeff, kOctaveSnap);
			mTrebleCoeffs = coeffsFor (std::exp2 (mTrebleOct.value));
		}

		// DRY ONLY ONCE BOTH GAINS HAVE ARRIVED AT EXACTLY UNITY. Going
		// back to 0 dB, the gains glide home on the filtered path first,
		// then the dry signal is faded in - so the two paths being mixed
		// differ only in phase. A LINEAR ramp, so it arrives exactly.
		mMix.target = (mBass.value == 1.0 && mTreble.value == 1.0
		               && mBass.target == 1.0 && mTreble.target == 1.0
		               && !mForceFiltered) ? 0.0 : 1.0;
		if (mMix.value < mMix.target)
			mMix.value = std::min (mMix.target, mMix.value + mMixStep);
		else if (mMix.value > mMix.target)
			mMix.value = std::max (mMix.target, mMix.value - mMixStep);

		const double bass   = mBass.value;
		const double treble = mTreble.value;
		const double wetMix = mMix.value;

		for (int ch = 0; ch < 2; ++ch)
		{
			SvfState* const st = mState[ch];
			const double x = channels[ch][i];

			// Split 1, at the bass frequency: one shared first section,
			// then a second low-pass and a second high-pass - LR4 each.
			const SvfOut s1 = tick (mBassCoeffs, st[0], x);
			const double low  = tick (mBassCoeffs, st[1], s1.lp).lp;
			const double rest = tick (mBassCoeffs, st[2], s1.hp).hp;

			// The low band through split 2's allpass, to stay in phase.
			const double lowAligned = tick (mTrebleCoeffs, st[3], low).ap;

			// Split 2, at the treble frequency.
			const SvfOut s2 = tick (mTrebleCoeffs, st[4], rest);
			const double mid  = tick (mTrebleCoeffs, st[5], s2.lp).lp;
			const double high = tick (mTrebleCoeffs, st[6], s2.hp).hp;

			const double wet = bass * lowAligned + mid + treble * high;

			// Untouched while dry: the filters ran (they stay settled for
			// the moment they are faded in) but the sample is not written.
			if (wetMix == 1.0)
				channels[ch][i] = static_cast<float> (wet);
			else if (wetMix > 0.0)
				channels[ch][i] = static_cast<float> (x + (wet - x) * wetMix);
		}
	}

	flushStates ();
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

	for (int column = 0; column < kColumns; ++column)
		mColumnNote[column] = kDrumMap[column].note;

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
	mIsolator.setSampleRate (mSampleRate);

	// The band-limited read's banks: sized here, off the audio thread, and
	// the steps of the usual high-rate files built in advance.
	for (SincBank& bank : mBanks)
	{
		bank.weights.assign (static_cast<std::size_t> (SincBank::kPhases + 1) * SincBank::kMaxTaps, 0.f);
		bank.ratio = 0.0;
	}
	int slot = 0;
	for (double fileRate : { 88200.0, 96000.0, 176400.0, 192000.0 })
		if (fileRate / mSampleRate > 1.0 + 1e-9 && slot < kBankSlots)
			mBanks[slot++].build (fileRate / mSampleRate);

	reset ();
}

//------------------------------------------------------------------------
int SampleDrumDsp::bankFor (double step)
{
	for (int i = 0; i < kBankSlots; ++i)
		if (mBanks[i].ratio == step)
			return i;

	// A rate nobody built for. Take a slot no playing voice is using -
	// rebuilding one under a voice would change its filter mid-hit.
	for (int i = 0; i < kBankSlots; ++i)
	{
		bool inUse = false;
		for (const Voice& voice : mVoices)
			inUse = inUse || (voice.active () && voice.bank == i);
		if (!inUse && !mBanks[i].weights.empty ())
		{
			mBanks[i].build (step);
			return i;
		}
	}
	return -1;   // eight different high rates at once: the linear read
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
	mVoicesSounding.store (0, std::memory_order_relaxed);

	// The isolator's glides snap and its filters empty, for the trim's
	// reason, and so nothing of the old project's tail rings into the new.
	mIsolator.reset ();
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
void SampleDrumDsp::setColumnNote (int column, int note)
{
	if (column < 0 || column >= kColumns)
		return;
	mColumnNote[column] = std::min (kMaxNote, std::max (kMinNote, note));
}

//------------------------------------------------------------------------
int SampleDrumDsp::columnNote (int column) const
{
	return (column >= 0 && column < kColumns) ? mColumnNote[column] : -1;
}

//------------------------------------------------------------------------
unsigned int SampleDrumDsp::columnsForNote (int note) const
{
	unsigned int mask = 0u;
	for (int column = 0; column < kColumns; ++column)
		if (mColumnNote[column] == note)
			mask |= 1u << column;
	return mask;
}

//------------------------------------------------------------------------
int SampleDrumDsp::cellForNote (int note, float velocity01) const
{
	const int row = rowForVelocity (velocityTo127 (velocity01));
	if (row < 0)
		return -1;
	for (int column = 0; column < kColumns; ++column)
		if (mColumnNote[column] == note)
			return chooseCell (column, row, loadedMask ());
	return -1;
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
	const int row = rowForVelocity (velocityTo127 (velocity01));
	if (row < 0)
		return;

	// EVERY COLUMN ON THIS NOTE plays - see setColumnNote. A note no
	// column has is ignored, not folded onto a neighbour: silence is
	// visibly a mapping question, a wrong drum sounds like a bug.
	const unsigned int loaded = loadedMask ();
	for (int column = 0; column < kColumns; ++column)
	{
		if (mColumnNote[column] != note)
			continue;
		const int cell = chooseCell (column, row, loaded);
		if (cell >= 0)
			queueHit (sampleOffset, cell, column, velocityTo127 (velocity01));
	}
}

//------------------------------------------------------------------------
void SampleDrumDsp::queueHit (int sampleOffset, int cell, int column, int velocity127)
{
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
	mPending[at] = { std::max (0, sampleOffset), cell, column, velocity127 };
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
	mPending[0] = { 0, cell, columnOfCell (cell), 0 };
	++mPendingCount;
}

//------------------------------------------------------------------------
bool SampleDrumDsp::startVoice (int cell, int column)
{
	const SampleBuffer* sample = mCells[cell].load (std::memory_order_acquire);
	if (sample == nullptr)
		return false;   // emptied between the note and the block

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
	voice.bank     = (voice.step > 1.0 + 1e-9) ? bankFor (voice.step) : -1;
	voice.age      = mNextAge++;
	*slot = voice;

	mHits[cell].fetch_add (1u, std::memory_order_relaxed);
	return true;
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

			// At a settled level gainStep is exactly 0 and the gain is the
			// target itself - 1.0 at 0 dB, so unity stays bit-identical.
			const float gain = gainStart + gainStep * static_cast<float> (i + 1);

			float l, r;
			if (voice.bank >= 0)
			{
				// A FILE ABOVE THE SESSION'S RATE: band-limited, so what
				// cannot exist at the session's rate is removed rather than
				// folded back. See SincBank.
				mBanks[voice.bank].read (data, frames, voice.position, l, r);
				l *= gain;
				r *= gain;
			}
			else
			{
				// LINEAR INTERPOLATION between this frame and the next, the
				// last frame leaning towards silence. At a step of exactly
				// 1 the fraction is exactly 0 and the output is the file's
				// own samples, bit for bit - the tests hold it to that.
				const float frac = static_cast<float> (voice.position - frame);
				const float* a = data + static_cast<std::size_t> (frame) * kSampleChannels;
				const bool   last = (frame + 1 >= frames);
				const float  nextL = last ? 0.f : a[kSampleChannels];
				const float  nextR = last ? 0.f : a[kSampleChannels + 1];

				l = (a[0] + (nextL - a[0]) * frac) * gain;
				r = (a[1] + (nextR - a[1]) * frac) * gain;
			}

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
		if (startVoice (hit.cell, hit.column))
			mLastHit.store (encodeLastHit (hit.cell, hit.velocity, ++mHitSequence),
			                std::memory_order_relaxed);
	}
	mPendingCount = 0;

	renderVoices (left, right, cursor, numSamples);

	// What the panel's lamps read. A voice that is fading still counts:
	// it is still making a sound.
	bool sounding[kCellCount] = {};
	int voices = 0;
	for (const Voice& voice : mVoices)
		if (voice.active ())
		{
			sounding[voice.cell] = true;
			++voices;
		}
	mVoicesSounding.store (voices, std::memory_order_relaxed);
	for (int cell = 0; cell < kCellCount; ++cell)
		mSounding[cell].store (sounding[cell], std::memory_order_relaxed);

	// The isolator, then the trim - so the trim stays the last stage, the
	// one that answers clipping, including a boosted bass's.
	mIsolator.process (left, right, numSamples);
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
int SampleDrumDsp::voicesSounding () const
{
	return mVoicesSounding.load (std::memory_order_relaxed);
}

//------------------------------------------------------------------------
std::uint32_t SampleDrumDsp::lastHit () const
{
	return mLastHit.load (std::memory_order_relaxed);
}

//------------------------------------------------------------------------
double isolatorResponseDb (double hz, double bassDb, double bassHz, double trebleDb, double trebleHz)
{
	using C = std::complex<double>;
	if (!(hz > 0.0) || !(bassHz > 0.0) || !(trebleHz > 0.0))
		return 0.0;

	// One Butterworth section at fc, as the SVF's LP and HP; LR4 is two in
	// series, and an LR4 pair's sum is the allpass Isolator puts on the low
	// band so it stays in phase with the other two.
	const auto lr4 = [hz] (double fc, C& lp, C& hp)
	{
		const C s (0.0, hz / fc);
		const C d = s * s + std::sqrt (2.0) * s + 1.0;
		lp = (1.0 / d) * (1.0 / d);
		hp = (s * s / d) * (s * s / d);
	};
	C lpB, hpB, lpT, hpT;
	lr4 (bassHz, lpB, hpB);
	lr4 (trebleHz, lpT, hpT);

	const double gb = isolatorGain (bassDb), gt = isolatorGain (trebleDb);
	const C total = gb * lpB * (lpT + hpT) + hpB * (lpT + gt * hpT);
	const double mag = std::abs (total);
	const double floorMag = std::pow (10.0, kIsolatorResponseFloorDb / 20.0);
	return 20.0 * std::log10 (std::max (mag, floorMag));
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
//------------------------------------------------------------------------
// SincBank
//------------------------------------------------------------------------
namespace {
/** sinc (u) x kaiser (u / kZeroCrossings) for u = 0 .. kZeroCrossings,
    512 points per zero crossing, built once. */
struct SincTable
{
	static constexpr int kResolution = 512;
	float v[SincBank::kZeroCrossings * kResolution + 2];

	SincTable ()
	{
		const double pi = 3.14159265358979323846;
		auto besselI0 = [] (double x)
		{
			double sum = 1.0, term = 1.0;
			for (int k = 1; k < 40; ++k)
			{
				term *= (x / (2.0 * k)) * (x / (2.0 * k));
				sum += term;
			}
			return sum;
		};
		const double norm = besselI0 (SincBank::kBeta);
		const int n = SincBank::kZeroCrossings * kResolution;
		for (int i = 0; i <= n; ++i)
		{
			const double u = static_cast<double> (i) / kResolution;
			const double x = u / SincBank::kZeroCrossings;
			const double window = besselI0 (SincBank::kBeta * std::sqrt (std::max (0.0, 1.0 - x * x))) / norm;
			v[i] = static_cast<float> (((i == 0) ? 1.0 : std::sin (pi * u) / (pi * u)) * window);
		}
		v[n + 1] = 0.f;
	}
};

const SincTable& sincTable ()
{
	static const SincTable table;   // first touched by setSampleRate, off the audio thread
	return table;
}
} // namespace

void SincBank::build (double r)
{
	if (weights.empty ())
		return;                          // never sized: the caller falls back to linear
	const float* table = sincTable ().v;
	const double fc    = kCutoff / r;
	const double half  = std::min (kZeroCrossings / fc, static_cast<double> (kMaxHalfWidth));
	const double edge  = static_cast<double> (kZeroCrossings * SincTable::kResolution);

	ratio = r;
	lead  = static_cast<int> (std::ceil (half));
	taps  = std::min (2 * lead + 1, kMaxTaps);

	for (int p = 0; p <= kPhases; ++p)
	{
		float* row = weights.data () + static_cast<std::size_t> (p) * kMaxTaps;
		const double frac = static_cast<double> (p) / kPhases;
		double sum = 0.0;
		for (int j = 0; j < taps; ++j)
		{
			const double t = static_cast<double> (j - lead) - frac;
			double w = 0.0;
			if (std::fabs (t) <= half)
			{
				const double ax = std::fabs (t * fc) * SincTable::kResolution;
				if (ax < edge)
				{
					const int i = static_cast<int> (ax);
					w = table[i] + (table[i + 1] - table[i]) * (ax - static_cast<double> (i));
				}
			}
			row[j] = static_cast<float> (w);
			sum += w;
		}
		const float norm = (sum > 0.0) ? static_cast<float> (1.0 / sum) : 0.f;
		for (int j = 0; j < taps; ++j)
			row[j] *= norm;
	}
}

void SincBank::read (const float* data, int frames, double position, float& left, float& right) const
{
	const int whole = static_cast<int> (position);
	const double phase = (position - static_cast<double> (whole)) * kPhases;
	int p = static_cast<int> (phase);
	if (p >= kPhases)
		p = kPhases - 1;
	const float f = static_cast<float> (phase - static_cast<double> (p));

	const float* row0 = weights.data () + static_cast<std::size_t> (p) * kMaxTaps;
	const float* row1 = row0 + kMaxTaps;
	const int first = whole - lead;

	// Only the taps inside the file: beyond either end is silence.
	const int j0 = std::max (0, -first);
	const int j1 = std::min (taps, frames - first);
	const float* src = data + static_cast<std::ptrdiff_t> (first) * kSampleChannels;
	float l = 0.f, r = 0.f;
	for (int j = j0; j < j1; ++j)
	{
		const float w = row0[j] + (row1[j] - row0[j]) * f;
		l += w * src[2 * j];
		r += w * src[2 * j + 1];
	}
	left  = l;
	right = r;
}

} // namespace SampleDrum
