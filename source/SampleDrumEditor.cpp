//------------------------------------------------------------------------
// SampleDrum - the panel, implementation
//------------------------------------------------------------------------

#include "SampleDrumEditor.h"

#include "SampleDrumController.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

using namespace VSTGUI;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SampleDrum {

namespace {

/** The voice meter's colours in this design (d2p.js header: vcol, voff). */
const CColor kVoiceOn  (0xff, 0xae, 0x2e, 0xff);
const CColor kVoiceOff (0x2a, 0x1f, 0x0e, 0xff);

std::string upper (std::string s)
{
	for (auto& ch : s)
		ch = static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
	return s;
}

/** "-6.0 DB", "0.0 DB", "+1.5 DB" - and "OFF" at the bottom of a cell's
    level, from the DSP's own silence test. */
std::string formatDb (double db, bool offAtBottom)
{
	if (offAtBottom && cellLevelGain (db) <= 0.0)
		return "OFF";
	char text[32] = {};
	if (std::fabs (db) < 0.05)
		std::snprintf (text, sizeof (text), "0.0 DB");
	else
		std::snprintf (text, sizeof (text), "%+.1f DB", db);
	return text;
}

/** The isolator's readouts. From the parameter table and the DSP's own
    kill test, so the panel cannot say "KILL" while sound comes out. */
std::string formatIsolator (ParamID tag, double normalized)
{
	const double plain = paramDef (tag).toPlain (normalized);
	char text[32] = {};
	if (tag == kBassGain || tag == kTrebleGain)
	{
		if (isolatorGain (plain) <= 0.0)
			return "KILL";
		return formatDb (plain, false);
	}
	if (plain >= 1000.0)
		std::snprintf (text, sizeof (text), "%.2f KHZ", plain / 1000.0);
	else
		std::snprintf (text, sizeof (text), "%.0f HZ", plain);
	return text;
}

/** The panel's bitmaps are drawn at 2x, so they are sharp on a Retina
    screen; declaring the scale makes VSTGUI draw them at 1200 x 640.
    (ForTranEditor::bitmap2x.) */
CBitmap* bitmap2x (const char* name)
{
	auto* bmp = new CBitmap (CResourceDescription (name));
	if (auto pb = bmp->getPlatformBitmap ())
		pb->setScaleFactor (2.);
	return bmp;
}

double toDb (float linear)
{
	return (linear > 1e-6f) ? 20.0 * std::log10 (static_cast<double> (linear)) : -120.0;
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
CRect SampleDrumEditor::cellScreen (int column, int row)
{
	const double x = stripX (column) + kCellInset, y = kCellTop + row * kCellPitch;
	return CRect (x, y, x + kCellWidth, y + kCellHeight);
}

//------------------------------------------------------------------------
double SampleDrumEditor::now () const
{
	return std::chrono::duration<double> (Clock::now () - mEpoch).count ();
}

//------------------------------------------------------------------------
double SampleDrumEditor::paramNormalized (ParamID tag) const
{
	return mController ? mController->getParamNormalized (tag) : 0.0;
}

//------------------------------------------------------------------------
PvKnob* SampleDrumEditor::addKnob (ParamID tag, double cx, double cy, double outer)
{
	auto* knob = new PvKnob (CPoint (cx, cy), outer, this, static_cast<int32_t> (tag));
	knob->setValueNormalized (static_cast<float> (paramNormalized (tag)));
	if (isTableParam (tag))
	{
		knob->setDefaultValue (static_cast<float> (paramDef (tag).defaultNormalized ()));
		knob->setTooltipText (paramDef (tag).title);
	}
	frame->addView (knob);
	return knob;
}

//------------------------------------------------------------------------
PvText* SampleDrumEditor::addValue (double x, double y, double size, SciFi::Align align)
{
	auto* text = new PvText (CPoint (x, y), size, false, align, Pv::kInk, 90.);
	frame->addView (text);
	return text;
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshValue (ParamID tag, double normalized)
{
	if (tag == kOutputTrim && mTrimText)
		mTrimText->setText (formatDb (paramDef (tag).toPlain (normalized), false));

	for (int i = 0; i < 4; ++i)
		if (tag == kIsolatorTags[i] && mIsolatorText[i])
		{
			mIsolatorText[i]->setText (formatIsolator (tag, normalized));
			refreshIsolatorScreen ();
		}

	const int cell = cellOfLevelParam (tag);
	if (isCellIndex (cell) && mLevelText[cell])
		mLevelText[cell]->setText (formatDb (paramDef (tag).toPlain (normalized), true));
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshIsolatorScreen ()
{
	if (mIsolatorScreen == nullptr)
		return;
	const auto plain = [this] (ParamID tag) { return paramDef (tag).toPlain (paramNormalized (tag)); };
	mIsolatorScreen->setIsolator (plain (kBassGain), plain (kBassFreq), plain (kTrebleGain), plain (kTrebleFreq));
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshFallbacks ()
{
	if (mController == nullptr)
		return;

	// The same mask the DSP builds: a cell counts only if it will play.
	unsigned int loaded = 0;
	for (int cell = 0; cell < kCellCount; ++cell)
		if (!mController->cellPath (cell).empty () && mController->cellStatus (cell) == SampleStatus::Loaded)
			loaded |= 1u << cell;

	for (int cell = 0; cell < kCellCount; ++cell)
		if (mCells[cell])
		{
			const int plays = chooseCell (columnOfCell (cell), rowOfCell (cell), loaded);
			mCells[cell]->setFallback (plays >= 0 && plays != cell ? rowOfCell (plays) : -1);
		}
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshHeader ()
{
	if (mEngine == nullptr)
		return;
	int loaded = 0;
	if (mController)
		for (int cell = 0; cell < kCellCount; ++cell)
			if (!mController->cellPath (cell).empty () && mController->cellStatus (cell) == SampleStatus::Loaded)
				++loaded;

	char text[96] = {};
	if (mSampleRate > 0.)
		std::snprintf (text, sizeof (text), "%.0f HZ  /  %d VOICES  /  %d OF %d CELLS",
		               mSampleRate, kMaxVoices, loaded, kCellCount);
	else
		std::snprintf (text, sizeof (text), "%d VOICES  /  %d OF %d CELLS", kMaxVoices, loaded, kCellCount);
	mEngine->setText (text);
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshLastHit ()
{
	if (mLastHitScreen == nullptr || mController == nullptr)
		return;
	const LastHit hit = decodeLastHit (mLastHitWord);
	if (!isCellIndex (hit.cell))
	{
		mLastHitScreen->setHit ("", "", "", 0, 0., {});
		return;
	}
	const int column = columnOfCell (hit.cell), row = rowOfCell (hit.cell);
	char band[16];
	std::snprintf (band, sizeof (band), "%d-%d", bandLow (row), bandHigh (row));
	mLastHitScreen->setHit (upper (kDrumMap[column].name), band, upper (fileStemOf (mController->cellPath (hit.cell))),
	                        hit.velocity, mController->cellSeconds (hit.cell), mController->cellThumbnail (hit.cell));
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
bool PLUGIN_API SampleDrumEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame)
		return false;

	const CRect frameSize (0, 0, kEditorWidth, kEditorHeight);
	frame = new CFrame (frameSize, this);
	frame->setBackgroundColor (Pv::kPanel);

	mPanelBitmap = VSTGUI::owned (bitmap2x ("pv_panel.png"));
	mWearBitmap  = VSTGUI::owned (bitmap2x ("pv_wear.png"));
	if (mPanelBitmap)
		frame->setBackground (mPanelBitmap);

	// ---- header: the engine line and the voice meter ----------------------
	mEngine = new PvText (CPoint (346., 32.), 10.5, false, SciFi::Align::Left, Pv::kInk, 420.);
	frame->addView (mEngine);
	mVoiceMeter = new PvVoiceMeter (CRect (970., 19., 1178., 37.), kMaxVoices, kVoiceOn, kVoiceOff);
	frame->addView (mVoiceMeter);

	// ---- the eight strips --------------------------------------------------
	for (int column = 0; column < kColumns; ++column)
	{
		const double x = stripX (column), mid = x + kStripWidth / 2.;

		// THE NOTE BOX - the column's note parameter, not a label.
		auto* box = new SampleDrumNoteBox (CRect (mid - kNoteWidth / 2., kNoteTop, mid + kNoteWidth / 2.,
		                                          kNoteTop + kNoteHeight),
		                                   this, static_cast<int32_t> (columnNoteParam (column)), column);
		box->setValueNormalized (static_cast<float> (paramNormalized (columnNoteParam (column))));
		mNotes[column] = box;
		frame->addView (box);

		for (int row = 0; row < kRows; ++row)
		{
			const int cell = cellIndex (column, row);
			const CRect screen = cellScreen (column, row);
			CRect viewRect = screen;
			viewRect.extend (SampleDrumCellView::kGlowMargin, SampleDrumCellView::kGlowMargin);
			auto* view = new SampleDrumCellView (viewRect, cell);

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

			// THE LEVEL KNOB under it, and its value beside it.
			const ParamID level = cellLevelParam (cell);
			mLevels[cell] = addKnob (level, screen.left + 14., screen.bottom + 22., kLevelOuter);
			mLevelText[cell] = addValue (screen.left + 34., screen.bottom + 20., 8.5, SciFi::Align::Left);
			refreshValue (level, paramNormalized (level));
		}

		// THE HIT LAMP - an indicator: no listener, no parameter.
		mHitLamps[column] = new PvLamp (CPoint (x + 22., kHitLampY), nullptr, 0);
		mHitLamps[column]->setMouseEnabled (false);
		frame->addView (mHitLamps[column]);
	}
	refreshSharedNotes ();

	// ---- MASTER --------------------------------------------------------------
	mLastHitScreen = new PvLastHitOled (CRect (978., 106., 1170., 190.));
	frame->addView (mLastHitScreen);

	// The isolator: its response, then Bass and Bass Freq, Treble and
	// Treble Freq, each with its value under it.
	mIsolatorScreen = new PvIsolatorOled (CRect (978., 234., 1170., 288.));
	frame->addView (mIsolatorScreen);
	const double isoX[4] = { 996., 1040., 1104., 1150. };
	for (int i = 0; i < 4; ++i)
	{
		const ParamID tag = kIsolatorTags[i];
		mIsolator[i] = addKnob (tag, isoX[i], 332., 12.);
		mIsolatorText[i] = addValue (isoX[i], 353., 7.5, SciFi::Align::Centre);
		refreshValue (tag, paramNormalized (tag));
	}
	refreshIsolatorScreen ();

	mTrim = addKnob (kOutputTrim, 1018., 432., 22.);
	mTrimText = addValue (1018., 465., 9.5, SciFi::Align::Centre);
	refreshValue (kOutputTrim, paramNormalized (kOutputTrim));

	mMeter = new PvOutputMeter (CRect (1072., 394., 1170., 528.));
	frame->addView (mMeter);
	mOver = new PvLamp (CPoint (992., 504.), nullptr, 0);
	mOver->setMouseEnabled (false);
	frame->addView (mOver);

	// BYPASS - the one lamp that is a switch: an instrument's bypass is
	// silence (SampleDrumProcessor::process).
	mBypass = new PvLamp (CPoint (992., 600.), this, static_cast<int32_t> (kBypass));
	mBypass->setValueNormalized (static_cast<float> (paramNormalized (kBypass)));
	mBypass->setDefaultValue (0.f);
	mBypass->setTooltipText ("Bypass: silence");
	frame->addView (mBypass);

	for (int cell = 0; cell < kCellCount; ++cell)
		refreshCell (cell);
	refreshHeader ();
	refreshLastHit ();

	// ---- the wear, over everything, invisible to the mouse -----------------
	if (mWearBitmap)
		frame->addView (new PvOverlay (frameSize, mWearBitmap));

	mTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { onTimer (); }, kTimerMs, true);

	frame->open (parent, platformType);
	return true;
}

//------------------------------------------------------------------------
void PLUGIN_API SampleDrumEditor::close ()
{
	mTimer = nullptr;

	for (int cell = 0; cell < kCellCount; ++cell)
	{
		mCells[cell] = nullptr;
		mLevels[cell] = nullptr;
		mLevelText[cell] = nullptr;
	}
	for (int column = 0; column < kColumns; ++column)
	{
		mNotes[column] = nullptr;
		mHitLamps[column] = nullptr;
	}
	for (int i = 0; i < 4; ++i)
	{
		mIsolator[i] = nullptr;
		mIsolatorText[i] = nullptr;
	}
	mTrim = nullptr;
	mTrimText = nullptr;
	mBypass = nullptr;
	mOver = nullptr;
	mEngine = nullptr;
	mVoiceMeter = nullptr;
	mLastHitScreen = nullptr;
	mIsolatorScreen = nullptr;
	mMeter = nullptr;
	mHaveHits = false;
	mLastTick = -1.;

	if (frame)
	{
		frame->forget ();
		frame = nullptr;
	}
	mPanelBitmap = nullptr;
	mWearBitmap = nullptr;
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
	refreshValue (tag, value);
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
	const auto set = [normalized] (CControl* control)
	{
		if (control)
		{
			control->setValueNormalized (static_cast<float> (normalized));
			control->invalid ();
		}
	};

	if (tag == kOutputTrim)
		set (mTrim);
	if (tag == kBypass)
		set (mBypass);
	for (int i = 0; i < 4; ++i)
		if (tag == kIsolatorTags[i])
			set (mIsolator[i]);

	const int cell = cellOfLevelParam (tag);
	if (isCellIndex (cell))
		set (mLevels[cell]);

	const int column = columnOfNoteParam (tag);
	if (column >= 0 && mNotes[column])
	{
		set (mNotes[column]);
		refreshSharedNotes ();
	}

	refreshValue (tag, normalized);
}

//------------------------------------------------------------------------
void SampleDrumEditor::refreshCell (int cell)
{
	if (!isCellIndex (cell) || mCells[cell] == nullptr || mController == nullptr)
		return;

	mCells[cell]->setPath (mController->cellPath (cell));
	mCells[cell]->setStatus (mController->cellStatus (cell));
	mCells[cell]->setThumbnail (mController->cellThumbnail (cell));

	// What one cell holds changes what every empty cell in its column
	// borrows, and the header's count.
	refreshFallbacks ();
	refreshHeader ();
	if (decodeLastHit (mLastHitWord).cell == cell)
		refreshLastHit ();
}

//------------------------------------------------------------------------
void SampleDrumEditor::setActivity (const std::uint32_t* hits, const std::uint8_t* sounding,
                                    const OutputActivity& output)
{
	if (hits == nullptr || sounding == nullptr)
		return;

	const double t = now ();
	for (int cell = 0; cell < kCellCount; ++cell)
	{
		// A MOVED COUNT is a hit, however short - the whole reason for
		// counting rather than only asking "is it sounding now". The first
		// reply after opening only sets the baseline, or every cell ever
		// struck would flash at once.
		if (mHaveHits && hits[cell] != mLastHits[cell])
		{
			mFlash[cell] = 1.f;
			mHitAt[cell] = t;
		}
		mLastHits[cell] = hits[cell];
		mSounding[cell] = (sounding[cell] != 0);
	}

	// THE PEAKS are held until the next tick draws them, so two replies
	// between ticks cannot lose one.
	for (int ch = 0; ch < 2; ++ch)
		mPendingPeak[ch] = std::max (mPendingPeak[ch], output.peak[ch]);

	if (output.voices != mVoices)
	{
		mVoices = output.voices;
		if (mVoiceMeter)
			mVoiceMeter->setVoices (mVoices);
	}
	if (output.sampleRate > 0. && output.sampleRate != mSampleRate)
	{
		mSampleRate = output.sampleRate;
		refreshHeader ();
	}
	if (output.lastHit != mLastHitWord)
	{
		// The first reply after opening sets the screen without a playhead.
		const LastHit hit = decodeLastHit (output.lastHit);
		if (mHaveHits && isCellIndex (hit.cell))
			mHitAt[hit.cell] = t;
		mLastHitWord = output.lastHit;
		refreshLastHit ();
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

	const double t = now ();
	const double dt = (mLastTick < 0.) ? 0. : std::min (0.25, t - mLastTick);
	mLastTick = t;

	// ---- cells and the column lamps ---------------------------------------
	bool columnLit[kColumns] = {};
	for (int cell = 0; cell < kCellCount; ++cell)
	{
		const float lamp = std::max (mFlash[cell], mSounding[cell] ? kSoundingLamp : 0.f);
		if (mCells[cell])
		{
			mCells[cell]->setLamp (lamp);
			const double seconds = mController ? mController->cellSeconds (cell) : 0.;
			const double position = (lamp > 0.f && seconds > 0.) ? (t - mHitAt[cell]) / seconds : -1.;
			mCells[cell]->setPlayhead (position <= 1. ? position : -1.);
		}
		if (lamp > 0.25f)
			columnLit[columnOfCell (cell)] = true;
		mFlash[cell] = std::max (0.f, mFlash[cell] - kFlashDecay);
	}
	for (int column = 0; column < kColumns; ++column)
		if (mHitLamps[column] && (mHitLamps[column]->getValueNormalized () >= 0.5f) != columnLit[column])
		{
			mHitLamps[column]->setValueNormalized (columnLit[column] ? 1.f : 0.f);
			mHitLamps[column]->invalid ();
		}

	// ---- the last hit's playhead ------------------------------------------
	if (mLastHitScreen && mController)
	{
		const LastHit hit = decodeLastHit (mLastHitWord);
		double position = -1.;
		if (isCellIndex (hit.cell) && mHaveHits)
		{
			const double seconds = mController->cellSeconds (hit.cell);
			if (seconds > 0. && mSounding[hit.cell])
				position = (t - mHitAt[hit.cell]) / seconds;
		}
		mLastHitScreen->setPlayhead (position <= 1. ? position : -1.);
	}

	// ---- the meter: fall at a steady rate, hold the peak, light OVER ------
	for (int ch = 0; ch < 2; ++ch)
	{
		const double peak = toDb (mPendingPeak[ch]);
		mPendingPeak[ch] = 0.f;
		mMeterDb[ch] = std::max (peak, mMeterDb[ch] - kMeterFallDbPerSecond * dt);
		if (peak >= mHoldDb[ch])
		{
			mHoldDb[ch] = peak;
			mHoldAge[ch] = 0.;
		}
		else if ((mHoldAge[ch] += dt) > kPeakHoldSeconds)
			mHoldDb[ch] -= kMeterFallDbPerSecond * dt;
		if (peak >= 0.)
			mOverUntil = t + kOverHoldSeconds;
	}
	if (mMeter)
		mMeter->setLevels (mMeterDb[0], mMeterDb[1], mHoldDb[0], mHoldDb[1]);
	if (mOver)
	{
		const bool over = t < mOverUntil;
		if ((mOver->getValueNormalized () >= 0.5f) != over)
		{
			mOver->setValueNormalized (over ? 1.f : 0.f);
			mOver->invalid ();
		}
	}
}

//------------------------------------------------------------------------
} // namespace SampleDrum
