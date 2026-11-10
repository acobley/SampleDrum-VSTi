//------------------------------------------------------------------------
// SampleDrum - edit controller
//
// The half of the plug-in that talks to the host about parameters and
// owns the panel. It does NOT share memory with the processor - they may
// not even be in the same process - which is the reason for three rules
// that run through this project:
//
//   * anything the panel displays that the DSP decides comes from one
//     shared function both call (SampleDrumKit.h, SampleDrumDsp.h);
//   * the state stream is read by the same function the processor writes
//     it with (SampleDrumState.h);
//   * everything else travels as a message on the UI thread
//     (SampleDrumIDs.h) - never from process().
//
// THE CELLS. This half keeps its own copy of the 32 paths and statuses
// for the panel. A drop comes in through setCellPath(), which updates the
// copy, tells every open editor, and sends the path to the processor -
// the half that loads the file and saves the project. The processor
// replies with how the file read, and that is what the cell then shows.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumKit.h"
#include "SampleDrumParams.h"
#include "SampleDrumSample.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <cstdint>
#include <string>
#include <vector>

namespace SampleDrum {

class SampleDrumEditor;

//------------------------------------------------------------------------
class SampleDrumController : public Steinberg::Vst::EditControllerEx1
{
public:
	SampleDrumController ();
	~SampleDrumController () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IEditController*)new SampleDrumController;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	/** The PROCESSOR's state, read with the processor's own reader. */
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	/** The controller's own state: none yet, so both succeed doing
	    nothing - a failure here makes some hosts log an error for nothing. */
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
	                                                  Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

	/** Cell statuses and lamp data from the processor. */
	Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

	//--------------------------------------------------------------------
	// For the editor
	//--------------------------------------------------------------------
	const std::string& cellPath (int cell) const;
	SampleStatus cellStatus (int cell) const;

	/** The cell's waveform and length as the processor last reported them
	    - believed, like the status, only for the path it was made from.
	    Empty / 0 otherwise. */
	const std::vector<std::int8_t>& cellThumbnail (int cell) const;
	double cellSeconds (int cell) const;

	/** A file dropped on a cell from OUTSIDE, or "" to empty it. */
	void setCellPath (int cell, const std::string& path);

	/** A cell dragged onto another cell of THIS instance.

	    `copy` false MOVES it, which is a SWAP: the two cells exchange
	    files, so nothing is lost - the target's sample lands where the
	    dragged one came from (or that cell empties, if the target was
	    empty). `copy` true leaves the source as it was.

	    THE LEVEL TRAVELS WITH THE SAMPLE, as Project6's pad settings do:
	    it was set for that sound. The embedded copy travels too - each
	    half names the cell its file came from. */
	void moveCell (int from, int to, bool copy);

	/** A cell EMPTIED from the panel (right-click menu or Option-click).
	    The processor lets go of the embedded copy, so the next save no
	    longer carries the file. THE CELL'S LEVEL STAYS, for the next
	    file dropped on it - as a replaced file inherits it. */
	void clearCell (int cell);

	/** This instance's own random id, put in every cell drag so a drop
	    can tell a drag from this instance from one from another. */
	std::uint64_t instanceId () const { return mInstanceId; }
	void auditionCell (int cell);
	void requestActivity ();

	void editorAttached (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorRemoved (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorDestroyed (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;

private:
	void refreshCellInEditors (int cell);

	/** The one door every cell change goes through. `carriedFrom` is the
	    source cell of a drag, or -1. */
	void setCellPath (int cell, const std::string& path, int carriedFrom);

	/** One parameter as a COMPLETE EDIT GESTURE - begin, set, perform,
	    end - so the host records it and every open editor follows. Only
	    when it changes: a gesture that writes a value over itself is an
	    undo point that says nothing. */
	void writeParam (Steinberg::Vst::ParamID tag, double normalized);

	std::uint64_t mInstanceId = 0;

	std::vector<SampleDrumEditor*> mEditors;

	/** What the panel shows in each cell. */
	std::string  mPaths[kCellCount];

	/** What the processor last REPORTED, and for which path. cellStatus()
	    only believes a report whose path matches mPaths - see the note on
	    kSampleDrumCellStatusMessage. Until then a named cell is assumed
	    loaded: an optimistic guess for the moment in between, rather than
	    a cell that flashes "could not be read" on every project load. */
	std::string  mReportedPaths[kCellCount];
	SampleStatus mReported[kCellCount] = {};
	std::vector<std::int8_t> mThumbnails[kCellCount];
	double mSeconds[kCellCount] = {};
};

//------------------------------------------------------------------------
} // namespace SampleDrum
