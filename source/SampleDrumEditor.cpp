//------------------------------------------------------------------------
// SampleDrum - the panel, implementation
//------------------------------------------------------------------------

#include "SampleDrumEditor.h"

#include "SampleDrumController.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace VSTGUI;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SampleDrum {

using namespace Colours;

namespace {

/** Behind everything, so a failed draw reads as a dark panel rather than
    whatever the host left in the window. Project6's panel colour. */
const CColor kPanel (64, 64, 64, 255);

} // namespace

//------------------------------------------------------------------------
SampleDrumEditor::SampleDrumEditor (SampleDrumController* controller)
: VSTGUIEditor (controller)
, mController (controller)
{
	ViewRect rect (0, 0, kEditorWidth, kEditorHeight);
	setRect (rect);
}

//------------------------------------------------------------------------
CRect SampleDrumEditor::cellRect (int column, int row) const
{
	const CCoord x = kGridLeft + column * (kCellWidth + kCellGap);
	const CCoord y = kGridTop + row * (kCellHeight + kCellGap);
	return CRect (x, y, x + kCellWidth, y + kCellHeight);
}

//------------------------------------------------------------------------
void SampleDrumEditor::addLabel (const CRect& rect, const std::string& text,
                                 const CColor& colour, CHoriTxtAlign align)
{
	auto* label = new CTextLabel (rect, text.c_str (), nullptr, CParamDisplay::kNoFrame);
	label->setFont (panelFont ());
	label->setFontColor (colour);
	label->setTransparency (true);
	label->setHoriAlign (align);
	label->setMouseEnabled (false);
	frame->addView (label);
}

//------------------------------------------------------------------------
bool PLUGIN_API SampleDrumEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame)
		return false;

	frame = new CFrame (CRect (0, 0, kEditorWidth, kEditorHeight), this);
	frame->setBackgroundColor (kPanel);

	addLabel (CRect (kMargin, kMargin, kEditorWidth - kMargin, kMargin + kTitleHeight),
	          "SampleDrum", kValue, kLeftText);

	// COLUMN HEADINGS, from the drum map: the drum, and under it the note.
	for (int column = 0; column < kColumns; ++column)
	{
		const CRect cell = cellRect (column, 0);
		const CCoord half = kHeaderHeight / 2;
		addLabel (CRect (cell.left, kHeaderTop, cell.right, kHeaderTop + half),
		          kDrumMap[column].name, kLabel, kCenterText);
		addLabel (CRect (cell.left, kHeaderTop + half, cell.right, kGridTop - 2),
		          "note " + std::to_string (kDrumMap[column].note), kValue, kCenterText);
	}

	// ROW LABELS, from the velocity bands - softest at the top.
	addLabel (CRect (kMargin, kHeaderTop + kHeaderHeight / 2, kGridLeft - 6, kGridTop - 2),
	          "velocity", kLabel, kLeftText);
	for (int row = 0; row < kRows; ++row)
	{
		const CRect cell = cellRect (0, row);
		addLabel (CRect (kMargin, cell.top, kGridLeft - 6, cell.bottom),
		          std::to_string (bandLow (row)) + " - " + std::to_string (bandHigh (row)),
		          kValue, kLeftText);
	}

	// THE GRID.
	for (int row = 0; row < kRows; ++row)
	{
		for (int column = 0; column < kColumns; ++column)
		{
			const int cell = cellIndex (column, row);
			auto* view = new SampleDrumCellView (cellRect (column, row), cell);

			// Both handlers go THROUGH THE CONTROLLER, which writes back to
			// this cell and to the same cell in any other open editor.
			view->setPathHandler ([this] (int index, const std::string& path)
			{
				if (mController)
					mController->setCellPath (index, path);
			});
			view->setAuditionHandler ([this] (int index)
			{
				if (mController)
					mController->auditionCell (index);
			});

			mCells[cell] = view;
			frame->addView (view);
			refreshCell (cell);
		}
	}

	// THE OUTPUT TRIM, the one parameter so far, as a SpySlider.
	mTrim = new SpySlider (CRect (kGridLeft, kSliderTop, kGridLeft + kSliderWidth,
	                              kSliderTop + kSliderHeight),
	                       this, static_cast<int32_t> (kOutputTrim));
	mTrim->setLabel ("Output Trim");
	mTrim->setFormatter ([] (float normalized)
	{
		// The same formatting the controller gives the host, so the panel
		// and a generic editor cannot disagree about what it says.
		char text[32] = {};
		std::snprintf (text, sizeof (text), "%+.1f dB", paramDef (kOutputTrim).toPlain (normalized));
		return std::string (text);
	});
	if (mController)
		mTrim->setValueNormalized (static_cast<float> (mController->getParamNormalized (kOutputTrim)));
	frame->addView (mTrim);

	addLabel (CRect (kGridLeft + kSliderWidth + 12, kSliderTop, kEditorWidth - kMargin,
	                 kSliderTop + kSliderHeight),
	          "Drop .wav files on the cells.  Click to play, Option-click to empty.",
	          kValue, kLeftText);

	mTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { onTimer (); }, kTimerMs, true);

	frame->open (parent, platformType);
	return true;
}

//------------------------------------------------------------------------
void PLUGIN_API SampleDrumEditor::close ()
{
	mTimer = nullptr;

	for (auto*& cell : mCells)
		cell = nullptr;
	mTrim = nullptr;
	mHaveHits = false;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
void SampleDrumEditor::valueChanged (CControl* control)
{
	if (mController == nullptr || control == nullptr)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	const ParamValue value = control->getValueNormalized ();

	mController->setParamNormalized (tag, value);
	mController->performEdit (tag, value);
}

//------------------------------------------------------------------------
void SampleDrumEditor::controlBeginEdit (CControl* control)
{
	if (mController && control)
		mController->beginEdit (static_cast<ParamID> (control->getTag ()));
}

//------------------------------------------------------------------------
void SampleDrumEditor::controlEndEdit (CControl* control)
{
	if (mController && control)
		mController->endEdit (static_cast<ParamID> (control->getTag ()));
}

//------------------------------------------------------------------------
void SampleDrumEditor::updateControl (ParamID tag, ParamValue normalized)
{
	if (tag == kOutputTrim && mTrim)
	{
		mTrim->setValueNormalized (static_cast<float> (normalized));
		mTrim->invalid ();
	}
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshCell (int cell)
{
	if (!isCellIndex (cell) || mCells[cell] == nullptr || mController == nullptr)
		return;

	mCells[cell]->setPath (mController->cellPath (cell));
	mCells[cell]->setStatus (mController->cellStatus (cell));
}

//------------------------------------------------------------------------
void SampleDrumEditor::setActivity (const std::uint32_t* hits, const std::uint8_t* sounding)
{
	if (hits == nullptr || sounding == nullptr)
		return;

	for (int cell = 0; cell < kCellCount; ++cell)
	{
		// A MOVED COUNT is a hit, however short - the whole reason for
		// counting rather than only asking "is it sounding now". The first
		// reply after opening only sets the baseline, or every cell ever
		// struck would flash at once.
		if (mHaveHits && hits[cell] != mLastHits[cell])
			mFlash[cell] = 1.f;
		mLastHits[cell] = hits[cell];
		mSounding[cell] = (sounding[cell] != 0);
	}
	mHaveHits = true;
}

//------------------------------------------------------------------------
void SampleDrumEditor::onTimer ()
{
	// Ask for the next reply first; it arrives through the controller's
	// notify() and setActivity() before or after this tick, either is fine.
	if (mController)
		mController->requestActivity ();

	for (int cell = 0; cell < kCellCount; ++cell)
	{
		const float lamp = std::max (mFlash[cell], mSounding[cell] ? kSoundingLamp : 0.f);
		if (mCells[cell])
			mCells[cell]->setLamp (lamp);
		mFlash[cell] = std::max (0.f, mFlash[cell] - kFlashDecay);
	}
}

//------------------------------------------------------------------------
} // namespace SampleDrum
