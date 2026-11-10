//------------------------------------------------------------------------
// SampleDrum - the panel, implementation
//------------------------------------------------------------------------

#include "SampleDrumEditor.h"

#include "SampleDrumController.h"
#include "SampleDrumDsp.h"

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

/** The isolator sliders' readouts. From the parameter table and the DSP's
    own kill test, so the panel cannot say "kill" while sound comes out. */
std::string formatIsolator (ParamID tag, float normalized)
{
	const double plain = paramDef (tag).toPlain (normalized);
	char text[32] = {};
	if (tag == kBassGain || tag == kTrebleGain)
	{
		if (isolatorGain (plain) <= 0.0)
			std::snprintf (text, sizeof (text), "kill");
		else
			std::snprintf (text, sizeof (text), "%+.1f dB", plain);
	}
	else if (plain >= 1000.0)
		std::snprintf (text, sizeof (text), "%.2f kHz", plain / 1000.0);
	else
		std::snprintf (text, sizeof (text), "%.0f Hz", plain);
	return std::string (text);
}

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
CRect SampleDrumEditor::wellRect (int column, int row) const
{
	CRect r = cellRect (column, row);
	r.bottom = r.top + kWellHeight;
	return r;
}

//------------------------------------------------------------------------
CRect SampleDrumEditor::levelRect (int column, int row) const
{
	CRect r = cellRect (column, row);
	r.top = r.bottom - kLevelHeight;
	return r;
}

//------------------------------------------------------------------------
void SampleDrumEditor::showLevelOverlay (ParamID tag, double normalized)
{
	const int cell = cellOfLevelParam (tag);
	if (!isCellIndex (cell) || mCells[cell] == nullptr)
		return;

	// From the parameter's own table and the DSP's own silence test, so
	// the cell, the host and the audio cannot disagree about the setting.
	const double db = paramDef (tag).toPlain (normalized);
	char text[32] = {};
	if (cellLevelGain (db) <= 0.0)
		std::snprintf (text, sizeof (text), "off");
	else
		std::snprintf (text, sizeof (text), "%+.1f dB", db);
	mCells[cell]->setOverlay (text);
}

//------------------------------------------------------------------------
void SampleDrumEditor::clearLevelOverlay (ParamID tag)
{
	const int cell = cellOfLevelParam (tag);
	if (isCellIndex (cell) && mCells[cell])
		mCells[cell]->setOverlay (std::string ());
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshSharedNotes ()
{
	for (int a = 0; a < kColumns; ++a)
	{
		if (mNotes[a] == nullptr)
			continue;
		bool shared = false;
		for (int b = 0; b < kColumns && !shared; ++b)
			shared = (b != a && mNotes[b] && mNotes[b]->note () == mNotes[a]->note ());
		mNotes[a]->setShared (shared);
	}
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

		// THE NOTE BOX - the column's note parameter, not a label.
		auto* box = new SampleDrumNoteBox (CRect (cell.left + 8, kHeaderTop + half, cell.right - 8,
		                                          kGridTop - 3),
		                                   this, static_cast<int32_t> (columnNoteParam (column)), column);
		if (mController)
			box->setValueNormalized (
				static_cast<float> (mController->getParamNormalized (columnNoteParam (column))));
		mNotes[column] = box;
		frame->addView (box);
	}

	// ROW LABELS, from the velocity bands - softest at the top.
	addLabel (CRect (kMargin, kHeaderTop + kHeaderHeight / 2, kGridLeft - 6, kGridTop - 2),
	          "note / velocity", kLabel, kLeftText);
	refreshSharedNotes ();
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
			auto* view = new SampleDrumCellView (wellRect (column, row), cell);

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
			view->setMoveHandler ([this] (int from, int to, bool copy)
			{
				if (mController)
					mController->moveCell (from, to, copy);
			});

			// "Clear cell" from the right-click menu, and Option-click.
			// THE CONTROLLER IS CAPTURED, NOT THE EDITOR: the menu calls
			// back after it closes, and the controller outlives every
			// editor window, where this one may already be shut.
			if (auto* controller = mController)
			{
				view->setOwner (controller->instanceId ());
				view->setClearHandler ([controller] (int index) { controller->clearCell (index); });
			}

			mCells[cell] = view;
			frame->addView (view);
			refreshCell (cell);

			// THE LEVEL BAR under it - an ordinary parameter control.
			auto* level = new SampleDrumLevelBar (levelRect (column, row), this,
			                                      static_cast<int32_t> (cellLevelParam (cell)), cell);
			if (mController)
				level->setValueNormalized (
					static_cast<float> (mController->getParamNormalized (cellLevelParam (cell))));
			mLevels[cell] = level;
			frame->addView (level);
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
	mTrim->setDefaultValue (static_cast<float> (paramDef (kOutputTrim).defaultNormalized ()));
	if (mController)
		mTrim->setValueNormalized (static_cast<float> (mController->getParamNormalized (kOutputTrim)));
	frame->addView (mTrim);

	// THE OUTPUT ISOLATOR: bass, its crossover, treble, its crossover.
	for (int i = 0; i < 4; ++i)
	{
		const ParamID tag = kIsolatorTags[i];
		const CCoord x = kIsolatorLeft + i * (kSliderWidth + kSliderGap);
		auto* slider = new SpySlider (CRect (x, kSliderTop, x + kSliderWidth, kSliderTop + kSliderHeight),
		                              this, static_cast<int32_t> (tag));
		slider->setLabel (paramDef (tag).title);
		slider->setFormatter ([tag] (float normalized) { return formatIsolator (tag, normalized); });
		slider->setDefaultValue (static_cast<float> (paramDef (tag).defaultNormalized ()));
		if (mController)
			slider->setValueNormalized (static_cast<float> (mController->getParamNormalized (tag)));
		mIsolator[i] = slider;
		frame->addView (slider);
	}

	addLabel (CRect (kGridLeft, kHelpTop, kEditorWidth - kMargin, kHelpTop + kHelpHeight),
	          "Drop .wav on a cell.  Click: play.  Drag to another cell: move (Option: copy).  "
	          "Right-click: empty.  Bar under a cell: level.  Option-click a slider: default.",
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
	for (auto*& level : mLevels)
		level = nullptr;
	for (auto*& box : mNotes)
		box = nullptr;
	mTrim = nullptr;
	for (auto*& slider : mIsolator)
		slider = nullptr;
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

	if (isCellLevelParam (tag))
		showLevelOverlay (tag, value);
}

//------------------------------------------------------------------------
void SampleDrumEditor::controlBeginEdit (CControl* control)
{
	if (mController == nullptr || control == nullptr)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	mController->beginEdit (tag);

	// The readout appears when the gesture STARTS, so a bar pressed and
	// not yet moved already says what it is set to.
	if (isCellLevelParam (tag))
		showLevelOverlay (tag, control->getValueNormalized ());
}

//------------------------------------------------------------------------
void SampleDrumEditor::controlEndEdit (CControl* control)
{
	if (mController == nullptr || control == nullptr)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	mController->endEdit (tag);

	if (isCellLevelParam (tag))
		clearLevelOverlay (tag);
}

//------------------------------------------------------------------------
void SampleDrumEditor::updateControl (ParamID tag, ParamValue normalized)
{
	if (tag == kOutputTrim && mTrim)
	{
		mTrim->setValueNormalized (static_cast<float> (normalized));
		mTrim->invalid ();
	}

	for (int i = 0; i < 4; ++i)
		if (tag == kIsolatorTags[i] && mIsolator[i])
		{
			mIsolator[i]->setValueNormalized (static_cast<float> (normalized));
			mIsolator[i]->invalid ();
		}

	const int cell = cellOfLevelParam (tag);
	if (isCellIndex (cell) && mLevels[cell])
	{
		mLevels[cell]->setValueNormalized (static_cast<float> (normalized));
		mLevels[cell]->invalid ();
	}

	const int column = columnOfNoteParam (tag);
	if (column >= 0 && mNotes[column])
	{
		mNotes[column]->setValueNormalized (static_cast<float> (normalized));
		mNotes[column]->invalid ();
		refreshSharedNotes ();
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
