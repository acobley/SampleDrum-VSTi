//------------------------------------------------------------------------
// SampleDrum - the note box at the top of each column, implementation
//------------------------------------------------------------------------

#include "SampleDrumNoteBox.h"

#include "SampleDrumControls.h"
#include "SampleDrumKit.h"
#include "SampleDrumParams.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace VSTGUI;

namespace SampleDrum {

namespace {

const CColor kBoxFill   ( 30,  30,  30, 255);
const CColor kBoxEdge   (100, 100, 100, 255);
const CColor kArrow     (150, 150, 150, 255);
const CColor kShared    (255, 190,  60, 255);   // Project6's "pending" amber

/** How wide each clickable end is, as a fraction of the box. */
constexpr CCoord kEndFraction = 0.25;

} // namespace

//------------------------------------------------------------------------
SampleDrumNoteBox::SampleDrumNoteBox (const CRect& size, IControlListener* listener,
                                      int32_t tag, int column)
: CControl (size, listener, tag)
, mColumn (column)
{
	setMouseEnabled (true);
	setMin (0.f);
	setMax (1.f);
	refreshTooltip ();
}

//------------------------------------------------------------------------
int SampleDrumNoteBox::note () const
{
	return noteFromNormalized (getValueNormalized ());
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::setShared (bool shared)
{
	if (shared == mShared)
		return;
	mShared = shared;
	refreshTooltip ();
	invalid ();
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::refreshTooltip ()
{
	std::string text = "The MIDI note this column plays on.\n"
	                   "Click < or > to step (Shift: an octave), drag up/down, or scroll.\n"
	                   "Option-click: back to the GM default.";
	if (mShared)
		text += "\nAnother column is on this note too - one hit plays both.";
	setTooltipText (text.c_str ());
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	context->setFillColor (kBoxFill);
	context->drawRect (r, kDrawFilled);
	context->setLineWidth (1.);
	context->setFrameColor (kBoxEdge);
	context->drawRect (r, kDrawStroked);

	const CCoord endWidth = r.getWidth () * kEndFraction;
	context->setFont (panelFont ());

	context->setFontColor (kArrow);
	context->drawString ("<", CRect (r.left, r.top, r.left + endWidth, r.bottom), kCenterText, true);
	context->drawString (">", CRect (r.right - endWidth, r.top, r.right, r.bottom), kCenterText, true);

	// THE SHARED FORMATTING - the same text the host shows for this
	// parameter.
	char text[16] = {};
	formatNote (note (), text, static_cast<int> (sizeof (text)));
	context->setFontColor (mShared ? kShared : Colours::kValue);
	context->drawString (text, CRect (r.left + endWidth, r.top, r.right - endWidth, r.bottom),
	                     kCenterText, true);

	setDirty (false);
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::stepTo (int target, bool inGesture)
{
	target = std::min (kMaxNote, std::max (kMinNote, target));
	if (target == note ())
		return;

	if (!inGesture)
		beginEdit ();
	setValueNormalized (static_cast<float> (normalizedFromNote (target)));
	valueChanged ();
	if (!inGesture)
		endEdit ();
	invalid ();
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;

	event.consumed = true;

	if (event.modifiers.has (ModifierKey::Alt))
	{
		stepTo (kDrumMap[std::min (kColumns - 1, std::max (0, mColumn))].note, false);
		return;
	}

	// A PRESS IS A CLICK OR A DRAG, and which is decided by whether it
	// moves: the edit gesture opens now either way, so a drag is one undo
	// step in the host however far it travels.
	mDragging = true;
	mMoved = false;
	mPressPoint = event.mousePosition;
	mDragStartY = event.mousePosition.y;
	mDragStartNote = note ();
	beginEdit ();
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (!mDragging)
		return;

	const double dy = mDragStartY - event.mousePosition.y;   // up is higher
	if (!mMoved && std::fabs (dy) < kPixelsPerNote)
		return;

	mMoved = true;
	stepTo (mDragStartNote + static_cast<int> (dy / kPixelsPerNote), true);
	event.consumed = true;
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::onMouseUpEvent (MouseUpEvent& event)
{
	if (!mDragging)
		return;

	mDragging = false;

	// NOT MOVED: it was a click, and which end it landed on says which way.
	// The middle does nothing - it is where the number is read.
	if (!mMoved)
	{
		const CRect r = getViewSize ();
		const CCoord endWidth = r.getWidth () * kEndFraction;
		const int step = event.modifiers.has (ModifierKey::Shift) ? 12 : 1;

		if (mPressPoint.x < r.left + endWidth)
			stepTo (note () - step, true);
		else if (mPressPoint.x > r.right - endWidth)
			stepTo (note () + step, true);
	}

	endEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::onMouseCancelEvent (MouseCancelEvent& /*event*/)
{
	// A cancelled press still closes its gesture, or the host is left
	// holding an edit open for ever.
	if (!mDragging)
		return;
	mDragging = false;
	endEdit ();
}

//------------------------------------------------------------------------
void SampleDrumNoteBox::onMouseWheelEvent (MouseWheelEvent& event)
{
	if (event.deltaY == 0.)
		return;
	stepTo (note () + (event.deltaY > 0. ? 1 : -1), false);
	event.consumed = true;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
