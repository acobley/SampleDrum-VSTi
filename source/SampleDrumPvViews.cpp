//------------------------------------------------------------------------
// SampleDrum - the Polivoks panel's own screens, implementation
//------------------------------------------------------------------------

#include "SampleDrumPvViews.h"

#include "SampleDrumDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace SampleDrum {

namespace {

CColor hex (uint32_t rgb, double a = 1.)
{
	return CColor (static_cast<uint8_t> (rgb >> 16), static_cast<uint8_t> ((rgb >> 8) & 0xff),
	               static_cast<uint8_t> (rgb & 0xff), static_cast<uint8_t> (255 * a));
}

void line (CDrawContext* c, const CPoint& a, const CPoint& b, const CColor& col, double w)
{
	c->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	c->setLineWidth (w);
	c->setFrameColor (col);
	c->drawLine (a, b);
}

void dashed (CDrawContext* c, const CPoint& a, const CPoint& b, const CColor& col, double w,
             double on, double off)
{
	const CCoord dashes[] = { on, off };
	c->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0.,
	                             2, dashes));
	c->setLineWidth (w);
	c->setFrameColor (col);
	c->drawLine (a, b);
}

void text (CDrawContext* c, const std::string& s, double x, double y, const CColor& col,
           SciFi::Align align, double size)
{
	SciFi::drawText (c, s, CPoint (x, y), size, false, col, align);
}

} // namespace

//========================================================================
// Helpers
//========================================================================
void Pv::drawGlass (CDrawContext* context, const CRect& r)
{
	if (auto p = owned (context->createGraphicsPath ()))
	{
		p->addRect (r);
		auto g = owned (CGradient::create ({ { 0., hex (0xffffff, 0.07) }, { 0.45, hex (0xffffff, 0.015) },
		                                     { 0.46, hex (0xffffff, 0.) }, { 1., hex (0xffffff, 0.) } }));
		if (g)
			context->fillLinearGradient (p, *g, r.getTopLeft (),
			                             CPoint (r.left + 0.35 * r.getWidth (), r.bottom));
	}
	line (context, CPoint (r.left + 3., r.top + 0.8), CPoint (r.right - 3., r.top + 0.8),
	      hex (0xffffff, 0.12), 0.6);
}

void Pv::drawThumbnail (CDrawContext* context, const CRect& box, const std::vector<std::int8_t>& thumb,
                        const CColor& colour, bool bright)
{
	const double mid = box.top + box.getHeight () / 2.;
	line (context, CPoint (box.left, mid), CPoint (box.right, mid), Pv::alpha (colour, 0.35), 0.6);

	const size_t n = thumb.size () / 2;
	if (n < 2)
		return;
	auto p = owned (context->createGraphicsPath ());
	if (!p)
		return;
	const double half = box.getHeight () / 2., step = box.getWidth () / static_cast<double> (n - 1);
	for (size_t i = 0; i < n; ++i)
	{
		const CPoint q (box.left + i * step, mid - thumb[2 * i + 1] / 127. * half);
		if (i == 0) p->beginSubpath (q); else p->addLine (q);
	}
	for (size_t i = n; i-- > 0;)
		p->addLine (CPoint (box.left + i * step, mid - thumb[2 * i] / 127. * half));
	p->closeSubpath ();

	context->setFillColor (Pv::alpha (colour, bright ? 0.55 : 0.28));
	context->drawGraphicsPath (p, CDrawContext::kPathFilled);
	if (bright)
	{
		// the mock-up's bloom: a soft wide stroke under the hairline
		context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
		context->setLineWidth (1.6);
		context->setFrameColor (Pv::alpha (colour, 0.18));
		context->drawGraphicsPath (p, CDrawContext::kPathStroked);
	}
	context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->setLineWidth (0.5);
	context->setFrameColor (colour);
	context->drawGraphicsPath (p, CDrawContext::kPathStroked);
}

//========================================================================
// PvLastHitOled  (d2p.js: oled 978,106 192 x 84)
//========================================================================
void PvLastHitOled::setHit (const std::string& drum, const std::string& band, const std::string& file,
                            int velocity, double seconds, const std::vector<std::int8_t>& thumb)
{
	mDrum = drum;
	mBand = band;
	mFile = file;
	mVelocity = velocity;
	mSeconds = seconds;
	mThumb = thumb;
	invalid ();
}

void PvLastHitOled::setPlayhead (double position)
{
	if (std::fabs (position - mPlayhead) < 1e-4)
		return;
	mPlayhead = position;
	invalid ();
}

void PvLastHitOled::drawContent (CDrawContext* context)
{
	const CRect r = getViewSize ();
	const double x = r.left, y = r.top, w = r.getWidth ();
	// "LAST HIT" is printed on the panel.
	if (mDrum.empty ())
	{
		text (context, "NOTHING STRUCK YET", x + w / 2., y + 46., Pv::kOledText, SciFi::Align::Centre, 7.5);
		return;
	}

	char vel[24];
	if (mVelocity > 0)
		std::snprintf (vel, sizeof (vel), "VEL %d", mVelocity);
	else
		std::snprintf (vel, sizeof (vel), "CLICKED");
	text (context, vel, x + w - 6., y + 12., Pv::kScreen, SciFi::Align::Right, 7.5);

	const CRect wave (x + 6., y + 18., x + w - 6., y + 62.);
	Pv::drawThumbnail (context, wave, mThumb, Pv::kScreen, true);
	if (mPlayhead >= 0. && mPlayhead <= 1.)
	{
		const double px = wave.left + wave.getWidth () * mPlayhead;
		line (context, CPoint (px, wave.top), CPoint (px, wave.bottom), hex (0xffffff, 0.8), 1.);
	}

	text (context, mDrum + " " + mBand + "  " + mFile, x + 6., y + 78., Pv::kScreen, SciFi::Align::Left, 7.);
	if (mSeconds > 0.)
	{
		char len[24];
		std::snprintf (len, sizeof (len), "%.2f S", mSeconds);
		text (context, len, x + w - 6., y + 78., Pv::kOledText, SciFi::Align::Right, 7.);
	}
}

//========================================================================
// PvIsolatorOled  (sd.js isoOled; d2p.js: oled 978,234 192 x 54)
//========================================================================
void PvIsolatorOled::setIsolator (double bassDb, double bassHz, double trebleDb, double trebleHz)
{
	if (bassDb == mBassDb && bassHz == mBassHz && trebleDb == mTrebleDb && trebleHz == mTrebleHz)
		return;
	mBassDb = bassDb; mBassHz = bassHz; mTrebleDb = trebleDb; mTrebleHz = trebleHz;
	invalid ();
}

void PvIsolatorOled::drawContent (CDrawContext* context)
{
	const CRect r = getViewSize ();
	const double x = r.left, y = r.top, w = r.getWidth (), h = r.getHeight ();
	const double top = y + 16., bot = y + h - 14.;
	const auto fx = [&] (double f) { return x + 6. + std::log (f / 20.) / std::log (1000.) * (w - 12.); };
	const auto fy = [&] (double db) { return top + (bot - top) * (6. - std::max (-30., db)) / 36.; };
	const CColor col = Pv::kScreen;
	const double bx = fx (mBassHz), tx = fx (mTrebleHz);

	// the two outer bands, tinted
	context->setFillColor (Pv::alpha (col, 0.06));
	context->drawRect (CRect (x + 6., top, bx, bot), kDrawFilled);
	context->drawRect (CRect (tx, top, x + w - 6., bot), kDrawFilled);
	dashed (context, CPoint (x + 6., fy (0.)), CPoint (x + w - 6., fy (0.)), Pv::kOledText, 0.6, 2., 3.);
	dashed (context, CPoint (bx, top), CPoint (bx, bot), Pv::alpha (col, 0.5), 0.8, 3., 3.);
	dashed (context, CPoint (tx, top), CPoint (tx, bot), Pv::alpha (col, 0.5), 0.8, 3., 3.);

	std::vector<CPoint> pts;
	for (int i = 0; i <= 160; ++i)
	{
		const double f = 20. * std::pow (1000., i / 160.);
		pts.emplace_back (fx (f), fy (isolatorResponseDb (f, mBassDb, mBassHz, mTrebleDb, mTrebleHz)));
	}
	fillUnder (context, pts, bot, top, col);
	trace (context, pts, col);

	text (context, "LOW", x + 7., y + 11., Pv::kOledText, SciFi::Align::Left, 7.5);
	text (context, "MID", (bx + tx) / 2., y + 11., Pv::kOledText, SciFi::Align::Centre, 7.5);
	text (context, "HIGH", x + w - 7., y + 11., Pv::kOledText, SciFi::Align::Right, 7.5);

	char b[24], t[24];
	std::snprintf (b, sizeof (b), "%.0f HZ", mBassHz);
	if (mTrebleHz >= 1000.)
		std::snprintf (t, sizeof (t), "%.2f KHZ", mTrebleHz / 1000.);
	else
		std::snprintf (t, sizeof (t), "%.0f HZ", mTrebleHz);
	text (context, b, bx, y + h - 4., col, SciFi::Align::Centre, 7.5);
	text (context, t, tx, y + h - 4., col, SciFi::Align::Centre, 7.5);
}

//========================================================================
// PvOutputMeter  (sd.js outMeter; d2p.js: oled 1072,394 98 x 134, bw 20)
//========================================================================
void PvOutputMeter::setLevels (double leftDb, double rightDb, double leftHoldDb, double rightHoldDb)
{
	const double v[4] = { leftDb, rightDb, leftHoldDb, rightHoldDb };
	double* m[4] = { &mDb[0], &mDb[1], &mHold[0], &mHold[1] };
	bool changed = false;
	for (int i = 0; i < 4; ++i)
		if (std::fabs (*m[i] - v[i]) > 0.05)
		{
			*m[i] = v[i];
			changed = true;
		}
	if (changed)
		invalid ();
}

double PvOutputMeter::yOf (double db) const
{
	const CRect r = getViewSize ();
	const double top = r.top + 16., bot = r.bottom - 10.;
	const double t = std::clamp ((db + 60.) / 66., 0., 1.);
	return top + (bot - top) * (1. - std::pow (t, 1.6));
}

void PvOutputMeter::drawContent (CDrawContext* context)
{
	const CRect r = getViewSize ();
	const double bw = 20., gap = (r.getWidth () - 2. * bw) / 3.;
	constexpr int kSegs = 30;
	for (int k = 0; k < 2; ++k)
	{
		const double bx = r.left + gap + k * (bw + gap);
		for (int i = 0; i < kSegs; ++i)
		{
			const double db  = -60. + 66. * std::pow (static_cast<double> (i) / kSegs, 1. / 1.6);
			const double db2 = -60. + 66. * std::pow (static_cast<double> (i + 1) / kSegs, 1. / 1.6);
			const double sy = yOf (db), sy2 = yOf (db2);
			const CColor c = db >= 0. ? Pv::kRed : Pv::kScreen;
			const bool on = db < mDb[k];
			const CRect seg (bx, sy2 + 0.8, bx + bw, sy2 + 0.8 + std::max (1., sy - sy2 - 1.6));
			if (on)
			{
				context->setFillColor (Pv::alpha (c, 0.25));
				context->drawRect (CRect (seg).extend (1., 1.), kDrawFilled);
			}
			context->setFillColor (on ? c : Pv::alpha (c, 0.08));
			context->drawRect (seg, kDrawFilled);
		}
		if (mHold[k] > -60.)
			line (context, CPoint (bx, yOf (mHold[k])), CPoint (bx + bw, yOf (mHold[k])),
			      mHold[k] >= 0. ? Pv::kRed : Pv::kScreen, 1.4);
		text (context, k ? "R" : "L", bx + bw / 2., r.top + 11., Pv::kOledText, SciFi::Align::Centre, 8.);
	}
	const std::pair<double, const char*> marks[] = { { 6., "+6" }, { 0., "0" }, { -6., "-6" },
	                                                 { -12., "-12" }, { -24., "-24" }, { -48., "-48" } };
	for (const auto& [db, s] : marks)
		text (context, s, r.left + r.getWidth () / 2., yOf (db) + 2.8, db >= 0. ? Pv::kRed : Pv::kOledText,
		      SciFi::Align::Centre, 7.);
}

//------------------------------------------------------------------------
} // namespace SampleDrum
