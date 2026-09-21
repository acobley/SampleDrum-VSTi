//------------------------------------------------------------------------
// SampleDrum - the panel
//
//     SampleDrum
//                Kick   Snare  Clap  Closed HH  Open HH  Low Tom  High Tom  Crash
//                 36     38     39      42        46       45       50       49
//     1 - 31    [    ] [    ] [    ]  [    ]    [    ]   [    ]   [    ]   [    ]
//     32 - 63   [    ] [    ] [    ]  [    ]    [    ]   [    ]   [    ]   [    ]
//     64 - 95   [    ] [    ] [    ]  [    ]    [    ]   [    ]   [    ]   [    ]
//     96 - 127  [    ] [    ] [    ]  [    ]    [    ]   [    ]   [    ]   [    ]
//
//     [ Output Trim ]
//
// EVERY LABEL IS COMPUTED, not typed: the column names and notes come from
// kDrumMap and the row bands from bandLow/bandHigh in SampleDrumKit.h -
// the same functions the DSP uses to decide what plays. A panel that
// printed its own "96-127" while the DSP split somewhere else would be
// lying, and this is the arrangement that makes that impossible.
//
// THE LAMPS ARE PULLED, not pushed. A timer asks the processor, through
// the controller, for every cell's hit count and sounding flag; see
// SampleDrumIDs.h for why that is a request and a reply rather than 32
// parameters or a message from process().
//------------------------------------------------------------------------

#pragma once

#include "SampleDrumCellView.h"
#include "SampleDrumControls.h"
#include "SampleDrumKit.h"
#include "SampleDrumParams.h"

#include "public.sdk/source/vst/vstguieditor.h"

#include <cstdint>

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

	/** A cell's file or status changed. Reads both from the controller. */
	void refreshCell (int cell);

	/** The processor's answer to the timer's question. */
	void setActivity (const std::uint32_t* hits, const std::uint8_t* sounding);

	//--------------------------------------------------------------------
	// Layout - every number the panel is built from, in one place
	//--------------------------------------------------------------------
	static constexpr int kMargin      = 12;
	static constexpr int kTitleHeight = 22;
	static constexpr int kRowLabelWidth = 76;
	static constexpr int kCellWidth   = 100;
	static constexpr int kCellHeight  = 58;
	static constexpr int kCellGap     = 6;
	static constexpr int kHeaderHeight = 34;
	static constexpr int kSliderWidth  = 96;    // SpySlider's own size, as in Project6
	static constexpr int kSliderHeight = 27;

	static constexpr int kGridLeft = kMargin + kRowLabelWidth;
	static constexpr int kHeaderTop = kMargin + kTitleHeight + 6;
	static constexpr int kGridTop  = kHeaderTop + kHeaderHeight;
	static constexpr int kGridWidth  = kColumns * kCellWidth + (kColumns - 1) * kCellGap;
	static constexpr int kGridHeight = kRows * kCellHeight + (kRows - 1) * kCellGap;
	static constexpr int kSliderTop  = kGridTop + kGridHeight + 14;

	static constexpr int kEditorWidth  = kGridLeft + kGridWidth + kMargin;
	static constexpr int kEditorHeight = kSliderTop + kSliderHeight + kMargin;

	/** The lamp timer. 30 ms, as Project6: fast enough that a hit reads
	    as immediate, slow enough to cost nothing. */
	static constexpr int kTimerMs = 30;

	/** How much a struck cell's flash fades per tick: from full to out in
	    about 200 ms, long enough to see a hi-hat hit. */
	static constexpr float kFlashDecay = 0.15f;

	/** The lamp's level while a cell is still sounding after its flash. */
	static constexpr float kSoundingLamp = 0.45f;

private:
	VSTGUI::CRect cellRect (int column, int row) const;
	void onTimer ();
	void addLabel (const VSTGUI::CRect& rect, const std::string& text,
	               const VSTGUI::CColor& colour, VSTGUI::CHoriTxtAlign align);

	SampleDrumController* mController = nullptr;

	SampleDrumCellView* mCells[kCellCount] = {};
	SpySlider* mTrim = nullptr;

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mTimer;

	/** The hit counts last seen, so a change can be spotted. */
	std::uint32_t mLastHits[kCellCount] = {};
	bool mHaveHits = false;
	float mFlash[kCellCount] = {};
	bool mSounding[kCellCount] = {};
};

//------------------------------------------------------------------------
} // namespace SampleDrum
