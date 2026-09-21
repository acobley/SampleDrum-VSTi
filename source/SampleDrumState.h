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
//
// APPEND, NEVER INSERT - in the stream exactly as in the parameter table.
// Every reader copes with a stream that stops early by leaving the rest
// at its default, so a new block goes on the end and older projects load.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumKit.h"
#include "SampleDrumParams.h"

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
} // namespace SampleDrum
