//------------------------------------------------------------------------
// SampleDrum - the state stream, written once for both halves
//
// ONE SET OF FUNCTIONS, called by BOTH sides of the VST3 split: the
// processor's getState / setState write and read the project, and the
// controller's setComponentState reads the identical bytes to fill the
// panel. Two hand-written copies of a stream layout that had to agree is
// the bug this file exists to prevent (Project6SlotState.h, same idea).
//
// THE LAYOUT, version 1:
//
//     int32   kStateVersion
//     int32   parameter count, then that many doubles, NORMALISED
//     int32   bypass (0/1)
//     int32   cell count, then per cell: int32 byte length + UTF-8 path
//             (length 0 = empty cell), in cell-index order
//     int32   cell count, then per cell: int32 byte length + the cell's
//             file EXACTLY AS IT WAS ON DISK (length 0 = nothing
//             embedded) - see writeEmbedded. Appended 2026-09-26.
//
// The embedded block was APPENDED without bumping kStateVersion, by the
// rule below: a build without it stops after the paths, which is exactly
// "stop early, keep defaults", and reads its files from disk as before.
//
// APPEND, NEVER INSERT - in the stream exactly as in the parameter table.
// Every reader copes with a stream that stops early by leaving the rest
// at its default, so a new block goes on the end and older projects load.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumKit.h"
#include "SampleDrumParams.h"
#include "SampleDrumSample.h"

#include "base/source/fstreamer.h"

#include <string>

namespace SampleDrum {

/** Bumped only for a change that "stop early, keep defaults" cannot
    absorb. Readers refuse a stream from a NEWER version rather than guess. */
constexpr Steinberg::int32 kStateVersion = 1;

/** A sanity bound on one path, so a corrupt stream cannot ask for an
    arbitrary allocation. macOS stops at 1024 bytes. */
constexpr Steinberg::int32 kMaxPathBytes = 4096;

/** Everything the stream holds, in the form both halves use. */
struct StreamState
{
	double      params[kNumParams];
	bool        bypass = false;
	std::string paths[kCellCount];

	/** Set by readState only when it read the whole path block, so the
	    stream is positioned exactly at what follows it. A stream that
	    stopped early is somewhere in the middle, and reading the embedded
	    block from there would be reading garbage. Not written. */
	bool complete = false;

	/** Every parameter at its default, bypass off, every cell empty -
	    what a reader starts from, so a short stream cannot leave the
	    previous project's values behind. */
	StreamState ();
};

bool writeState (Steinberg::IBStreamer& streamer, const StreamState& state);

/** Returns false only for a stream that cannot be read at all (no
    version, or a newer one). A stream that STOPS EARLY is not an error:
    what it did not reach keeps its default. */
bool readState (Steinberg::IBStreamer& streamer, StreamState& state);

//------------------------------------------------------------------------
/** The embedded files: kCellCount length-prefixed blobs, each a cell's
    file exactly as it was on disk - not decoded audio, so a 16-bit file
    stays 16-bit sized. Zero length is "nothing embedded": an empty cell,
    a factory file (every recipient has those) or one that did not load.

    Written by the processor straight after writeState. LAST IN THE
    STREAM, deliberately: it is by far the largest block, and the
    controller's setComponentState calls readState alone and stops before
    it, so the panel never wades through audio it does not use. ANYTHING
    APPENDED LATER GOES AFTER THIS BLOCK. */
bool writeEmbedded (Steinberg::IBStreamer& streamer, const FileBytes* files);

/** Reads that block, having FIRST emptied every entry. Returns false when
    the block is absent (a project saved before it existed) or damaged;
    whatever was read before the damage is kept and the rest stays empty,
    so those cells fall back to the disk. Call only after readState has
    consumed the rest of the stream. */
bool readEmbedded (Steinberg::IBStreamer& streamer, FileBytes* files);

//------------------------------------------------------------------------
} // namespace SampleDrum
