//------------------------------------------------------------------------
// SampleDrum - the Polivoks panel's controls
//------------------------------------------------------------------------

#include "PvControls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace SampleDrum {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDragRange = 200.;   // px of vertical drag for the full sweep

/** Degrees from 12 o'clock, clockwise - the mock-up's pol(). */
CPoint pol (const CPoint& c, double r, double deg)
{
	const double t = deg * kPi / 180.;
	return CPoint (c.x + r * std::sin (t), c.y - r * std::cos (t));
}

CColor hex (uint32_t rgb, double a = 1.)
{
	return CColor (static_cast<uint8_t> (rgb >> 16), static_cast<uint8_t> ((rgb >> 8) & 0xff),
	               static_cast<uint8_t> (rgb & 0xff), static_cast<uint8_t> (255 * a));
}

void line (CDrawContext* c, const CPoint& a, const CPoint& b, const CColor& col, double w)
{
	c->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	c->setLineWidth (w);
	c->setFrameColor (col);
	c->drawLine (a, b);
}

void disc (CDrawContext* c, const CPoint& centre, double r, const CColor& fill)
{
	c->setFillColor (fill);
	c->drawEllipse (CRect (centre.x - r, centre.y - r, centre.x + r, centre.y + r), kDrawFilled);
}

void ring (CDrawContext* c, const CPoint& centre, double r, const CColor& col, double w)
{
	c->setLineStyle (kLineSolid);
	c->setLineWidth (w);
	c->setFrameColor (col);
	c->drawEllipse (CRect (centre.x - r, centre.y - r, centre.x + r, centre.y + r), kDrawStroked);
}

/** The mock-up's "capgrad": a black knob top lit from the upper left. */
void capFill (CDrawContext* c, CGraphicsPath* path, const CPoint& centre, double r)
{
	auto g = owned (CGradient::create ({ { 0., hex (0x3a3a3a) }, { 0.6, hex (0x161616) }, { 1., hex (0x050505) } }));
	if (g)
		c->fillRadialGradient (path, *g, CPoint (centre.x - 0.2 * r, centre.y - 0.3 * r), 1.4 * r);
}

void beginDraw (CDrawContext* c)
{
	c->setDrawMode (kAntiAliasing | kNonIntegralMode);
}

} // anonymous namespace

//========================================================================
// PvKnob
//========================================================================
PvKnob::PvKnob (const CPoint& centre, double outer, IControlListener* listener, int32_t tag)
: CControl (CRect (centre.x - outer, centre.y - outer, centre.x + outer, centre.y + outer),
            listener, tag, nullptr)
, mOuter (outer)
{
}

void PvKnob::draw (CDrawContext* context)
{
	beginDraw (context);
	const CRect r = getViewSize ();
	const CPoint c = r.getCenter ();
	const double body = mOuter - (mOuter >= 22. ? 6. : mOuter >= 14. ? 5. : 3.5);
	const int teeth = mOuter >= 14. ? 28 : 18;
	const double dip = mOuter >= 14. ? 1.4 : 0.9;

	// knurled skirt
	if (auto p = owned (context->createGraphicsPath ()))
	{
		for (int i = 0; i < teeth * 2; ++i)
		{
			const CPoint q = pol (c, (i % 2) ? body - dip : body, 360. * i / (teeth * 2));
			if (i == 0) p->beginSubpath (q); else p->addLine (q);
		}
		p->closeSubpath ();
		context->setFillColor (hex (0x0a0a0a));
		context->drawGraphicsPath (p, CDrawContext::kPathFilled);
		context->setLineStyle (kLineSolid);
		context->setLineWidth (0.6);
		context->setFrameColor (hex (0x2f2f2f));
		context->drawGraphicsPath (p, CDrawContext::kPathStroked);
	}
	// cap
	const double cr = body * 0.78;
	if (auto p = owned (context->createGraphicsPath ()))
	{
		p->addEllipse (CRect (c.x - cr, c.y - cr, c.x + cr, c.y + cr));
		capFill (context, p, c, cr);
		context->setLineWidth (0.8);
		context->setFrameColor (hex (0x000000));
		context->drawGraphicsPath (p, CDrawContext::kPathStroked);
	}
	// pointer
	const double ang = -135. + 270. * std::clamp (static_cast<double> (getValueNormalized ()), 0., 1.);
	line (context, pol (c, body * 0.15, ang), pol (c, body - 0.5, ang), Pv::kInk,
	      mOuter >= 22. ? 2.2 : mOuter >= 14. ? 1.8 : 1.4);
	setDirty (false);
}

void PvKnob::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;
	if (event.clickCount == 2 || event.modifiers.has (ModifierKey::Alt))
	{
		beginEdit ();
		setValueNormalized (getDefaultValue ());
		valueChanged ();
		endEdit ();
		invalid ();
		event.consumed = true;
		return;
	}
	mDragging = true;
	mStartValue = getValueNormalized ();
	mStartPoint = event.mousePosition;
	beginEdit ();
	event.consumed = true;
}

void PvKnob::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (!mDragging)
		return;
	const double range = event.modifiers.has (ModifierKey::Shift) ? kDragRange * 5. : kDragRange;
	const double dy = event.mousePosition.y - mStartPoint.y;
	setValueNormalized (static_cast<float> (std::clamp (mStartValue - dy / range, 0., 1.)));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

void PvKnob::onMouseUpEvent (MouseUpEvent& event)
{
	if (!mDragging)
		return;
	mDragging = false;
	endEdit ();
	event.consumed = true;
}

void PvKnob::onMouseCancelEvent (MouseCancelEvent& event)
{
	if (mDragging) { mDragging = false; endEdit (); }
	event.consumed = true;
}

void PvKnob::onMouseWheelEvent (MouseWheelEvent& event)
{
	const float step = event.modifiers.has (ModifierKey::Shift) ? 0.002f : 0.02f;
	beginEdit ();
	setValueNormalized (std::clamp (getValueNormalized () + static_cast<float> (event.deltaY) * step, 0.f, 1.f));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

void PvKnob::onMouseEnterEvent (MouseEnterEvent& event)
{
	if (mHover) mHover->controlHovered (this, true);
	event.consumed = true;
}

void PvKnob::onMouseExitEvent (MouseExitEvent& event)
{
	if (mHover) mHover->controlHovered (this, false);
	event.consumed = true;
}

//========================================================================
// PvLamp
//========================================================================
PvLamp::PvLamp (const CPoint& centre, IControlListener* listener, int32_t tag)
: CControl (CRect (centre.x - 9., centre.y - 9., centre.x + 9., centre.y + 9.), listener, tag, nullptr)
{
}

void PvLamp::draw (CDrawContext* context)
{
	beginDraw (context);
	const CPoint c = getViewSize ().getCenter ();
	const double r = 4.2;
	const bool on = getValueNormalized () >= 0.5f;
	disc (context, c, r + 1.6, hex (0x000000));
	ring (context, c, r + 1.6, hex (0x555555), 0.8);
	if (on)
		disc (context, c, r + 4., Pv::alpha (Pv::kLampOn, 0.22));
	disc (context, c, r, on ? Pv::kLampOn : Pv::kLampOff);
	disc (context, CPoint (c.x - r * 0.3, c.y - r * 0.3), r * 0.32, hex (0xffffff, on ? 0.55 : 0.22));
	setDirty (false);
}

void PvLamp::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft () || getListener () == nullptr)
		return;
	beginEdit ();
	setValueNormalized (getValueNormalized () >= 0.5f ? 0.f : 1.f);
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

void PvLamp::onMouseEnterEvent (MouseEnterEvent& event)
{
	if (mHover) mHover->controlHovered (this, true);
	event.consumed = true;
}

void PvLamp::onMouseExitEvent (MouseExitEvent& event)
{
	if (mHover) mHover->controlHovered (this, false);
	event.consumed = true;
}

//========================================================================
// PvText
//========================================================================
PvText::PvText (const CPoint& anchor, double size, bool bold, SciFi::Align align,
                const CColor& colour, double maxWidth)
: CView (CRect ()), mAnchor (anchor), mSize (size), mBold (bold), mAlign (align), mColour (colour)
{
	const double cap = size * 0.64;
	double l = anchor.x - 2., r = anchor.x + maxWidth;
	if (align == SciFi::Align::Centre) { l = anchor.x - maxWidth / 2.; r = anchor.x + maxWidth / 2.; }
	else if (align == SciFi::Align::Right) { l = anchor.x - maxWidth; r = anchor.x + 2.; }
	setViewSize (CRect (l, anchor.y - cap - 3., r, anchor.y + 3.));
	setMouseEnabled (false);
}

void PvText::setText (const std::string& text)
{
	if (text == mText)
		return;
	mText = text;
	invalid ();
}

void PvText::setColour (const CColor& colour)
{
	if (colour == mColour)
		return;
	mColour = colour;
	invalid ();
}

void PvText::draw (CDrawContext* context)
{
	SciFi::drawText (context, mText, mAnchor, mSize, mBold, mColour, mAlign);
	setDirty (false);
}

//========================================================================
// PvOled
//========================================================================
PvOled::PvOled (const CRect& size)
: CView (size)
{
	setMouseEnabled (false);
}

void PvOled::draw (CDrawContext* context)
{
	beginDraw (context);
	const CRect r = getViewSize ();
	context->saveGlobalState ();
	{
		ConcatClip clip (*context, r);
		drawContent (context);

		// the glass: a faint sheen across the top, and a hairline highlight
		if (auto p = owned (context->createGraphicsPath ()))
		{
			p->addRect (r);
			auto g = owned (CGradient::create ({ { 0., hex (0xffffff, 0.07) }, { 0.45, hex (0xffffff, 0.015) },
			                                     { 0.46, hex (0xffffff, 0.) }, { 1., hex (0xffffff, 0.) } }));
			if (g)
				context->fillLinearGradient (p, *g, r.getTopLeft (),
				                             CPoint (r.left + 0.35 * r.getWidth (), r.bottom));
		}
		line (context, CPoint (r.left + 3., r.top + 0.8), CPoint (r.right - 3., r.top + 0.8),
		      hex (0xffffff, 0.12), 0.6);
	}
	context->restoreGlobalState ();
	setDirty (false);
}

void PvOled::trace (CDrawContext* context, const std::vector<CPoint>& pts, const CColor& colour, double width)
{
	if (pts.size () < 2)
		return;
	auto p = owned (context->createGraphicsPath ());
	if (!p)
		return;
	p->beginSubpath (pts[0]);
	for (size_t i = 1; i < pts.size (); ++i)
		p->addLine (pts[i]);
	context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->setLineWidth (width * 2.6);
	context->setFrameColor (Pv::alpha (colour, 0.22));
	context->drawGraphicsPath (p, CDrawContext::kPathStroked);
	context->setLineWidth (width);
	context->setFrameColor (colour);
	context->drawGraphicsPath (p, CDrawContext::kPathStroked);
}

void PvOled::fillUnder (CDrawContext* context, const std::vector<CPoint>& pts, double base, double top,
                        const CColor& colour)
{
	if (pts.size () < 2)
		return;
	auto p = owned (context->createGraphicsPath ());
	if (!p)
		return;
	p->beginSubpath (pts[0]);
	for (size_t i = 1; i < pts.size (); ++i)
		p->addLine (pts[i]);
	p->addLine (CPoint (pts.back ().x, base));
	p->addLine (CPoint (pts.front ().x, base));
	p->closeSubpath ();
	auto g = owned (CGradient::create (0., 1., Pv::alpha (colour, 0.38), Pv::alpha (colour, 0.02)));
	if (g)
		context->fillLinearGradient (p, *g, CPoint (0., top), CPoint (0., base));
}

void PvOled::oledText (CDrawContext* context, const std::string& text, double x, double y,
                       const CColor& colour, SciFi::Align align, double size)
{
	SciFi::drawText (context, text, CPoint (x, y), size, false, colour, align);
}
//========================================================================
// PvVoiceMeter
//========================================================================
void PvVoiceMeter::setVoices (int count)
{
	if (count == mVoices)
		return;
	mVoices = count;
	invalid ();
}

void PvVoiceMeter::drawContent (CDrawContext* context)
{
	// pv.js header(): `segs` segments across 196 px, 0.64 of each lit.
	const CRect r = getViewSize ();
	const double pitch = 196. / mSegments, w = pitch * 0.64;
	for (int i = 0; i < mSegments; ++i)
	{
		const CRect seg (r.left + 6. + i * pitch, r.top + 5., r.left + 6. + i * pitch + w, r.top + 13.);
		const bool on = i < mVoices;
		if (on)
		{
			context->setFillColor (Pv::alpha (mOn, 0.25));
			context->drawRect (CRect (seg).extend (1.5, 2.), kDrawFilled);
		}
		context->setFillColor (on ? mOn : mOff);
		context->drawRect (seg, kDrawFilled);
	}
}

//========================================================================
// PvReadout
//========================================================================
PvReadout::PvReadout (const CRect& size)
: CView (size)
{
	setMouseEnabled (false);
}

void PvReadout::setText (const std::string& text)
{
	if (text == mText)
		return;
	mText = text;
	invalid ();
}

void PvReadout::draw (CDrawContext* context)
{
	if (mText.empty ())
		return;
	beginDraw (context);
	const CRect v = getViewSize ();
	const double w = SciFi::textWidth (mText, 9., true) + 16.;
	double cx = mAnchorX >= 0. ? mAnchorX : v.getCenter ().x;
	cx = std::clamp (cx, v.left + w / 2. + 1., v.right - w / 2. - 1.);
	const CRect plate (cx - w / 2., v.top, cx + w / 2., v.bottom);
	context->setFillColor (hex (0x000000));
	context->drawRect (plate, kDrawFilled);
	context->setLineStyle (kLineSolid);
	context->setLineWidth (1.);
	context->setFrameColor (Pv::kStroke);
	context->drawRect (plate, kDrawStroked);
	SciFi::drawText (context, mText, CPoint (cx, v.top + v.getHeight () / 2. + 3.), 9., true,
	                 Pv::kInk, SciFi::Align::Centre);
	setDirty (false);
}

//========================================================================
// PvOverlay
//========================================================================
PvOverlay::PvOverlay (const CRect& size, CBitmap* bitmap)
: CView (size), mBitmap (bitmap)
{
	setMouseEnabled (false);
}

void PvOverlay::draw (CDrawContext* context)
{
	if (mBitmap)
		context->drawBitmap (mBitmap, getViewSize ());
	setDirty (false);
}

//------------------------------------------------------------------------
} // namespace SampleDrum
