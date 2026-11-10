//------------------------------------------------------------------------
// SampleDrum - the Polivoks panel's own screens
//
// The three OLEDs in the MASTER strip of design 2 (Designs/SampleDrum-
// Polivoks-2-ChannelStrips-NearlyNew.png), drawn at the coordinates of
// tools/polivoks/d2p.js and sd.js:
//
//   PvLastHitOled    the cell last struck: its waveform, a playhead, the
//                    drum, the band, the file and the velocity
//   PvIsolatorOled   the isolator's response, LOW / MID / HIGH, both
//                    crossovers - from isolatorResponseDb, the frequency-
//                    domain twin of the DSP, so the screen cannot draw a
//                    curve the audio does not have
//   PvOutputMeter    L and R peak ladders with a peak-hold tick
//
// and two helpers the cells share with them: the glass, and a waveform
// from makeThumbnail.
//------------------------------------------------------------------------

#pragma once

#include "PvControls.h"

#include <cstdint>
#include <string>
#include <vector>

namespace SampleDrum {
namespace Pv {

/** The screens' one colour in this design (sd.js: every OLED amber). */
inline const VSTGUI::CColor kScreen = kAmber;
/** A lit cell's file name (d2.js '#fff2dc'). */
inline const VSTGUI::CColor kScreenBright { 0xff, 0xf2, 0xdc, 0xff };

/** The glass over a screen: PvOled's sheen and hairline, for views that
    are not PvOleds (the cells and note boxes are controls). */
void drawGlass (VSTGUI::CDrawContext* context, const VSTGUI::CRect& r);

/** A makeThumbnail waveform filling `box`, centred on its middle line.
    `bright` for a sounding cell (sd.js waveform, dim or not). */
void drawThumbnail (VSTGUI::CDrawContext* context, const VSTGUI::CRect& box,
                    const std::vector<std::int8_t>& thumb, const VSTGUI::CColor& colour, bool bright);

} // namespace Pv

//------------------------------------------------------------------------
class PvLastHitOled : public PvOled
{
public:
	using PvOled::PvOled;

	/** Nothing yet: `drum` empty. `velocity` 0 for an audition from the
	    panel; `playhead` 0..1, or negative when it has finished. */
	void setHit (const std::string& drum, const std::string& band, const std::string& file,
	             int velocity, double seconds, const std::vector<std::int8_t>& thumb);
	void setPlayhead (double position);

	CLASS_METHODS (PvLastHitOled, VSTGUI::CView)
protected:
	void drawContent (VSTGUI::CDrawContext* context) override;
private:
	std::string mDrum, mBand, mFile;
	int mVelocity = 0;
	double mSeconds = 0.;
	double mPlayhead = -1.;
	std::vector<std::int8_t> mThumb;
};

//------------------------------------------------------------------------
class PvIsolatorOled : public PvOled
{
public:
	using PvOled::PvOled;
	void setIsolator (double bassDb, double bassHz, double trebleDb, double trebleHz);
	CLASS_METHODS (PvIsolatorOled, VSTGUI::CView)
protected:
	void drawContent (VSTGUI::CDrawContext* context) override;
private:
	double mBassDb = 0., mBassHz = 200., mTrebleDb = 0., mTrebleHz = 3500.;
};

//------------------------------------------------------------------------
class PvOutputMeter : public PvOled
{
public:
	using PvOled::PvOled;
	/** Levels and peak-hold ticks in dBFS; below -60 is off the scale. */
	void setLevels (double leftDb, double rightDb, double leftHoldDb, double rightHoldDb);
	CLASS_METHODS (PvOutputMeter, VSTGUI::CView)
protected:
	void drawContent (VSTGUI::CDrawContext* context) override;
private:
	double yOf (double db) const;
	double mDb[2] = { -100., -100. };
	double mHold[2] = { -100., -100. };
};

//------------------------------------------------------------------------
} // namespace SampleDrum
