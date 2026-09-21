//------------------------------------------------------------------------
// SampleDrum - audio processor
//
// SampleDrum is an INSTRUMENT: an event input and one stereo audio
// output, no audio input. That decision reaches FOUR places and they
// must all agree, or the plug-in loads in the wrong list, or auval
// rejects the Audio Unit:
//
//   * PlugType::kInstrumentDrum in SampleDrumEntry.cpp
//   * the buses added in initialize() below
//   * what setBusArrangements accepts
//   * the single 0-in / 2-out entry in resource/au-info.plist
//
// auval checks the last two against each other.
//
// THE DSP RENDERS SILENCE. What is here is the bus layout, the parameter
// plumbing, the event handling and the state - which is exactly what the
// SDK validator exercises, and getting that validating before there is
// any audio to blame is the whole point of doing it in this order. See
// ../PORT-CHECKLIST.md phase 2.
//
// THE PROCESS CONTEXT IS NOT ASKED FOR, and this is the note about why
// that is a decision rather than an omission.
//
//   Since VST3 3.7 the ProcessContext is OPT-IN and the default is NO
//   FLAGS. Without a getProcessContextRequirements override,
//   data.processContext arrives with nothing valid in it: everything
//   that reads the tempo silently gets 120 in every host, the bar lines
//   land nowhere, and the validator prints "- None" rather than
//   complaining. A sequencer that launches on the bar would simply
//   never launch and nothing would say why.
//
//   Nothing here reads the tempo yet, so the override is absent
//   deliberately. THE MOMENT ANYTHING DOES - a sync division, a bar
//   launch, a tempo readout - add it back as:
//
//       Steinberg::uint32 PLUGIN_API getProcessContextRequirements ()
//           SMTG_OVERRIDE
//       {
//           processContextRequirements.needTempo ();
//           processContextRequirements.needTransportState ();
//           processContextRequirements.needProjectTimeMusic ();
//           processContextRequirements.needTimeSignature ();
//           return AudioEffect::getProcessContextRequirements ();
//       }
//
//   asking for only the fields actually read. AudioEffect already
//   implements IProcessContextRequirements; naming the fields is all
//   that is needed.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumDsp.h"
#include "SampleDrumParams.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <vector>

namespace SampleDrum {

//------------------------------------------------------------------------
class SampleDrumProcessor : public Steinberg::Vst::AudioEffect
{
public:
	SampleDrumProcessor ();

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IAudioProcessor*)new SampleDrumProcessor;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
	                                                  Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
	/** Read every change out of data.inputParameterChanges and hand the
	    values to the DSP. */
	void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);

	/** One normalised value onto the thing that owns it. The single
	    place a parameter id turns into DSP state, so a new parameter has
	    one place to be wired up and one place to be forgotten. */
	void applyParam (Steinberg::Vst::ParamID id, double normalized);

	/** Note on, note off, and everything else ignored. A stub with the
	    event input already unpacked, because an instrument whose events
	    never arrive looks identical to one whose voices are silent. */
	void handleEvent (const Steinberg::Vst::Event& event);

	/** Write the DSP's block out to the host's bus, converting to
	    64-bit if that is what it asked for. */
	void writeOutput (Steinberg::Vst::ProcessData& data, Steinberg::int32 numSamples);

	SampleDrumDsp mDsp;

	double mSampleRate = 44100.0;
	bool   mBypass     = false;

	/** THE 32-BIT SCRATCH THE DSP ALWAYS RENDERS INTO.

	    Sized in setupProcessing and never resized anywhere else, so the
	    audio thread never allocates. It exists because a host is
	    entitled to hand over 64-bit buffers (canProcessSampleSize
	    accepts kSample64), and the DSP is float throughout - rather than
	    write the DSP twice, it renders here and writeOutput widens on
	    the way out. */
	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
