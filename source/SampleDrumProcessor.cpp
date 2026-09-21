//------------------------------------------------------------------------
// SampleDrum - audio processor implementation
//------------------------------------------------------------------------

#include "SampleDrumProcessor.h"
#include "SampleDrumIDs.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SampleDrum {

//------------------------------------------------------------------------
/** Bumped whenever the layout of the state stream changes. setState
    reads it first and refuses a stream from the future rather than
    guessing at it. */
static const int32 kStateVersion = 1;

//------------------------------------------------------------------------
SampleDrumProcessor::SampleDrumProcessor ()
{
	setControllerClass (kSampleDrumControllerUID);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// An instrument: notes in, audio out, NO AUDIO INPUT. The empty
	// input side is what makes this an aumu rather than an aufx, and it
	// is why resource/au-info.plist says 0 in / 2 out.
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	// 16 channels, the MIDI convention. A host maps its MIDI port onto
	// this; without it the plug-in is an instrument nobody can play, and
	// the symptom is silence rather than an error.
	addEventInput (STR16 ("Event In"), 16);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::terminate ()
{
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                            SpeakerArrangement* outputs, int32 numOuts)
{
	// Stereo out, nothing in, and nothing else accepted. This must agree
	// with the AudioComponents entry in resource/au-info.plist, which
	// lists exactly one layout - 0 in / 2 out - or auval rejects the AU.
	//
	// Refusing everything else is the point: a layout accepted here and
	// absent from the plist, or the other way round, is the mismatch
	// auval finds and nothing else does.
	if (numIns != 0 || numOuts != 1)
		return kResultFalse;

	if (outputs[0] != SpeakerArr::kStereo)
		return kResultFalse;

	return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	// BOTH, because hosts offer 64-bit buffers and one that is refused
	// silently gets a plug-in the host will not load. The DSP is float
	// throughout; writeOutput widens into the host's buffer.
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setupProcessing (ProcessSetup& setup)
{
	mSampleRate = setup.sampleRate;

	// ALLOCATION BELONGS HERE, not in process(). Both the SpaceDub and
	// the ForTran DXis reallocated from inside their processing loops,
	// and this is the only place the host says how big a block to
	// expect.
	mDsp.setSampleRate (mSampleRate);
	mDsp.setMaxBlockSize (setup.maxSamplesPerBlock);

	mScratch.assign (static_cast<size_t> (setup.maxSamplesPerBlock) * kChannelCount, 0.f);

	return AudioEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setActive (TBool state)
{
	if (state)
	{
		mDsp.reset ();

		// setActive runs on the UI THREAD, which makes it one of the
		// few legitimate places to sendMessage from - should a panel
		// ever need the real sample rate, this is where it is sent.
		// There is no panel yet, so nothing is. See SampleDrumIDs.h.
	}

	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
void SampleDrumProcessor::applyParam (ParamID id, double normalized)
{
	// kBypass FIRST, because it is 1000 and is not in the table. Every
	// id is checked before anything indexes kParams - see the comment on
	// kBypass in SampleDrumParams.h.
	if (id == kBypass)
	{
		mBypass = (normalized >= 0.5);
		return;
	}

	if (!isTableParam (id))
		return;

	const ParamDef& def = paramDef (id);

	switch (id)
	{
		case kOutputTrim:
			// toInternal(), not toPlain() - identical today, and the
			// call is what makes it stay correct when a ported
			// parameter gives them different ranges.
			mDsp.setOutputTrimDb (def.toInternal (normalized));
			break;

		default:
			// A parameter in the table that nothing reads. Appending one
			// and forgetting this switch is the quiet failure; there is
			// no way to catch it at compile time with a table, so it is
			// named here instead.
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

		// THE LAST POINT IN THE BLOCK, not the first. Sample-accurate
		// automation within a block is not read here; the DSP's own
		// smoother is what stops that being audible, which is why the
		// smoothed column in the parameter table matters.
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
			// A NOTE THAT STARTS NOTHING, for now. When there are
			// samples, this is where a pad is struck: event.noteOn.pitch
			// picks the pad, .velocity is 0..1 already, and
			// event.sampleOffset is where in the block it lands - a
			// voice started at offset 0 regardless is the classic
			// timing bug, audible as everything quantised to the block
			// size.
			break;

		case Event::kNoteOffEvent:
			// Drums are mostly one-shots, so a note off may well do
			// nothing even in the finished plug-in - but it has to be
			// READ, or a host that sends note off without note on
			// (every host, on transport stop) leaves state behind.
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

	// SILENCE FLAGS. A host is entitled to skip a bus we declare silent,
	// and an instrument that renders silence and does not say so keeps
	// every downstream plug-in awake. Cleared - not set - the moment
	// there are voices; a bus flagged silent that is not is far worse
	// than one that is not flagged.
	data.outputs[0].silenceFlags = 0;
	for (int32 c = 0; c < data.outputs[0].numChannels; ++c)
		data.outputs[0].silenceFlags |= static_cast<uint64> (1) << c;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::process (ProcessData& data)
{
	// PARAMETERS FIRST, ALWAYS - before the early return below, or a
	// parameter-only block would be thrown away along with the block.
	applyParameterChanges (data.inputParameterChanges);

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

	// A PARAMETER-ONLY BLOCK: numSamples == 0, or no output bus at all.
	// Both are legal and both arrive in practice - hosts send them to
	// deliver automation between audible blocks. Returning kResultOk is
	// the correct answer; the work above has already been done.
	if (data.numSamples <= 0 || data.numOutputs < 1)
		return kResultOk;

	const int32 numSamples = data.numSamples;

	// NEVER TRUST THE BLOCK SIZE. setupProcessing promised
	// maxSamplesPerBlock, but a host that hands over more would walk off
	// the end of a scratch sized on that promise, so the scratch is
	// checked rather than assumed. Growing it here WOULD BE AN
	// ALLOCATION ON THE AUDIO THREAD, so the block is refused instead -
	// loudly silent beats intermittently crashing.
	if (mScratch.size () < static_cast<size_t> (numSamples) * kChannelCount)
		return kResultFalse;

	float* const left  = mScratch.data ();
	float* const right = mScratch.data () + numSamples;

	if (mBypass)
	{
		// AN INSTRUMENT'S BYPASS IS SILENCE, not a dry path: there is no
		// input to pass through. The host expects a bypassed plug-in to
		// be inaudible, and that is what this is.
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
tresult PLUGIN_API SampleDrumProcessor::getState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	streamer.writeInt32 (kStateVersion);

	// THE COUNT IS WRITTEN, so a stream from an older build with fewer
	// parameters can be read by a newer one. This is the other half of
	// "append, never insert".
	streamer.writeInt32 (kNumParams);

	for (int i = 0; i < kNumParams; ++i)
	{
		// NORMALISED, not plain. The plain range is allowed to change
		// between versions - a wider trim, say - and a state stream full
		// of plain values would silently rescale when it did.
		const ParamDef& def = kParams[i];
		double normalized = def.defaultNormalized ();

		if (def.id == kOutputTrim)
			normalized = def.toNormalized (mDsp.outputTrimDb ());

		streamer.writeDouble (normalized);
	}

	streamer.writeInt32 (mBypass ? 1 : 0);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumProcessor::setState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;
	if (version > kStateVersion)
		return kResultFalse;   // written by a newer build - do not guess

	int32 count = 0;
	if (!streamer.readInt32 (count))
		return kResultFalse;

	//--------------------------------------------------------------------
	// EVERYTHING THE STREAM DOES NOT MENTION GOES BACK TO ITS DEFAULT,
	// and this is the trap that reset is here to close.
	//
	// A host reuses one plug-in instance as the user loads project after
	// project. If setState only applied what the stream contained, a
	// short stream - written by an older build, or truncated - would
	// leave every unmentioned parameter holding the PREVIOUS project's
	// value. The user opens an old song and hears the settings from the
	// new one, with nothing anywhere saying why.
	//
	// So: defaults first, stream second. The controller's
	// setComponentState does exactly the same thing in the same order,
	// reading the identical layout - two halves that disagree about the
	// stream is the same bug wearing a different hat.
	//--------------------------------------------------------------------
	for (int i = 0; i < kNumParams; ++i)
		applyParam (kParams[i].id, kParams[i].defaultNormalized ());
	mBypass = false;

	for (int32 i = 0; i < count; ++i)
	{
		double v = 0.0;
		if (!streamer.readDouble (v))
			return kResultFalse;
		if (i < kNumParams)
			applyParam (static_cast<ParamID> (i), v);
	}

	// OPTIONAL BY DESIGN: a version-1 stream always has it, but reading
	// it conditionally is what lets a later version append fields
	// without every older reader failing.
	int32 bypass = 0;
	if (streamer.readInt32 (bypass))
		mBypass = (bypass != 0);

	// The trim must not GLIDE to a loaded project's value from the last
	// one's - that is an audible swoop on every project load.
	mDsp.reset ();

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
