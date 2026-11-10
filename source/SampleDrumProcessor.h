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
// WHAT THIS HALF OWNS: the 32 cell paths (it is the half the host asks
// for the project's state), each cell's file as it was on disk (embedded
// in the project, so it travels - see mFiles), the decoded SampleBuffers,
// and the DSP. Files
// are opened and decoded HERE, in notify() and setState(), which are the
// UI thread - never in process(). The DSP is handed bare pointers; the
// shared_ptrs stay here, and a replaced buffer is freed only once two
// further blocks have begun (see publishCell and the retire list).
//
// THE PROCESS CONTEXT IS NOT ASKED FOR, deliberately. Since VST3 3.7 it
// is OPT-IN and the default is NO FLAGS: without a
// getProcessContextRequirements override, anything that reads the tempo
// silently gets 120 in every host and the validator prints "- None"
// rather than complaining. Nothing here reads tempo - the kit plays what
// MIDI tells it, when it tells it. THE MOMENT ANYTHING DOES (a built-in
// sequencer, a tempo-synced effect), add:
//
//       Steinberg::uint32 PLUGIN_API getProcessContextRequirements ()
//           SMTG_OVERRIDE
//       {
//           processContextRequirements.needTempo ();
//           processContextRequirements.needTransportState ();
//           return AudioEffect::getProcessContextRequirements ();
//       }
//
// asking for only the fields actually read.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumDsp.h"
#include "SampleDrumKit.h"
#include "SampleDrumParams.h"
#include "SampleDrumSample.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
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

	/** UI thread: cell paths from the panel, auditions, lamp requests. */
	Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
	void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);

	/** One normalised value onto the thing that owns it. The single
	    place a parameter id turns into DSP state. */
	void applyParam (Steinberg::Vst::ParamID id, double normalized);

	void handleEvent (const Steinberg::Vst::Event& event);

	void writeOutput (Steinberg::Vst::ProcessData& data, Steinberg::int32 numSamples);

	//--------------------------------------------------------------------
	// Cells - UI THREAD ONLY
	//--------------------------------------------------------------------

	/** Decode mFiles[cell] if the project carried it, else read the file
	    at mPaths[cell]; publish it, keep the bytes for embedding, report
	    how it went. */
	void loadCell (int cell);

	/** Hand a buffer (or none) to the DSP and retire the one it replaces.
	    The old buffer is NOT freed here - a block may be reading it. */
	void publishCell (int cell, std::shared_ptr<const SampleBuffer> sample);

	/** Free what is safe to free. `force` when no block can be running. */
	void collectRetired (bool force);

	void sendCellStatus (int cell);
	void sendAllCellStatuses ();
	void sendActivity ();

	//--------------------------------------------------------------------
	SampleDrumDsp mDsp;

	double mSampleRate = 44100.0;
	bool   mBypass     = false;

	/** Normalised value of every table parameter, for getState. */
	double mParams[kNumParams] = {};

	/** THE AUTHORITATIVE COPY of what is in each cell - the saved one.
	    Strings allocate, so NOTHING ON THE AUDIO THREAD READS THESE. */
	std::string  mPaths[kCellCount];
	SampleStatus mStatus[kCellCount] = {};

	/** EACH CELL'S FILE AS IT WAS ON DISK, for embedding in the project,
	    and the rule for carrying one to another cell - see CellFiles.

	    Filled by loadCell from the disk read, by setState from the
	    stream, or by notify() from the cell a file was dragged from - and
	    when it holds a file, loadCell decodes that INSTEAD of the disk:
	    the embedded copy wins, so a project sounds as it was saved. Null
	    for an empty cell, a factory file and a file that did not load. */
	CellFiles mFiles;

	/** The decoded audio, OWNED HERE. The DSP holds bare pointers. */
	std::shared_ptr<const SampleBuffer> mSamples[kCellCount];

	/** Each cell's waveform for the panel (makeThumbnail) and length,
	    made once in loadCell. UI thread only. */
	std::vector<std::int8_t> mThumbnails[kCellCount];
	double mSeconds[kCellCount] = {};

	/** The output's peak on L and R since the panel last asked: raised by
	    the audio thread at the end of every block, taken with exchange(0)
	    by sendActivity on the UI thread. A peak raced between the two is
	    at worst shown one tick late. */
	std::atomic<float> mPeak[2] { { 0.f }, { 0.f } };

	/** A replaced buffer and the block number at which it was replaced. */
	struct Retired
	{
		std::shared_ptr<const SampleBuffer> sample;
		std::uint64_t at = 0;
	};
	std::vector<Retired> mRetired;

	/** Incremented at the top of EVERY process() call.

	    The whole lock-free handoff: a buffer retired when this read N
	    cannot be in use once it reads N + 2, because the block running at
	    the swap has returned before the next-but-one began - and the DSP's
	    syncCells() ends any voice still holding it inside the block after
	    the swap. No lock, no allocation on the audio thread, nothing freed
	    under a block that is reading it. */
	std::atomic<std::uint64_t> mBlockCounter { 0 };

	/** False when deactivated: no block can be running, so everything on
	    the retire list can go. */
	std::atomic<bool> mActive { false };

	/** Cells the panel asked to hear, one bit each. Set on the UI thread,
	    taken whole with exchange(0) by the audio thread. */
	std::atomic<std::uint32_t> mAuditions { 0u };

	/** Set by setState (UI thread), acted on at the top of the next
	    process() (audio thread). reset() touches every voice, and doing
	    that from the UI thread while a block is rendering them is a data
	    race; asking the audio thread to do it is not. */
	std::atomic<bool> mResetPending { false };

	/** 32-bit scratch the DSP always renders into, sized in
	    setupProcessing. A host may hand 64-bit buffers; writeOutput widens
	    on the way out rather than the DSP being written twice. */
	std::vector<float> mScratch;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
