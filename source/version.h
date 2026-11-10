//------------------------------------------------------------------------
// SampleDrum - version strings
//
// Keep MAJOR/SUB/RELEASE/BUILD in step with PLUGIN_VERSION in
// CMakeLists.txt and with CFBundleVersion in resource/au-info.plist.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/fplatform.h"

#define MAJOR_VERSION_STR "1"
#define MAJOR_VERSION_INT 1

#define SUB_VERSION_STR "1"
#define SUB_VERSION_INT 1

#define RELEASE_NUMBER_STR "0"
#define RELEASE_NUMBER_INT 0

#define BUILD_NUMBER_STR "0"
#define BUILD_NUMBER_INT 0

#define FULL_VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR "." BUILD_NUMBER_STR
#define VERSION_STR      MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR

#define stringOriginalFilename "SampleDrum.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription "SampleDrum VST3 (64Bit)"
#else
#define stringFileDescription "SampleDrum VST3"
#endif
// The VST3 vendor. NO FULL STOPS, and the same as the manufacturer part of
// the AU name in resource/au-info.plist ("AE Cobley: SampleDrum") and
// PLUGIN_COMPANY in CMakeLists.txt: hosts group plug-ins and file presets
// under it, and REAPER cuts an AU name at the first full stop.
#define stringCompanyName    "AE Cobley"
#define stringLegalCopyright "Copyright 2026 A. E. Cobley"
#define stringLegalTrademarks "VST is a trademark of Steinberg Media Technologies GmbH"
