//------------------------------------------------------------------------
// SampleDrum - audio processor implementation
//------------------------------------------------------------------------

#include "SampleDrumProcessor.h"
#include "DenormalGuard.h"
#include "SampleDrumIDs.h"
#include "SampleDrumState.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SampleDrum {

//------------------------------------------------------------------------
SampleDrumProcessor::SampleDrumProcessor ()
{
	setControllerClass (kSampleDrumControllerUID);

	for (int i = 0; i < kNumParams; ++i)
		mParams[i] = kParams[i].defaultNormalized ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// An instrument: notes in, audio out, NO AUDIO INPUT. The empty input
	// side is what makes this an aumu, and why the plist says 0 in / 2 out.
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	// 16 channels, the MIDI convention. Every channel plays the kit - a
	// GM drum part is conventionally on channel 10, but a host that sends
	// it on 1 should not get silence for it.
	addEventInput (STR16 ("Event In"), 16);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::terminate ()
{
	mActive.store (false, std::memory_order_release);
	for (int cell = 0; cell < kCellCount; ++cell)
		publishCell (cell, nullptr);
	collectRetired (true);
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                            SpeakerArrangement* outputs, int32 numOuts)
{
	// Stereo out, nothing in, nothing else accepted - agreeing with the
	// single 0-in / 2-out entry in resource/au-info.plist. A layout
	// accepted here and absent there is the mismatch auval finds.
	if (numIns != 0 || numOuts != 1)
		return kResultFalse;

	if (outputs[0] != SpeakerArr::kStereo)
		return kResultFalse;

	return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	// BOTH: hosts offer 64-bit buffers, and one that is refused may simply
	// not load the plug-in. The DSP is float; writeOutput widens.
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setupProcessing (ProcessSetup& setup)
{
	mSampleRate = setup.sampleRate;

	// ALLOCATION BELONGS HERE, the one place the host says how big a
	// block to expect - never in process().
	mDsp.setSampleRate (mSampleRate);
	mDsp.setMaxBlockSize (setup.maxSamplesPerBlock);

	mScratch.assign (static_cast<size_t> (std::max (0, setup.maxSamplesPerBlock)) * kChannelCount, 0.f);

	return AudioEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setActive (TBool state)
{
	if (state)
	{
		mDsp.reset ();
		mActive.store (true, std::memory_order_release);

		// setActive is the UI THREAD, so messages are legitimate here.
		// EVERY cell's status, every time: a panel opened after the files
		// were loaded, or a project restored before the two components
		// were connected, has no other way to learn that a cell failed.
		sendAllCellStatuses ();
	}
	else
	{
		// No block can be running once we are inactive, so everything on
		// the retire list is safe to free.
		mActive.store (false, std::memory_order_release);
		collectRetired (true);
	}

	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
void SampleDrumProcessor::applyParam (ParamID id, double normalized)
{
	// kBypass FIRST, because it is 1000 and not in the table. Every id is
	// checked before anything indexes kParams.
	if (id == kBypass)
	{
		mBypass = (normalized >= 0.5);
		return;
	}

	if (!isTableParam (id))
		return;

	mParams[id] = normalized;
	const ParamDef& def = paramDef (id);

	// THE 32 CELL LEVELS, by range rather than 32 case labels, so a cell
	// cannot be wired to its neighbour's bar.
	if (isCellLevelParam (id))
	{
		mDsp.setCellLevelDb (cellOfLevelParam (id), def.toInternal (normalized));
		return;
	}

	// THE 8 COLUMN NOTES, likewise by range. noteFromNormalized is the
	// rounding the panel and the host text use too.
	if (isColumnNoteParam (id))
	{
		mDsp.setColumnNote (columnOfNoteParam (id), noteFromNormalized (normalized));
		return;
	}

	switch (id)
	{
		case kOutputTrim:
			// toInternal(), not toPlain(): identical today, and the call is
			// what keeps it right when a parameter gives them different
			// ranges.
			mDsp.setOutputTrimDb (def.toInternal (normalized));
			break;

		// THE ISOLATOR. toInternal() takes the log taper for the two
		// frequencies (ParamType::LogFloat).
		case kBassGain:
			mDsp.setBassDb (def.toInternal (normalized));
			break;
		case kBassFreq:
			mDsp.setBassHz (def.toInternal (normalized));
			break;
		case kTrebleGain:
			mDsp.setTrebleDb (def.toInternal (normalized));
			break;
		case kTrebleFreq:
			mDsp.setTrebleHz (def.toInternal (normalized));
			break;

		default:
			// A parameter in the table that nothing reads. Appending one
			// and forgetting this switch is the quiet failure.
			break;
	}
}

//------------------------------------------------------------------------
void SampleDrumProcessor::applyParameterChanges (IParameterChanges* changes)
{
	if (changes == nullptr)
		return;

	const int32 count = changes->getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes->getParameterData (i);
		if (queue == nullptr)
			continue;

		const int32 points = queue->getPointCount ();
		if (points <= 0)
			continue;

		// THE LAST POINT IN THE BLOCK. The trim's smoother is what stops
		// the difference from sample-accurate automation being heard.
		int32      offset = 0;
		ParamValue value  = 0.0;
		if (queue->getPoint (points - 1, offset, value) == kResultTrue)
			applyParam (queue->getParameterId (), value);
	}
}

//------------------------------------------------------------------------
void SampleDrumProcessor::handleEvent (const Event& event)
{
	switch (event.type)
	{
		case Event::kNoteOnEvent:
			// The pitch picks the COLUMN through the GM drum map, the
			// velocity picks the ROW, and the sample offset is where in the
			// block it lands - all in SampleDrumKit.h, via the DSP.
			mDsp.noteOn (event.noteOn.pitch, event.noteOn.velocity, event.sampleOffset);
			break;

		case Event::kNoteOffEvent:
			// ONE-SHOTS: a drum plays to its end whatever note-off says,
			// which is what every drum sampler does and what the user
			// asked for. Read and deliberately ignored.
			break;

		default:
			break;
	}
}

//------------------------------------------------------------------------
void SampleDrumProcessor::writeOutput (ProcessData& data, int32 numSamples)
{
	if (data.numOutputs < 1 || data.outputs[0].numChannels < kChannelCount)
		return;

	const float* const left  = mScratch.data ();
	const float* const right = mScratch.data () + numSamples;

	bool silent = true;
	for (int32 i = 0; i < numSamples && silent; ++i)
		if (left[i] != 0.f || right[i] != 0.f)
			silent = false;

	// THE PANEL'S METER: the block's peak, held until the panel takes it.
	if (!silent)
	{
		const float* const channels[2] = { left, right };
		for (int ch = 0; ch < 2; ++ch)
		{
			float peak = 0.f;
			for (int32 i = 0; i < numSamples; ++i)
				peak = std::max (peak, std::fabs (channels[ch][i]));
			if (peak > mPeak[ch].load (std::memory_order_relaxed))
				mPeak[ch].store (peak, std::memory_order_relaxed);
		}
	}

	if (data.symbolicSampleSize == kSample64)
	{
		double** out = data.outputs[0].channelBuffers64;
		for (int32 i = 0; i < numSamples; ++i)
		{
			out[0][i] = static_cast<double> (left[i]);
			out[1][i] = static_cast<double> (right[i]);
		}
	}
	else
	{
		float** out = data.outputs[0].channelBuffers32;
		std::copy (left,  left  + numSamples, out[0]);
		std::copy (right, right + numSamples, out[1]);
	}

	// SILENCE FLAGS, set only when the block really is silent - checked,
	// not assumed. A bus flagged silent that is not gets skipped by the
	// host, which is far worse than one never flagged.
	data.outputs[0].silenceFlags = 0;
	if (silent)
		for (int32 c = 0; c < data.outputs[0].numChannels; ++c)
			data.outputs[0].silenceFlags |= static_cast<uint64> (1) << c;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::process (ProcessData& data)
{
	// Flush-to-zero for this call only; the host's FPU mode comes back on
	// return. See DenormalGuard.h.
	ScopedFlushDenormals noDenormals;

	// THE BLOCK COUNTER, first thing, every call - see mBlockCounter.
	mBlockCounter.fetch_add (1, std::memory_order_acq_rel);

	// A project was loaded: stop what the old one was playing and snap
	// the trim, here on the audio thread rather than in setState.
	if (mResetPending.exchange (false, std::memory_order_acq_rel))
		mDsp.reset ();

	// PARAMETERS BEFORE ANY EARLY RETURN, or a parameter-only block's
	// automation is thrown away with it.
	applyParameterChanges (data.inputParameterChanges);

	// SYNC BEFORE ANYTHING PLAYS, including on a parameter-only block:
	// a voice whose cell was replaced must be gone before its buffer is.
	mDsp.syncCells (data.numSamples);

	// The panel's audition clicks, all of them, taken at once.
	const std::uint32_t auditions = mAuditions.exchange (0u, std::memory_order_acq_rel);
	for (int cell = 0; cell < kCellCount; ++cell)
		if ((auditions & (1u << cell)) != 0u)
			mDsp.auditionCell (cell);

	if (data.inputEvents != nullptr)
	{
		const int32 count = data.inputEvents->getEventCount ();
		for (int32 i = 0; i < count; ++i)
		{
			Event event = {};
			if (data.inputEvents->getEvent (i, event) == kResultOk)
				handleEvent (event);
		}
	}

	// A PARAMETER-ONLY BLOCK. The DSP is still told, with a null render,
	// so that queued hits are dropped rather than played a block late and
	// any voice condemned by syncCells is ended now.
	if (data.numSamples <= 0 || data.numOutputs < 1)
	{
		mDsp.render (nullptr, nullptr, 0);
		return kResultOk;
	}

	const int32 numSamples = data.numSamples;

	// NEVER TRUST THE BLOCK SIZE. Growing the scratch here would be an
	// allocation on the audio thread, so an oversized block is refused.
	if (mScratch.size () < static_cast<size_t> (numSamples) * kChannelCount)
	{
		mDsp.render (nullptr, nullptr, 0);
		return kResultFalse;
	}

	float* const left  = mScratch.data ();
	float* const right = mScratch.data () + numSamples;

	if (mBypass)
	{
		// AN INSTRUMENT'S BYPASS IS SILENCE - there is no dry path. The
		// voices are stopped, not paused: one frozen while bypassed would
		// still hold a buffer pointer that syncCells had condemned.
		mDsp.reset ();
		std::fill (left,  left  + numSamples, 0.f);
		std::fill (right, right + numSamples, 0.f);
	}
	else
	{
		mDsp.render (left, right, numSamples);
	}

	writeOutput (data, numSamples);

	return kResultOk;
}

//------------------------------------------------------------------------
// Cells
//------------------------------------------------------------------------
void SampleDrumProcessor::loadCell (int cell)
{
	if (!isCellIndex (cell))
		return;

	const std::string& path = mPaths[cell];

	SampleStatus status = SampleStatus::Empty;
	std::shared_ptr<const SampleBuffer> sample;

	// THE BYTES: the project's embedded copy if it has one, else the disk.
	// Embedded first, because a project should sound as it was saved - on
	// this machine or another, whatever has happened to the file since.
	FileBytes bytes = mFiles.get (cell);

	if (path.empty ())
	{
		bytes.reset ();
	}
	else if (!isWavPath (path))
	{
		bytes.reset ();
		status = SampleStatus::NotWave;
	}
	else
	{
		if (!bytes)
		{
			const FileReadResult read = readWholeFile (path, kMaxFileBytes, bytes);
			if (read != FileReadResult::Ok)
				status = statusOfRead (read);
		}

		if (bytes)
		{
			SampleBuffer buffer;
			status = parseWav (bytes->data (), bytes->size (), buffer);
			if (status == SampleStatus::Loaded)
				sample = std::make_shared<const SampleBuffer> (std::move (buffer));
		}
	}

	// KEEP THE BYTES FOR EMBEDDING - only for a file that actually loaded,
	// and never for a factory file, which every recipient already has. A
	// file that will not parse is not worth carrying to someone else; the
	// project goes on naming its path, the most useful thing it can do.
	mFiles.keep (cell, (status == SampleStatus::Loaded && !isFactoryPath (path))
	                       ? std::move (bytes) : FileBytes ());

	// A FAILED LOAD KEEPS ITS PATH. The cell goes on showing the name and
	// the project goes on remembering it - the file may be on a drive
	// that is not plugged in today. It plays nothing, and says why.
	mStatus[cell] = status;
	mThumbnails[cell] = sample ? makeThumbnail (*sample) : std::vector<std::int8_t> ();
	mSeconds[cell] = sample ? sample->seconds () : 0.0;
	publishCell (cell, std::move (sample));
	sendCellStatus (cell);
}

//------------------------------------------------------------------------
void SampleDrumProcessor::publishCell (int cell, std::shared_ptr<const SampleBuffer> sample)
{
	if (!isCellIndex (cell))
		return;

	std::shared_ptr<const SampleBuffer> previous = std::move (mSamples[cell]);
	mSamples[cell] = std::move (sample);

	// The DSP gets a bare pointer (RELEASE-stored) into a buffer owned here.
	mDsp.setCellSample (cell, mSamples[cell].get ());

	// Read AFTER the swap, so any block still holding the old pointer
	// started at or before this value.
	const std::uint64_t at = mBlockCounter.load (std::memory_order_acquire);
	if (previous)
		mRetired.push_back ({ std::move (previous), at });

	collectRetired (false);
}

//------------------------------------------------------------------------
void SampleDrumProcessor::collectRetired (bool force)
{
	const std::uint64_t now = mBlockCounter.load (std::memory_order_acquire);
	const bool inactive = !mActive.load (std::memory_order_acquire);

	mRetired.erase (
		std::remove_if (mRetired.begin (), mRetired.end (),
		                [&] (const Retired& retired)
		                {
			                // +2, not +1: the block running at the swap may
			                // have started at `at` itself, so one increment
			                // proves only that it began, not that it ended.
			                return force || inactive || now >= retired.at + 2;
		                }),
		mRetired.end ());
}

//------------------------------------------------------------------------
void SampleDrumProcessor::sendCellStatus (int cell)
{
	if (!isCellIndex (cell))
		return;

	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kSampleDrumCellStatusMessage);
		message->getAttributes ()->setInt (kSampleDrumCellIndexAttribute, cell);
		message->getAttributes ()->setInt (kSampleDrumCellStatusAttribute,
		                                   static_cast<int64> (mStatus[cell]));
		if (!mPaths[cell].empty ())
			message->getAttributes ()->setBinary (kSampleDrumCellPathAttribute, mPaths[cell].data (),
			                                      static_cast<uint32> (mPaths[cell].size ()));
		if (!mThumbnails[cell].empty ())
		{
			message->getAttributes ()->setBinary (kSampleDrumThumbnailAttribute, mThumbnails[cell].data (),
			                                      static_cast<uint32> (mThumbnails[cell].size ()));
			message->getAttributes ()->setFloat (kSampleDrumSecondsAttribute, mSeconds[cell]);
		}
		sendMessage (message);
	}
}

//------------------------------------------------------------------------
void SampleDrumProcessor::sendAllCellStatuses ()
{
	for (int cell = 0; cell < kCellCount; ++cell)
		sendCellStatus (cell);
}

//------------------------------------------------------------------------
void SampleDrumProcessor::sendActivity ()
{
	// Written by the AUDIO thread with relaxed atomics, read here on the
	// UI thread. Nothing locks and nothing waits; a lamp one block late
	// is right to within a few milliseconds.
	uint32 hits[kCellCount];
	uint8  sounding[kCellCount];
	for (int cell = 0; cell < kCellCount; ++cell)
	{
		hits[cell]     = mDsp.cellHits (cell);
		sounding[cell] = mDsp.cellSounding (cell) ? 1 : 0;
	}

	if (auto* reply = allocateMessage ())
	{
		FReleaser releaser (reply);
		reply->setMessageID (kSampleDrumActivityDataMessage);
		reply->getAttributes ()->setBinary (kSampleDrumHitsAttribute, hits,
		                                    static_cast<uint32> (sizeof (hits)));
		reply->getAttributes ()->setBinary (kSampleDrumSoundingAttribute, sounding,
		                                    static_cast<uint32> (sizeof (sounding)));

		const float peak[2] = { mPeak[0].exchange (0.f, std::memory_order_relaxed),
		                        mPeak[1].exchange (0.f, std::memory_order_relaxed) };
		reply->getAttributes ()->setBinary (kSampleDrumPeakAttribute, peak,
		                                    static_cast<uint32> (sizeof (peak)));
		reply->getAttributes ()->setInt (kSampleDrumVoicesAttribute, mDsp.voicesSounding ());
		reply->getAttributes ()->setInt (kSampleDrumLastHitAttribute, static_cast<int64> (mDsp.lastHit ()));
		reply->getAttributes ()->setFloat (kSampleDrumRateAttribute, mSampleRate);
		sendMessage (reply);
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::notify (IMessage* message)
{
	if (message == nullptr)
		return kInvalidArgument;

	const FIDString id = message->getMessageID ();

	if (FIDStringsEqual (id, kSampleDrumActivityRequestMessage))
	{
		sendActivity ();
		return kResultOk;
	}

	if (FIDStringsEqual (id, kSampleDrumAuditionMessage))
	{
		int64 cell = -1;
		if (message->getAttributes ()->getInt (kSampleDrumCellIndexAttribute, cell) == kResultOk
		    && isCellIndex (static_cast<int> (cell)))
		{
			mAuditions.fetch_or (1u << static_cast<int> (cell), std::memory_order_acq_rel);
		}
		return kResultOk;
	}

	if (FIDStringsEqual (id, kSampleDrumCellMessage))
	{
		int64 cell = -1;
		if (message->getAttributes ()->getInt (kSampleDrumCellIndexAttribute, cell) != kResultOk
		    || !isCellIndex (static_cast<int> (cell)))
			return kResultOk;   // a future build's bigger grid: dropped, not clamped

		// An ABSENT path is a cleared cell - see SampleDrumIDs.h.
		std::string path;
		const void* bytes = nullptr;
		uint32 size = 0;
		if (message->getAttributes ()->getBinary (kSampleDrumCellPathAttribute, bytes, size) == kResultOk
		    && bytes != nullptr && size > 0 && size <= static_cast<uint32> (kMaxPathBytes))
		{
			path.assign (static_cast<const char*> (bytes), size);
		}

		// A CELL DRAGGED FROM ANOTHER brings that cell's embedded bytes -
		// see kSampleDrumCellFromAttribute and CellFiles::replace, which
		// also keeps what this cell held for the second half of a swap. A
		// DROP FROM OUTSIDE names no cell, carries nothing, and so reads
		// the disk afresh - which is also how a sample edited since it was
		// embedded is brought back in: drop it again. A CLEAR (empty path)
		// lets the embedded copy go, so the next save no longer carries it.
		int64 from = -1;
		message->getAttributes ()->getInt (kSampleDrumCellFromAttribute, from);
		const int fromCell = isCellIndex (static_cast<int> (from)) ? static_cast<int> (from) : -1;

		mFiles.replace (static_cast<int> (cell), mPaths[cell], path, fromCell,
		                fromCell >= 0 ? mPaths[fromCell] : std::string ());

		// notify() is the UI thread, the only reason it may open a file.
		mPaths[cell] = path;
		loadCell (static_cast<int> (cell));
		return kResultOk;
	}

	return AudioEffect::notify (message);
}

//------------------------------------------------------------------------
// State
//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::getState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	StreamState out;
	for (int i = 0; i < kNumParams; ++i)
		out.params[i] = mParams[i];
	out.bypass = mBypass;
	for (int cell = 0; cell < kCellCount; ++cell)
		out.paths[cell] = mPaths[cell];

	if (!writeState (streamer, out))
		return kResultFalse;

	// THE FILES THEMSELVES, last - see writeEmbedded. The 32 pointers are
	// snapshotted under the lock and streamed outside it: the bytes are
	// immutable, so a cell replaced mid-save only means this save carries
	// the old file. Holding the lock for the whole write would stall a
	// drop on the panel behind the save.
	FileBytes files[kCellCount];
	mFiles.snapshot (files);

	return writeEmbedded (streamer, files) ? kResultOk : kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	// readState starts from DEFAULTS and only overwrites what the stream
	// holds, so a short stream cannot leave the previous project's
	// settings - or its KIT - in this instance. The controller reads the
	// same bytes through the same function.
	StreamState in;
	if (!readState (streamer, in))
		return kResultFalse;

	for (int i = 0; i < kNumParams; ++i)
		applyParam (kParams[i].id, in.params[i]);
	mBypass = in.bypass;

	// The embedded files. readEmbedded empties all 32 first, so a project
	// saved before the block existed leaves every cell to be read from
	// disk exactly as before - and the NEXT save embeds them.
	{
		FileBytes files[kCellCount];
		if (in.complete)
			readEmbedded (streamer, files);

		mFiles.restore (files);
	}

	// Every cell reloaded, including ones that did not change - from the
	// embedded copy where the project carries one, from disk where it does
	// not. setState is the UI thread, so reading is allowed.
	for (int cell = 0; cell < kCellCount; ++cell)
	{
		mPaths[cell] = in.paths[cell];
		loadCell (cell);
	}

	// The trim must not GLIDE to a loaded project's value from the last
	// one's - an audible swoop on every project load. Done by the audio
	// thread at the top of its next block; see mResetPending.
	mResetPending.store (true, std::memory_order_release);

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
