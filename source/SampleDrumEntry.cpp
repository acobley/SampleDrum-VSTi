//------------------------------------------------------------------------
// SampleDrum - plug-in factory
//------------------------------------------------------------------------

#include "SampleDrumController.h"
#include "SampleDrumIDs.h"
#include "SampleDrumProcessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "SampleDrum"

using namespace Steinberg::Vst;
using namespace SampleDrum;

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF (stringCompanyName, "https://github.com/", "mailto:aecobley@googlemail.com")

	// SampleDrum is an INSTRUMENT, and specifically a drum one:
	// PlugType::kInstrumentDrum. This string is what a host reads to
	// decide which list to put it in, and it has to agree with the
	// 'aumu' type code in resource/au-info.plist and with the buses the
	// processor adds - notes in, stereo out, no audio input.
	//
	// Unlike the UIDs this category is NOT permanent - hosts re-read it
	// on every scan - so PlugType::kInstrumentSampler is a free change
	// later if SampleDrum grows beyond drums.
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kSampleDrumProcessorUID),
	            PClassInfo::kManyInstances,
	            kVstAudioEffectClass,
	            stringPluginName,
	            Vst::kDistributable,
	            PlugType::kInstrumentDrum,
	            FULL_VERSION_STR,
	            kVstVersionString,
	            SampleDrumProcessor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (kSampleDrumControllerUID),
	            PClassInfo::kManyInstances,
	            kVstComponentControllerClass,
	            stringPluginName "Controller",
	            0,
	            "",
	            FULL_VERSION_STR,
	            kVstVersionString,
	            SampleDrumController::createInstance)

END_FACTORY
