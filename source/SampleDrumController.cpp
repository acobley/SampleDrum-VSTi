//------------------------------------------------------------------------
// SampleDrum - edit controller implementation
//------------------------------------------------------------------------

#include "SampleDrumController.h"
#include "SampleDrumDsp.h"
#include "SampleDrumIDs.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SampleDrum {

//------------------------------------------------------------------------
namespace {

/** A RangeParameter that formats its own text, so the host's generic
    editor shows the same number a panel will. */
class SampleDrumParameter : public RangeParameter
{
public:
	SampleDrumParameter (const ParamDef& def)
	// TITLE AND UNITS MUST NOT BE NULL. RangeParameter passes them
	// straight to UString::assign, which dereferences both without a
	// null check. The symptom is not a crash in the plug-in: it is the
	// SDK VALIDATOR segfaulting during EditController::initialize, in
	// the post-build step, which sends you looking at the build system.
	// Empty string, never null.
	: RangeParameter (USTRING (def.title), def.id, USTRING (def.units), def.plainMin, def.plainMax,
	                  def.plainDefault, def.stepCount, ParameterInfo::kCanAutomate,
	                  kRootUnitId, USTRING (def.title))
	, mDef (def)
	{
	}

	void toString (ParamValue normalized, String128 string) const SMTG_OVERRIDE
	{
		char text[64] = {};

		if (mDef.type == ParamType::Bool)
		{
			std::snprintf (text, sizeof (text), "%s", (normalized >= 0.5) ? "On" : "Off");
		}
		else if (mDef.id == kOutputTrim)
		{
			// The sign is explicit: a trim reading "3.0 dB" when it is
			// boosting and "-3.0 dB" when it is cutting makes the
			// centre hard to find at a glance.
			std::snprintf (text, sizeof (text), "%+.1f", mDef.toPlain (normalized));
		}
		else
		{
			std::snprintf (text, sizeof (text), "%.2f", mDef.toPlain (normalized));
		}

		UString (string, str16BufferSize (String128)).assign (USTRING (text));
	}

private:
	ParamDef mDef;
};

} // anonymous namespace

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::initialize (FUnknown* context)
{
	const tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
		return result;

	for (int i = 0; i < kNumParams; ++i)
		parameters.addParameter (new SampleDrumParameter (kParams[i]));

	// Standard VST3 bypass, id 1000 and not part of the table - see
	// SampleDrumParams.h.
	parameters.addParameter (STR16 ("Bypass"), STR16 (""), 1, 0,
	                         ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypass);

	// Read-only output parameters - the mechanism for a value the DSP
	// computes per block - are added here too, from id 1001 up. None
	// yet. STR16 ("") rather than nullptr for their units, for the
	// reason in the banner above.

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::terminate ()
{
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::setComponentState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (!streamer.readInt32 (version))
		return kResultFalse;

	int32 count = 0;
	if (!streamer.readInt32 (count))
		return kResultFalse;

	// DEFAULTS FIRST, exactly as the processor's setState does, and for
	// the same reason: a short stream must not leave the last project's
	// values showing in the host. The two halves read the identical
	// layout in the identical order - one that disagrees with the other
	// is a controller that lies about what the DSP is doing.
	for (int i = 0; i < kNumParams; ++i)
		setParamNormalized (kParams[i].id, kParams[i].defaultNormalized ());
	setParamNormalized (kBypass, 0.0);

	for (int32 i = 0; i < count; ++i)
	{
		double v = 0.0;
		if (!streamer.readDouble (v))
			return kResultFalse;
		if (i < kNumParams)
			setParamNormalized (static_cast<ParamID> (i), v);
	}

	int32 bypass = 0;
	if (streamer.readInt32 (bypass))
		setParamNormalized (kBypass, bypass ? 1.0 : 0.0);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::setState (IBStream* /*state*/)
{
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::getState (IBStream* /*state*/)
{
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
