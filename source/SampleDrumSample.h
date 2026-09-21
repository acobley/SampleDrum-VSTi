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

#include <cstddef>
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
	NoFrames            ///< a wav with no audio in it
};

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

/** Read a file from disk and parse it.

    ON THE UI THREAD ONLY. It opens a file, allocates and decodes; none of
    those things may happen on the audio thread. */
SampleStatus loadWavFile (const std::string& path, SampleBuffer& out);

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
} // namespace SampleDrum
