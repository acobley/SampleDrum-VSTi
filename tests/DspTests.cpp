//------------------------------------------------------------------------
// SampleDrum - DSP tests
//
// SDK-FREE, compiled directly against the two SDK-free sources:
//
//   c++ -std=c++17 -O2 -Isource tests/DspTests.cpp
//       source/SampleDrumDsp.cpp source/SampleDrumSample.cpp
//       -o /tmp/dsptests  &&  /tmp/dsptests
//
// THIS IS THE ONLY EXECUTABLE VERIFICATION ON THIS PROJECT during
// development. The session that writes the code reaches this Mac through
// a Linux VM and cannot run cmake, Xcode, the SDK validator or auval, so
// everything that can be asserted without a host is asserted here.
//
// The suite ends by MEASURING THE OUTPUT LEVEL of the default patch and
// of a full-scale hit, and printing both in dBFS. A level that clips
// masks every other fault and sends you chasing the wrong bug.
//------------------------------------------------------------------------

#include "SampleDrumDsp.h"
#include "DenormalGuard.h"
#include "SampleDrumKit.h"
#include "SampleDrumSample.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

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
	++gChecks;
	if (!(std::fabs (got - want) <= tolerance))
	{
		++gFailures;
		std::printf ("  FAIL  %s: got %.9g, want %.9g (tol %.3g)\n",
		             what.c_str (), got, want, tolerance);
	}
}

const double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

std::string str (int v) { return std::to_string (v); }

double peakDbFS (const std::vector<float>& block)
{
	float peak = 0.f;
	for (float v : block)
		peak = std::max (peak, std::fabs (v));
	return (peak <= 0.f) ? -std::numeric_limits<double>::infinity ()
	                     : 20.0 * std::log10 (static_cast<double> (peak));
}

float peakOf (const std::vector<float>& block, int from = 0, int to = -1)
{
	if (to < 0)
		to = static_cast<int> (block.size ());
	float peak = 0.f;
	for (int i = from; i < to; ++i)
		peak = std::max (peak, std::fabs (block[i]));
	return peak;
}

/** A stereo sample: `frames` frames of a constant DC value, or of
    `shape(i)` when given. Constant values make "which cell played" a
    matter of reading one number off the output. */
std::shared_ptr<SampleBuffer> makeSample (int frames, float value, double rate = 48000.0)
{
	auto s = std::make_shared<SampleBuffer> ();
	s->frameCount = frames;
	s->sourceRate = rate;
	s->samples.assign (static_cast<std::size_t> (frames) * kSampleChannels, value);
	s->peak = std::fabs (value);
	return s;
}

std::shared_ptr<SampleBuffer> makeShapedSample (int frames, double rate = 48000.0)
{
	auto s = makeSample (frames, 0.f, rate);
	for (int i = 0; i < frames; ++i)
	{
		const float v = static_cast<float> (std::sin (i * 0.05) * 0.7 + ((i % 5) - 2) * 1e-6);
		s->samples[i * 2]     = v;
		s->samples[i * 2 + 1] = -v;
	}
	return s;
}

float velocityFor (int v127) { return static_cast<float> (v127) / 127.f; }

/** Render `frames` samples in blocks of `block`, syncing first as the
    processor does. */
void renderInto (SampleDrumDsp& dsp, std::vector<float>& left, std::vector<float>& right,
                 int frames, int block)
{
	left.assign (frames, 0.f);
	right.assign (frames, 0.f);
	for (int at = 0; at < frames; at += block)
	{
		const int n = std::min (block, frames - at);
		dsp.syncCells (n);
		dsp.render (left.data () + at, right.data () + at, n);
	}
}

//------------------------------------------------------------------------
// Little-endian writers for building WAV files in memory.
//------------------------------------------------------------------------
void put16 (std::vector<unsigned char>& b, uint16_t v)
{
	b.push_back (v & 0xFF); b.push_back (v >> 8);
}
void put32 (std::vector<unsigned char>& b, uint32_t v)
{
	for (int i = 0; i < 4; ++i) b.push_back ((v >> (8 * i)) & 0xFF);
}
void putTag (std::vector<unsigned char>& b, const char* t)
{
	for (int i = 0; i < 4; ++i) b.push_back (static_cast<unsigned char> (t[i]));
}

/** A complete WAV: header, an optional odd-length junk chunk BEFORE fmt
    (the case that catches a missing pad byte), fmt, data. */
std::vector<unsigned char> makeWav (uint16_t format, uint16_t channels, uint32_t rate,
                                    uint16_t bits, const std::vector<unsigned char>& data,
                                    bool oddChunkFirst = false)
{
	std::vector<unsigned char> body;
	putTag (body, "WAVE");
	if (oddChunkFirst)
	{
		putTag (body, "LIST"); put32 (body, 3);
		body.push_back ('a'); body.push_back ('b'); body.push_back ('c');
		body.push_back (0);   // the pad byte the size does not count
	}
	putTag (body, "fmt "); put32 (body, 16);
	put16 (body, format); put16 (body, channels); put32 (body, rate);
	put32 (body, rate * channels * (bits / 8)); put16 (body, channels * (bits / 8));
	put16 (body, bits);
	putTag (body, "data"); put32 (body, static_cast<uint32_t> (data.size ()));
	body.insert (body.end (), data.begin (), data.end ());

	std::vector<unsigned char> file;
	putTag (file, "RIFF"); put32 (file, static_cast<uint32_t> (body.size ()));
	file.insert (file.end (), body.begin (), body.end ());
	return file;
}

} // anonymous namespace

//------------------------------------------------------------------------
// 1. The shared conversions
//------------------------------------------------------------------------
static void testConversions ()
{
	std::printf ("dbToGain / gainToDb\n");

	check (dbToGain (0.0) == 1.0, "0 dB is exactly unity");
	checkClose (dbToGain (-24.0), 0.063095734, 1e-9, "-24 dB (bottom of range)");
	checkClose (dbToGain (-6.0),  0.501187234, 1e-9, "-6 dB");
	checkClose (dbToGain (12.0),  3.981071706, 1e-9, "+12 dB (top of range)");
	for (double db : { -24.0, -6.0, 0.0, 12.0 })
		checkClose (gainToDb (dbToGain (db)), db, 1e-9, "round trip at " + std::to_string (db));
	check (gainToDb (0.0) == -180.0, "zero gain reads -180 dB, not -inf");
}

//------------------------------------------------------------------------
// 2. The drum map
//------------------------------------------------------------------------
static void testDrumMap ()
{
	std::printf ("drum map\n");

	const int expected[kColumns] = { 36, 38, 39, 42, 46, 45, 50, 49 };
	for (int c = 0; c < kColumns; ++c)
	{
		check (kDrumMap[c].note == expected[c], "column " + str (c) + " is GM note " + str (expected[c]));
		check (defaultColumnForNote (expected[c]) == c, "note " + str (expected[c]) + " plays column " + str (c));
		check (kDrumMap[c].name != nullptr && kDrumMap[c].name[0] != 0, "column " + str (c) + " has a name");
	}

	for (int note : { 0, 35, 37, 40, 41, 44, 51, 60, 127, -1, 200 })
		check (defaultColumnForNote (note) == -1, "unmapped note " + str (note) + " plays nothing");

	// Every note is in the map once.
	for (int a = 0; a < kColumns; ++a)
		for (int b = a + 1; b < kColumns; ++b)
			check (kDrumMap[a].note != kDrumMap[b].note, "notes of columns " + str (a) + " and " + str (b) + " differ");
}

//------------------------------------------------------------------------
// 3. The velocity bands, at every edge
//------------------------------------------------------------------------
static void testVelocityBands ()
{
	std::printf ("velocity bands (row 0 top = soft, row 3 bottom = hard)\n");

	struct Edge { int velocity; int row; };
	const Edge edges[] = {
		{ 0, -1 }, { -5, -1 },
		{ 1, 0 }, { 16, 0 }, { 31, 0 },
		{ 32, 1 }, { 48, 1 }, { 63, 1 },
		{ 64, 2 }, { 80, 2 }, { 95, 2 },
		{ 96, 3 }, { 112, 3 }, { 127, 3 }, { 200, 3 },
	};
	for (const Edge& e : edges)
		check (rowForVelocity (e.velocity) == e.row,
		       "velocity " + str (e.velocity) + " -> row " + str (e.row));

	// The panel's labels come from bandLow/bandHigh; they must describe
	// the SAME split rowForVelocity makes, with no gaps and no overlaps.
	for (int row = 0; row < kRows; ++row)
	{
		check (rowForVelocity (bandLow (row)) == row, "row " + str (row) + " label's low end is in the row");
		check (rowForVelocity (bandHigh (row)) == row, "row " + str (row) + " label's high end is in the row");
		if (row > 0)
			check (bandLow (row) == bandHigh (row - 1) + 1, "rows " + str (row - 1) + "/" + str (row) + " meet with no gap");
	}
	check (bandLow (0) == 1 && bandHigh (kRows - 1) == 127, "the bands cover 1..127");

	// VST3's float velocity back to MIDI, exactly, for every value.
	bool allExact = true;
	for (int v = 0; v <= 127; ++v)
		if (velocityTo127 (static_cast<float> (v) / 127.f) != v)
			allExact = false;
	check (allExact, "velocityTo127 inverts v/127 for every MIDI velocity");
}

//------------------------------------------------------------------------
// 4. Which cell plays: the asked-for row, else the nearest loaded
//------------------------------------------------------------------------
static void testChooseCell ()
{
	std::printf ("cell choice and fallback\n");

	auto bit = [] (int column, int row) { return 1u << cellIndex (column, row); };

	check (chooseCell (0, 0, 0u) == -1, "an empty column plays nothing");

	// One sample in a column plays at every velocity.
	for (int row = 0; row < kRows; ++row)
		check (chooseCell (2, row, bit (2, 1)) == cellIndex (2, 1),
		       "a lone sample in row 1 answers a row-" + str (row) + " hit");

	// The asked-for row wins when it is loaded.
	const unsigned full = bit (5, 0) | bit (5, 1) | bit (5, 2) | bit (5, 3);
	for (int row = 0; row < kRows; ++row)
		check (chooseCell (5, row, full) == cellIndex (5, row), "a full column plays row " + str (row) + " for row " + str (row));

	// Nearest, and a tie goes SOFTER.
	check (chooseCell (1, 1, bit (1, 0) | bit (1, 2)) == cellIndex (1, 0), "tie between rows 0 and 2 goes to the softer, row 0");
	check (chooseCell (1, 2, bit (1, 0) | bit (1, 3)) == cellIndex (1, 3), "row 3 is nearer to row 2 than row 0 is");
	check (chooseCell (1, 3, bit (1, 0)) == cellIndex (1, 0), "a hard hit falls all the way back to row 0");

	// Never another column's sample.
	check (chooseCell (4, 2, bit (3, 2) | bit (5, 2)) == -1, "neighbouring columns are never borrowed from");

	// Out of range asks.
	check (chooseCell (-1, 0, ~0u) == -1 && chooseCell (8, 0, ~0u) == -1, "bad column plays nothing");
	check (chooseCell (0, -1, ~0u) == -1 && chooseCell (0, 4, ~0u) == -1, "bad row plays nothing");

	// Cell numbering: across, a row at a time.
	check (cellIndex (0, 0) == 0 && cellIndex (7, 0) == 7 && cellIndex (0, 1) == 8
	       && cellIndex (7, 3) == 31, "cells number across the panel a row at a time");
	for (int cell = 0; cell < kCellCount; ++cell)
		check (cellIndex (columnOfCell (cell), rowOfCell (cell)) == cell, "cell " + str (cell) + " round-trips");

	check (chokes (3, 4), "closed hat chokes open hat");
	check (!chokes (4, 3), "open hat does not choke closed hat");
	bool noSelf = true;
	for (int c = 0; c < kColumns; ++c)
		if (chokes (c, c))
			noSelf = false;
	check (noSelf, "no column chokes itself");
}

//------------------------------------------------------------------------
// 5. The WAV reader
//------------------------------------------------------------------------
static void testWav ()
{
	std::printf ("WAV reader\n");

	// 16-bit mono: -32768, 0, 16384, 32767.
	{
		std::vector<unsigned char> d;
		for (int16_t v : { int16_t (-32768), int16_t (0), int16_t (16384), int16_t (32767) })
			put16 (d, static_cast<uint16_t> (v));
		SampleBuffer b;
		const SampleStatus s = parseWav (makeWav (1, 1, 44100, 16, d, true).data (),
		                                 makeWav (1, 1, 44100, 16, d, true).size (), b);
		check (s == SampleStatus::Loaded, "16-bit mono with an odd chunk before fmt loads");
		check (b.frameCount == 4 && b.sourceRate == 44100.0, "four frames at 44.1 k");
		if (b.frameCount == 4)
		{
			check (b.samples[0] == -1.f, "-32768 is exactly -1");
			check (b.samples[1] == -1.f, "mono is duplicated to the right");
			check (b.samples[4] == 0.5f, "16384 is 0.5");
			checkClose (b.samples[6], 32767.0 / 32768.0, 1e-9, "32767 is just under 1");
			check (b.peak == 1.f, "peak is recorded");
		}
	}

	// 24-bit stereo.
	{
		std::vector<unsigned char> d = { 0x00, 0x00, 0x40,   0x00, 0x00, 0xC0 };  // +0.5, -0.5
		const auto file = makeWav (1, 2, 48000, 24, d);
		SampleBuffer b;
		check (parseWav (file.data (), file.size (), b) == SampleStatus::Loaded, "24-bit stereo loads");
		check (b.frameCount == 1 && b.samples[0] == 0.5f && b.samples[1] == -0.5f, "24-bit values and sign");
	}

	// 32-bit float.
	{
		std::vector<unsigned char> d;
		const float values[] = { 0.25f, -0.75f };
		for (float f : values) { uint32_t u; std::memcpy (&u, &f, 4); put32 (d, u); }
		const auto file = makeWav (3, 1, 96000, 32, d);
		SampleBuffer b;
		check (parseWav (file.data (), file.size (), b) == SampleStatus::Loaded, "32-bit float loads");
		check (b.frameCount == 2 && b.samples[0] == 0.25f && b.samples[2] == -0.75f, "float values exact");
	}

	// 8-bit is unsigned.
	{
		std::vector<unsigned char> d = { 128, 255, 0 };
		const auto file = makeWav (1, 1, 22050, 8, d);
		SampleBuffer b;
		check (parseWav (file.data (), file.size (), b) == SampleStatus::Loaded, "8-bit loads");
		check (b.samples[0] == 0.f && b.samples[4] == -1.f, "8-bit 128 is silence, 0 is -1");
	}

	// Refusals, each with its own reason.
	{
		SampleBuffer b;
		const unsigned char junk[] = "this is not a wav file at all";
		check (parseWav (junk, sizeof (junk), b) == SampleStatus::NotWave, "junk is NotWave");
		check (parseWav (nullptr, 0, b) == SampleStatus::NotWave, "nothing is NotWave");

		std::vector<unsigned char> d (8, 0);
		const auto adpcm = makeWav (2, 1, 44100, 4, d);
		check (parseWav (adpcm.data (), adpcm.size (), b) == SampleStatus::UnsupportedFormat, "ADPCM is UnsupportedFormat");

		const auto empty = makeWav (1, 1, 44100, 16, {});
		check (parseWav (empty.data (), empty.size (), b) == SampleStatus::NoFrames, "no data is NoFrames");
		check (b.empty (), "a refused file leaves the buffer empty");

		// 21 s at 1 kHz, 8-bit: just over the cap.
		std::vector<unsigned char> longData (21000, 128);
		const auto tooLong = makeWav (1, 1, 1000, 8, longData);
		check (parseWav (tooLong.data (), tooLong.size (), b) == SampleStatus::TooLong, "21 s is TooLong");

		// Truncated: header claims more data than there is. Uses what is there.
		std::vector<unsigned char> d2;
		put16 (d2, 1000); put16 (d2, 2000);
		auto cut = makeWav (1, 1, 44100, 16, d2);
		cut[cut.size () - 4 - 4] = 0xFF;  // the data size field, low byte: now claims 255
		SampleBuffer c;
		check (parseWav (cut.data (), cut.size (), c) == SampleStatus::Loaded && c.frameCount == 2,
		       "a data chunk claiming too much reads what is there");
	}

	check (loadWavFile ("/nonexistent/definitely/not/here.wav", *std::make_unique<SampleBuffer> ())
	           == SampleStatus::Missing, "a missing file is Missing (file not found), not a crash");
	for (int s = 0; s <= static_cast<int> (SampleStatus::NoFrames); ++s)
		check (sampleStatusText (static_cast<SampleStatus> (s)) != nullptr, "status " + str (s) + " has text");
}

//------------------------------------------------------------------------
// 6. Paths from drags
//------------------------------------------------------------------------
static void testPaths ()
{
	std::printf ("paths\n");

	check (wavPathFromDragText ("/Users/andy/Kick.wav") == "/Users/andy/Kick.wav", "a plain path");
	check (wavPathFromDragText ("file:///Users/andy/My%20Kit/Snare%201.WAV\n")
	           == "/Users/andy/My Kit/Snare 1.WAV", "a Finder file URL, decoded, any case");
	check (wavPathFromDragText ("/a/one.wav\n/a/two.wav") == "/a/one.wav", "several files: the first");
	check (wavPathFromDragText ("/a/100%.wav") == "/a/100%.wav", "a literal per-cent survives");
	check (wavPathFromDragText ("/a/loop.mp3").empty (), "an mp3 is refused");
	check (wavPathFromDragText ("/a/.wav").empty (), "a hidden file with no name is refused");
	check (wavPathFromDragText ("/a/folder.wav/").empty (), "a directory is refused");
	check (wavPathFromDragText ("").empty (), "nothing is nothing");

	check (fileStemOf ("/x/y/Kick Hard.wav") == "Kick Hard", "the stem the cell shows");
	check (fileNameOf ("C:\\kit\\snare.wav") == "snare.wav", "a Windows path's name");
}

//------------------------------------------------------------------------
// 7. The smoother never steps hard enough to click
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
		check (s.coefficient () <= 0.01f, "coefficient <= 1% at " + str (static_cast<int> (rate)));

		float previous = s.value (), worst = 0.f;
		for (int i = 0; i < static_cast<int> (rate); ++i)
		{
			const float v = s.next ();
			worst = std::max (worst, std::fabs (v - previous));
			previous = v;
		}
		check (worst <= 0.011f, "no step over ~1% at " + str (static_cast<int> (rate)));
		checkClose (previous, 1.0, 1e-3, "reaches target at " + str (static_cast<int> (rate)));
	}
}

//------------------------------------------------------------------------
// 8. Unity is BIT-IDENTICAL, and half gain is not
//------------------------------------------------------------------------
static void testUnityIsBitIdentical ()
{
	std::printf ("unity: trim and playback are bit-identical\n");

	const int kBlock = 512;
	std::vector<float> reference (kBlock);
	for (int i = 0; i < kBlock; ++i)
		reference[i] = static_cast<float> (std::sin (i * 0.037) * 0.9 + ((i % 7) - 3) * 1e-6);
	reference[0] = 1.f; reference[1] = -1.f; reference[2] = 1e-7f;

	for (double rate : kRates)
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setOutputTrimDb (0.0);
		dsp.reset ();
		std::vector<float> l (reference), r (reference);
		dsp.applyTrim (l.data (), r.data (), kBlock);
		check (std::memcmp (l.data (), reference.data (), kBlock * sizeof (float)) == 0
		       && std::memcmp (r.data (), reference.data (), kBlock * sizeof (float)) == 0,
		       "trim bit-identical at 0 dB, " + str (static_cast<int> (rate)));
	}

	// THE WHOLE PATH: a sample at the host's rate, struck at unity, comes
	// out as the file's own samples, bit for bit.
	for (double rate : kRates)
	{
		auto sample = makeShapedSample (1000, rate);
		SampleDrumDsp dsp;
		dsp.setSampleRate (rate);
		dsp.setCellSample (cellIndex (0, 3), sample.get ());
		dsp.noteOn (36, 1.f, 0);

		std::vector<float> l, r;
		renderInto (dsp, l, r, 1200, 256);

		bool identical = true;
		for (int i = 0; i < 1000; ++i)
			if (l[i] != sample->samples[i * 2] || r[i] != sample->samples[i * 2 + 1])
				identical = false;
		check (identical, "a hit plays the file bit-identically at " + str (static_cast<int> (rate)));
		check (peakOf (l, 1000) == 0.f, "and stops when the file ends at " + str (static_cast<int> (rate)));
	}

	// NEGATIVE CONTROL. The same comparisons at half gain MUST fail -
	// proof that they can tell the difference at all.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setOutputTrimDb (gainToDb (0.5));
		dsp.reset ();
		std::vector<float> l (reference), r (reference);
		dsp.applyTrim (l.data (), r.data (), kBlock);
		check (std::memcmp (l.data (), reference.data (), kBlock * sizeof (float)) != 0,
		       "NEGATIVE CONTROL: half-gain trim is NOT bit-identical");
		checkClose (l[0], 0.5, 1e-6, "half gain really halves full scale");

		auto sample = makeShapedSample (1000, 48000.0);
		dsp.setCellSample (0, sample.get ());
		dsp.noteOn (36, velocityFor (10), 0);
		std::vector<float> pl, pr;
		renderInto (dsp, pl, pr, 1000, 256);
		bool identical = true;
		for (int i = 0; i < 1000; ++i)
			if (pl[i] != sample->samples[i * 2])
				identical = false;
		check (!identical, "NEGATIVE CONTROL: playback at half trim is NOT bit-identical");
	}
}

//------------------------------------------------------------------------
// 9. The right cell plays, at the right sample
//------------------------------------------------------------------------
static void testPlayback ()
{
	std::printf ("playback: velocity picks the cell, offset picks the sample\n");

	// Every cell of the snare column holds a DIFFERENT constant, so the
	// output level says which one played.
	std::shared_ptr<SampleBuffer> rows[kRows];
	for (int row = 0; row < kRows; ++row)
		rows[row] = makeSample (200, 0.1f * (row + 1));

	for (int row = 0; row < kRows; ++row)
	{
		for (int velocity : { bandLow (row), bandHigh (row) })
		{
			SampleDrumDsp dsp;
			dsp.setSampleRate (48000.0);
			for (int r = 0; r < kRows; ++r)
				dsp.setCellSample (cellIndex (1, r), rows[r].get ());

			check (dsp.cellForNote (38, velocityFor (velocity)) == cellIndex (1, row),
			       "cellForNote: snare at " + str (velocity) + " is row " + str (row));

			dsp.noteOn (38, velocityFor (velocity), 0);
			std::vector<float> l, r;
			renderInto (dsp, l, r, 100, 64);
			checkClose (l[50], 0.1 * (row + 1), 1e-6,
			            "snare at velocity " + str (velocity) + " plays row " + str (row) + "'s sample");
		}
	}

	// Sample-accurate start.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto s = makeSample (50, 0.5f);
		dsp.setCellSample (0, s.get ());
		dsp.syncCells (128);
		dsp.noteOn (36, 1.f, 37);
		std::vector<float> l (128), r (128);
		dsp.render (l.data (), r.data (), 128);
		check (peakOf (l, 0, 37) == 0.f, "silent before the note's offset");
		check (l[37] == 0.5f && l[86] == 0.5f && l[87] == 0.f, "starts exactly at offset 37 and lasts 50 samples");
	}

	// Two notes in one block, delivered OUT of order, still start on time.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto kick = makeSample (10, 0.25f);
		auto snare = makeSample (10, 0.5f);
		dsp.setCellSample (cellIndex (0, 3), kick.get ());
		dsp.setCellSample (cellIndex (1, 3), snare.get ());
		dsp.syncCells (64);
		dsp.noteOn (38, 1.f, 40);
		dsp.noteOn (36, 1.f, 5);
		std::vector<float> l (64), r (64);
		dsp.render (l.data (), r.data (), 64);
		check (l[5] == 0.25f && l[40] == 0.5f && l[20] == 0.f, "out-of-order events play at their own offsets");
	}

	// A second hit on the same drum OVERLAPS the first.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto s = makeSample (100, 0.25f);
		dsp.setCellSample (0, s.get ());
		dsp.syncCells (64);
		dsp.noteOn (36, 0.1f, 0);
		dsp.noteOn (36, 0.1f, 10);
		std::vector<float> l (64), r (64);
		dsp.render (l.data (), r.data (), 64);
		check (l[5] == 0.25f && l[20] == 0.5f, "two hits on one drum sum rather than cut");
		check (dsp.cellHits (0) == 2u, "and both count as hits for the panel");
	}

	// Ignored: unmapped note, velocity 0, empty column.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto s = makeSample (100, 0.25f);
		dsp.setCellSample (0, s.get ());
		dsp.noteOn (37, 1.f, 0);   // side stick: not mapped
		dsp.noteOn (36, 0.f, 0);   // velocity 0: a note-off
		dsp.noteOn (38, 1.f, 0);   // snare column is empty
		std::vector<float> l, r;
		renderInto (dsp, l, r, 64, 64);
		check (peakOf (l) == 0.f && dsp.activeVoices () == 0, "unmapped, velocity-0 and empty-column hits are silent");
	}

	// Audition plays THAT cell, ignoring the bands.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto soft = makeSample (100, 0.1f);
		auto hard = makeSample (100, 0.4f);
		dsp.setCellSample (cellIndex (6, 0), soft.get ());
		dsp.setCellSample (cellIndex (6, 3), hard.get ());
		dsp.auditionCell (cellIndex (6, 0));
		std::vector<float> l, r;
		renderInto (dsp, l, r, 64, 64);
		checkClose (l[10], 0.1, 1e-7, "audition plays the clicked cell");
		check (dsp.cellSounding (cellIndex (6, 0)) && !dsp.cellSounding (cellIndex (6, 3)), "and lights only that cell");
	}
}

//------------------------------------------------------------------------
// 10. Resampling
//------------------------------------------------------------------------
static void testResampling ()
{
	std::printf ("resampling to the host rate\n");

	struct Case { double file; double host; };
	const Case cases[] = { { 44100.0, 48000.0 }, { 48000.0, 44100.0 }, { 44100.0, 96000.0 }, { 96000.0, 44100.0 } };

	for (const Case& c : cases)
	{
		const int frames = 4410;
		auto s = makeSample (frames, 0.5f, c.file);
		SampleDrumDsp dsp;
		dsp.setSampleRate (c.host);
		dsp.setCellSample (0, s.get ());
		dsp.noteOn (36, 1.f, 0);

		const int total = static_cast<int> (frames * c.host / c.file) + 200;
		std::vector<float> l, r;
		renderInto (dsp, l, r, total, 512);

		int last = -1;
		for (int i = 0; i < total; ++i)
			if (l[i] != 0.f)
				last = i;
		const double seconds = (last + 1) / c.host;
		checkClose (seconds, frames / c.file, 2.0 / c.host,
		            "a " + str (static_cast<int> (c.file)) + " file lasts as long at " + str (static_cast<int> (c.host)));
		if (c.file <= c.host)
			check (peakOf (l) <= 0.5f + 1e-6f, "resampling does not overshoot a constant");
		else
		{
			// A FILE ABOVE THE HOST'S RATE is read band-limited (SincBank),
			// and a constant that starts and stops dead is a step: a
			// band-limited step rings, ~9 % (Gibbs), at its two ends. That
			// is the correct picture of the file at the lower rate, not an
			// error. What must hold is the level in between - unity DC
			// gain - and that the ringing is no more than Gibbs.
			const float mid = l[static_cast<std::size_t> (total / 3)];
			checkClose (mid, 0.5, 1e-4, "a " + str (static_cast<int> (c.file)) +
			            " file at " + str (static_cast<int> (c.host)) + ": a constant keeps its level (unity DC gain)");
			check (peakOf (l) <= 0.5f * 1.10f, "and its ends ring no more than a band-limited step (Gibbs, ~9 %)");
		}
	}
}

//------------------------------------------------------------------------
// 11. The hi-hat choke
//------------------------------------------------------------------------
static void testChoke ()
{
	std::printf ("hi-hat choke\n");

	for (double rate : { 44100.0, 96000.0 })
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (rate);
		auto open = makeSample (static_cast<int> (rate), 0.5f, rate);      // a second of open hat
		auto closed = makeSample (10, 0.f, rate);                          // silent, so the open hat is all we hear
		dsp.setCellSample (cellIndex (4, 3), open.get ());
		dsp.setCellSample (cellIndex (3, 3), closed.get ());

		dsp.noteOn (46, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 1000, 256);
		check (l[999] == 0.5f, "open hat rings");

		dsp.noteOn (42, 1.f, 0);
		const int fade = static_cast<int> (kChokeFadeSeconds * rate + 0.5);
		renderInto (dsp, l, r, fade + 100, 256);

		float worstStep = 0.f;
		for (int i = 1; i < fade + 100; ++i)
			worstStep = std::max (worstStep, std::fabs (l[i] - l[i - 1]));
		check (l[0] > 0.f && l[0] <= 0.5f, "the choke fades rather than cuts at " + str (static_cast<int> (rate)));
		check (peakOf (l, fade) == 0.f, "open hat is silent within the fade at " + str (static_cast<int> (rate)));
		check (worstStep < 0.5f / fade * 1.5f, "the choke has no step (no click) at " + str (static_cast<int> (rate)));
		check (!dsp.cellSounding (cellIndex (4, 3)), "the open hat's lamp goes out");
	}

	// The other way round does nothing.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto closed = makeSample (48000, 0.5f);
		auto open = makeSample (10, 0.f);
		dsp.setCellSample (cellIndex (3, 3), closed.get ());
		dsp.setCellSample (cellIndex (4, 3), open.get ());
		dsp.noteOn (42, 1.f, 0);
		dsp.noteOn (46, 1.f, 100);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 2000, 256);
		check (l[1999] == 0.5f, "an open hat does not choke a closed one");
	}
}

//------------------------------------------------------------------------
// 12. Replacing a sample under a playing voice
//------------------------------------------------------------------------
static void testReplacement ()
{
	std::printf ("replacing a cell while it plays\n");

	// With samples to render: the voice fades out INSIDE the block.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto a = makeSample (48000, 0.5f);
		auto b = makeSample (48000, 0.25f);
		dsp.setCellSample (0, a.get ());
		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 256, 256);

		dsp.setCellSample (0, b.get ());   // UI thread replaces it
		renderInto (dsp, l, r, 64, 64);    // a block SHORTER than the fade
		check (dsp.activeVoices () == 0, "the old voice is gone by the end of the very next block");
		check (l[0] > 0.f && l[63] < l[0], "and it faded rather than cut");

		// a is now free to be retired: nothing may read it again.
		a->samples.assign (a->samples.size (), 99.f);
		renderInto (dsp, l, r, 256, 256);
		check (peakOf (l) == 0.f, "nothing reads the retired buffer afterwards");
	}

	// A parameter-only block: stops at once.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto a = makeSample (48000, 0.5f);
		dsp.setCellSample (0, a.get ());
		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 256, 256);
		dsp.setCellSample (0, nullptr);
		dsp.syncCells (0);
		dsp.render (nullptr, nullptr, 0);
		check (dsp.activeVoices () == 0, "a zero-sample block ends the orphaned voice immediately");
	}

	// Unchanged cells are left alone.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto a = makeSample (48000, 0.5f);
		auto other = makeSample (100, 0.1f);
		dsp.setCellSample (0, a.get ());
		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 256, 256);
		dsp.setCellSample (5, other.get ());
		renderInto (dsp, l, r, 256, 256);
		check (l[255] == 0.5f, "changing another cell does not disturb a playing one");
	}
}

//------------------------------------------------------------------------
// 13. Voice limit and stealing
//------------------------------------------------------------------------
static void testVoiceLimit ()
{
	std::printf ("voice limit\n");

	SampleDrumDsp dsp;
	dsp.setSampleRate (48000.0);
	auto s = makeSample (48000, 0.01f);
	dsp.setCellSample (0, s.get ());

	for (int i = 0; i < 100; ++i)
		dsp.noteOn (36, 1.f, i);
	std::vector<float> l, r;
	renderInto (dsp, l, r, 128, 128);
	check (dsp.activeVoices () <= kMaxVoices + kSpareVoices, "never more voices than the pool");

	renderInto (dsp, l, r, 1024, 256);
	check (dsp.activeVoices () <= kMaxVoices, "once steals have faded, at most kMaxVoices play");
	checkClose (l[1023], 0.01 * kMaxVoices, 1e-5, "and exactly kMaxVoices are still sounding");

	// A flood beyond the event queue is dropped, not a crash.
	for (int i = 0; i < kMaxEventsPerBlock + 50; ++i)
		dsp.noteOn (36, 1.f, 0);
	renderInto (dsp, l, r, 64, 64);
	check (true, "an event flood beyond the queue survived");
}

//------------------------------------------------------------------------
// 14. Degenerate blocks
//------------------------------------------------------------------------
static void testDegenerateBlocks ()
{
	std::printf ("zero frames, null buffers, bad input\n");

	SampleDrumDsp dsp;
	dsp.setSampleRate (48000.0);
	auto s = makeSample (100, 0.5f);
	dsp.setCellSample (0, s.get ());
	dsp.noteOn (36, 1.f, 0);

	std::vector<float> l (512, 0.f), r (512, 0.f);
	dsp.render (l.data (), r.data (), 0);
	dsp.render (l.data (), r.data (), -1);
	dsp.render (nullptr, r.data (), 512);
	dsp.render (l.data (), nullptr, 512);
	dsp.applyTrim (nullptr, nullptr, 512);
	dsp.setCellSample (-1, s.get ());
	dsp.setCellSample (kCellCount, s.get ());
	dsp.auditionCell (-1);
	dsp.auditionCell (kCellCount);
	dsp.noteOn (36, 1.f, 100000);   // an offset past any block
	dsp.render (l.data (), r.data (), 16);
	check (l[15] == 0.5f, "an offset past the block plays on its last sample");
	check (dsp.cellHits (-1) == 0u && !dsp.cellSounding (99), "bad cell queries are safe");

	SampleBuffer empty;
	dsp.setCellSample (1, &empty);
	dsp.auditionCell (1);
	dsp.render (l.data (), r.data (), 16);
	check (!dsp.cellSounding (1), "an empty buffer is published as no buffer");

	dsp.setSampleRate (0.0);
	check (dsp.sampleRate () >= 1000.0, "an absurd sample rate falls back");
	dsp.setSampleRate (std::numeric_limits<double>::quiet_NaN ());
	check (dsp.sampleRate () >= 1000.0, "a NaN sample rate falls back");
	check (std::isfinite (dsp.currentTrimGain ()), "trim gain is still finite");
}


//------------------------------------------------------------------------
// 15. Each cell's own level
//------------------------------------------------------------------------
static void testCellLevels ()
{
	std::printf ("cell levels\n");

	// The law, at both ends and the middle.
	check (cellLevelGain (kCellLevelMinDb) == 0.0, "the bottom of the bar is SILENCE, not -40 dB");
	check (cellLevelGain (-100.0) == 0.0, "below the bottom is silence too");
	check (cellLevelGain (0.0) == 1.0, "0 dB is exactly unity");
	checkClose (cellLevelGain (-6.0), 0.501187234, 1e-9, "-6 dB");
	checkClose (cellLevelGain (-39.9), dbToGain (-39.9), 1e-12, "just above the bottom is a real (tiny) level");
	checkClose (cellLevelGain (kCellLevelMaxDb), 3.981071706, 1e-9, "+12 dB (top of range)");
	checkClose (cellLevelGain (40.0), dbToGain (kCellLevelMaxDb), 1e-12, "above the top is clamped");
	check (kCellLevelDefaultDb == 0.0, "every cell defaults to unity - a new kit plays its files as they are");

	// A settled level scales a hit; neighbours are untouched.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto s = makeSample (1000, 0.5f);
		dsp.setCellSample (cellIndex (0, 3), s.get ());
		dsp.setCellSample (cellIndex (1, 3), s.get ());
		dsp.setCellLevelDb (cellIndex (0, 3), -6.0);
		dsp.reset ();   // settle

		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 256, 256);
		checkClose (l[100], 0.5 * dbToGain (-6.0), 1e-6, "kick cell at -6 dB plays at -6 dB");

		dsp.noteOn (38, 1.f, 0);
		SampleDrumDsp fresh;   // (the kick is still ringing in dsp)
		fresh.setSampleRate (48000.0);
		fresh.setCellSample (cellIndex (1, 3), s.get ());
		fresh.noteOn (38, 1.f, 0);
		renderInto (fresh, l, r, 256, 256);
		check (l[100] == 0.5f, "the snare cell next to it is still at unity, bit for bit");

		// Two voices on one cell get the SAME gain.
		SampleDrumDsp two;
		two.setSampleRate (48000.0);
		two.setCellSample (0, s.get ());
		two.setCellLevelDb (0, -6.0);
		two.reset ();
		two.noteOn (36, 0.1f, 0);
		two.noteOn (36, 0.1f, 0);
		renderInto (two, l, r, 256, 256);
		checkClose (l[100], 2 * 0.5 * dbToGain (-6.0), 1e-6, "two voices on one cell share its level");
	}

	// Pulled to the bottom: silent.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto s = makeSample (1000, 0.5f);
		dsp.setCellSample (0, s.get ());
		dsp.setCellLevelDb (0, kCellLevelMinDb);
		dsp.reset ();
		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 512, 256);
		check (peakOf (l) == 0.f, "a cell pulled to the bottom is exactly silent");
	}

	// Moved WHILE A SAMPLE RINGS: smoothed at every rate, and it arrives.
	for (double rate : kRates)
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (rate);
		const int frames = static_cast<int> (rate * 2);
		auto s = makeSample (frames, 0.5f, rate);
		dsp.setCellSample (0, s.get ());
		dsp.noteOn (36, 1.f, 0);

		std::vector<float> l, r;
		renderInto (dsp, l, r, 512, 512);
		dsp.setCellLevelDb (0, kCellLevelMinDb);   // yank it to off
		renderInto (dsp, l, r, static_cast<int> (rate), 64);   // small blocks, many ramps

		float worst = std::fabs (l[0] - 0.5f);
		for (std::size_t i = 1; i < l.size (); ++i)
			worst = std::max (worst, std::fabs (l[i] - l[i - 1]));
		check (worst <= 0.5f * 0.011f,
		       "a level move never steps more than ~1% per sample at " + str (static_cast<int> (rate)));
		check (l.back () == 0.f && dsp.currentCellGain (0) == 0.f,
		       "and it arrives - exactly silent within a second at " + str (static_cast<int> (rate)));

		dsp.setCellLevelDb (0, 0.0);
		renderInto (dsp, l, r, static_cast<int> (rate) / 2, 512);
		check (dsp.currentCellGain (0) == 1.f, "back up, it arrives at exactly unity at " + str (static_cast<int> (rate)));
	}

	// Bad cell indices are ignored.
	{
		SampleDrumDsp dsp;
		dsp.setCellLevelDb (-1, -6.0);
		dsp.setCellLevelDb (kCellCount, -6.0);
		check (dsp.currentCellGain (-1) == 0.f && dsp.cellLevelDb (99) == kCellLevelDefaultDb,
		       "out-of-range cells are safe");
	}
}

//------------------------------------------------------------------------
// 16. Each column's note
//------------------------------------------------------------------------
static void testColumnNotes ()
{
	std::printf ("column notes\n");

	// Formatting - the one copy the host and the panel share.
	char t[16];
	formatNote (36, t, sizeof (t)); check (std::string (t) == "36 C1", "36 is C1");
	formatNote (42, t, sizeof (t)); check (std::string (t) == "42 F#1", "42 is F#1");
	formatNote (60, t, sizeof (t)); check (std::string (t) == "60 C3", "60 is C3 (the Cubase/Logic convention)");
	formatNote (0, t, sizeof (t));  check (std::string (t) == "0 C-2", "0 is C-2");
	formatNote (127, t, sizeof (t)); check (std::string (t) == "127 G8", "127 is G8");
	check (std::atoi (t) == 127, "the NUMBER leads, so the host's text-to-value reading gets it back");

	// Defaults: a new instance plays as before.
	SampleDrumDsp fresh;
	bool defaults = true;
	for (int c = 0; c < kColumns; ++c)
		if (fresh.columnNote (c) != kDrumMap[c].note)
			defaults = false;
	check (defaults, "every column starts on its GM note");

	auto kick = makeSample (100, 0.25f);
	auto snare = makeSample (100, 0.5f);

	// Move the kick column to 35: 35 plays it, 36 no longer does.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setCellSample (cellIndex (0, 3), kick.get ());
		dsp.setColumnNote (0, 35);
		check (dsp.columnNote (0) == 35, "the kick column is on 35");

		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 64, 64);
		check (peakOf (l) == 0.f, "its old note, 36, now plays nothing");

		dsp.noteOn (35, 1.f, 0);
		renderInto (dsp, l, r, 64, 64);
		check (l[10] == 0.25f, "its new note, 35, plays the kick");
		check (dsp.cellForNote (35, 1.f) == cellIndex (0, 3), "cellForNote follows the new note");
	}

	// Two columns on one note: ONE HIT PLAYS BOTH (layering).
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setCellSample (cellIndex (0, 3), kick.get ());
		dsp.setCellSample (cellIndex (1, 3), snare.get ());
		dsp.setColumnNote (1, 36);
		check (dsp.columnsForNote (36) == 0x3u, "36 now names columns 0 and 1");
		check (dsp.columnsForNote (38) == 0u, "and 38 names nothing");
		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 64, 64);
		checkClose (l[10], 0.75, 1e-7, "a shared note layers both columns");
		check (dsp.cellHits (cellIndex (0, 3)) == 1u && dsp.cellHits (cellIndex (1, 3)) == 1u,
		       "and both cells light");
	}

	// The choke is by COLUMN: hats moved to other notes still choke.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		auto open = makeSample (48000, 0.5f);
		auto closed = makeSample (10, 0.f);
		dsp.setCellSample (cellIndex (4, 3), open.get ());
		dsp.setCellSample (cellIndex (3, 3), closed.get ());
		dsp.setColumnNote (3, 44);   // pedal hat
		dsp.setColumnNote (4, 26);
		dsp.noteOn (26, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, 512, 256);
		dsp.noteOn (44, 1.f, 0);
		renderInto (dsp, l, r, 1024, 256);
		check (l[1023] == 0.f, "the hat columns choke on whatever notes they are given");
	}

	// Range: clamped, and bad columns ignored.
	{
		SampleDrumDsp dsp;
		dsp.setColumnNote (2, 200);
		check (dsp.columnNote (2) == 127, "a note above 127 is clamped");
		dsp.setColumnNote (2, -5);
		check (dsp.columnNote (2) == 0, "a note below 0 is clamped");
		dsp.setColumnNote (-1, 40);
		dsp.setColumnNote (kColumns, 40);
		check (dsp.columnNote (-1) == -1 && dsp.columnNote (kColumns) == -1, "bad columns are ignored");
		check (dsp.columnsForNote (200) == 0u, "an impossible note plays nothing");
	}
}

//------------------------------------------------------------------------
// 17. The output level
//------------------------------------------------------------------------
static void measureLevels ()
{
	std::printf ("output level\n");

	const int kBlock = 4096;

	// The default patch: no samples loaded, trim 0 dB.
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setOutputTrimDb (0.0);
		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, kBlock, 512);
		std::printf ("  default patch (empty grid), kick at 127: L %s, R %s\n",
		             std::isfinite (peakDbFS (l)) ? std::to_string (peakDbFS (l)).c_str () : "-inf dBFS (silence)",
		             std::isfinite (peakDbFS (r)) ? std::to_string (peakDbFS (r)).c_str () : "-inf dBFS (silence)");
		check (!std::isfinite (peakDbFS (l)), "an empty grid renders exact silence");

		// render() must CLEAR the buffer, not add to what the host left.
		std::vector<float> dirtyL (kBlock, 0.5f), dirtyR (kBlock, -0.5f);
		dsp.render (dirtyL.data (), dirtyR.data (), kBlock);
		check (peakOf (dirtyL) == 0.f && peakOf (dirtyR) == 0.f, "render clears the host's buffer");
	}

	// A full-scale sample, struck once: UNITY, so its own peak.
	{
		auto s = makeShapedSample (2000, 48000.0);
		for (float& v : s->samples)
			v /= 0.7f;   // scale the shape to full scale
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setCellSample (0, s.get ());
		dsp.noteOn (36, 1.f, 0);
		std::vector<float> l, r;
		renderInto (dsp, l, r, kBlock, 512);
		std::printf ("  one full-scale hit: %+.2f dBFS\n", peakDbFS (l));
		checkClose (peakDbFS (l), 0.0, 0.01, "one hit plays at the file's own level (unity)");

		// Two full-scale drums on the same sample: the headroom question.
		dsp.reset ();
		dsp.setCellSample (1, s.get ());
		dsp.noteOn (36, 1.f, 0);
		dsp.noteOn (38, 1.f, 0);
		renderInto (dsp, l, r, kBlock, 512);
		std::printf ("  two full-scale hits together: %+.2f dBFS  <- over 0: needs Output Trim or quieter samples\n",
		             peakDbFS (l));
		check (peakDbFS (l) > 5.9, "voices SUM - two coincident full-scale hits reach +6 dBFS");
	}
}

//------------------------------------------------------------------------
// Projects that travel: factory paths and whole-file reads
//------------------------------------------------------------------------
static void testPortability ()
{
	const std::string root = kFactorySampleRoot;

	// isFactoryPath is TEXTUAL, and only for things INSIDE the root.
	check (isFactoryPath (root + "/Kits/kick.wav"),     "a file inside the factory root is factory");
	check (isFactoryPath (root + "/snare.wav"),         "a file directly in the root is factory");
	check (!isFactoryPath (root),                       "the root itself is not a file");
	check (!isFactoryPath (root + "/"),                 "the root with a slash is not a file");
	check (!isFactoryPath (root + "Extra/kick.wav"),    "a sibling whose name starts with the root is not factory");
	check (!isFactoryPath ("/Users/andy/Samples/kick.wav"), "a user's own file is not factory");
	check (!isFactoryPath (""),                         "an empty path is not factory");
	check (root.compare (0, 14, "/Users/Shared/") == 0, "the root is under /Users/Shared - the same on every Mac");

	check (std::string (sampleStatusText (SampleStatus::Missing)) == "file not found",
	       "Missing reads 'file not found'");
	check (kLastSampleStatus == SampleStatus::Missing, "kLastSampleStatus is the last value");
	check (static_cast<int> (SampleStatus::NoFrames) == 6 && static_cast<int> (SampleStatus::Missing) == 7,
	       "Missing was APPENDED - the old values keep their numbers");

	char dirTemplate[] = "/tmp/sampledrum-tests-XXXXXX";
	const char* dir = mkdtemp (dirTemplate);
	check (dir != nullptr, "a scratch directory");
	if (dir == nullptr)
		return;
	const std::string base = dir;

	const std::vector<unsigned char> pcm = { 0x00, 0x40, 0x00, 0xC0 };   // +0.5, -0.5
	const std::vector<unsigned char> wav = makeWav (1, 1, 44100, 16, pcm);
	const std::string good = base + "/good.wav";
	{
		std::ofstream f (good, std::ios::binary);
		f.write (reinterpret_cast<const char*> (wav.data ()), static_cast<std::streamsize> (wav.size ()));
	}
	const std::string empty = base + "/empty.wav";
	{ std::ofstream f (empty, std::ios::binary); }

	FileBytes bytes;
	check (readWholeFile (good, kMaxFileBytes, bytes) == FileReadResult::Ok, "a real file reads Ok");
	check (bytes && *bytes == wav,                     "... byte for byte");

	check (readWholeFile (base + "/nope.wav", kMaxFileBytes, bytes) == FileReadResult::Missing,
	       "no file at the path is Missing");
	check (!bytes,                                     "... and gives no bytes");
	check (readWholeFile ("", kMaxFileBytes, bytes) == FileReadResult::Missing, "an empty path is Missing");
	check (readWholeFile (empty, kMaxFileBytes, bytes) == FileReadResult::Unreadable,
	       "an empty file is Unreadable, not Missing");
	check (readWholeFile (base, kMaxFileBytes, bytes) == FileReadResult::Unreadable,
	       "a directory is Unreadable");
	check (readWholeFile (good, wav.size () - 1, bytes) == FileReadResult::TooBig,
	       "a file over the ceiling is TooBig");
	check (readWholeFile (good, wav.size (), bytes) == FileReadResult::Ok,
	       "a file exactly at the ceiling is Ok");

	check (statusOfRead (FileReadResult::Missing) == SampleStatus::Missing,       "Missing -> Missing");
	check (statusOfRead (FileReadResult::Unreadable) == SampleStatus::Unreadable, "Unreadable -> Unreadable");
	check (statusOfRead (FileReadResult::TooBig) == SampleStatus::TooLong,        "TooBig -> TooLong");

	SampleBuffer buffer;
	check (loadWavFile (base + "/nope.wav", buffer) == SampleStatus::Missing,
	       "loadWavFile tells a missing file apart");
	check (loadWavFile (good, buffer) == SampleStatus::Loaded && buffer.frameCount == 2,
	       "loadWavFile still loads a real one");

	// THE EMBEDDED COPY IS ENOUGH: once the bytes are in hand, the file on
	// disk can go and the cell still decodes to the same audio.
	FileBytes kept;
	readWholeFile (good, kMaxFileBytes, kept);
	std::remove (good.c_str ());
	SampleBuffer fromDisk, fromCopy;
	check (loadWavFile (good, fromDisk) == SampleStatus::Missing, "the file is gone from disk");
	check (kept && parseWav (kept->data (), kept->size (), fromCopy) == SampleStatus::Loaded
	           && fromCopy.samples == std::vector<float> ({ 0.5f, 0.5f, -0.5f, -0.5f }),
	       "... and the kept bytes still decode to the same audio");

	std::remove (empty.c_str ());
	rmdir (dir);
}

//------------------------------------------------------------------------
// Cell-to-cell drags: the payload, and the embedded copy travelling
//------------------------------------------------------------------------
static FileBytes bytesOf (unsigned char seed)
{
	return std::make_shared<std::vector<unsigned char>> (std::vector<unsigned char> (16, seed));
}

static void testCellDrag ()
{
	const std::uint64_t me = 0x0123456789ABCDEFull;

	{
		const std::string packed = encodeCellDrag (me, 17, "/Users/andy/Kits/kick 01.wav");
		std::uint64_t owner = 0; int cell = -1; std::string path;
		check (decodeCellDrag (packed, &owner, &cell, &path), "a cell drag decodes");
		check (owner == me && cell == 17 && path == "/Users/andy/Kits/kick 01.wav",
		       "... owner, cell and path intact");
		check (decodeCellDrag (packed + std::string (1, '\0'), nullptr, nullptr, &path)
		       && path == "/Users/andy/Kits/kick 01.wav", "... with a trailing NUL from the platform");
		check (decodeCellDrag (encodeCellDrag (0, 0, "/a.wav"), &owner, &cell, nullptr)
		       && owner == 0 && cell == 0, "owner 0, cell 0");
		check (decodeCellDrag (encodeCellDrag (~0ull, kCellCount - 1, "/a.wav"), &owner, &cell, nullptr)
		       && owner == ~0ull && cell == kCellCount - 1, "the largest owner and last cell");
	}
	check (encodeCellDrag (me, 3, "").empty (),          "an empty cell encodes to nothing");
	check (encodeCellDrag (me, -1, "/a.wav").empty (),   "a bad cell encodes to nothing");
	check (encodeCellDrag (me, kCellCount, "/a.wav").empty (), "... at either end");
	check (encodeCellDrag (me, 3, "/a\nb.wav").empty (), "a path with a newline encodes to nothing");

	check (!decodeCellDrag ("/Users/andy/kick.wav", nullptr, nullptr, nullptr), "a plain path is not a cell drag");
	check (!decodeCellDrag ("file:///Users/andy/kick.wav", nullptr, nullptr, nullptr), "nor is a file URL");
	check (!decodeCellDrag ("", nullptr, nullptr, nullptr),                 "nor is nothing");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcdef/3", nullptr, nullptr, nullptr), "no path");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcdef/3\n", nullptr, nullptr, nullptr), "an empty path");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcdef/\n/a.wav", nullptr, nullptr, nullptr), "no cell");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcdef/32\n/a.wav", nullptr, nullptr, nullptr), "cell 32");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcdef/99999999999999\n/a.wav", nullptr, nullptr, nullptr),
	       "a huge cell, without overflow");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcdef/3x\n/a.wav", nullptr, nullptr, nullptr), "junk after the cell");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcde/3\n/a.wav", nullptr, nullptr, nullptr), "a short owner");
	check (!decodeCellDrag ("SampleDrum/cell:0123456789abcdeg/3\n/a.wav", nullptr, nullptr, nullptr), "a non-hex owner");
	check (!decodeCellDrag ("Project6/slot:3\n/a.wav", nullptr, nullptr, nullptr), "Project6's pad drag is not ours");
	check (wavPathFromDragText (encodeCellDrag (me, 3, "/a.wav")).empty (),
	       "a cell drag is never mistaken for a file drop");

	// THE CARRY RULE. Paths as the processor's mPaths would hold them.
	const std::string kick = "/k.wav", snare = "/s.wav", hat = "/h.wav";
	{
		// MOVE into an empty cell: a copy to `to`, then `from` emptied.
		CellFiles f;
		f.keep (0, bytesOf (1));
		f.replace (5, "", kick, 0, kick);
		check (f.get (5) && (*f.get (5))[0] == 1, "a moved cell carries its embedded copy");
		f.replace (0, kick, "", 5, kick);
		check (!f.get (0),                        "... and the source lets go of it");
		check (f.get (5) && (*f.get (5))[0] == 1, "... while the target keeps it");
	}
	{
		// SWAP: to <- from's file; then from <- to's OLD file.
		CellFiles f;
		f.keep (0, bytesOf (1));                // kick in 0
		f.keep (9, bytesOf (2));                // snare in 9
		f.replace (9, snare, kick, 0, kick);
		check (f.get (9) && (*f.get (9))[0] == 1, "swap, first half: the target gets the kick");
		f.replace (0, kick, snare, 9, kick);    // 9 now holds the kick; its snare is PREVIOUS
		check (f.get (0) && (*f.get (0))[0] == 2, "swap, second half: the source gets the snare from 'previous'");
		check (f.get (9) && (*f.get (9))[0] == 1, "... and the target still has the kick");
	}
	{
		// COPY: the source is untouched and both cells share the bytes.
		CellFiles f;
		f.keep (3, bytesOf (7));
		f.replace (4, "", hat, 3, hat);
		check (f.get (3) && f.get (4) && f.get (3) == f.get (4), "a copy shares the same immutable bytes");
	}
	{
		// A FILE FROM OUTSIDE carries nothing, so loadCell reads the disk.
		CellFiles f;
		f.keep (0, bytesOf (1));
		f.replace (0, kick, kick, -1, "");
		check (!f.get (0), "a drop from outside - even of the same file - reads the disk afresh");
	}
	{
		// A CLEAR lets go of the copy.
		CellFiles f;
		f.keep (6, bytesOf (1));
		f.replace (6, kick, "", -1, "");
		check (!f.get (6), "a cleared cell carries nothing into the next save");
	}
	{
		// ONLY FOR THE FILE THE BYTES WERE READ FROM.
		CellFiles f;
		f.keep (0, bytesOf (1));
		f.replace (5, "", snare, 0, kick);
		check (!f.get (5), "a path that does not match the source carries nothing");
	}
	{
		// THE PREVIOUS FILE LASTS ONE MESSAGE. After an unrelated change,
		// a late second half finds nothing and reads the disk.
		CellFiles f;
		f.keep (9, bytesOf (2));
		f.replace (9, snare, kick, -1, "");
		f.replace (20, "", hat, -1, "");
		f.replace (0, "", snare, 9, kick);
		check (!f.get (0), "a held previous file is let go at the next cell message");
	}
	{
		// setState wipes what was held.
		CellFiles f;
		f.keep (9, bytesOf (2));
		f.replace (9, snare, kick, -1, "");
		FileBytes none[kCellCount];
		f.restore (none);
		f.replace (0, "", snare, 9, "");
		check (!f.get (0), "restore forgets the previous files");
		FileBytes all[kCellCount];
		f.keep (31, bytesOf (4));
		f.snapshot (all);
		check (all[31] && (*all[31])[0] == 4 && !all[30], "snapshot returns what is held");
	}
	{
		CellFiles f;
		f.replace (-1, "", kick, 0, kick);
		f.replace (kCellCount, "", kick, 0, kick);
		f.keep (kCellCount, bytesOf (1));
		check (!f.get (-1) && !f.get (kCellCount), "bad cells are refused, not clamped");
	}
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------
// 10b. A file above the host's rate is band-limited, not aliased
//------------------------------------------------------------------------
namespace {
/** Level of `hz` in `x` (from `from`, `n` samples), Blackman-windowed, dB
    re a full-scale sine. */
double toneDb (const std::vector<float>& x, int from, int n, double hz, double fs)
{
	const double pi = 3.14159265358979323846;
	double re = 0.0, im = 0.0, wsum = 0.0;
	for (int i = 0; i < n; ++i)
	{
		const double w = 0.42 - 0.5 * std::cos (2.0 * pi * i / (n - 1)) + 0.08 * std::cos (4.0 * pi * i / (n - 1));
		const double v = x[static_cast<std::size_t> (from + i)];
		re += w * v * std::cos (2.0 * pi * hz * i / fs);
		im -= w * v * std::sin (2.0 * pi * hz * i / fs);
		wsum += w;
	}
	return 20.0 * std::log10 (std::sqrt (re * re + im * im) / (wsum / 2.0) + 1e-30);
}

std::shared_ptr<SampleBuffer> sineSample (double hz, double rate, int frames)
{
	auto s = makeSample (frames, 0.f, rate);
	for (int i = 0; i < frames; ++i)
		s->samples[static_cast<std::size_t> (i) * 2] = s->samples[static_cast<std::size_t> (i) * 2 + 1]
		    = static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * hz * i / rate));
	s->peak = 1.f;
	return s;
}

std::vector<float> playAt (const std::shared_ptr<SampleBuffer>& s, double host, int total)
{
	SampleDrumDsp dsp;
	dsp.setSampleRate (host);
	dsp.setCellSample (0, s.get ());
	dsp.noteOn (36, 1.f, 0);
	std::vector<float> l, r;
	renderInto (dsp, l, r, total, 512);
	return l;
}
} // namespace

static void testBandLimited ()
{
	std::printf ("a file above the host's rate is band-limited\n");

	// 30 kHz in a 96 kHz file cannot exist at 48 kHz; read linearly at a
	// step of 2 it came out folded to 18 kHz at 0 dB.
	const auto high = sineSample (30000.0, 96000.0, 96000);
	const auto out = playAt (high, 48000.0, 40000);
	const double folded = toneDb (out, 4000, 32768, 18000.0, 48000.0);

	// NEGATIVE CONTROL: the linear read, as the voice used to do it.
	std::vector<float> linear (40000);
	for (int i = 0; i < 40000; ++i)
		linear[static_cast<std::size_t> (i)] = high->samples[static_cast<std::size_t> (2 * i) * 2];
	const double foldedLinear = toneDb (linear, 4000, 32768, 18000.0, 48000.0);
	std::printf ("    30 kHz in a 96 k file, at 48 k: folded to 18 kHz %.1f dB (linear read %.1f dB)\n",
	             folded, foldedLinear);
	check (folded < -65.0, "96 k file at 48 k: 30 kHz does not fold back to 18 kHz");
	check (foldedLinear > -3.0, "NEGATIVE CONTROL: the linear read folds it at nearly full level");

	// What fits, passes: 10 kHz in a 96 k file is 10 kHz at 48 k.
	const double pass = toneDb (playAt (sineSample (10000.0, 96000.0, 96000), 48000.0, 40000), 4000, 32768, 10000.0, 48000.0);
	std::printf ("    10 kHz in a 96 k file, at 48 k: %.2f dB\n", pass);
	check (std::fabs (pass) < 0.5, "96 k file at 48 k: 10 kHz passes within 0.5 dB");

	// A rate nobody built a bank for in advance gets one when it plays.
	const double odd = toneDb (playAt (sineSample (30000.0, 64000.0, 64000), 48000.0, 40000), 4000, 32768, 18000.0, 48000.0);
	std::printf ("    30 kHz in a 64 k file, at 48 k: folded to 18 kHz %.1f dB\n", odd);
	check (odd < -65.0, "64 k file at 48 k (an unusual rate): 30 kHz does not fold back");

	// The guard process() runs under. Every value through memory: the
	// guard orders memory accesses round its mode switch.
	if (ScopedFlushDenormals::supported ())
	{
		volatile float tiny = 1.0e-37f, half = 0.01f;
		volatile float before = tiny * half;
		volatile float inside = 1.f;
		{ ScopedFlushDenormals guard; inside = tiny * half; }
		volatile float after = tiny * half;
		check (before != 0.f && inside == 0.f && after == before,
		       "ScopedFlushDenormals: flush-to-zero inside, the host's mode restored after");
	}
}

//------------------------------------------------------------------------
// THE OUTPUT ISOLATOR
//------------------------------------------------------------------------

/** A sine's level through `iso`, in dB relative to the input: Hann-windowed
    I/Q demodulation of input and output over at least ten cycles, after
    the filters have settled - accurate to well under 0.01 dB, and
    independent of the phase shift. */
double isolatorGainDb (Isolator& iso, double hz, double rate)
{
	const int settle = static_cast<int> (0.3 * rate);
	const int window = static_cast<int> (std::max (0.2 * rate, 10.0 * rate / hz));
	const int total = settle + window;

	std::vector<float> left (static_cast<size_t> (total)), right;
	for (int i = 0; i < total; ++i)
		left[i] = static_cast<float> (0.25 * std::sin (2.0 * M_PI * hz * i / rate));
	const std::vector<float> input = left;
	right = left;

	for (int at = 0; at < total; at += 512)
	{
		const int n = std::min (512, total - at);
		iso.process (left.data () + at, right.data () + at, n);
	}

	auto level = [&] (const std::vector<float>& y)
	{
		double si = 0.0, co = 0.0;
		for (int i = 0; i < window; ++i)
		{
			const int k = settle + i;
			const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (window - 1));
			const double ph = 2.0 * M_PI * hz * k / rate;
			si += w * y[k] * std::sin (ph);
			co += w * y[k] * std::cos (ph);
		}
		return std::sqrt (si * si + co * co);
	};

	const double in = level (input), out = level (left);
	return (out <= 0.0) ? -300.0 : 20.0 * std::log10 (out / in);
}

/** A fresh isolator at `rate`, settled at the given settings. */
void setIsolator (Isolator& iso, double rate, double bassDb, double trebleDb,
                  double bassHz = kBassHzDefault, double trebleHz = kTrebleHzDefault)
{
	iso.setBassDb (bassDb);
	iso.setTrebleDb (trebleDb);
	iso.setBassHz (bassHz);
	iso.setTrebleHz (trebleHz);
	iso.setSampleRate (rate);   // snaps
}

static void testIsolator ()
{
	std::printf ("output isolator\n");

	// ---- the shared laws ---------------------------------------------
	check (isolatorGain (kIsolatorMinDb) == 0.0, "isolator: the bottom of the range is a kill (exactly 0)");
	check (isolatorGain (-1000.0) == 0.0, "isolator: below the range is a kill");
	check (isolatorGain (0.0) == 1.0, "isolator: 0 dB is exactly 1");
	check (isolatorGain (7e-15) == 1.0 && isolatorGain (-3e-6) == 1.0,
	       "isolator: a 0 dB default that came through a normalised value is still exactly 1");
	check (isolatorGain (0.01) != 1.0, "isolator: 0.01 dB is not snapped to unity");
	checkClose (isolatorGain (6.0), std::pow (10.0, 6.0 / 20.0), 1e-12, "isolator: +6 dB");
	checkClose (isolatorGain (60.0), std::pow (10.0, 6.0 / 20.0), 1e-12, "isolator: clamped at +6 dB");
	checkClose (isolatorGain (-39.9), std::pow (10.0, -39.9 / 20.0), 1e-12, "isolator: just above the kill is -39.9 dB");

	checkClose (logRangeToPlain (0.0, kBassHzMin, kBassHzMax), kBassHzMin, 1e-9, "log range: 0 -> min");
	checkClose (logRangeToPlain (1.0, kBassHzMin, kBassHzMax), kBassHzMax, 1e-9, "log range: 1 -> max");
	checkClose (logRangeToPlain (0.5, kTrebleHzMin, kTrebleHzMax),
	            std::sqrt (kTrebleHzMin * kTrebleHzMax), 1e-9, "log range: the middle is the geometric middle");
	checkClose (logRangeToPlain (-1.0, kBassHzMin, kBassHzMax), kBassHzMin, 1e-9, "log range: clamped below");
	checkClose (logRangeToNormalized (1e9, kBassHzMin, kBassHzMax), 1.0, 1e-12, "log range: clamped above");
	{
		double worst = 0.0;
		for (int i = 0; i <= 1000; ++i)
		{
			const double n = i / 1000.0;
			worst = std::max (worst, std::fabs (logRangeToNormalized (
				logRangeToPlain (n, kTrebleHzMin, kTrebleHzMax), kTrebleHzMin, kTrebleHzMax) - n));
		}
		checkClose (worst, 0.0, 1e-12, "log range: round trip");
	}
	check (kBassHzMax < kTrebleHzMin, "isolator: the two frequency ranges do not overlap");

	const double freqs[] = { 20, 50, 100, 200, 350, 500, 1000, 1500, 2000, 3500, 5000, 8000, 12000, 16000, 20000 };

	for (double rate : kRates)
	{
		const std::string r = " @" + str (static_cast<int> (rate));

		// ---- 0 dB / 0 dB: the input, bit for bit ---------------------
		{
			Isolator iso;
			setIsolator (iso, rate, 0.0, 0.0);
			std::vector<float> left (4096), right (4096);
			std::uint32_t seed = 12345u;
			for (size_t i = 0; i < left.size (); ++i)
			{
				seed = seed * 1664525u + 1013904223u;
				left[i] = static_cast<float> (static_cast<int> (seed >> 8) % 20001 - 10000) / 10000.f;
				right[i] = -left[i] * 0.5f;
			}
			const std::vector<float> l0 = left, r0 = right;
			for (size_t at = 0; at < left.size (); at += 256)
				iso.process (left.data () + at, right.data () + at, 256);
			check (std::memcmp (l0.data (), left.data (), left.size () * sizeof (float)) == 0
			       && std::memcmp (r0.data (), right.data (), right.size () * sizeof (float)) == 0,
			       "isolator: 0 dB / 0 dB is bit-identical" + r);
			check (iso.mix () == 0.0, "isolator: 0 dB / 0 dB is dry" + r);
		}

		// ---- the band sum itself is flat, at the default and both ends
		{
			const double pairs[][2] = { { kBassHzDefault, kTrebleHzDefault },
			                            { kBassHzMin, kTrebleHzMin },
			                            { kBassHzMax, kTrebleHzMax } };
			for (const auto& pair : pairs)
			{
				double worst = 0.0;
				for (double hz : freqs)
				{
					if (hz > 0.45 * rate)
						continue;
					Isolator iso;
					setIsolator (iso, rate, 0.0, 0.0, pair[0], pair[1]);
					iso.forceFilteredForTest (true);
					worst = std::max (worst, std::fabs (isolatorGainDb (iso, hz, rate)));
				}
				checkClose (worst, 0.0, 0.01, "isolator: filtered path flat within 0.01 dB, "
				            + str (static_cast<int> (pair[0])) + "/" + str (static_cast<int> (pair[1])) + " Hz" + r);
			}
		}

		// ---- kills ---------------------------------------------------
		{
			Isolator iso;
			setIsolator (iso, rate, kIsolatorMinDb, 0.0);
			const double low = isolatorGainDb (iso, 50.0, rate);
			setIsolator (iso, rate, kIsolatorMinDb, 0.0);
			const double mid = isolatorGainDb (iso, 1000.0, rate);
			check (low < -40.0, "isolator: bass kill takes 50 Hz down > 40 dB (" + std::to_string (low) + ")" + r);
			checkClose (mid, 0.0, 0.1, "isolator: bass kill leaves 1 kHz" + r);
		}
		{
			Isolator iso;
			setIsolator (iso, rate, 0.0, kIsolatorMinDb);
			const double high = isolatorGainDb (iso, 14000.0, rate);
			setIsolator (iso, rate, 0.0, kIsolatorMinDb);
			const double mid = isolatorGainDb (iso, 1000.0, rate);
			check (high < -40.0, "isolator: treble kill takes 14 kHz down > 40 dB (" + std::to_string (high) + ")" + r);
			checkClose (mid, 0.0, 0.1, "isolator: treble kill leaves 1 kHz" + r);
		}
		{
			Isolator iso;
			setIsolator (iso, rate, kIsolatorMinDb, kIsolatorMinDb);
			const double mid = isolatorGainDb (iso, 1000.0, rate);
			checkClose (mid, 0.0, 0.2, "isolator: both killed leaves the mid band" + r);
		}

		// ---- boost and cut land where the knob says ------------------
		{
			Isolator iso;
			setIsolator (iso, rate, 6.0, -12.0);
			const double low = isolatorGainDb (iso, 30.0, rate);
			setIsolator (iso, rate, 6.0, -12.0);
			const double high = isolatorGainDb (iso, 16000.0, rate);
			checkClose (low, 6.0, 0.1, "isolator: +6 dB bass at 30 Hz" + r);
			if (16000.0 < 0.45 * rate)
				checkClose (high, -12.0, 0.3, "isolator: -12 dB treble at 16 kHz" + r);
		}

		// ---- the kill follows its frequency --------------------------
		{
			Isolator iso;
			setIsolator (iso, rate, kIsolatorMinDb, 0.0, kBassHzMin, kTrebleHzDefault);
			const double atMin = isolatorGainDb (iso, 150.0, rate);
			setIsolator (iso, rate, kIsolatorMinDb, 0.0, kBassHzMax, kTrebleHzDefault);
			const double atMax = isolatorGainDb (iso, 150.0, rate);
			checkClose (atMin, 0.0, 0.5, "isolator: bass kill at 50 Hz leaves 150 Hz" + r);
			check (atMax < -30.0, "isolator: bass kill at 500 Hz removes 150 Hz" + r);
		}

		// ---- a kill yanked in under a ringing sine: no click ----------
		{
			Isolator iso;
			setIsolator (iso, rate, 0.0, 0.0);
			const double hz = 100.0, amp = 0.5;
			const int total = static_cast<int> (rate);
			std::vector<float> left (static_cast<size_t> (total)), right;
			for (int i = 0; i < total; ++i)
				left[i] = static_cast<float> (amp * std::sin (2.0 * M_PI * hz * i / rate));
			right = left;
			bool killed = false, released = false, wentFiltered = false;
			for (int at = 0; at < total; at += 256)
			{
				// By block, not by `at == total / 4`: a quarter of the rate
				// is not a multiple of 256, and the kill would never land.
				if (!killed && at >= total / 4)
				{
					iso.setBassDb (kIsolatorMinDb);
					killed = true;
				}
				if (!released && at >= total / 2)
				{
					iso.setBassDb (0.0);
					released = true;
				}
				wentFiltered = wentFiltered || iso.mix () == 1.0;
				iso.process (left.data () + at, right.data () + at, std::min (256, total - at));
			}
			const double natural = 2.0 * M_PI * hz / rate * amp;
			double worst = 0.0;
			for (int i = 1; i < total; ++i)
				worst = std::max (worst, std::fabs (static_cast<double> (left[i]) - left[i - 1]));
			check (killed && released && wentFiltered, "isolator: the kill test really killed" + r);
			check (worst < 1.5 * natural, "isolator: a kill in and out under a 100 Hz sine does not click ("
			       + std::to_string (worst / natural) + "x the sine's own slope)" + r);
			check (iso.mix () == 0.0 && iso.bassGain () == 1.0, "isolator: back at 0 dB it returns to dry" + r);
		}

		// ---- frequencies swept hard under noise: stable ---------------
		{
			Isolator iso;
			setIsolator (iso, rate, -12.0, -12.0);
			const int total = static_cast<int> (rate);
			std::vector<float> left (static_cast<size_t> (total)), right (static_cast<size_t> (total));
			std::uint32_t seed = 99u;
			for (int i = 0; i < total; ++i)
			{
				seed = seed * 1664525u + 1013904223u;
				left[i] = right[i] = static_cast<float> (static_cast<int> (seed >> 8) % 20001 - 10000) / 20000.f;
			}
			int block = 0;
			for (int at = 0; at < total; at += 64, ++block)
			{
				// Ends of the ranges, alternately, every 64 samples.
				iso.setBassHz ((block & 1) ? kBassHzMax : kBassHzMin);
				iso.setTrebleHz ((block & 1) ? kTrebleHzMin : kTrebleHzMax);
				iso.process (left.data () + at, right.data () + at, std::min (64, total - at));
			}
			bool finite = true;
			float peak = 0.f;
			for (int i = 0; i < total; ++i)
			{
				finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);
				peak = std::max (peak, std::fabs (left[i]));
			}
			check (finite && peak < 2.f, "isolator: frequencies swept end to end stay stable" + r);
			check (std::memcmp (left.data (), right.data (), left.size () * sizeof (float)) == 0,
			       "isolator: both channels the same for the same input" + r);
		}

		// ---- the tail ends in exact zeros ----------------------------
		{
			Isolator iso;
			setIsolator (iso, rate, -6.0, 3.0);
			std::vector<float> left (512), right (512);
			left[0] = right[0] = 1.f;
			iso.process (left.data (), right.data (), 512);
			bool zero = false;
			for (int b = 0; b < static_cast<int> (2.0 * rate / 512) && !zero; ++b)
			{
				std::fill (left.begin (), left.end (), 0.f);
				std::fill (right.begin (), right.end (), 0.f);
				iso.process (left.data (), right.data (), 512);
				zero = std::all_of (left.begin (), left.end (), [] (float v) { return v == 0.f; })
				       && std::all_of (right.begin (), right.end (), [] (float v) { return v == 0.f; });
			}
			check (zero, "isolator: an impulse's tail ends in exact silence within 2 s" + r);
		}
	}

	// ---- wired into the instrument --------------------------------------
	{
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		std::vector<float> left (256), right (256);
		dsp.setBassDb (kIsolatorMinDb);
		dsp.render (left.data (), right.data (), 256);
		check (dsp.isolator ().mix () > 0.0, "isolator: SampleDrumDsp runs it in render()");
		check (std::all_of (left.begin (), left.end (), [] (float v) { return v == 0.f; }),
		       "isolator: an empty kit is still exact silence with a kill on");
		dsp.setBassHz (90.0);
		dsp.setTrebleHz (9000.0);
		dsp.reset ();
		checkClose (dsp.isolator ().bassHz (), 90.0, 1e-9, "isolator: bass frequency reaches the DSP");
		checkClose (dsp.isolator ().trebleHz (), 9000.0, 1e-9, "isolator: treble frequency reaches the DSP");
		check (dsp.isolator ().bassGain () == 0.0, "isolator: reset snaps the gain");
	}
}

//------------------------------------------------------------------------
// What the Polivoks panel reads: thumbnails, the isolator's response,
// the voice count and the last hit.
//------------------------------------------------------------------------
static void testPanelData ()
{
	std::printf ("panel data\n");

	// THUMBNAILS - absolute, min/max per slice, both channels.
	{
		auto full = makeSample (960, 1.f);
		const auto t = makeThumbnail (*full);
		check (t.size () == static_cast<std::size_t> (kThumbnailPoints) * 2, "a thumbnail is 2 x kThumbnailPoints bytes");
		check (t[0] == 0 && t[1] == 127, "a full-scale DC file: min 0, max 127");

		auto shaped = makeShapedSample (4800);   // +/-0.7, L and R opposite
		const auto s = makeThumbnail (*shaped);
		int lo = 0, hi = 0;
		for (std::size_t i = 0; i < s.size (); i += 2) { lo = std::min<int> (lo, s[i]); hi = std::max<int> (hi, s[i + 1]); }
		check (hi == 89 && lo == -89, "0.7 of full scale reads as 89, both signs (L and R both counted)");

		auto tiny = makeSample (10, -0.5f);
		const auto few = makeThumbnail (*tiny);
		check (few.size () == static_cast<std::size_t> (kThumbnailPoints) * 2, "fewer frames than points still fills every point");
		bool allQuiet = true;
		for (std::size_t i = 0; i < few.size (); i += 2)
			if (few[i] != -64 || few[i + 1] != 0)
				allQuiet = false;
		check (allQuiet, "and repeats rather than leaving gaps (-0.5 reads -64)");

		SampleBuffer empty;
		check (makeThumbnail (empty).empty (), "an empty buffer has no thumbnail");
		check (makeThumbnail (*full, 0).empty (), "nor does zero points");
	}

	// THE ISOLATOR'S RESPONSE.
	{
		bool flat = true;
		for (double hz = 20.0; hz <= 20000.0; hz *= 1.1)
		{
			if (std::fabs (isolatorResponseDb (hz, 0.0, 200.0, 0.0, 3500.0)) > 1e-9) flat = false;
			if (std::fabs (isolatorResponseDb (hz, 0.0, 480.0, 0.0, 1600.0)) > 1e-9) flat = false;
		}
		check (flat, "at 0 dB / 0 dB the response is flat at any two crossovers");

		checkClose (isolatorResponseDb (20.0, 6.0, 200.0, 0.0, 3500.0), 6.0, 0.01, "bass +6 reads +6 dB well below the crossover");
		checkClose (isolatorResponseDb (20000.0, 0.0, 200.0, -12.0, 3500.0), -12.0, 0.05, "treble -12 reads -12 dB well above it");
		check (isolatorResponseDb (20.0, kIsolatorMinDb, 200.0, 0.0, 3500.0) < -70.0, "bass killed: deep cut at 20 Hz");
		check (isolatorResponseDb (20000.0, 0.0, 200.0, kIsolatorMinDb, 3500.0) < -50.0, "treble killed: deep cut at 20 kHz");
		check (isolatorResponseDb (900.0, kIsolatorMinDb, 200.0, kIsolatorMinDb, 3500.0) > -1.5, "both killed: the mid band is still there");
		check (isolatorResponseDb (900.0, kIsolatorMinDb, 200.0, kIsolatorMinDb, 3500.0) >= kIsolatorResponseFloorDb, "never below the floor");
		checkClose (isolatorResponseDb (20.0, 0.0005, 200.0, 0.0, 3500.0), 0.0, 1e-9, "within the unity snap is exactly 0 dB, as the DSP");
	}

	// THE VOICE COUNT AND THE LAST HIT.
	{
		auto kick = makeSample (48000, 0.25f);
		auto snare = makeSample (48000, 0.25f);
		SampleDrumDsp dsp;
		dsp.setSampleRate (48000.0);
		dsp.setMaxBlockSize (256);
		dsp.setCellSample (cellIndex (0, 3), kick.get ());
		dsp.setCellSample (cellIndex (1, 0), snare.get ());
		dsp.syncCells (256);
		std::vector<float> l (256), r (256);

		check (decodeLastHit (dsp.lastHit ()).cell == -1, "nothing struck yet");
		check (dsp.voicesSounding () == 0, "no voices yet");

		dsp.noteOn (36, velocityFor (100), 10);
		dsp.render (l.data (), r.data (), 256);
		LastHit a = decodeLastHit (dsp.lastHit ());
		check (a.cell == cellIndex (0, 3) && a.velocity == 100, "a kick at 100: cell 24, velocity 100");
		check (dsp.voicesSounding () == 1, "one voice");

		dsp.noteOn (38, velocityFor (5), 0);   // row 0 is loaded for the snare
		dsp.render (l.data (), r.data (), 256);
		LastHit b = decodeLastHit (dsp.lastHit ());
		check (b.cell == cellIndex (1, 0) && b.velocity == 5 && b.sequence == a.sequence + 1, "a soft snare follows, sequence moved on");
		check (dsp.voicesSounding () == 2, "two voices");

		dsp.auditionCell (cellIndex (0, 3));
		dsp.render (l.data (), r.data (), 256);
		LastHit c = decodeLastHit (dsp.lastHit ());
		check (c.cell == cellIndex (0, 3) && c.velocity == 0 && c.sequence == b.sequence + 1, "an audition: velocity 0, the same cell again is still news");

		dsp.noteOn (49, velocityFor (100), 0);   // crash column is empty
		dsp.render (l.data (), r.data (), 256);
		check (decodeLastHit (dsp.lastHit ()).sequence == c.sequence, "a note that plays nothing is not a hit");

		dsp.reset ();
		check (dsp.voicesSounding () == 0, "reset: no voices");

		check (decodeLastHit (encodeLastHit (31, 127, 0x12345)).cell == 31
		       && decodeLastHit (encodeLastHit (31, 127, 0x12345)).velocity == 127
		       && decodeLastHit (encodeLastHit (31, 127, 0x12345)).sequence == 0x2345, "encode/decode round trip; the sequence wraps at 16 bits");
		check (decodeLastHit (encodeLastHit (40, 1, 1)).cell == -1, "a cell out of range decodes as nothing");
	}
}

int main ()
{
	std::printf ("SampleDrum DSP tests\n");
	std::printf ("--------------------\n");

	testConversions ();
	testDrumMap ();
	testVelocityBands ();
	testChooseCell ();
	testWav ();
	testPaths ();
	testPortability ();
	testCellDrag ();
	testSmootherStep ();
	testUnityIsBitIdentical ();
	testPlayback ();
	testResampling ();
	testBandLimited ();
	testChoke ();
	testReplacement ();
	testVoiceLimit ();
	testDegenerateBlocks ();
	testCellLevels ();
	testColumnNotes ();
	testIsolator ();
	testPanelData ();
	measureLevels ();

	std::printf ("--------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);

	return (gFailures == 0) ? 0 : 1;
}
