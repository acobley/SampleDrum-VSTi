//------------------------------------------------------------------------
// SampleDrum - parameter definitions
//
// THREE RANGES PER PARAMETER, even though this plug-in is brand new and
// has one parameter. The shape is inherited from the DXi ports in this
// folder and is kept on purpose:
//
//   normalised   0..1, what VST3 automates and what the host stores
//   plain        what the USER sees - the number on the panel and in the
//                host's own generic editor
//   internal     what the DSP is handed, via toInternal()
//
// A Cakewalk DXi kept the last two explicitly - an "external" range in
// Parameters.h and an "internal" one reached through
// CParamEnvelope::MapToInternal - and a port has to reproduce both or
// the numbers on screen stop matching the original.
//
// FOR THIS PROJECT INTERNAL == PLAIN. Every parameter below sets
// internalMin/internalMax equal to plainMin/plainMax, so toInternal()
// returns the same number as toPlain(). THE SLOT IS EMPTY, NOT ABSENT -
// that is the point of saying so here. The first parameter ported in
// from a DXi will need its own internal range, and this is the machinery
// for it rather than something to invent then.
//
// Note what is NOT a range mapping: dB to linear gain. That is the DSP's
// arithmetic and lives in SampleDrumDsp.h, where the editor can call the
// same copy of it.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace SampleDrum {

//------------------------------------------------------------------------
enum Param : Steinberg::Vst::ParamID
{
	/** The output trim, in dB.

	    THE PLACEHOLDER, and it is here to be useful rather than to fill
	    space: it is the only thing that proves the whole chain - host
	    moves a parameter, process() reads it out of
	    data.inputParameterChanges, the DSP hears it, and the panel and
	    the host's generic editor agree about what it says. A scaffold
	    with no parameters validates without ever exercising any of that.

	    When the real sampler arrives, this can stay: every
	    instrument wants one. */
	kOutputTrim = 0,

	kNumParams,

	//--------------------------------------------------------------------
	/** Standard VST3 bypass.
	
	    1000, DELIBERATELY FAR PAST THE END OF THE TABLE, and this is the
	    trap it exists to make obvious. kParams is indexed by ParamID,
	    and every id that reaches an index must be range-checked first -
	    paramDef() below is the only place that indexing happens, and it
	    is the only place that check has to be right. An unchecked
	    kParams[kBypass] reads 1000 entries past a one-entry array.
	
	    Any "enabled" switch a ported DXi had stays SEPARATE from this,
	    with its own original default. They are not the same control:
	    kIsBypass is the host's, and the host expects it to pass audio
	    through untouched.
	
	    IDs 1001 upwards are the space for READ-ONLY OUTPUT parameters -
	    the mechanism for a value the DSP computes per block that the
	    panel needs. That is what they are for and not messages: a
	    sendMessage from process() is silently discarded by the host's
	    connection proxy (see SampleDrumIDs.h), whereas
	    data.outputParameterChanges is delivered on the UI thread and
	    cannot be lost. Nothing publishes one yet. */
	kBypass = 1000
};

//------------------------------------------------------------------------
/** APPEND, NEVER INSERT.
 
    A parameter added in the middle renumbers every id after it, and a
    project saved by the old build then restores its values into the
    wrong parameters - quietly, with no error anywhere. New parameters go
    immediately before kNumParams, and a parameter that is no longer
    wanted is left in place and hidden rather than removed. */
constexpr int kFirstFreeParamSlot = kNumParams;

//------------------------------------------------------------------------
enum class ParamType
{
	Float,
	Bool,
	Enum
};

//------------------------------------------------------------------------
struct ParamDef
{
	Steinberg::Vst::ParamID id;
	const char* title;          // shown by the host - NEVER null, see below
	const char* units;          // likewise
	ParamType   type;
	double      plainMin;       // what the user sees
	double      plainMax;
	double      plainDefault;
	double      internalMin;    // what the DSP is handed
	double      internalMax;
	int         stepCount;      // 0 = continuous
	bool        smoothed;       // interpolated per sample by the DSP

	//--------------------------------------------------------------------
	double toPlain (double normalized) const
	{
		return plainMin + normalized * (plainMax - plainMin);
	}

	double toNormalized (double plain) const
	{
		if (plainMax == plainMin)
			return 0.0;
		return (plain - plainMin) / (plainMax - plainMin);
	}

	/** The DXi's ParamInfo::MapToInternal. Identical to toPlain() for
	    every parameter in this project - see the banner. */
	double toInternal (double normalized) const
	{
		if (type == ParamType::Bool)
			return (normalized < 0.5) ? 0.0 : 1.0;

		const double v = internalMin + normalized * (internalMax - internalMin);
		if (type == ParamType::Enum)
		{
			// Enums were rounded to the nearest step, and the step count
			// is preserved even where two steps show the same name.
			return static_cast<double> (static_cast<long> (v + 0.5));
		}
		return v;
	}

	double defaultNormalized () const { return toNormalized (plainDefault); }
};

//------------------------------------------------------------------------
extern const ParamDef kParams[kNumParams];

/** THE ONLY PLACE kParams IS INDEXED, and therefore the only place the
    range check has to be right. ids are dense from 0 to kNumParams, and
    anything outside that - kBypass, an output parameter, a garbled id
    from a broken host - gets entry 0 rather than a read off the end.

    Callers that must distinguish "not a table parameter" from "the first
    one" test the id themselves; isTableParam() is that test. */
inline bool isTableParam (Steinberg::Vst::ParamID id)
{
	return id < static_cast<Steinberg::Vst::ParamID> (kNumParams);
}

inline const ParamDef& paramDef (Steinberg::Vst::ParamID id)
{
	return kParams[isTableParam (id) ? id : 0];
}

//------------------------------------------------------------------------
} // namespace SampleDrum
