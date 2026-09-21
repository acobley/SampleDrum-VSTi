//------------------------------------------------------------------------
// SampleDrum - one cell of the grid, implementation
//------------------------------------------------------------------------

#include "SampleDrumCellView.h"

#include "SampleDrumControls.h"
#include "SampleDrumKit.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace VSTGUI;

namespace SampleDrum {

namespace {

// The well, from Project6's SpySampleSlot palette so the two plug-ins
// read as a family.
const CColor kWellEmpty   ( 40,  40,  40, 255);
const CColor kWellLoaded  ( 58,  58,  58, 255);
const CColor kWellLit     (190,  60,  50, 255);   // lamp full on
const CColor kWellShadow  (100, 100, 100, 255);
const CColor kWellHigh    (200, 200, 200, 255);
const CColor kText        (255, 255, 255, 255);
const CColor kTextBad     (208, 132, 132, 255);
const CColor kTextHint    (110, 110, 110, 255);
const CColor kDropAccept  ( 50, 255,  50, 255);

CColor mix (const CColor& a, const CColor& b, float t)
{
	t = std::min (1.f, std::max (0.f, t));
	auto channel = [t] (uint8_t x, uint8_t y)
	{
		return static_cast<uint8_t> (x + (y - x) * t + 0.5f);
	};
	return CColor (channel (a.red, b.red), channel (a.green, b.green),
	               channel (a.blue, b.blue), 255);
}

/** A RECESSED well: dark top-left, light bottom-right - the reverse of a
    raised bar. Local, as in Project6, so SampleDrumControls.cpp stays a
    straight namespace-renamed copy. */
void drawWellEdge (CDrawContext* context, const CRect& r)
{
	context->setLineWidth (1.);
	context->setFrameColor (kWellShadow);
	context->drawLine (CPoint (r.left, r.bottom - 1), CPoint (r.left, r.top));
	context->drawLine (CPoint (r.left, r.top), CPoint (r.right - 1, r.top));
	context->setFrameColor (kWellHigh);
	context->drawLine (CPoint (r.right - 1, r.top + 1), CPoint (r.right - 1, r.bottom - 1));
	context->drawLine (CPoint (r.right - 1, r.bottom - 1), CPoint (r.left + 1, r.bottom - 1));
}

/** Shorten `text` from the end until it fits `width`. */
std::string fitted (CDrawContext* context, std::string text, CCoord width)
{
	if (context->getStringWidth (text.c_str ()) <= width)
		return text;
	while (!text.empty () && context->getStringWidth ((text + "...").c_str ()) > width)
		text.pop_back ();
	return text + "...";
}

} // namespace

//------------------------------------------------------------------------
SampleDrumCellView::SampleDrumCellView (const CRect& size, int cell)
: CView (size)
, mCell (cell)
{
	refreshTooltip ();
}

//------------------------------------------------------------------------
void SampleDrumCellView::setPath (const std::string& path)
{
	if (mPath == path)
		return;
	mPath = path;
	refreshTooltip ();
	invalid ();
}

//------------------------------------------------------------------------
void SampleDrumCellView::setStatus (SampleStatus status)
{
	if (mStatus == status)
		return;
	mStatus = status;
	refreshTooltip ();
	invalid ();
}

//------------------------------------------------------------------------
void SampleDrumCellView::setLamp (float lit)
{
	// Quantised, so a lamp decaying through tiny steps does not redraw
	// the cell thirty times a second for changes nobody can see.
	lit = std::floor (std::min (1.f, std::max (0.f, lit)) * 16.f) / 16.f;
	if (lit == mLamp)
		return;
	mLamp = lit;
	invalid ();
}

//------------------------------------------------------------------------
void SampleDrumCellView::setPathHandler (std::function<void (int, const std::string&)> handler)
{
	mPathHandler = std::move (handler);
}

//------------------------------------------------------------------------
void SampleDrumCellView::setAuditionHandler (std::function<void (int)> handler)
{
	mAuditionHandler = std::move (handler);
}

//------------------------------------------------------------------------
void SampleDrumCellView::refreshTooltip ()
{
	std::string text;
	if (mPath.empty ())
		text = "Drop a .wav file here";
	else
	{
		text = mPath;
		if (mStatus != SampleStatus::Loaded)
			text += std::string ("\nWill not play: ") + sampleStatusText (mStatus);
		text += "\nClick to play, Option-click to empty";
	}
	setTooltipText (text.c_str ());
}

//------------------------------------------------------------------------
void SampleDrumCellView::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();

	const CColor resting = mPath.empty () ? kWellEmpty : kWellLoaded;
	context->setFillColor (mix (resting, kWellLit, mLamp));
	context->drawRect (r, kDrawFilled);
	drawWellEdge (context, r);

	if (mDragOver)
	{
		CRect highlight = r;
		highlight.inset (1., 1.);
		context->setLineWidth (2.);
		context->setFrameColor (kDropAccept);
		context->drawRect (highlight, kDrawStroked);
	}

	context->setFont (panelFont ());
	const CCoord lineHeight = panelFont ()->getSize () + 3.;
	const CCoord width = r.getWidth () - 8.;

	if (mPath.empty ())
	{
		context->setFontColor (kTextHint);
		context->drawString ("drop .wav", r, kCenterText, true);
		setDirty (false);
		return;
	}

	// THE NAME, and under it - only when there is something to say - why
	// the cell will not play. A cell that shows a name and is silent when
	// struck, with no reason given, is the failure this panel must not have.
	const std::string name = fitted (context, fileStemOf (mPath), width);
	const bool bad = (mStatus != SampleStatus::Loaded);

	CRect line (r.left + 4., r.top + (r.getHeight () - lineHeight * (bad ? 2. : 1.)) * 0.5,
	            r.right - 4., 0.);
	line.bottom = line.top + lineHeight;

	context->setFontColor (bad ? kTextBad : kText);
	context->drawString (name.c_str (), line, kCenterText, true);

	if (bad)
	{
		line.offset (0., lineHeight);
		context->setFont (panelFontSmall ());
		context->drawString (fitted (context, sampleStatusText (mStatus), width).c_str (),
		                     line, kCenterText, true);
	}

	setDirty (false);
}

//------------------------------------------------------------------------
void SampleDrumCellView::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;

	event.consumed = true;

	// OPTION-CLICK EMPTIES. A modifier rather than a plain right-click,
	// so a stray click can never throw a sample away.
	if (event.modifiers.has (ModifierKey::Alt))
	{
		if (!mPath.empty () && mPathHandler)
			mPathHandler (mCell, std::string ());
		return;
	}

	if (playable () && mAuditionHandler)
		mAuditionHandler (mCell);
}

//------------------------------------------------------------------------
std::string SampleDrumCellView::firstWavPath (IDataPackage* package)
{
	if (package == nullptr)
		return std::string ();

	const uint32_t count = package->getCount ();
	for (uint32_t i = 0; i < count; ++i)
	{
		const void* buffer = nullptr;
		IDataPackage::Type type = IDataPackage::kError;
		const uint32_t size = package->getData (i, buffer, type);

		// THE TYPE IS NOT TRUSTED, only the content: a Finder drag comes
		// back as kText holding a file:// URL, so insisting on kFilePath
		// would refuse every drag there is. See Project6SlotView.cpp.
		if (type == IDataPackage::kError || type == IDataPackage::kBinary
		    || buffer == nullptr || size == 0)
			continue;

		// The size MAY or may not include the terminator depending on the
		// platform layer; strnlen handles both, and a stray NUL inside the
		// string would make it compare unequal to the same path read back
		// from a project.
		const char* chars = static_cast<const char*> (buffer);
		const std::string path = wavPathFromDragText (std::string (chars, ::strnlen (chars, size)));
		if (!path.empty ())
			return path;
	}

	return std::string ();
}

//------------------------------------------------------------------------
DragOperation SampleDrumCellView::onDragEnter (DragEventData data)
{
	// Tested HERE, not only on the drop, so the pointer says no before
	// the button is released. Lighting up for an .mp3 and then ignoring
	// it is worse than never lighting up.
	mDragOver = !firstWavPath (data.drag).empty ();
	invalid ();
	return onDragMove (data);
}

//------------------------------------------------------------------------
DragOperation SampleDrumCellView::onDragMove (DragEventData /*data*/)
{
	// A file from outside is always a COPY - it stays where it is on disk.
	return mDragOver ? DragOperation::Copy : DragOperation::None;
}

//------------------------------------------------------------------------
void SampleDrumCellView::onDragLeave (DragEventData /*data*/)
{
	if (!mDragOver)
		return;
	mDragOver = false;
	invalid ();
}

//------------------------------------------------------------------------
bool SampleDrumCellView::onDrop (DragEventData data)
{
	// VSTGUI does NOT call onDragLeave after a drop, so the highlight is
	// taken off here or the cell stays lit for ever.
	mDragOver = false;
	invalid ();

	const std::string path = firstWavPath (data.drag);
	if (path.empty () || !mPathHandler)
		return false;

	// mPath is NOT set here - see the banner.
	mPathHandler (mCell, path);
	return true;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
