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
void SampleDrumCellView::setOverlay (const std::string& text)
{
	if (mOverlay == text)
		return;
	mOverlay = text;
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
void SampleDrumCellView::setMoveHandler (std::function<void (int, int, bool)> handler)
{
	mMoveHandler = std::move (handler);
}

//------------------------------------------------------------------------
void SampleDrumCellView::setClearHandler (std::function<void (int)> handler)
{
	mClearHandler = std::move (handler);
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
		text += "\nClick to play; drag to another cell to move it (hold Option to copy)"
		        "\nRight-click or Option-click to empty";
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

	// THE OVERLAY WINS: while the bar below is dragged, the level is what
	// the reader wants, and the name will be back when the drag ends.
	if (!mOverlay.empty ())
	{
		context->setFontColor (kText);
		context->drawString (mOverlay.c_str (), r, kCenterText, true);
		setDirty (false);
		return;
	}

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
	// RIGHT-CLICK: the cell's menu, on a cell that holds something. On a
	// Mac a Control-click arrives here as a right-click too - the
	// platform's own convention for a context menu.
	if (event.buttonState.isRight ())
	{
		if (!mPath.empty () && mClearHandler)
		{
			mPressed = false;
			showCellMenu (event.mousePosition);
			event.consumed = true;
		}
		return;
	}

	if (!event.buttonState.isLeft ())
		return;

	// CONSUMED EITHER WAY: it is what makes the frame send this view the
	// move and up events that follow.
	event.consumed = true;

	mPressed        = true;
	mDragStarted    = false;
	mPressPoint     = event.mousePosition;
	mClearOnRelease = event.modifiers.has (ModifierKey::Alt);

	// A PLAIN CLICK PLAYS AT ONCE - see the banner for why here and not on
	// release. An Option-press does not: it is an empty, or a copy-drag.
	if (!mClearOnRelease && playable () && mAuditionHandler)
		mAuditionHandler (mCell);
}

//------------------------------------------------------------------------
void SampleDrumCellView::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (!mPressed || mDragStarted)
		return;

	event.consumed = true;

	// AN EMPTY CELL HAS NOTHING TO PICK UP.
	if (mPath.empty ())
		return;

	const CCoord dx = event.mousePosition.x - mPressPoint.x;
	const CCoord dy = event.mousePosition.y - mPressPoint.y;
	if (std::fabs (dx) < kDragThreshold && std::fabs (dy) < kDragThreshold)
		return;

	// A DRAG. mPressed cleared FIRST, so the release that ends it cannot
	// also be read as a click - and an Option-drag is a copy, never an
	// empty.
	mPressed        = false;
	mDragStarted    = true;
	mClearOnRelease = false;
	beginCellDrag ();
}

//------------------------------------------------------------------------
void SampleDrumCellView::onMouseUpEvent (MouseUpEvent& event)
{
	if (!mPressed)
	{
		// The tail of a drag, or a release whose press this view never saw.
		mDragStarted = false;
		mClearOnRelease = false;
		return;
	}

	mPressed = false;
	event.consumed = true;

	// OPTION-CLICK EMPTIES - on release, having not moved.
	if (mClearOnRelease)
	{
		mClearOnRelease = false;
		if (!mPath.empty () && mClearHandler)
			mClearHandler (mCell);
	}
}

//------------------------------------------------------------------------
void SampleDrumCellView::onMouseCancelEvent (MouseCancelEvent&)
{
	// A cancelled press is neither a click nor an empty.
	mPressed        = false;
	mDragStarted    = false;
	mClearOnRelease = false;
}

//------------------------------------------------------------------------
void SampleDrumCellView::beginCellDrag ()
{
	const std::string payload = encodeCellDrag (mOwner, mCell, mPath);
	if (payload.empty ())
		return;

	// THE TERMINATOR IS INCLUDED: the macOS layer builds the pasteboard
	// item with stringWithUTF8String, which reads to a NUL and takes no
	// length (Project6SlotView.cpp).
	auto package = CDropSource::create (payload.c_str (),
	                                    static_cast<uint32_t> (payload.size () + 1),
	                                    IDataPackage::kText);
	if (!package)
		return;

	doDrag (DragDescription (package));
}

//------------------------------------------------------------------------
void SampleDrumCellView::showCellMenu (const CPoint& where)
{
	auto* frame = getFrame ();
	if (frame == nullptr || !mClearHandler)
		return;

	auto menu = makeOwned<COptionMenu> ();
	menu->setStyle (COptionMenu::kPopupStyle);

	const std::string title = "Clear cell (" + fileNameOf (mPath) + ")";
	menu->addEntry (title.c_str ());

	CPoint at (where);
	localToFrame (at);

	// THE CALLBACK RUNS LATER, after the menu closes - by which time the
	// editor may have been shut. The handler (which captures the
	// controller, not the editor) and the index are copied in, and `self`
	// keeps this view alive until the callback has run.
	const int index = mCell;
	auto handler = mClearHandler;
	SharedPointer<SampleDrumCellView> self (this);
	menu->popup (frame, at, [self, index, handler] (COptionMenu* chosen)
	{
		if (chosen && chosen->getLastResult () == 0 && handler)
			handler (index);
	});
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
bool SampleDrumCellView::cellDrag (IDataPackage* package, std::uint64_t& owner, int& from,
                                   std::string& path)
{
	if (package == nullptr)
		return false;

	const uint32_t count = package->getCount ();
	for (uint32_t i = 0; i < count; ++i)
	{
		const void* buffer = nullptr;
		IDataPackage::Type type = IDataPackage::kError;
		const uint32_t size = package->getData (i, buffer, type);
		if (type == IDataPackage::kError || buffer == nullptr || size == 0)
			continue;

		const char* chars = static_cast<const char*> (buffer);
		if (decodeCellDrag (std::string (chars, ::strnlen (chars, size)), &owner, &from, &path))
			return true;
	}
	return false;
}

//------------------------------------------------------------------------
bool SampleDrumCellView::copyRequested (const Modifiers& modifiers)
{
	return modifiers.has (ModifierKey::Alt) || modifiers.has (ModifierKey::Control);
}

//------------------------------------------------------------------------
DragOperation SampleDrumCellView::onDragEnter (DragEventData data)
{
	std::uint64_t owner = 0;
	int from = -1;
	std::string path;

	if (cellDrag (data.drag, owner, from, path))
	{
		// A CELL DROPPED ON ITSELF IS NOT A DROP - lighting up would
		// promise something. A cell from ANOTHER INSTANCE is a file by
		// another name, and is accepted as one if it is a wav.
		mDragIsOwnCell = (owner == mOwner);
		mDragOver = mDragIsOwnCell ? (from != mCell) : isWavPath (path);
	}
	else
	{
		// Tested HERE, not only on the drop, so the pointer says no before
		// the button is released. Lighting up for an .mp3 and then
		// ignoring it is worse than never lighting up.
		mDragIsOwnCell = false;
		mDragOver = !firstWavPath (data.drag).empty ();
	}

	invalid ();
	return onDragMove (data);
}

//------------------------------------------------------------------------
DragOperation SampleDrumCellView::onDragMove (DragEventData data)
{
	if (!mDragOver)
		return DragOperation::None;

	// A file from outside is always a COPY - it stays where it is on disk.
	// Only a cell of this instance can MOVE, and the modifier is read
	// live, so the cursor says which before the button comes up.
	if (!mDragIsOwnCell)
		return DragOperation::Copy;
	return copyRequested (data.modifiers) ? DragOperation::Copy : DragOperation::Move;
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

	// ANOTHER CELL first: its payload is this plug-in's own shape and
	// cannot be mistaken for a file.
	std::uint64_t owner = 0;
	int from = -1;
	std::string dragged;
	if (cellDrag (data.drag, owner, from, dragged))
	{
		if (owner == mOwner)
		{
			if (from == mCell || !isCellIndex (from) || !mMoveHandler)
				return false;
			// THE MODIFIER IS READ AT THE DROP, the platform's own rule.
			mMoveHandler (from, mCell, copyRequested (data.modifiers));
			return true;
		}

		// FROM ANOTHER INSTANCE: its cell number means nothing here, and
		// its embedded copy is in another processor. Taken as a drop of
		// the path from outside - a copy, read from disk.
		if (!isWavPath (dragged) || !mPathHandler)
			return false;
		mPathHandler (mCell, dragged);
		return true;
	}

	const std::string path = firstWavPath (data.drag);
	if (path.empty () || !mPathHandler)
		return false;

	// mPath is NOT set here - see the banner.
	mPathHandler (mCell, path);
	return true;
}

//------------------------------------------------------------------------
} // namespace SampleDrum
