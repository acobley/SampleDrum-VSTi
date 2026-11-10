//------------------------------------------------------------------------
// SampleDrum - the Polivoks panel's controls
//
// COPIED FROM ForTran-VSTi/source/PvControls.*, the classes SampleDrum uses
// and nothing else changed but the namespace, IHoverListener moving in here,
// the voice meter taking a segment count and colours, and a lamp with no
// listener being an indicator. Keep it diffable against ForTran's.
//
// The panel itself - boxes, titles, legends, printed knob scales, the OLED
// bezels - is one bitmap, resource/pv_panel.png, rendered at 2x from the
// mock-up (tools/polivoks). Everything that MOVES is drawn here, with paths
// and the 70s sci-fi lettering, at the coordinates the mock-up used:
//
//   PvKnob      black knurled knob, white pointer (the scale is printed)
//   PvLamp      red jewel lamp, a toggle
//   PvText      a line of lettering, set by the editor
//   PvOled      a screen's picture, ending with the glass (SampleDrum's
//               own screens are in SampleDrumPvViews.h)
//   PvReadout   the hover readout, as a lettered plate
//   PvOverlay   the wear (resource/pv_wear.png), on top of everything and
//               invisible to the mouse
//------------------------------------------------------------------------

#pragma once

#include "SciFiLettering.h"

#include "vstgui/vstgui.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace SampleDrum {

/** Told when the pointer enters or leaves a control (ForTranControls.h). */
struct IHoverListener
{
	virtual ~IHoverListener () = default;
	virtual void controlHovered (VSTGUI::CControl* control, bool entered) = 0;
};

namespace Pv {

// The mock-up's palette (Designs/source/polivoks/pv.js, T and OLED).
inline const VSTGUI::CColor kInk      { 0xf4, 0xf1, 0xe8, 0xff };
inline const VSTGUI::CColor kMuted    { 0xb9, 0xb6, 0xae, 0xff };
inline const VSTGUI::CColor kDimLabel { 0x86, 0x83, 0x7c, 0xff };
inline const VSTGUI::CColor kTick     { 0xe9, 0xe6, 0xdc, 0xff };
inline const VSTGUI::CColor kRed      { 0xe2, 0x34, 0x2b, 0xff };
inline const VSTGUI::CColor kLampOn   { 0xff, 0x3b, 0x2f, 0xff };
inline const VSTGUI::CColor kLampOff  { 0x3a, 0x15, 0x12, 0xff };
inline const VSTGUI::CColor kStroke   { 0xcf, 0xcc, 0xc4, 0xff };
inline const VSTGUI::CColor kPanel    { 0x12, 0x12, 0x12, 0xff };
inline const VSTGUI::CColor kOledText { 0x8d, 0x96, 0xa3, 0xff };
inline const VSTGUI::CColor kAmber    { 0xff, 0xae, 0x2e, 0xff };
inline const VSTGUI::CColor kBlue     { 0x2f, 0xb8, 0xff, 0xff };
inline const VSTGUI::CColor kMint     { 0x38, 0xf0, 0xa0, 0xff };
inline const VSTGUI::CColor kViolet   { 0xc2, 0x7b, 0xff, 0xff };

inline VSTGUI::CColor alpha (VSTGUI::CColor c, double a)
{
	c.alpha = static_cast<uint8_t> (c.alpha * a);
	return c;
}

} // namespace Pv

//------------------------------------------------------------------------
/** Knurled knob. `outer` is the radius to the end of the printed scale, as
    in the mock-up; the knob body is a little inside it. The view's rect is
    the square around the scale. Drag vertically (shift for fine), wheel,
    and alt-click (or double-click) returns to the default. */
class PvKnob : public VSTGUI::CControl
{
public:
	PvKnob (const VSTGUI::CPoint& centre, double outer,
	        VSTGUI::IControlListener* listener, int32_t tag);

	void setHoverListener (IHoverListener* l) { mHover = l; }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;
	void onMouseEnterEvent (VSTGUI::MouseEnterEvent& event) override;
	void onMouseExitEvent (VSTGUI::MouseExitEvent& event) override;

	CLASS_METHODS (PvKnob, VSTGUI::CControl)

private:
	double mOuter;
	IHoverListener* mHover = nullptr;
	bool mDragging = false;
	float mStartValue = 0.f;
	VSTGUI::CPoint mStartPoint;
};

//------------------------------------------------------------------------
/** A red jewel lamp that is also the switch: click to toggle. With no
    listener it is an indicator only, and ignores the mouse. */
class PvLamp : public VSTGUI::CControl
{
public:
	PvLamp (const VSTGUI::CPoint& centre, VSTGUI::IControlListener* listener, int32_t tag);

	void setHoverListener (IHoverListener* l) { mHover = l; }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseEnterEvent (VSTGUI::MouseEnterEvent& event) override;
	void onMouseExitEvent (VSTGUI::MouseExitEvent& event) override;

	CLASS_METHODS (PvLamp, VSTGUI::CControl)

private:
	IHoverListener* mHover = nullptr;
};

//------------------------------------------------------------------------
/** One line of lettering at a fixed baseline. Mouse-transparent. The view
    is sized generously around the anchor; setText only redraws if the text
    or colour really changed. */
class PvText : public VSTGUI::CView
{
public:
	PvText (const VSTGUI::CPoint& anchor, double size, bool bold,
	        SciFi::Align align, const VSTGUI::CColor& colour, double maxWidth = 140.);

	void setText (const std::string& text);
	void setColour (const VSTGUI::CColor& colour);

	void draw (VSTGUI::CDrawContext* context) override;

	CLASS_METHODS (PvText, VSTGUI::CView)

private:
	VSTGUI::CPoint mAnchor;   // frame coordinates
	double mSize;
	bool mBold;
	SciFi::Align mAlign;
	VSTGUI::CColor mColour;
	std::string mText;
};

//------------------------------------------------------------------------
/** An OLED behind glass. The bezel, the black and the dot grid are on the
    panel bitmap; this draws the picture and then the glass over it. */
class PvOled : public VSTGUI::CView
{
public:
	explicit PvOled (const VSTGUI::CRect& size);

	void draw (VSTGUI::CDrawContext* context) override;

protected:
	/** Draw the picture, in frame coordinates. */
	virtual void drawContent (VSTGUI::CDrawContext* context) = 0;

	/** A trace with a soft bloom under it, as the mock-up's filter does. */
	static void trace (VSTGUI::CDrawContext* context, const std::vector<VSTGUI::CPoint>& pts,
	                   const VSTGUI::CColor& colour, double width = 1.6);
	/** Fill under a trace down to `base`, fading as the mock-up's does. */
	static void fillUnder (VSTGUI::CDrawContext* context, const std::vector<VSTGUI::CPoint>& pts,
	                       double base, double top, const VSTGUI::CColor& colour);
	static void oledText (VSTGUI::CDrawContext* context, const std::string& text,
	                      double x, double y, const VSTGUI::CColor& colour,
	                      SciFi::Align align = SciFi::Align::Left, double size = 9.);
};

//------------------------------------------------------------------------
/** The header's voice meter: `segments` segments, the sounding voices lit. */
class PvVoiceMeter : public PvOled
{
public:
	PvVoiceMeter (const VSTGUI::CRect& size, int segments, const VSTGUI::CColor& on, const VSTGUI::CColor& off)
	: PvOled (size), mSegments (segments < 1 ? 1 : segments), mOn (on), mOff (off) {}
	void setVoices (int count);
	CLASS_METHODS (PvVoiceMeter, VSTGUI::CView)
protected:
	void drawContent (VSTGUI::CDrawContext* context) override;
private:
	int mSegments;
	VSTGUI::CColor mOn, mOff;
	int mVoices = 0;
};

//------------------------------------------------------------------------
/** The hover readout: a black plate with a hairline border and lettering,
    centred on an x and kept inside the window. */
class PvReadout : public VSTGUI::CView
{
public:
	explicit PvReadout (const VSTGUI::CRect& size);
	void setText (const std::string& text);
	void setAnchorX (VSTGUI::CCoord x) { mAnchorX = x; invalid (); }
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (PvReadout, VSTGUI::CView)
private:
	std::string mText;
	VSTGUI::CCoord mAnchorX = -1.;
};

//------------------------------------------------------------------------
/** A full-window bitmap drawn over everything, that the mouse passes
    straight through. Carries the wear. */
class PvOverlay : public VSTGUI::CView
{
public:
	PvOverlay (const VSTGUI::CRect& size, VSTGUI::CBitmap* bitmap);
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (PvOverlay, VSTGUI::CView)
private:
	VSTGUI::SharedPointer<VSTGUI::CBitmap> mBitmap;
};

//------------------------------------------------------------------------
} // namespace SampleDrum
