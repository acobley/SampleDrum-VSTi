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

#include "SampleDrumDsp.h"

namespace SampleDrum {

//------------------------------------------------------------------------
const ParamDef kParams[kNumParams] = {
//   id             title           units  type               pMin  pMax  pDef  iMin  iMax  steps smooth
	{kOutputTrim,  "Output Trim",  "dB",  ParamType::Float,  -24., 12.,  0.,   -24., 12.,  0,    true },

	//--------------------------------------------------------------------
	// ONE LEVEL PER CELL, in cell order: column + row * 8, so the rows
	// below run across the panel a row at a time, softest band first.
	// The range lives in SampleDrumDsp.h so the DSP, this table and the
	// panel read one copy. Titles name the drum and the velocity band,
	// because that is what a host's automation list is searched by.
	// Internal == plain: the DSP takes dB too.
	//--------------------------------------------------------------------
	// ---- row 0: velocity 1-31 -------------------------------------
	{cellLevelParam ( 0), "Kick 1-31 Level",         "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 1), "Snare 1-31 Level",        "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 2), "Clap 1-31 Level",         "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 3), "Closed HH 1-31 Level",    "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 4), "Open HH 1-31 Level",      "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 5), "Low Tom 1-31 Level",      "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 6), "High Tom 1-31 Level",     "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 7), "Crash 1-31 Level",        "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },

	// ---- row 1: velocity 32-63 ------------------------------------
	{cellLevelParam ( 8), "Kick 32-63 Level",        "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam ( 9), "Snare 32-63 Level",       "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (10), "Clap 32-63 Level",        "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (11), "Closed HH 32-63 Level",   "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (12), "Open HH 32-63 Level",     "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (13), "Low Tom 32-63 Level",     "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (14), "High Tom 32-63 Level",    "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (15), "Crash 32-63 Level",       "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },

	// ---- row 2: velocity 64-95 ------------------------------------
	{cellLevelParam (16), "Kick 64-95 Level",        "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (17), "Snare 64-95 Level",       "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (18), "Clap 64-95 Level",        "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (19), "Closed HH 64-95 Level",   "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (20), "Open HH 64-95 Level",     "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (21), "Low Tom 64-95 Level",     "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (22), "High Tom 64-95 Level",    "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (23), "Crash 64-95 Level",       "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },

	// ---- row 3: velocity 96-127 -----------------------------------
	{cellLevelParam (24), "Kick 96-127 Level",       "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (25), "Snare 96-127 Level",      "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (26), "Clap 96-127 Level",       "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (27), "Closed HH 96-127 Level",  "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (28), "Open HH 96-127 Level",    "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (29), "Low Tom 96-127 Level",    "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (30), "High Tom 96-127 Level",   "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
	{cellLevelParam (31), "Crash 96-127 Level",      "dB", ParamType::Float, kCellLevelMinDb, kCellLevelMaxDb, kCellLevelDefaultDb, kCellLevelMinDb, kCellLevelMaxDb, 0, true },
};

//------------------------------------------------------------------------
} // namespace SampleDrum
