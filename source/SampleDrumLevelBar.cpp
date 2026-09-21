//------------------------------------------------------------------------
// SampleDrum - the level bar under one cell, implementation
//
// Lifted from Project6's SpySlotLevel; see the header.
//------------------------------------------------------------------------

#include "SampleDrumLevelBar.h"

#include "SampleDrumControls.h"

#include <algorithm>
#include <cmath>

using namespace VSTGUI;

namespace SampleDrum {

namespace {

/** The DXi's own slider law: one unit of a 0..100 range per pixel of
    horizontal movement, in either direction. */
constexpr float kUnitsPerPixel = 0.01f;

/** The raised edge SpySlider draws round its bar - light top-left, lighter
    bottom-right - in the same colours. Local, so SampleDrumControls.cpp
    stays a straight namespace-renamed copy. */
void drawEdge (CDrawContext* context, const CRect& r)
{
	context->setLineWidth (1.);
	context->setFrameColor (Colours::kBarLight);
	context->drawLine (CPoint (r.left, r.bottom - 1), CPoint (r.left, r.top));
	context->drawLine (CPoint (r.left, r.top), CPoint (r.right - 1, r.top));
	context->setFrameColor (Colours::kBarHigh);
	context->drawLine (CPoint (r.right - 1, r.top + 1), CPoint (r.right - 1, r.bottom - 1));
	context->drawLine (CPoint (r.right - 1, r.bottom - 1), CPoint (r.left + 1, r.bottom - 1));
}

} // namespace

//------------------------------------------------------------------------
SampleDrumLevelBar::SampleDrumLevelBar (const CRect& size, IControlListener* listener,
                                        int32_t tag, int cell)
: CControl (size, listener, tag)
, mCell (cell)
{
	setMouseEnabled (true);
	setMin (0.f);
	setMax (1.f);
	setTooltipText ("Level - drag left and right, Shift for fine");
}

//------------------------------------------------------------------------
void SampleDrumLevelBar::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	// The ground, then the fill to the current value, then the edge - the
	// order SpySlider draws its bar in, and the same colours.
	context->setFillColor (Colours::kLampOff);
	context->drawRect (r, kDrawFilled);

	CRect fill (r);
	fill.inset (1., 1.);
	fill.right = fill.left
	             + fill.getWidth () * std::clamp (static_cast<double> (getValueNormalized ()), 0.0, 1.0);
	if (fill.getWidth () > 0. && fill.getHeight () > 0.)
	{
		context->setFillColor (Colours::kBarFill);
		context->drawRect (fill, kDrawFilled);
	}

	drawEdge (context, r);

	setDirty (false);
}

//------------------------------------------------------------------------
void SampleDrumLevelBar::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;

	// NO ABSOLUTE POSITIONING: the value moves by increments from wherever
	// it was.
	mDragging = true;
	mLastPoint = event.mousePosition;
	beginEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SampleDrumLevelBar::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (!mDragging)
		return;

	const CCoord dx = event.mousePosition.x - mLastPoint.x;
	if (std::fabs (dx) < 1.)
		return;

	mLastPoint = event.mousePosition;

	const float scale = event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f;
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (dx) * kUnitsPerPixel * scale, 0.f, 1.f));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SampleDrumLevelBar::onMouseUpEvent (MouseUpEvent& event)
{
	if (!mDragging)
		return;

	mDragging = false;
	endEdit ();
	event.consumed = true;
}

//------------------------------------------------------------------------
void SampleDrumLevelBar::onMouseCancelEvent (MouseCancelEvent& /*event*/)
{
	// A cancelled drag still has to end its edit, or the host is left
	// holding a gesture open for ever.
	if (!mDragging)
		return;

	mDragging = false;
	endEdit ();
}

//------------------------------------------------------------------------
void SampleDrumLevelBar::onMouseWheelEvent (MouseWheelEvent& event)
{
	const float step = event.modifiers.has (ModifierKey::Shift) ? 0.002f : 0.01f;
	beginEdit ();
	setValueNormalized (std::clamp (
		getValueNormalized () + static_cast<float> (event.deltaY) * step, 0.f, 1.f));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
