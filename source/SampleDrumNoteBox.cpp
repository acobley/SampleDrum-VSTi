//------------------------------------------------------------------------
// SampleDrum - the note box at the top of each column, implementation
//------------------------------------------------------------------------

#include "SampleDrumNoteBox.h"

#include "SampleDrumPvViews.h"
#include "SampleDrumKit.h"
#include "SampleDrumParams.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace VSTGUI;

namespace SampleDrum {

namespace {

// THE POLIVOKS NOTE BOX (tools/polivoks/d2p.js): the bezel is on the panel
// bitmap; this draws "< 36 C1 >" in amber lettering and the glass. Every
// screen is amber in this design, so a SHARED note - the warning - is red.

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
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	const CRect r = getViewSize ();
	const double baseline = r.top + r.getHeight () / 2. + 3.2;

	SciFi::drawText (context, "\xE2\x80\xB9", CPoint (r.left + 7., baseline), 9., false, Pv::kOledText,
	                 SciFi::Align::Left);
	SciFi::drawText (context, "\xE2\x80\xBA", CPoint (r.right - 7., baseline), 9., false, Pv::kOledText,
	                 SciFi::Align::Right);

	// THE SHARED FORMATTING - the same text the host shows for this
	// parameter.
	char text[16] = {};
	formatNote (note (), text, static_cast<int> (sizeof (text)));
	SciFi::drawText (context, text, CPoint (r.getCenter ().x, baseline), 9., false,
	                 mShared ? Pv::kRed : Pv::kScreen, SciFi::Align::Centre);

	Pv::drawGlass (context, r);
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
