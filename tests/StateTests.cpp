//------------------------------------------------------------------------
// SampleDrum - the state stream, including the embedded files
//
// Needs the VST3 SDK for IBStreamer and MemoryStream, and nothing else of
// it - no plug-in, no host. What it proves is what a project file has to
// get right: what the processor writes, the processor reads back; the
// controller's reader stops before the audio; and an older or damaged
// stream degrades to "read the file from disk" rather than to garbage.
// Modelled on Project6-VSTi/tests/StateTests.cpp.
//
// Build and run (from the repo root):
//
//   SDK=external/vst3sdk; B=$SDK/base/source
//   c++ -std=c++17 -O2 -Wall -Wextra -DRELEASE=1 -I$SDK -Isource \
//       tests/StateTests.cpp source/SampleDrumState.cpp source/SampleDrumParams.cpp \
//       source/SampleDrumDsp.cpp source/SampleDrumSample.cpp \
//       $SDK/public.sdk/source/common/memorystream.cpp \
//       $B/fstreamer.cpp $B/fstring.cpp $B/fobject.cpp $B/fbuffer.cpp \
//       $B/updatehandler.cpp $B/fdebug.cpp $SDK/base/thread/source/flock.cpp \
//       $SDK/pluginterfaces/base/funknown.cpp \
//       $SDK/pluginterfaces/base/conststringtable.cpp -lpthread \
//       -o /tmp/statetests && /tmp/statetests
//
// ON macOS add -framework CoreFoundation: fstring.cpp is built on
// CFString there (Project6 found this the hard way).
//------------------------------------------------------------------------

#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/istringresult.h"
#include "pluginterfaces/base/iupdatehandler.h"
#undef INIT_CLASS_IID

#include "SampleDrumState.h"
#include "SampleDrumDsp.h"
#include "SampleDrumParams.h"

#include <cmath>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace SampleDrum;

namespace {

int gChecks = 0;
int gFailures = 0;

void check (bool condition, const char* what)
{
	++gChecks;
	std::printf ("  %-66s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++gFailures;
}

void section (const char* title) { std::printf ("\n%s\n", title); }

FileBytes blob (std::size_t size, unsigned char seed)
{
	auto bytes = std::make_shared<std::vector<unsigned char>> (size);
	for (std::size_t i = 0; i < size; ++i)
		(*bytes)[i] = static_cast<unsigned char> (seed + i * 7);
	return bytes;
}

bool same (const FileBytes& a, const FileBytes& b)
{
	if (!a || !b)
		return !a && !b;
	return *a == *b;
}

/** A project as the processor's getState writes it. */
StreamState sampleProject ()
{
	StreamState s;
	s.paths[0]  = "/Users/andy/Kits/kick.wav";
	s.paths[9]  = std::string (kFactorySampleRoot) + "/Snares/snare.wav";
	s.paths[31] = "/Volumes/Drive/crash.wav";
	s.params[0] = 0.25;
	s.bypass = true;
	return s;
}

void writeProject (IBStream* stream, const StreamState& s, const FileBytes* files, bool withFiles)
{
	IBStreamer out (stream, kLittleEndian);
	writeState (out, s);
	if (withFiles)
		writeEmbedded (out, files);
	stream->seek (0, IBStream::kIBSeekSet, nullptr);
}

} // namespace

int main ()
{
	std::printf ("SampleDrum state tests\n");

	FileBytes files[kCellCount];
	files[0]  = blob (100000, 1);
	files[31] = blob (57, 9);          // cell 9 is factory: loadCell never keeps one

	//--------------------------------------------------------------------
	section ("1. Paths and files round-trip, in the order getState writes them");
	//--------------------------------------------------------------------
	{
		auto* memory = new MemoryStream ();
		writeProject (memory, sampleProject (), files, true);

		IBStreamer in (memory, kLittleEndian);
		StreamState got;
		check (readState (in, got),                       "the state reads");
		check (got.complete,                              "... to the end of the paths");
		check (got.paths[0] == sampleProject ().paths[0]
		       && got.paths[9] == sampleProject ().paths[9]
		       && got.paths[31] == sampleProject ().paths[31], "the paths come back");
		check (got.params[0] == 0.25 && got.bypass,       "the parameters and bypass come back");

		FileBytes back[kCellCount];
		check (readEmbedded (in, back),                   "the embedded block reads");
		bool all = true;
		for (int c = 0; c < kCellCount; ++c)
			all = all && same (back[c], files[c]);
		check (all,                                       "every file comes back byte for byte");
		check (!back[9],                                  "the factory cell carries nothing");

		int64 pos = 0, end = 0;
		memory->tell (&pos);
		memory->seek (0, IBStream::kIBSeekEnd, &end);
		check (pos == end,                                "nothing is left unread");
		memory->release ();
	}

	//--------------------------------------------------------------------
	section ("2. The controller's reader stops before the audio");
	//--------------------------------------------------------------------
	{
		auto* memory = new MemoryStream ();
		writeProject (memory, sampleProject (), files, true);

		IBStreamer in (memory, kLittleEndian);
		StreamState got;
		check (readState (in, got) && got.complete,       "readState alone reads a stream with files");
		check (got.paths[31] == sampleProject ().paths[31], "... and gets the paths right");

		int64 pos = 0, end = 0;
		memory->tell (&pos);
		memory->seek (0, IBStream::kIBSeekEnd, &end);
		check (end - pos > 100000,                        "... having read none of the audio");
		memory->release ();
	}

	//--------------------------------------------------------------------
	section ("3. A project saved before the block existed");
	//--------------------------------------------------------------------
	{
		auto* memory = new MemoryStream ();
		writeProject (memory, sampleProject (), files, false);

		IBStreamer in (memory, kLittleEndian);
		StreamState got;
		check (readState (in, got) && got.complete,       "the old stream reads as before");

		FileBytes back[kCellCount];
		back[3] = blob (10, 3);                           // must be cleared
		check (!readEmbedded (in, back),                  "the block is reported absent");
		bool none = true;
		for (int c = 0; c < kCellCount; ++c)
			none = none && !back[c];
		check (none,                                      "every cell falls back to disk");
		memory->release ();
	}

	//--------------------------------------------------------------------
	section ("4. Damage");
	//--------------------------------------------------------------------
	{
		// Truncated in the middle of the last file: what came before stays.
		auto* full = new MemoryStream ();
		writeProject (full, sampleProject (), files, true);
		int64 size = 0;
		full->seek (0, IBStream::kIBSeekEnd, &size);

		auto* cut = new MemoryStream ();
		std::vector<char> raw (static_cast<std::size_t> (size - 20));
		full->seek (0, IBStream::kIBSeekSet, nullptr);
		full->read (raw.data (), static_cast<int32> (raw.size ()), nullptr);
		cut->write (raw.data (), static_cast<int32> (raw.size ()), nullptr);
		cut->seek (0, IBStream::kIBSeekSet, nullptr);

		IBStreamer in (cut, kLittleEndian);
		StreamState got;
		readState (in, got);
		FileBytes back[kCellCount];
		check (!readEmbedded (in, back),                  "a truncated block is reported");
		check (same (back[0], files[0]),                  "... the file before the damage is kept");
		check (!back[31],                                 "... the damaged one falls back to disk");
		full->release ();
		cut->release ();
	}
	for (int32 bad : { -1, 0x7FFFFFFF })
	{
		auto* memory = new MemoryStream ();
		{
			IBStreamer out (memory, kLittleEndian);
			writeState (out, sampleProject ());
			out.writeInt32 (kCellCount);
			out.writeInt32 (bad);
		}
		memory->seek (0, IBStream::kIBSeekSet, nullptr);
		IBStreamer in (memory, kLittleEndian);
		StreamState got;
		readState (in, got);
		FileBytes back[kCellCount];
		check (!readEmbedded (in, back) && !back[0],
		       bad < 0 ? "a negative length stops without allocating"
		               : "a 2 GB length stops without allocating");
		memory->release ();
	}
	{
		// A stream that stops inside the path block is NOT complete, so
		// the processor never reads the embedded block from mid-stream.
		auto* memory = new MemoryStream ();
		{
			IBStreamer out (memory, kLittleEndian);
			StreamState s = sampleProject ();
			out.writeInt32 (kStateVersion);
			out.writeInt32 (kNumParams);
			for (int i = 0; i < kNumParams; ++i)
				out.writeDouble (s.params[i]);
			out.writeInt32 (0);
			out.writeInt32 (kCellCount);
			out.writeInt32 (5);                           // a path, cut short
			out.writeRaw ("/Us", 3);
		}
		memory->seek (0, IBStream::kIBSeekSet, nullptr);
		IBStreamer in (memory, kLittleEndian);
		StreamState got;
		check (readState (in, got) && !got.complete,      "a stream cut in the paths is not complete");
		memory->release ();
	}

	//--------------------------------------------------------------------
	section ("5. Sizes");
	//--------------------------------------------------------------------
	{
		auto* memory = new MemoryStream ();
		FileBytes none[kCellCount];
		writeProject (memory, sampleProject (), none, true);
		int64 withEmpty = 0;
		memory->seek (0, IBStream::kIBSeekEnd, &withEmpty);
		memory->release ();

		auto* bare = new MemoryStream ();
		writeProject (bare, sampleProject (), none, false);
		int64 without = 0;
		bare->seek (0, IBStream::kIBSeekEnd, &without);
		bare->release ();

		check (withEmpty - without == 4 + 4 * kCellCount,
		       "an empty kit costs 132 bytes: a count and 32 zero lengths");
	}

	//--------------------------------------------------------------------
	section ("6. The output isolator's four parameters");
	//--------------------------------------------------------------------
	{
		// A PROJECT FROM 1.0.1.0, which had 41 parameters. The four
		// isolator parameters must come back at their defaults - flat, and
		// so bit-identical - with everything before them read as saved.
		const int oldCount = kBassGain;
		auto* memory = new MemoryStream ();
		{
			IBStreamer out (memory, kLittleEndian);
			out.writeInt32 (kStateVersion);
			out.writeInt32 (oldCount);
			for (int i = 0; i < oldCount; ++i)
				out.writeDouble (i == 0 ? 0.25 : kParams[i].defaultNormalized ());
			out.writeInt32 (0);
			out.writeInt32 (kCellCount);
			for (int c = 0; c < kCellCount; ++c)
				out.writeInt32 (0);
		}
		memory->seek (0, IBStream::kIBSeekSet, nullptr);
		IBStreamer in (memory, kLittleEndian);
		StreamState got;
		check (readState (in, got) && got.complete,       "a 41-parameter project reads to the end");
		check (got.params[0] == 0.25,                     "... with its trim as saved");
		bool defaults = true;
		for (int id : { kBassGain, kBassFreq, kTrebleGain, kTrebleFreq })
			defaults = defaults && got.params[id] == kParams[id].defaultNormalized ();
		check (defaults,                                  "... and the isolator at its defaults");
		memory->release ();
	}
	{
		// The defaults are the ones intended, through the log taper.
		const ParamDef& bassHz = paramDef (kBassFreq);
		const ParamDef& trebleHz = paramDef (kTrebleFreq);
		check (std::fabs (bassHz.toPlain (bassHz.defaultNormalized ()) - kBassHzDefault) < 1e-9
		       && std::fabs (trebleHz.toPlain (trebleHz.defaultNormalized ()) - kTrebleHzDefault) < 1e-9,
		                                                  "the frequencies default to 200 Hz and 3500 Hz");
		check (std::fabs (bassHz.toPlain (0.5) - std::sqrt (kBassHzMin * kBassHzMax)) < 1e-9,
		                                                  "a frequency's middle is the geometric middle");
		check (std::fabs (bassHz.toInternal (0.3) - bassHz.toPlain (0.3)) < 1e-12
		       && std::fabs (trebleHz.toInternal (0.7) - trebleHz.toPlain (0.7)) < 1e-12,
		                                                  "the DSP is handed the same Hz the host shows");
		const ParamDef& bass = paramDef (kBassGain);
		const float hostFloat = static_cast<float> (bass.defaultNormalized ());
		check (isolatorGain (bass.toPlain (bass.defaultNormalized ())) == 1.0
		       && isolatorGain (bass.toPlain (hostFloat)) == 1.0
		       && isolatorGain (bass.toPlain (0.0)) == 0.0,
		                                                  "the gains default to exactly unity (even via float) and bottom out in a kill");
		bool titled = true;
		for (int id : { kBassGain, kBassFreq, kTrebleGain, kTrebleFreq })
			titled = titled && kParams[id].id == static_cast<Steinberg::Vst::ParamID> (id)
			         && kParams[id].title != nullptr && kParams[id].units != nullptr;
		check (titled,                                    "each row's id is its index, title and units not null");
	}

	std::printf ("\n%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
