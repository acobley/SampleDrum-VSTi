//------------------------------------------------------------------------
// SampleDrum - the level bar under one cell
//
// LIFTED from Project6-VSTi/source/Project6SlotView.* (SpySlotLevel), with
// the MIDI-pad dimming taken out - every cell here holds audio. The class
// is renamed because it no longer lives beside the rest of Project6's
// slot controls; the drawing and the drag law are unchanged.
//
// A SlideSpin's bar with everything else taken off: no label, no value
// text, no lamp - just the bar, and the DXi's drag law, which is RELATIVE
// and one unit of a hundred per pixel (Shift for fine). Absolute
// positioning on a bar this size would make every setting a coarse one.
//
// It is a proper CControl with its own parameter tag, cellLevelParam
// (cell), so a host can automate a cell's level. While it is dragged the
// editor asks the cell above to show the value, because there is no room
// for a number here.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/vstgui.h"

namespace SampleDrum {

//------------------------------------------------------------------------
class SampleDrumLevelBar : public VSTGUI::CControl
{
public:
	SampleDrumLevelBar (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	                    int32_t tag, int cell);

	int cell () const { return mCell; }

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (SampleDrumLevelBar, VSTGUI::CControl)

private:
	int mCell = 0;
	bool mDragging = false;
	VSTGUI::CPoint mLastPoint;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
