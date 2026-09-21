//------------------------------------------------------------------------
// SampleDrum - the state stream, implementation
//------------------------------------------------------------------------

#include "SampleDrumState.h"

#include <algorithm>

using namespace Steinberg;

namespace SampleDrum {

//------------------------------------------------------------------------
StreamState::StreamState ()
{
	for (int i = 0; i < kNumParams; ++i)
		params[i] = kParams[i].defaultNormalized ();
}

//------------------------------------------------------------------------
bool writeState (IBStreamer& streamer, const StreamState& state)
{
	if (!streamer.writeInt32 (kStateVersion))
		return false;

	// THE COUNT IS WRITTEN, so a newer build with more parameters can read
	// an older stream - the other half of "append, never insert".
	// NORMALISED values, so a plain range widened later does not rescale
	// every saved project.
	if (!streamer.writeInt32 (static_cast<int32> (kNumParams)))
		return false;
	for (int i = 0; i < kNumParams; ++i)
		if (!streamer.writeDouble (state.params[i]))
			return false;

	if (!streamer.writeInt32 (state.bypass ? 1 : 0))
		return false;

	if (!streamer.writeInt32 (static_cast<int32> (kCellCount)))
		return false;
	for (int cell = 0; cell < kCellCount; ++cell)
	{
		const std::string& path = state.paths[cell];
		const int32 length = static_cast<int32> (path.size ());
		if (!streamer.writeInt32 (length))
			return false;
		// NO TERMINATOR: the length is the length, so any path round-trips.
		if (length > 0 && streamer.writeRaw (path.data (), length) != length)
			return false;
	}

	return true;
}

//------------------------------------------------------------------------
bool readState (IBStreamer& streamer, StreamState& state)
{
	// DEFAULTS FIRST, unconditionally. A host reuses one instance as the
	// user loads project after project; a reader that only wrote what it
	// found would leave an old song playing the new song's kit.
	state = StreamState ();

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return false;
	if (version > kStateVersion)
		return false;   // written by a newer build - do not guess

	int32 count = 0;
	if (!streamer.readInt32 (count) || count < 0 || count > 100000)
		return true;    // stops early: everything keeps its default
	for (int32 i = 0; i < count; ++i)
	{
		double v = 0.0;
		if (!streamer.readDouble (v))
			return true;
		// A stream from a build with MORE parameters is read to the end -
		// the bytes must be consumed either way - and the extras dropped.
		if (i < kNumParams)
			state.params[i] = std::min (1.0, std::max (0.0, v));
	}

	int32 bypass = 0;
	if (!streamer.readInt32 (bypass))
		return true;
	state.bypass = (bypass != 0);

	int32 cells = 0;
	if (!streamer.readInt32 (cells) || cells < 0 || cells > 4096)
		return true;
	for (int32 i = 0; i < cells; ++i)
	{
		int32 length = 0;
		if (!streamer.readInt32 (length) || length < 0 || length > kMaxPathBytes)
			return true;

		std::string path;
		if (length > 0)
		{
			path.resize (static_cast<std::string::size_type> (length));
			if (streamer.readRaw (&path[0], length) != length)
				return true;
		}
		if (isCellIndex (i))
			state.paths[i] = path;
	}

	return true;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
