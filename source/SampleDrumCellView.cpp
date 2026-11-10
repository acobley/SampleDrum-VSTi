//------------------------------------------------------------------------
// SampleDrum - one cell of the grid, implementation
//------------------------------------------------------------------------

#include "SampleDrumCellView.h"

#include "SampleDrumPvViews.h"
#include "SampleDrumKit.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace VSTGUI;

namespace SampleDrum {

namespace {

/** Shorten `text` from the end until it fits `width` in the lettering. */
std::string fitted (std::string text, double size, double width)
{
	if (SciFi::textWidth (text, size, false) <= width)
		return text;
	while (!text.empty () && SciFi::textWidth (text + "..", size, false) > width)
		text.pop_back ();
	return text + "..";
}

std::string upper (std::string s)
{
	for (auto& ch : s)
		ch = static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
	return s;
}

void text (CDrawContext* c, const std::string& s, double x, double y, const CColor& col,
           SciFi::Align align, double size)
{
	SciFi::drawText (c, s, CPoint (x, y), size, false, col, align);
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
void SampleDrumCellView::setThumbnail (const std::vector<std::int8_t>& thumb)
{
	if (thumb == mThumb)
		return;
	mThumb = thumb;
	invalid ();
}

//------------------------------------------------------------------------
void SampleDrumCellView::setFallback (int row)
{
	if (row == mFallbackRow)
		return;
	mFallbackRow = row;
	invalid ();
}

//------------------------------------------------------------------------
void SampleDrumCellView::setPlayhead (double position)
{
	// Half-pixel steps: no redraw for a change nobody can see.
	position = (position < 0.) ? -1. : std::floor (std::min (1., position) * 180.) / 180.;
	if (position == mPlayhead)
		return;
	mPlayhead = position;
	invalid ();
}

//------------------------------------------------------------------------
// THE POLIVOKS CELL (tools/polivoks/d2p.js): an amber OLED behind glass.
// The bezel, the black, the dot grid and the band in the top right corner
// are printed on the panel bitmap; this draws what is in the cell. The
// view is kGlowMargin bigger than the screen all round, for the lit
// border.
//------------------------------------------------------------------------
void SampleDrumCellView::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	CRect r = getViewSize ();
	r.inset (kGlowMargin, kGlowMargin);
	const double x = r.left, y = r.top, w = r.getWidth (), h = r.getHeight ();
	const bool lit = mLamp > 0.02f;
	const CColor amber = Pv::kScreen;

	// LIT: the amber border, with its bloom - brightest on the hit, held
	// dimmer while the cell is still sounding.
	if (lit)
	{
		CRect b = r;
		b.extend (3., 3.);
		if (auto p = owned (context->createRoundRectGraphicsPath (b, 4.)))
		{
			context->setLineStyle (kLineSolid);
			context->setLineWidth (4.);
			context->setFrameColor (Pv::alpha (amber, 0.22 * mLamp));
			context->drawGraphicsPath (p, CDrawContext::kPathStroked);
			context->setLineWidth (1.6);
			context->setFrameColor (Pv::alpha (amber, 0.35 + 0.65 * mLamp));
			context->drawGraphicsPath (p, CDrawContext::kPathStroked);
		}
	}

	context->saveGlobalState ();
	{
		ConcatClip clip (*context, r);

		if (!mOverlay.empty ())
		{
			// THE OVERLAY WINS - a level being set is what the reader wants.
			text (context, upper (mOverlay), x + w / 2., y + h / 2. + 3., Pv::kInk, SciFi::Align::Centre, 9.);
		}
		else if (mPath.empty ())
		{
			text (context, "EMPTY", x + w / 2., y + h / 2., Pv::kOledText, SciFi::Align::Centre, 7.5);
			// WHAT THIS VELOCITY PLAYS INSTEAD - chooseCell's answer.
			if (mFallbackRow >= 0)
			{
				char t[32];
				std::snprintf (t, sizeof (t), "PLAYS %d-%d %s", bandLow (mFallbackRow), bandHigh (mFallbackRow),
				               mFallbackRow > rowOfCell (mCell) ? "\xE2\x86\x93" : "\xE2\x86\x91");
				text (context, t, x + w / 2., y + h / 2. + 12., Pv::alpha (amber, 0.7), SciFi::Align::Centre, 6.5);
			}
			else
				text (context, "DROP A .WAV", x + w / 2., y + h / 2. + 12., Pv::alpha (Pv::kOledText, 0.7),
				      SciFi::Align::Centre, 6.5);
		}
		else
		{
			// THE NAME, and - only when there is something to say - why the
			// cell will not play. A cell that shows a name and is silent when
			// struck, with no reason given, is the failure this must not have.
			const bool bad = (mStatus != SampleStatus::Loaded);
			const CColor nameColour = bad ? Pv::kRed : (lit ? Pv::kScreenBright : amber);
			text (context, fitted (upper (fileStemOf (mPath)), 7., w - 10.), x + 5., y + h - 5., nameColour,
			      SciFi::Align::Left, 7.);
			if (bad)
				text (context, fitted (upper (sampleStatusText (mStatus)), 7., w - 10.), x + w / 2., y + h / 2. + 2.,
				      Pv::kRed, SciFi::Align::Centre, 7.);
			else
			{
				const CRect wave (x + 4., y + 14., x + w - 4., y + h - 14.);
				Pv::drawThumbnail (context, wave, mThumb, amber, lit);
				if (lit && mPlayhead >= 0. && mPlayhead <= 1.)
				{
					const double px = wave.left + wave.getWidth () * mPlayhead;
					context->setLineStyle (kLineSolid);
					context->setLineWidth (1.);
					context->setFrameColor (CColor (255, 255, 255, 204));
					context->drawLine (CPoint (px, wave.top), CPoint (px, wave.bottom));
				}
			}
		}

		Pv::drawGlass (context, r);
	}
	context->restoreGlobalState ();

	if (mDragOver)
	{
		CRect highlight = r;
		highlight.inset (1., 1.);
		context->setLineStyle (kLineSolid);
		context->setLineWidth (2.);
		context->setFrameColor (Pv::kMint);
		context->drawRect (highlight, kDrawStroked);
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
