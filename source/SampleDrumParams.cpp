//------------------------------------------------------------------------
// SampleDrum - parameter table
//
// Column order matches the DXi ports in this folder, which in turn
// matched CMediaParams::m_aParamInfo:
//   id, title, units, type, plain min/max/default, internal min/max,
//   step count, smoothed
//
// TITLE AND UNITS MUST NEVER BE NULL. RangeParameter hands both straight
// to UString::assign, which dereferences without a null check; the
// symptom is not a crash in the plug-in but the SDK VALIDATOR
// segfaulting in the post-build step, which sends you looking in
// entirely the wrong place. Empty string, never null - which is why the
// units column below reads "dB" and not a bare comma.
//------------------------------------------------------------------------

#include "SampleDrumParams.h"

namespace SampleDrum {

//------------------------------------------------------------------------
const ParamDef kParams[kNumParams] = {
//   id             title           units  type               pMin  pMax  pDef  iMin  iMax  steps smooth
	{kOutputTrim,  "Output Trim",  "dB",  ParamType::Float,  -24., 12.,  0.,   -24., 12.,  0,    true },
};

//------------------------------------------------------------------------
} // namespace SampleDrum
