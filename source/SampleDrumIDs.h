//------------------------------------------------------------------------
// SampleDrum - class UIDs and message identifiers
//
// These UIDs were generated fresh for this plug-in from os.urandom.
// NEVER CHANGE THEM once a build has been shipped: hosts store them in
// the project file, so a changed UID means every existing session
// silently loses the plug-in. The same is true of the four-character
// codes in resource/au-info.plist and of both bundle identifiers in
// CMakeLists.txt.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace SampleDrum {

static const Steinberg::FUID kSampleDrumProcessorUID  (0xAFAD783D, 0xFCC97C1E, 0x28EF80EF, 0xD4DC5FCF);
static const Steinberg::FUID kSampleDrumControllerUID (0xDBD88CFF, 0x07124209, 0x6131B114, 0x20CECD1D);

// The plug-in category is declared once, in SampleDrumEntry.cpp, using the
// SDK's own PlugType::kInstrumentDrum - not repeated as a string here.

//------------------------------------------------------------------------
// Processor <-> controller messages
//
// RESERVED SPACE - there are none yet, and the scaffold deliberately
// ships without the IConnectionPoint traffic FilterDrum started with,
// because with no editor there is nobody to receive it. Read the rule
// below BEFORE adding the first one.
//
// Every message must travel on the UI THREAD. sendMessage from
// process() returns success and is then silently discarded by the
// host's connection proxy - it does nothing and tells you nothing, so a
// value the DSP produces per block can never come out this way. Anything
// the DSP computes per block that a panel needs goes out through
// data.outputParameterChanges instead, as a hidden read-only parameter
// from id 1001 up; see the note beside kBypass in SampleDrumParams.h.
//
// Messages ARE legitimate from setActive, setState and notify. Add new
// ones here, each with a comment saying which of those it is sent from.
// (A sampler's likeliest first one: the controller telling the processor
// which file to load - sent from the controller, received in notify,
// and the file itself read OFF the audio thread.)
//
// The other limitation to know before adding one: if a host never
// connects the two components, a message is never delivered and nothing
// says so. That is why parameters, which cannot be lost this way, carry
// everything that can be expressed as a number.
//------------------------------------------------------------------------

//------------------------------------------------------------------------
} // namespace SampleDrum
