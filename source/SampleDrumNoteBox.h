//------------------------------------------------------------------------
// SampleDrum - the note box at the top of each column
//
//     [ <   36 C1   > ]
//
// Which MIDI note plays this column. A proper CControl on the column's
// note parameter, columnNoteParam (column), so the host saves it and
// shows it; the value is the parameter's normalised value, and the note
// is ALWAYS read through noteFromNormalized - the rounding the processor
// uses - so the box can never show a note the DSP is not answering to.
//
// Three ways to change it, because a note is picked from 128 and nobody
// wants to click 90 times:
//
//   * click the < or > end          one semitone   (Shift: an octave)
//   * drag up or down               one semitone per kPixelsPerNote
//   * the scroll wheel              one semitone
//
// and Option-click puts it back to the column's GM default.
//
// SHARED NOTES ARE SHOWN, not refused: two columns on one note both play
// (layering), which is sometimes the point and sometimes a mistake. The
// box's number turns red so the mistake is visible at a glance.
//
// Drawn in the Polivoks style (amber lettering behind glass); the box's
// bezel is printed on the panel bitmap.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/vstgui.h"

namespace SampleDrum {

//------------------------------------------------------------------------
class SampleDrumNoteBox : public VSTGUI::CControl
{
public:
	SampleDrumNoteBox (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
	                   int32_t tag, int column);

	int column () const { return mColumn; }

	/** The note currently shown - noteFromNormalized of the value. */
	int note () const;

	/** Another column is on the same note: draw the number red. */
	void setShared (bool shared);

	void draw (VSTGUI::CDrawContext* context) override;

	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	/** Pixels of vertical travel per semitone when dragging. 4 puts the
	    whole range in about 500 pixels - fine enough to land on a note,
	    fast enough to cross an octave in a flick. */
	static constexpr double kPixelsPerNote = 4.;

	CLASS_METHODS (SampleDrumNoteBox, VSTGUI::CControl)

private:
	/** Set the note, as ONE edit gesture if none is open. */
	void stepTo (int note, bool inGesture);
	void refreshTooltip ();

	int mColumn = 0;
	bool mShared = false;

	bool mDragging = false;
	bool mMoved = false;
	double mDragStartY = 0.;
	int mDragStartNote = 0;
	VSTGUI::CPoint mPressPoint;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
