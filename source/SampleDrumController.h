//------------------------------------------------------------------------
// SampleDrum - edit controller
//
// The half of the plug-in that talks to the host about parameters. It
// does NOT share memory with the processor - they may not even be in the
// same process - which is the reason for two rules that run through this
// project:
//
//   * anything a panel displays that the DSP computes comes from one
//     shared function both call (SampleDrumDsp.h), not from two copies
//     of the arithmetic;
//   * a value the DSP produces per block reaches here through
//     data.outputParameterChanges, never through sendMessage from
//     process() (SampleDrumIDs.h).
//
// NO EDITOR YET, deliberately. EditControllerEx1::createView returns
// nullptr, so hosts show their own generic parameter list - which is all
// the SDK validator needs, and it keeps the first validating build down
// to processor, controller and entry (PORTING-GUIDE.md step 6).
//
// WHEN ONE IS ADDED, it needs five overrides here: createView,
// editorAttached, editorRemoved, editorDestroyed and a setParamNormalized
// that walks the open editors. And one trap, in editorDestroyed: it is
// called from ~EditorView(), by which point the derived editor
// sub-object is already gone, so dynamic_cast<SampleDrumEditor*> returns
// NULL and the entry silently survives as a dangling pointer. Compare
// the UPCAST pointer instead:
//
//     static_cast<EditorView*> (e) == editor
//
// FilterDrumController.cpp is the worked example, and
// SpyBandControls.{h,cpp} is the control set that lifts across with only
// a namespace rename (no bitmaps).
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumParams.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace SampleDrum {

//------------------------------------------------------------------------
class SampleDrumController : public Steinberg::Vst::EditControllerEx1
{
public:
	SampleDrumController () = default;
	~SampleDrumController () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IEditController*)new SampleDrumController;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	/** The PROCESSOR's state, which is the one that matters - it is what
	    the host saved. Reads the identical layout getState wrote. */
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	/** The CONTROLLER's own state - panel-only settings with no
	    parameter behind them. There are none yet, so both are no-ops
	    that return kResultOk rather than kResultFalse: a host that saves
	    an empty controller state and gets a failure back logs an error
	    for nothing. */
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
