//------------------------------------------------------------------------
// SampleDrum - the 70s sci-fi lettering
//------------------------------------------------------------------------

#include "SciFiLettering.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"

#include <cstdlib>
#include <unordered_map>
#include <vector>

using namespace VSTGUI;

namespace SampleDrum {
namespace SciFi {

namespace {

constexpr double kTrack = 1.05;       // grid units between letters, plus the stroke
constexpr double kWordGap = 4.4;      // grid units between words
constexpr double kCapPerSize = 0.64;  // cap height per CSS px of font-size

struct GlyphSource
{
	char32_t code;
	float    advance;   // drawn width in grid units, not counting stroke and track
	const char* path;   // M / L / Q / Z on the 4 x 6 grid, y down
};

// Generated from rbfont.js - keep the two in step.
const GlyphSource kGlyphs[] = {
	{ 0x0041, 4.0f, "M0,6 L0,2 Q0,0 2,0 Q4,0 4,2 L4,6 M0,3.6 L4,3.6" },
	{ 0x0042, 4.0f, "M0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3 M2.8,3 Q4,3 4,4.5 Q4,6 2.8,6 L0,6 L0,0" },
	{ 0x0043, 4.0f, "M4,0 L1.6,0 Q0,0 0,1.6 L0,4.4 Q0,6 1.6,6 L4,6" },
	{ 0x0044, 4.0f, "M0,0 L2,0 Q4,0 4,2 L4,4 Q4,6 2,6 L0,6 Z" },
	{ 0x0045, 4.0f, "M4,0 L0,0 L0,6 L4,6 M0,3 L3.2,3" },
	{ 0x0046, 4.0f, "M4,0 L0,0 L0,6 M0,3 L3.2,3" },
	{ 0x0047, 4.0f, "M4,0 L1.6,0 Q0,0 0,1.6 L0,4.4 Q0,6 1.6,6 L4,6 L4,3.2 L2.2,3.2" },
	{ 0x0048, 4.0f, "M0,0 L0,6 M4,0 L4,6 M0,3 L4,3" },
	{ 0x0049, 0.0f, "M0,0 L0,6" },
	{ 0x004A, 4.0f, "M4,0 L4,4.4 Q4,6 2.4,6 L0,6" },
	{ 0x004B, 4.0f, "M0,0 L0,6 M0,3 L1.8,3 L4,0 M1.8,3 L4,6" },
	{ 0x004C, 4.0f, "M0,0 L0,6 L4,6" },
	{ 0x004D, 5.0f, "M0,6 L0,1.4 Q0,0 1.25,0 Q2.5,0 2.5,1.4 L2.5,4.2 M2.5,1.4 Q2.5,0 3.75,0 Q5,0 5,1.4 L5,6" },
	{ 0x004E, 4.0f, "M0,6 L0,0 L4,6 L4,0" },
	{ 0x004F, 4.0f, "M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z" },
	{ 0x0050, 4.0f, "M0,6 L0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3" },
	{ 0x0051, 4.0f, "M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z M2.6,4.4 L4.3,6.2" },
	{ 0x0052, 4.0f, "M0,6 L0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3 M2.4,3 L4,6" },
	{ 0x0053, 4.0f, "M4,0 L1.4,0 Q0,0 0,1.5 Q0,3 1.5,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.6,6 L0,6" },
	{ 0x0054, 4.0f, "M0,0 L4,0 M2,0 L2,6" },
	{ 0x0055, 4.0f, "M0,0 L0,4 Q0,6 2,6 Q4,6 4,4 L4,0" },
	{ 0x0056, 4.0f, "M0,0 L0,3.4 L2,6 L4,3.4 L4,0" },
	{ 0x0057, 5.0f, "M0,0 L0,4.6 Q0,6 1.25,6 Q2.5,6 2.5,4.6 L2.5,1.8 M2.5,4.6 Q2.5,6 3.75,6 Q5,6 5,4.6 L5,0" },
	{ 0x0058, 4.0f, "M0,0 L4,6 M4,0 L0,6" },
	{ 0x0059, 4.0f, "M0,0 L0,1.4 Q0,3 2,3 Q4,3 4,1.4 L4,0 M2,3 L2,6" },
	{ 0x005A, 4.0f, "M0,0 L4,0 L0,6 L4,6" },
	{ 0x0030, 4.0f, "M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z" },
	{ 0x0031, 1.2f, "M0,1 L1.2,0 L1.2,6" },
	{ 0x0032, 4.0f, "M0,0 L2.5,0 Q4,0 4,1.5 Q4,3 2.5,3 L1.5,3 Q0,3 0,4.5 L0,6 L4,6" },
	{ 0x0033, 4.0f, "M0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L1.2,3 M2.8,3 Q4,3 4,4.5 Q4,6 2.8,6 L0,6" },
	{ 0x0034, 4.0f, "M3,6 L3,0 L0,4 L4,4" },
	{ 0x0035, 4.0f, "M4,0 L0,0 L0,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.5,6 L0,6" },
	{ 0x0036, 4.0f, "M3.6,0 L1.6,0 Q0,0 0,1.6 L0,4.5 Q0,6 1.5,6 L2.5,6 Q4,6 4,4.5 Q4,3 2.5,3 L0,3" },
	{ 0x0037, 4.0f, "M0,0 L4,0 L1.6,6" },
	{ 0x0038, 4.0f, "M1.5,3 Q0,3 0,1.5 Q0,0 1.5,0 L2.5,0 Q4,0 4,1.5 Q4,3 2.5,3 Z M1.5,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.5,6 L1.5,6 Q0,6 0,4.5 Q0,3 1.5,3" },
	{ 0x0039, 4.0f, "M0.4,6 L2.4,6 Q4,6 4,4.4 L4,1.5 Q4,0 2.5,0 L1.5,0 Q0,0 0,1.5 Q0,3 1.5,3 L4,3" },
	{ 0x002E, 0.0f, "M0,6 L0,6.01" },
	{ 0x002C, 0.0f, "M0,6 L-0.4,7" },
	{ 0x003A, 0.0f, "M0,2 L0,2.01 M0,5.5 L0,5.51" },
	{ 0x00B7, 0.0f, "M0,3 L0,3.01" },
	{ 0x2022, 0.0f, "M0,3 L0,3.01" },
	{ 0x25CF, 1.6f, "M0.8,3 L0.8,3.01" },
	{ 0x0025, 4.0f, "M0,0 L1.1,0 L1.1,1.3 L0,1.3 Z M4,0 L0,6 M2.9,4.7 L4,4.7 L4,6 L2.9,6 Z" },
	{ 0x002D, 3.0f, "M0,3 L3,3" },
	{ 0x2212, 3.0f, "M0,3 L3,3" },
	{ 0x2013, 3.0f, "M0,3 L3,3" },
	{ 0x2014, 4.0f, "M0,3 L4,3" },
	{ 0x002B, 3.0f, "M0,3 L3,3 M1.5,1.5 L1.5,4.5" },
	{ 0x00D7, 3.0f, "M0,1.6 L3,4.6 M3,1.6 L0,4.6" },
	{ 0x002F, 3.0f, "M0,6 L3,0" },
	{ 0x003D, 3.0f, "M0,2 L3,2 M0,4 L3,4" },
	{ 0x2192, 4.4f, "M0,3 L4.4,3 M2.8,1.4 L4.4,3 L2.8,4.6" },
	{ 0x2193, 3.0f, "M1.5,0 L1.5,6 M0,4.5 L1.5,6 L3,4.5" },
	{ 0x2191, 3.0f, "M1.5,6 L1.5,0 M0,1.5 L1.5,0 L3,1.5" },
	{ 0x0023, 3.6f, "M1.2,0.6 L0.8,5.4 M2.8,0.6 L2.4,5.4 M0,2 L3.6,2 M0,4 L3.6,4" },
	{ 0x2039, 2.2f, "M2.2,1 L0,3 L2.2,5" },
	{ 0x203A, 2.2f, "M0,1 L2.2,3 L0,5" },
	{ 0x2304, 3.4f, "M0,2.4 L1.7,4 L3.4,2.4" },
	{ 0x2506, 0.0f, "M0,0 L0,1.4 M0,2.3 L0,3.7 M0,4.6 L0,6" },
	{ 0x0027, 0.0f, "M0,0 L0,1.6" },
	{ 0x0028, 1.2f, "M1.2,0 Q0,1 0,3 Q0,5 1.2,6" },
	{ 0x0029, 1.2f, "M0,0 Q1.2,1 1.2,3 Q1.2,5 0,6" },
};

struct Cmd
{
	char op;               // 'M', 'L', 'Q', 'Z'
	float x1, y1, x2, y2;  // Q uses both points, M and L the first
};

struct Glyph
{
	float advance = 0.f;
	std::vector<Cmd> cmds;
};

std::vector<Cmd> parse (const char* s)
{
	std::vector<Cmd> out;
	const char* p = s;
	auto number = [&p] () {
		char* end = nullptr;
		const float v = std::strtof (p, &end);
		p = end;
		while (*p == ',' || *p == ' ')
			++p;
		return v;
	};
	while (*p)
	{
		const char op = *p++;
		while (*p == ' ')
			++p;
		Cmd c { op, 0.f, 0.f, 0.f, 0.f };
		if (op == 'M' || op == 'L')
		{
			c.x1 = number ();
			c.y1 = number ();
		}
		else if (op == 'Q')
		{
			c.x1 = number (); c.y1 = number ();
			c.x2 = number (); c.y2 = number ();
		}
		out.push_back (c);
		while (*p == ' ')
			++p;
	}
	return out;
}

const std::unordered_map<char32_t, Glyph>& glyphs ()
{
	static const std::unordered_map<char32_t, Glyph> table = [] {
		std::unordered_map<char32_t, Glyph> t;
		for (const auto& g : kGlyphs)
			t[g.code] = Glyph { g.advance, parse (g.path) };
		return t;
	} ();
	return table;
}

/** UTF-8 to code points, upper-casing ASCII on the way. Malformed bytes
    become U+FFFD, which has no glyph and draws as a dot. */
std::u32string decode (const std::string& s)
{
	std::u32string out;
	for (size_t i = 0; i < s.size ();)
	{
		const unsigned char c = static_cast<unsigned char> (s[i]);
		char32_t cp = 0xFFFD;
		int len = 1;
		if (c < 0x80)                    { cp = c; }
		else if ((c >> 5) == 0x6 && i + 1 < s.size ())
		{ cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); len = 2; }
		else if ((c >> 4) == 0xE && i + 2 < s.size ())
		{ cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); len = 3; }
		else if ((c >> 3) == 0x1E && i + 3 < s.size ())
		{ cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); len = 4; }
		if (cp >= 'a' && cp <= 'z')
			cp -= 32;
		out.push_back (cp);
		i += len;
	}
	return out;
}

const Glyph& glyphFor (char32_t cp)
{
	const auto& t = glyphs ();
	auto it = t.find (cp);
	if (it != t.end ())
		return it->second;
	return t.at (0x00B7);   // middle dot
}

/** Width in grid units, as rbConvert lays a run out: each word is its
    glyphs, advance + stroke + track apiece, less the last track; words are
    kWordGap apart. */
double unitsWidth (const std::u32string& text, double w)
{
	double total = 0., word = 0.;
	bool inWord = false, any = false;
	for (char32_t cp : text)
	{
		if (cp == U' ')
		{
			if (inWord)
			{
				total += (any ? kWordGap : 0.) + (word - kTrack);
				any = true;
			}
			inWord = false;
			word = 0.;
			continue;
		}
		inWord = true;
		word += glyphFor (cp).advance + w + kTrack;
	}
	if (inWord)
		total += (any ? kWordGap : 0.) + (word - kTrack);
	return total > 0. ? total : 0.;
}

/** Add the glyphs to `path`, the run's top-left at `origin`, `s` px per unit. */
void buildPath (CGraphicsPath* path, const std::u32string& text, const CPoint& origin,
                double s, double w)
{
	double x = 0.;
	bool prevSpace = true, started = false;
	for (char32_t cp : text)
	{
		if (cp == U' ')
		{
			prevSpace = true;
			continue;
		}
		if (prevSpace && started)
			x += kWordGap - kTrack;     // the word gap replaces the last track
		prevSpace = false;
		started = true;

		const Glyph& g = glyphFor (cp);
		const double ox = origin.x + (x + w / 2.) * s;
		const double oy = origin.y + (w / 2.) * s;
		auto P = [&] (double gx, double gy) { return CPoint (ox + gx * s, oy + gy * s); };
		CPoint cur, start;
		for (const Cmd& c : g.cmds)
		{
			switch (c.op)
			{
				case 'M':
					cur = start = P (c.x1, c.y1);
					path->beginSubpath (cur);
					break;
				case 'L':
					cur = P (c.x1, c.y1);
					path->addLine (cur);
					break;
				case 'Q':
				{
					// quadratic to cubic: c1 = p0 + 2/3 (q - p0), c2 = p2 + 2/3 (q - p2)
					const CPoint q = P (c.x1, c.y1), p2 = P (c.x2, c.y2);
					const CPoint c1 (cur.x + 2. / 3. * (q.x - cur.x), cur.y + 2. / 3. * (q.y - cur.y));
					const CPoint c2 (p2.x + 2. / 3. * (q.x - p2.x), p2.y + 2. / 3. * (q.y - p2.y));
					path->addBezierCurve (c1, c2, p2);
					cur = p2;
					break;
				}
				case 'Z':
					path->closeSubpath ();
					cur = start;
					break;
				default:
					break;
			}
		}
		x += g.advance + w + kTrack;
	}
}

void stroke (CDrawContext* context, CGraphicsPath* path, double width, const CColor& colour)
{
	context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	context->setLineWidth (width);
	context->setFrameColor (colour);
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

} // anonymous namespace

//------------------------------------------------------------------------
double textWidth (const std::string& utf8, double size, bool bold)
{
	const double s = size * kCapPerSize / 6.;
	return unitsWidth (decode (utf8), bold ? 1.25 : 0.95) * s;
}

//------------------------------------------------------------------------
void drawText (CDrawContext* context, const std::string& utf8, const CPoint& anchor,
               double size, bool bold, const CColor& colour, Align align)
{
	if (!context || utf8.empty ())
		return;
	const std::u32string text = decode (utf8);
	const double cap = size * kCapPerSize, s = cap / 6., w = bold ? 1.25 : 0.95;
	const double width = unitsWidth (text, w) * s;
	double left = anchor.x;
	if (align == Align::Centre) left -= width / 2.;
	else if (align == Align::Right) left -= width;

	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	buildPath (path, text, CPoint (left, anchor.y - cap), s, w);
	context->saveGlobalState ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	stroke (context, path, w * s, colour);
	context->restoreGlobalState ();
}

//------------------------------------------------------------------------
void drawWordmark (CDrawContext* context, const std::string& utf8, const CPoint& topLeft,
                   double cap, const CColor& colour, const CColor& cut)
{
	if (!context || utf8.empty ())
		return;
	const double s = cap / 6., w = 1.7;
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	buildPath (path, decode (utf8), topLeft, s, w);
	context->saveGlobalState ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	stroke (context, path, w * s, colour);
	stroke (context, path, 0.32 * s, cut);
	context->restoreGlobalState ();
}

} // namespace SciFi
} // namespace SampleDrum
