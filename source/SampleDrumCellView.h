//------------------------------------------------------------------------
// SampleDrum - one cell of the 8 x 4 grid
//
// Drag a .wav onto it from the Finder and it takes the file - the same
// drop handling as Project6's SpySampleSlot, cut down to what a cell here
// does. Click a loaded cell and it plays (an audition, at the start of
// the next block). Option-click it and it empties.
//
// IT IS NOT A PARAMETER, and deliberately not a CControl: a file path is
// not a number no host can automate, so it travels to the processor as a
// message and is saved in the state stream by hand. The cell never sets
// its own path either - it hands the drop to the editor, which writes
// through the controller, which writes back to this cell AND to the same
// cell in any other editor open on this instance, so two windows cannot
// drift.
//
// WHAT IT SHOWS: the file's name; in red, why it will not play, when it
// will not; and a lamp - the well lights while the cell sounds, and
// FLASHES when struck, because a hi-hat hit is shorter than one redraw
// and would otherwise never be seen at all.
//
// No bitmaps, like every control in this folder's plug-ins: rectangles
// and text, so the panel is resolution-independent for free.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumSample.h"

#include "vstgui/vstgui.h"
#include "vstgui/lib/dragging.h"

#include <functional>
#include <string>

namespace SampleDrum {

//------------------------------------------------------------------------
class SampleDrumCellView : public VSTGUI::CView, public VSTGUI::DropTargetAdapter
{
public:
	SampleDrumCellView (const VSTGUI::CRect& size, int cell);

	int cell () const { return mCell; }

	/** What the cell holds; "" is empty. Called by the editor only. */
	void setPath (const std::string& path);
	const std::string& path () const { return mPath; }

	/** How the file read, as the processor reported it. */
	void setStatus (SampleStatus status);

	/** From the editor's timer: `lit` 0..1, the lamp's brightness. */
	void setLamp (float lit);

	bool playable () const { return !mPath.empty () && mStatus == SampleStatus::Loaded; }

	/** (cell, path) when a wav is dropped; (cell, "") on Option-click. */
	void setPathHandler (std::function<void (int, const std::string&)> handler);

	/** (cell) when a loaded cell is clicked. */
	void setAuditionHandler (std::function<void (int)> handler);

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;

	// Drag and drop. Returning `this` as the target is the SDK's own
	// pattern for a view that is its own target (CTabButton, ColorView),
	// safe because IDropTarget and CView share one virtual IReference.
	VSTGUI::SharedPointer<VSTGUI::IDropTarget> getDropTarget () override { return this; }
	VSTGUI::DragOperation onDragEnter (VSTGUI::DragEventData data) override;
	VSTGUI::DragOperation onDragMove (VSTGUI::DragEventData data) override;
	void onDragLeave (VSTGUI::DragEventData data) override;
	bool onDrop (VSTGUI::DragEventData data) override;

	CLASS_METHODS (SampleDrumCellView, VSTGUI::CView)

private:
	/** The first wav path in the package, or "". The first, not all: a
	    drag of five files onto one cell is a drop on ONE cell. */
	static std::string firstWavPath (VSTGUI::IDataPackage* package);

	void refreshTooltip ();

	int mCell = 0;
	std::string mPath;
	SampleStatus mStatus = SampleStatus::Empty;
	float mLamp = 0.f;
	bool mDragOver = false;

	std::function<void (int, const std::string&)> mPathHandler;
	std::function<void (int)> mAuditionHandler;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
