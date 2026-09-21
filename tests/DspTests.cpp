//------------------------------------------------------------------------
// SampleDrum - DSP tests
//
// SDK-FREE, compiled directly against SampleDrumDsp.cpp:
//
//   c++ -std=c++17 -O2 -I../source DspTests.cpp ../source/SampleDrumDsp.cpp
//       -o /tmp/dsptests  &&  /tmp/dsptests
//
// THIS IS THE ONLY EXECUTABLE VERIFICATION ON THIS PROJECT during
// development. The session that writes the code reaches this Mac through
// a Linux VM and cannot run cmake, Xcode, the SDK validator or auval, so
// everything that can be asserted without a host is asserted here - and
// the DSP being free of SDK types is what makes that possible.
//
// The suite ends by MEASURING THE DEFAULT PATCH'S OUTPUT LEVEL and
// printing it in dBFS. It is unity for a silent scaffold and therefore
// looks pointless, which is exactly why it is written now: the
// measurement is the one the first real DSP has to repeat, and a level
// that clips masks other faults and sends you chasing the wrong bug for
// a day.
//------------------------------------------------------------------------

#include "SampleDrumDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace SampleDrum;

//------------------------------------------------------------------------
namespace {

int gChecks = 0;
int gFailures = 0;

void check (bool condition, const std::string& what)
{
	++gChecks;
	if (!condition)
	{
		++gFailures;
		std::printf ("  FAIL  %s\n", what.c_str ());
	}
}

void checkClose (double got, double want, double tolerance, const std::string& what)
{
	const bool ok = std::fabs (got - want) <= tolerance;
	++gChecks;
	if (!ok)
	{
		++gFailures;
		std::printf ("  FAIL  %s: got %.9g, want %.9g (tol %.3g)\n",
		             what.c_str (), got, want, tolerance);
	}
}

/** The rates anyone actually runs. Every rate-dependent assertion is
    made at all of them, because a coefficient that is only right at
    44.1 k is the classic "works on my machine" bug. */
const double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

double peakDbFS (const std::vector<float>& block)
{
	float peak = 0.f;
	for (float v : block)
		peak = std::max (peak, std::fabs (v));
	return (peak <= 0.f) ? -std::numeric_limits<double>::infinity ()
	                     : 20.0 * std::log10 (static_cast<double> (peak));
}

} // anonymous namespace

//------------------------------------------------------------------------
// 1. The shared conversions, at both ends and in the middle
//------------------------------------------------------------------------
static void testConversions ()
{
	std::printf ("dbToGain / gainToDb\n");

	// EXACTLY 1.0, not approximately. Everything below about the default
	// patch being bit-identical rests on this one being exact, which is
	// why dbToGain special-cases zero instead of calling pow.
	check (dbToGain (0.0) == 1.0, "0 dB is exactly unity");

	// Both ends of the parameter's plain range, and the middle.
	checkClose (dbToGain (-24.0), 0.063095734, 1e-9, "-24 dB (bottom of range)");
	checkClose (dbToGain (-6.0),  0.501187234, 1e-9, "-6 dB (middle-ish)");
	checkClose (dbToGain (12.0),  3.981071706, 1e-9, "+12 dB (top of range)");

	// Round trip.
	for (double db : { -24.0, -12.0, -6.0, 0.0, 6.0, 12.0 })
		checkClose (gainToDb (dbToGain (db)), db, 1e-9, "round trip at " + std::to_string (db));

	// The floor, so a silent reading is a number rather than -inf.
	check (gainToDb (0.0) == -180.0, "zero gain reads -180 dB, not -inf");
}

//------------------------------------------------------------------------
// 2. The smoother never steps hard enough to click
//------------------------------------------------------------------------
static void testSmootherStep ()
{
	std::printf ("smoother step limit\n");

	for (double rate : kRates)
	{
		Smoother s;
		s.setSampleRate (rate, kTrimSmoothingSeconds);
		s.snap (0.f);
		s.setTarget (1.f);

		// The worst single step a one-pole can take is its coefficient,
		// on the first sample of the longest possible move. Asserting
		// the coefficient AND then walking the glide catches both a bad
		// formula and a bad implementation of it.
		check (s.coefficient () <= 0.01f,
		       "coefficient <= 1% at " + std::to_string (static_cast<int> (rate)) + " Hz");

		float previous = s.value ();
		float worst = 0.f;
		for (int i = 0; i < static_cast<int> (rate); ++i)
		{
			const float v = s.next ();
			worst = std::max (worst, std::fabs (v - previous));
			previous = v;
		}

		check (worst <= 0.011f,
		       "no sample steps more than ~1% at " + std::to_string (static_cast<int> (rate)) + " Hz");

		// And it actually arrives: a smoother that never reaches its
		// target is a parameter that never takes effect.
		checkClose (previous, 1.0, 1e-3,
		            "reaches the target within a second at " + std::to_string (static_cast<int> (rate)) + " Hz");
	}

	// Zero seconds means NO smoothing, which is how a parameter that
	// must not be interpolated is expressed - not a degenerate case.
	Smoother instant;
	instant.setSampleRate (48000.0, 0.0);
	instant.snap (0.f);
	instant.setTarget (1.f);
	check (instant.next () == 1.f, "zero smoothing time follows the target immediately");
}

//------------------------------------------------------------------------
// 3. Unity is BIT-IDENTICAL, and half gain is not
//------------------------------------------------------------------------
static void testUnityIsBitIdentical ()
{
	std::printf ("unity passes the signal through untouched\n");

	const int kBlock = 512;

	// A block with awkward values in it: full scale both ways, something
	// tiny, and a shape that would show any rounding.
	std::vector<float> reference (kBlock);
	for (int i = 0; i < kBlock; ++i)
		reference[i] = static_cast<float> (std::sin (i * 0.037) * 0.9 + ((i % 7) - 3) * 1e-6);
	reference[0]   =  1.f;
	reference[1]   = -1.f;
	reference[2]   =  1e-7f;

	for (double rate : kRates)
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setMaxBlockSize (kBlock);
		dsp.setOutputTrimDb (0.0);
		dsp.reset ();   // snap, so the smoother is settled at unity

		std::vector<float> left (reference), right (reference);
		dsp.applyTrim (left.data (), right.data (), kBlock);

		const bool identicalL = std::memcmp (left.data (), reference.data (),
		                                     kBlock * sizeof (float)) == 0;
		const bool identicalR = std::memcmp (right.data (), reference.data (),
		                                     kBlock * sizeof (float)) == 0;

		check (identicalL && identicalR,
		       "bit-identical at 0 dB, " + std::to_string (static_cast<int> (rate)) + " Hz");
	}

	//--------------------------------------------------------------------
	// THE NEGATIVE CONTROL. The same comparison at half gain MUST fail.
	//
	// A guard that has never failed is a guess: if memcmp were being
	// handed the same pointer twice, or the block were all zeros, or
	// applyTrim were a no-op, every assertion above would pass and mean
	// nothing. This is what proves the comparison can tell the
	// difference.
	//--------------------------------------------------------------------
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setMaxBlockSize (kBlock);
		dsp.setOutputTrimDb (gainToDb (0.5));
		dsp.reset ();

		std::vector<float> left (reference), right (reference);
		dsp.applyTrim (left.data (), right.data (), kBlock);

		const bool identical = std::memcmp (left.data (), reference.data (),
		                                    kBlock * sizeof (float)) == 0;
		check (!identical, "NEGATIVE CONTROL: half gain is NOT bit-identical");

		// And it is half, to the precision a float multiply gives.
		checkClose (left[0], 0.5, 1e-6, "half gain really halves full scale");
	}
}

//------------------------------------------------------------------------
// 4. The trim reaches the audio, and the parameter's whole range works
//------------------------------------------------------------------------
static void testTrimTakesEffect ()
{
	std::printf ("trim takes effect across its range\n");

	const int kBlock = 256;

	for (double db : { -24.0, -12.0, -6.0, 0.0, 6.0, 12.0 })
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setMaxBlockSize (kBlock);
		dsp.setOutputTrimDb (db);
		dsp.reset ();

		checkClose (dsp.currentTrimGain (), dbToGain (db), 1e-6,
		            "reset snaps the gain to " + std::to_string (db) + " dB");

		std::vector<float> left (kBlock, 1.f), right (kBlock, 1.f);
		dsp.applyTrim (left.data (), right.data (), kBlock);

		checkClose (left[kBlock - 1], dbToGain (db), 1e-5,
		            "a unit block comes out at " + std::to_string (db) + " dB");

		// BOTH CHANNELS OFF THE SAME GAIN VALUE. A trim that advanced
		// its smoother once per channel would walk the stereo image
		// while it glided, and this is the assertion that catches it.
		check (left[kBlock - 1] == right[kBlock - 1], "both channels get the identical gain");
	}
}

//------------------------------------------------------------------------
// 5. Degenerate blocks do not crash
//------------------------------------------------------------------------
static void testDegenerateBlocks ()
{
	std::printf ("zero frames and null buffers\n");

	SampleDrumDsp dsp;
	dsp.setSampleRate (48000.0);
	dsp.setMaxBlockSize (512);
	dsp.setOutputTrimDb (0.0);

	std::vector<float> left (512, 0.f), right (512, 0.f);

	// A PARAMETER-ONLY BLOCK is legal and arrives in practice - hosts
	// send numSamples == 0 to deliver automation between audible blocks.
	dsp.render (left.data (), right.data (), 0);
	dsp.render (left.data (), right.data (), -1);
	dsp.applyTrim (left.data (), right.data (), 0);
	dsp.render (nullptr, right.data (), 512);
	dsp.render (left.data (), nullptr, 512);
	dsp.render (nullptr, nullptr, 512);
	dsp.applyTrim (nullptr, nullptr, 512);

	check (true, "survived zero, negative and null-buffer blocks");

	// A sample rate nobody would ask for must not produce a NaN
	// coefficient that then poisons every sample forever.
	dsp.setSampleRate (0.0);
	check (dsp.sampleRate () >= 1000.0, "an absurd sample rate falls back to something sane");

	dsp.setMaxBlockSize (0);
	dsp.render (left.data (), right.data (), 64);
	check (true, "a zero max block size does not crash render");

	// And the smoother's state is finite after all of that.
	check (std::isfinite (dsp.currentTrimGain ()), "trim gain is still finite");
}

//------------------------------------------------------------------------
// 6. The default patch's output level
//------------------------------------------------------------------------
static void measureDefaultLevel ()
{
	std::printf ("default patch level\n");

	const int kBlock = 4096;

	SampleDrumDsp dsp;
	dsp.setSampleRate (48000.0);
	dsp.setMaxBlockSize (kBlock);
	dsp.setOutputTrimDb (0.0);        // the parameter table's default
	dsp.reset ();

	std::vector<float> left (kBlock, 0.f), right (kBlock, 0.f);
	dsp.render (left.data (), right.data (), kBlock);

	const double peakL = peakDbFS (left);
	const double peakR = peakDbFS (right);

	std::printf ("  measured peak: L %s dBFS, R %s dBFS\n",
	             std::isfinite (peakL) ? std::to_string (peakL).c_str () : "-inf (silence)",
	             std::isfinite (peakR) ? std::to_string (peakR).c_str () : "-inf (silence)");

	// SILENCE IS THE CORRECT ANSWER TODAY, because renderVoices is
	// empty. When it is not, this assertion is the one to change - and
	// the number to insist on is a peak comfortably BELOW 0 dBFS. A
	// default patch that clips masks every other fault in the signal
	// path.
	check (!std::isfinite (peakL) && !std::isfinite (peakR),
	       "the silent scaffold renders exact silence");

	// render() must also CLEAR the block it is given, not add to
	// whatever was left in it - a host reuses its buffers, so an
	// instrument that forgets this plays back the last block forever.
	std::vector<float> dirtyL (kBlock, 0.5f), dirtyR (kBlock, -0.5f);
	dsp.render (dirtyL.data (), dirtyR.data (), kBlock);
	check (!std::isfinite (peakDbFS (dirtyL)) && !std::isfinite (peakDbFS (dirtyR)),
	       "render clears the host's buffer rather than adding to it");
}

//------------------------------------------------------------------------
int main ()
{
	std::printf ("SampleDrum DSP tests\n");
	std::printf ("--------------------\n");

	testConversions ();
	testSmootherStep ();
	testUnityIsBitIdentical ();
	testTrimTakesEffect ();
	testDegenerateBlocks ();
	measureDefaultLevel ();

	std::printf ("--------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);

	return (gFailures == 0) ? 0 : 1;
}
