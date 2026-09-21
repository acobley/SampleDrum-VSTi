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
	           == SampleStatus::Unreadable, "a missing file is Unreadable, not a crash");
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
		check (peakOf (l) <= 0.5f + 1e-6f, "resampling does not overshoot a constant");
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
	testSmootherStep ();
	testUnityIsBitIdentical ();
	testPlayback ();
	testResampling ();
	testChoke ();
	testReplacement ();
	testVoiceLimit ();
	testDegenerateBlocks ();
	testCellLevels ();
	testColumnNotes ();
	measureLevels ();

	std::printf ("--------------------\n");
	std::printf ("%d checks, %d failures\n", gChecks, gFailures);

	return (gFailures == 0) ? 0 : 1;
}
