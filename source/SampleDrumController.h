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

#include <string>
#include <vector>

namespace SampleDrum {

class SampleDrumEditor;

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

	/** A file dropped on a cell, or "" to empty it. */
	void setCellPath (int cell, const std::string& path);
	void auditionCell (int cell);
	void requestActivity ();

	void editorAttached (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorRemoved (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
	void editorDestroyed (Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;

private:
	void refreshCellInEditors (int cell);

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
};

//------------------------------------------------------------------------
} // namespace SampleDrum
