//------------------------------------------------------------------------
// SampleDrum - sample buffers, the WAV reader that fills them, and the
// few questions about a file PATH the panel has to ask
//
// SDK-FREE, like SampleDrumDsp.*. The WAV reader is lifted from
// Project6-VSTi/source/Project6Sample.* with the tempo detection, the
// waveform summary and the MIDI statuses taken out - a drum hit has no
// tempo to fit, and a cell here only ever holds audio. What is left is
// the part that was already tested there: parseWav() takes BYTES, so
// tests/DspTests.cpp builds wav files in memory and reads them back with
// no disk and no host in the way; loadWavFile() is the thin part that
// opens a file and is the only part a test cannot reach.
//
// Everything comes out as INTERLEAVED STEREO FLOAT at the file's own
// sample rate. Converting on load rather than on playback means the audio
// thread deals with exactly one layout however odd the file was.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumKit.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace SampleDrum {

/** Everything a loaded sample is converted to. */
constexpr int kSampleChannels = 2;

/** The longest file a cell will take.

    Thirty-two cells of one-shots, so the cap is about bounding an
    ACCIDENTAL drop - an album side dragged onto a snare cell - rather
    than about what drums need. Twenty seconds of stereo float is about
    7 MB at 44.1 k, and even a long crash tail is well under it. A longer
    file is REFUSED and says so, rather than being silently truncated. */
constexpr double kMaxSampleSeconds = 20.0;

//------------------------------------------------------------------------
/** What happened when a cell's file was read.

    A cell that will not play has to be able to SAY SO. The alternative -
    a cell that takes the drop, shows the name and is silent when hit -
    is the failure Project6's notes keep coming back to: a control that
    looks live and is not. */
enum class SampleStatus
{
	Empty,              ///< no file in this cell
	Loaded,             ///< read, converted, ready
	Unreadable,         ///< the file could not be opened or read
	NotWave,            ///< not a RIFF/WAVE file at all
	UnsupportedFormat,  ///< a wav, but not a PCM or float layout we decode
	TooLong,            ///< longer than kMaxSampleSeconds
	NoFrames,           ///< a wav with no audio in it

	// APPENDED, never inserted: the status crosses to the controller as
	// an integer, so the existing values must keep their numbers.
	Missing             ///< no file at the path, and no copy in the project
};

/** The last value, for range-checking a status that arrived as an
    integer. Moves with every append. */
constexpr SampleStatus kLastSampleStatus = SampleStatus::Missing;

/** One short phrase, for the cell's tooltip. Never null. */
const char* sampleStatusText (SampleStatus status);

//------------------------------------------------------------------------
/** Decoded audio, ready to play.

    IMMUTABLE ONCE BUILT. The audio thread reads one of these through a
    bare pointer while the UI thread may be building the next; that is
    only safe because nothing ever modifies a buffer after it has been
    published. Load a new file, publish a new buffer, retire the old one -
    see SampleDrumProcessor::publishCell. */
struct SampleBuffer
{
	/** Interleaved, kSampleChannels floats per frame. */
	std::vector<float> samples;
	int    frameCount = 0;
	double sourceRate = 44100.0;
	/** The largest absolute sample in the file, for the level report. */
	float  peak = 0.f;

	bool empty () const { return frameCount <= 0 || samples.empty (); }
	double seconds () const { return (sourceRate > 0.0) ? frameCount / sourceRate : 0.0; }
};

//------------------------------------------------------------------------
/** Parse RIFF/WAVE bytes into `out`.

    PCM at 8, 16, 24 and 32 bits, IEEE float at 32 and 64, any channel
    count (mono is duplicated to both sides; beyond two, the first two are
    taken), and WAVE_FORMAT_EXTENSIBLE. Chunks other than "fmt " and
    "data" are skipped, including odd-length ones with their pad byte.

    `out` is left empty unless the return is SampleStatus::Loaded. */
SampleStatus parseWav (const unsigned char* data, std::size_t size, SampleBuffer& out);

/** Read a file from disk and parse it - readWholeFile then parseWav.

    ON THE UI THREAD ONLY. It opens a file, allocates and decodes; none of
    those things may happen on the audio thread. */
SampleStatus loadWavFile (const std::string& path, SampleBuffer& out);

//------------------------------------------------------------------------
// FACTORY CONTENT AND EMBEDDED FILES - projects that travel
//
// A cell names its file by absolute path, and a path means nothing on
// somebody else's Mac. The same two mechanisms as Project6 1.2.0.0
// (Project6Slots.h) make a project travel:
//
//   FACTORY FILES live in kFactorySampleRoot, which an installer can fill
//   and which is THE SAME PATH ON EVERY MAC - /Users/Shared carries
//   nobody's user name. A cell loaded from there needs nothing carried
//   with the project, so it is never embedded.
//
//   EVERYTHING ELSE is embedded: the file's original bytes ride in the
//   state stream (see SampleDrumState.h), and on load THE EMBEDDED COPY
//   WINS over the file on disk, so a project sounds as it was saved - on
//   this machine or any other.
//
// NOT inside SampleDrum.vst3: writing into a signed bundle breaks its
// signature, a reinstall replaces the bundle, the plug-in cannot write to
// /Library, and the AU loads a DIFFERENT copy of the VST3 from inside its
// own .component. Project6's PORTING-NOTES section 21 has the detail.
//------------------------------------------------------------------------
constexpr const char* kFactorySampleRoot = "/Users/Shared/SampleDrum/Samples";

/** True for a path INSIDE kFactorySampleRoot. Textual, not a filesystem
    query: the answer has to be the same on a machine where the factory
    content is not installed, or a project saved there would start
    embedding files every recipient already has. The root itself, and a
    sibling whose name merely starts with it, are not factory paths. */
bool isFactoryPath (const std::string& path);

/** A cell's file exactly as it was on disk. Shared and immutable, so the
    state writer can take a reference under a lock and stream it without
    copying. */
using FileBytes = std::shared_ptr<const std::vector<unsigned char>>;

/** The most that is read from disk, or accepted from a project, for one
    cell - checked before any header is trusted, so a pathological file or
    a hostile stream cannot ask for an arbitrary allocation. The real limit
    is kMaxSampleSeconds; 20 s of 8-channel 64-bit float at 192 k is under
    this. */
constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;

/** What happened when a file was read from disk. MISSING is kept apart
    from UNREADABLE because it is the one a person can fix - the file is on
    a drive that is not plugged in, or was never sent. */
enum class FileReadResult
{
	Ok,
	Missing,     ///< nothing at that path
	Unreadable,  ///< there, but could not be opened or read, or is empty
	TooBig       ///< larger than `maxBytes`
};

/** The whole of a file. ON THE UI THREAD ONLY - it opens a file and
    allocates. `out` is null unless the result is Ok. */
FileReadResult readWholeFile (const std::string& path, std::size_t maxBytes, FileBytes& out);

/** The SampleStatus a failed read means to the panel. */
SampleStatus statusOfRead (FileReadResult result);

//------------------------------------------------------------------------
/** The processor's 32 embedded files, and the rule for carrying one from
    cell to cell - SDK-free so the swap can be tested without a host.

    WHY CARRYING MATTERS: on a machine that has only the project, the
    embedded copy is the only copy there is. A cell dragged to another
    must take its bytes along; re-reading the path would find nothing.
    Ported from Project6Processor::notify (1.2.0.0).

    THE SWAP is two cell messages, and the second names a cell the first
    has already replaced. So each cell's PREVIOUS file is kept - just one,
    and only until the next cell message - which is exactly where the
    second half finds it.

    LOCKED INSIDE: getState may run on any thread while notify() replaces
    a cell on the UI thread. The audio thread never touches this. */
class CellFiles
{
public:
	/** A cell's path is changing from `oldPath` to `newPath`.

	    `from` is the cell the file came from (a drag), or -1 for a file
	    from outside, which carries nothing so the disk is read afresh -
	    also how an edited sample is brought back in. `fromPathNow` is
	    what `from` holds at this moment.

	    Bytes are carried ONLY FOR THE FILE THEY WERE READ FROM: the path
	    must match, in `from` as it is now or in what it held before its
	    last change. */
	void replace (int cell, const std::string& oldPath, const std::string& newPath,
	              int from, const std::string& fromPathNow);

	/** What loadCell should decode, or null to read the disk. */
	FileBytes get (int cell) const;

	/** After a load: keep `bytes` for embedding, or null. */
	void keep (int cell, FileBytes bytes);

	/** For getState: all 32, snapshotted under the lock. */
	void snapshot (FileBytes* out) const;

	/** For setState: all 32 replaced, and nothing held from before. */
	void restore (FileBytes* in);

private:
	struct Held
	{
		std::string path;
		FileBytes bytes;
	};

	mutable std::mutex mLock;
	FileBytes mFiles[kCellCount];
	Held mPrevious[kCellCount];
};

//------------------------------------------------------------------------
// A CELL DRAGGED TO ANOTHER CELL
//
// The payload is TEXT - the one type every platform layer of VSTGUI
// carries both ways - in a shape no file drag can have:
//
//     SampleDrum/cell:<owner, 16 hex digits>/<cell>\n<path>
//
// THE OWNER is the dragging instance's own random id. Project6's payload
// carries only the index, so a pad dragged from one instance onto
// another would move the TARGET's own pad at that index. Here a drag
// from another instance is recognised and treated as a drop of its path
// from outside: a copy, read from disk.
//------------------------------------------------------------------------
extern const char* const kCellDragPrefix;

/** "" for an empty path or a bad cell. */
std::string encodeCellDrag (std::uint64_t owner, int cell, const std::string& path);

/** True, filling whatever is not null, only for a well-formed payload
    naming a real cell and a non-empty path. Never throws. */
bool decodeCellDrag (const std::string& text, std::uint64_t* owner, int* cell, std::string* path);

//------------------------------------------------------------------------
// Paths
//------------------------------------------------------------------------

/** True for a path whose name ends ".wav", in any case. KICK.WAV is a
    wav file, and refusing it would look like the drop had failed. */
bool isWavPath (const std::string& path);

/** The last path component, or "" for a path ending in a separator. */
std::string fileNameOf (const std::string& path);

/** The name without its extension - what a cell shows. */
std::string fileStemOf (const std::string& path);

/** Turn whatever a drag carried into a wav path, or "".

    THE TYPE IS NOT TRUSTED, only the content: a file dragged from the
    Finder arrives as a percent-escaped file:// URL reported as TEXT,
    because VSTGUI asks the pasteboard for a string before a file URL.
    Handles a plain path, a file:// URL, and several lines (the first is
    taken - a drop on one cell is a drop on one cell). Lifted from
    Project6Slots.cpp. */
std::string wavPathFromDragText (const std::string& text);

//------------------------------------------------------------------------
// The panel's picture of a sample
//------------------------------------------------------------------------

/** How many columns a cell's waveform is drawn with. */
constexpr int kThumbnailPoints = 96;

/** A waveform as a sample editor draws it: for each of `points` equal
    slices of the file, the lowest and highest sample on either channel,
    as signed bytes - min0, max0, min1, max1 ... ABSOLUTE, so 127 is full
    scale and a quiet file looks quiet, which is what its level knob is
    for. Made by the processor from the decoded audio (the controller
    never has it, and on another Mac only the project's embedded copy
    exists), and sent to the panel with the cell's status. Empty for an
    empty buffer or `points` < 1. */
std::vector<std::int8_t> makeThumbnail (const SampleBuffer& sample, int points = kThumbnailPoints);

//------------------------------------------------------------------------
} // namespace SampleDrum
