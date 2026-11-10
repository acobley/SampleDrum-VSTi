//------------------------------------------------------------------------
// SampleDrum - one cell of the 8 x 4 grid
//
// Drag a .wav onto it from the Finder and it takes the file - the same
// drop handling as Project6's SpySampleSlot, cut down to what a cell here
// does. Click a loaded cell and it plays (an audition, at the start of
// the next block). DRAG IT ONTO ANOTHER CELL to move it there - a swap -
// or hold Option or Control at the drop to copy it. RIGHT-CLICK (or
// Control-click) for "Clear cell"; Option-click also empties it.
//
// THE AUDITION STAYS ON THE PRESS, unlike Project6, which moved its pad
// launch to the release when pads became draggable: a launched LOOP that
// was only being picked up would keep running. A one-shot heard as a cell
// is picked up is harmless - arguably useful - and a drum that sounds on
// release feels late. OPTION-CLICK EMPTIES ON RELEASE, not on press, so
// that pressing Option and dragging is a COPY, not a cell emptied.
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
// THE POLIVOKS LOOK (since the Polivoks panel): an amber OLED with the
// file's waveform, its name in the 70s sci-fi lettering, an amber border
// while it sounds, a playhead across the waveform, and - when empty - which
// row a hit here plays instead. Paths and lettering only; the screen's
// bezel is on the panel bitmap behind it.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumSample.h"

#include "vstgui/vstgui.h"
#include "vstgui/lib/dragging.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

	/** Show this INSTEAD of the name until set back to "". It exists for
	    the level bar underneath, which has no room for a number: while
	    the bar is dragged the cell reads out the level - Project6's trick,
	    using space that is already there. */
	void setOverlay (const std::string& text);

	/** From the editor's timer: `lit` 0..1, the lamp's brightness. */
	void setLamp (float lit);

	/** The waveform (makeThumbnail), from the processor via the controller. */
	void setThumbnail (const std::vector<std::int8_t>& thumb);

	/** An EMPTY cell: the row a hit at this velocity plays instead
	    (chooseCell), or -1 when the column is empty. */
	void setFallback (int row);

	/** Where the last hit has got to, 0..1 across the waveform; negative
	    hides it. Only drawn while the cell is lit. */
	void setPlayhead (double position);

	/** How far the view reaches past its screen on every side, for the
	    lit border (d2p.js: the screen's rect extended by 3, stroke 1.6). */
	static constexpr VSTGUI::CCoord kGlowMargin = 4.;

	bool playable () const { return !mPath.empty () && mStatus == SampleStatus::Loaded; }

	/** (cell, path) when a wav is dropped; (cell, "") on Option-click. */
	void setPathHandler (std::function<void (int, const std::string&)> handler);

	/** (cell) when a loaded cell is clicked. */
	void setAuditionHandler (std::function<void (int)> handler);

	/** (from, to, copy) when a cell of THIS INSTANCE is dropped here. */
	void setMoveHandler (std::function<void (int, int, bool)> handler);

	/** (cell) when this cell is to be EMPTIED - from the right-click
	    menu or an Option-click. Like a drop, the cell does not empty
	    itself: the change goes through the controller and comes back. */
	void setClearHandler (std::function<void (int)> handler);

	/** This instance's id, stamped on every drag that starts here and
	    compared on every drop - see encodeCellDrag. */
	void setOwner (std::uint64_t owner) { mOwner = owner; }

	void draw (VSTGUI::CDrawContext* context) override;

	/** How far the pointer may travel before a press becomes a drag:
	    Project6's four pixels. */
	static constexpr VSTGUI::CCoord kDragThreshold = 4.;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;

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

	/** Is this drag a cell rather than a file? Fills the rest if so. */
	static bool cellDrag (VSTGUI::IDataPackage* package, std::uint64_t& owner, int& from,
	                      std::string& path);

	/** Held at the drop, it copies instead of moving. Control as well as
	    Option: macOS turns a Control-CLICK into a right-click, but a
	    Control held during a drag arrives as Control. Project6's rule. */
	static bool copyRequested (const VSTGUI::Modifiers& modifiers);

	/** The right-click menu: one entry, "Clear cell (name)" - the name on
	    it so what is about to go is in front of the person choosing. */
	void showCellMenu (const VSTGUI::CPoint& where);

	/** Put this cell on the pasteboard, once a press has become a drag. */
	void beginCellDrag ();

	void refreshTooltip ();

	int mCell = 0;
	std::string mPath;
	std::string mOverlay;
	SampleStatus mStatus = SampleStatus::Empty;
	float mLamp = 0.f;
	std::vector<std::int8_t> mThumb;
	int mFallbackRow = -1;
	double mPlayhead = -1.;
	bool mDragOver = false;
	/** The operation the current drag-over will be: a file from outside
	    and a cell from another instance are always Copy. */
	bool mDragIsOwnCell = false;

	/** A press that has not yet decided whether it is a click or a drag. */
	bool mPressed = false;
	bool mDragStarted = false;
	/** Option was down at the press: a release without a drag empties. */
	bool mClearOnRelease = false;
	VSTGUI::CPoint mPressPoint;

	std::uint64_t mOwner = 0;

	std::function<void (int, const std::string&)> mPathHandler;
	std::function<void (int)> mAuditionHandler;
	std::function<void (int, int, bool)> mMoveHandler;
	std::function<void (int)> mClearHandler;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
