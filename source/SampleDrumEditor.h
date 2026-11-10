//------------------------------------------------------------------------
// SampleDrum - the panel: the Polivoks black panel, design 2
//
//     Designs/SampleDrum-Polivoks-2-ChannelStrips-NearlyNew.png, 1200 x 640
//
//     SAMPLE DRUM | 48000 HZ / 32 VOICES / 29 OF 32 CELLS        VOICES [.....]
//     +------+ +------+          ... eight strips ...   +--------------------+
//     | KICK | | SNARE|                                  | MASTER             |
//     |<36 C1>                                           | [ last hit       ] |
//     | [ 1-31  ] cell, level knob                        | ISOLATOR           |
//     | [ 32-63 ]                                         | [ response       ] |
//     | [ 64-95 ]                                         |  BASS     TREBLE   |
//     | [ 96-127]                                         | trim      [meter]  |
//     |  o HIT                                            | o OVER  HAT CHOKE  |
//     +------+                                            | o BYPASS           |
//
// HOW IT IS BUILT - ForTran's Polivoks method (ForTran-VSTi/README.md):
// everything that never changes (boxes, titles, legends, printed knob
// scales, the OLED bezels, the band in each cell's corner, the wordmark) is
// ONE BITMAP, resource/pv_panel.png, rendered at 2x from the mock-up in
// tools/polivoks/d2p.js. The wear is a second, transparent bitmap,
// resource/pv_wear.png, laid over everything. Everything that MOVES is drawn
// by the views below, with paths and the 70s sci-fi lettering, AT THE
// COORDINATES d2p.js USES. Move something in one, move it in the other, and
// run tools/polivoks/make-panel.sh.
//
// A COLUMN IS A STRIP: the drum's name (printed), its note box (a control,
// SampleDrumNoteBox), its four velocity cells soft to hard
// (SampleDrumCellView - drop, click, drag, right-click as before), a level
// KNOB under each cell (the cell's level parameter, the bar's replacement)
// with its value beside it, and a HIT lamp that lights when any cell in the
// column is struck.
//
// THE MASTER STRIP: the last cell struck (waveform, playhead, drum, band,
// file, velocity), the OUTPUT ISOLATOR (its response, and Bass, Bass Freq,
// Treble, Treble Freq as knobs), OUTPUT TRIM, a peak meter with an OVER
// lamp, and BYPASS - the one lamp that is a switch.
//
// EVERY LABEL THE DSP DECIDES IS COMPUTED, not typed: the notes from
// formatNote, the bands from bandLow/bandHigh, the fallback each empty cell
// shows from chooseCell, the isolator's curve from isolatorResponseDb, the
// "kill" and "off" readings from isolatorGain and cellLevelGain. The panel
// cannot say one thing while the audio does another.
//
// THE LAMPS AND THE METER ARE PULLED, not pushed. A timer asks the
// processor, through the controller, for every cell's hit count and
// sounding flag, the output's peaks, the voice count and the last hit; see
// SampleDrumIDs.h for why that is a request and a reply.
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumCellView.h"
#include "SampleDrumDsp.h"
#include "SampleDrumKit.h"
#include "SampleDrumNoteBox.h"
#include "SampleDrumParams.h"
#include "SampleDrumPvViews.h"

#include "public.sdk/source/vst/vstguieditor.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace SampleDrum {

class SampleDrumController;

//------------------------------------------------------------------------
class SampleDrumEditor : public Steinberg::Vst::VSTGUIEditor,
                         public VSTGUI::IControlListener
{
public:
	explicit SampleDrumEditor (SampleDrumController* controller);

	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;

	// IControlListener
	void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;
	void controlBeginEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;
	void controlEndEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;

	//--------------------------------------------------------------------
	// From the controller
	//--------------------------------------------------------------------

	/** A parameter moved - from the host, automation or another window. */
	void updateControl (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue normalized);

	/** A cell's file, status or waveform changed. Reads them from the controller. */
	void refreshCell (int cell);

	/** The processor's answer to the timer's question. */
	void setActivity (const std::uint32_t* hits, const std::uint8_t* sounding, const OutputActivity& output);

	//--------------------------------------------------------------------
	// Layout - d2p.js's numbers, in one place
	//--------------------------------------------------------------------
	static constexpr int kEditorWidth  = 1200;
	static constexpr int kEditorHeight = 640;

	static constexpr double kStripLeft   = 16.;    // SX(c) = 16 + c * 118
	static constexpr double kStripPitch  = 118.;
	static constexpr double kStripWidth  = 110.;
	static constexpr double kCellTop     = 146.;   // RY(r) = 146 + r * 112
	static constexpr double kCellPitch   = 112.;
	static constexpr double kCellInset   = 8.;     // screen left = strip + 8
	static constexpr double kCellWidth   = 94.;    // OW
	static constexpr double kCellHeight  = 62.;    // OH
	static constexpr double kNoteTop     = 114.;
	static constexpr double kNoteWidth   = 92.;
	static constexpr double kNoteHeight  = 18.;
	static constexpr double kLevelOuter  = 12.;    // knob (ox + 14, y + OH + 22)
	static constexpr double kHitLampY    = 604.;   // lamp (x + 22, 604)

	static double stripX (int column) { return kStripLeft + column * kStripPitch; }
	/** The cell's SCREEN (the view is kGlowMargin bigger). */
	static VSTGUI::CRect cellScreen (int column, int row);

	/** The lamp timer. 30 ms, as before. */
	static constexpr int kTimerMs = 30;

	/** How much a struck cell's flash fades per tick: from full to out in
	    about 200 ms, long enough to see a hi-hat hit. */
	static constexpr float kFlashDecay = 0.15f;

	/** The lamp's level while a cell is still sounding after its flash. */
	static constexpr float kSoundingLamp = 0.45f;

	/** Meter ballistics: the bar falls this fast; the peak tick holds this
	    long, then falls at the same rate; OVER stays lit this long. */
	static constexpr double kMeterFallDbPerSecond = 26.;
	static constexpr double kPeakHoldSeconds = 1.5;
	static constexpr double kOverHoldSeconds = 1.5;

private:
	using Clock = std::chrono::steady_clock;

	double now () const;

	/** One knob on a parameter, with its value printed at (vx, vy). */
	PvKnob* addKnob (Steinberg::Vst::ParamID tag, double cx, double cy, double outer);
	PvText* addValue (double x, double y, double size, SciFi::Align align);

	void refreshValue (Steinberg::Vst::ParamID tag, double normalized);
	void refreshIsolatorScreen ();
	void refreshFallbacks ();
	void refreshHeader ();
	void refreshLastHit ();

	/** Mark every note box whose note another column shares. */
	void refreshSharedNotes ();
	void onTimer ();

	double paramNormalized (Steinberg::Vst::ParamID tag) const;

	SampleDrumController* mController = nullptr;

	VSTGUI::SharedPointer<VSTGUI::CBitmap> mPanelBitmap, mWearBitmap;

	SampleDrumCellView* mCells[kCellCount] = {};
	PvKnob*             mLevels[kCellCount] = {};
	PvText*             mLevelText[kCellCount] = {};
	SampleDrumNoteBox*  mNotes[kColumns] = {};
	PvLamp*             mHitLamps[kColumns] = {};

	/** Bass, Bass Freq, Treble, Treble Freq - in kIsolatorTags order. */
	static constexpr Steinberg::Vst::ParamID kIsolatorTags[4] = { kBassGain, kBassFreq, kTrebleGain, kTrebleFreq };
	PvKnob* mIsolator[4] = {};
	PvText* mIsolatorText[4] = {};
	PvKnob* mTrim = nullptr;
	PvText* mTrimText = nullptr;
	PvLamp* mBypass = nullptr;
	PvLamp* mOver = nullptr;

	PvText*         mEngine = nullptr;
	PvVoiceMeter*   mVoiceMeter = nullptr;
	PvLastHitOled*  mLastHitScreen = nullptr;
	PvIsolatorOled* mIsolatorScreen = nullptr;
	PvOutputMeter*  mMeter = nullptr;

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mTimer;

	/** The hit counts last seen, so a change can be spotted. */
	std::uint32_t mLastHits[kCellCount] = {};
	bool mHaveHits = false;
	float mFlash[kCellCount] = {};
	bool mSounding[kCellCount] = {};
	/** When each cell was last struck, for its playhead. */
	double mHitAt[kCellCount] = {};

	std::uint32_t mLastHitWord = 0;
	double mSampleRate = 0.;
	int mVoices = 0;

	/** Peaks received since the last tick (linear), and the meter's state. */
	float mPendingPeak[2] = {};
	double mMeterDb[2] = { -100., -100. };
	double mHoldDb[2] = { -100., -100. };
	double mHoldAge[2] = {};
	double mOverUntil = -1.;
	double mLastTick = -1.;

	Clock::time_point mEpoch = Clock::now ();
};

//------------------------------------------------------------------------
} // namespace SampleDrum
