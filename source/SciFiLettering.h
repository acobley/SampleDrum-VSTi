//------------------------------------------------------------------------
// SampleDrum - the 70s sci-fi lettering, drawn as stroked paths
//
// Andy's own hand-drawn face (tools/polivoks/rbfont.js; copied from ForTran-VSTi, only the namespace
// and two glyphs - # and the up arrow - added): heavy,
// wide, rounded-square capitals and figures on a 4 x 6 grid, stroked with
// round caps and joins. There is no font file and no font licence - every
// glyph is a path string in SciFiLettering.cpp.
//
// Sizes are given as the CSS font-size the panel mock-up used, so the C++
// and the HTML agree: cap height is 0.64 x size, the stroke is 1.25 grid
// units bold or 0.95 regular, letters are 1.05 units apart plus the stroke,
// and words 4.4 units apart. Text is anchored on its BASELINE, as an SVG
// <text> is.
//
// Lower case is drawn in capitals. A character with no glyph draws as a dot.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cpoint.h"

#include <string>

namespace VSTGUI { class CDrawContext; }

namespace SampleDrum {
namespace SciFi {

enum class Align { Left, Centre, Right };

/** Width in pixels of `utf8` at CSS font-size `size`. Shared by drawing and
    layout, so a label that is measured to fit is the label that is drawn. */
double textWidth (const std::string& utf8, double size, bool bold);

/** Draw `utf8` with its baseline at `anchor.y`; `anchor.x` is the left,
    centre or right end according to `align`. */
void drawText (VSTGUI::CDrawContext* context, const std::string& utf8,
               const VSTGUI::CPoint& anchor, double size, bool bold,
               const VSTGUI::CColor& colour, Align align = Align::Left);

/** The wordmark: heavier strokes (1.7 units) at cap height `cap`, with a
    thin inline cut in `cut` through every stroke. */
void drawWordmark (VSTGUI::CDrawContext* context, const std::string& utf8,
                   const VSTGUI::CPoint& topLeft, double cap,
                   const VSTGUI::CColor& colour, const VSTGUI::CColor& cut);

} // namespace SciFi
} // namespace SampleDrum
