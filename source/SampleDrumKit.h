//------------------------------------------------------------------------
// SampleDrum - the shape of the kit: the grid, the drum map, the
// velocity bands and the choke
//
// SDK-FREE AND HEADER-ONLY, and the single copy of every rule that says
// which cell a note plays. The DSP calls these to decide what to play;
// the panel calls THE SAME FUNCTIONS to label its columns and rows; the
// tests call them to pin every boundary. A second copy of any of this -
// the panel printing "96-127" from its own table while the DSP split at
// 95 - is the panel lying about what you will hear.
//
// THE GRID
//
//     8 columns x 4 rows. A COLUMN IS A DRUM: one MIDI note, from the
//     General MIDI percussion map. A ROW IS A VELOCITY BAND, and the
//     harder the hit the LOWER the row - row 0 at the top is the softest
//     band, row 3 at the bottom the hardest. Each of the 32 cells holds
//     one .wav.
//
//     Cell index = column + row * kColumns, so cell 0 is the top-left and
//     the indices run across the panel a row at a time - the order the
//     state stream saves them in. THAT ORDER IS PERMANENT once a build
//     ships: a project saves 32 paths by position.
//------------------------------------------------------------------------

#pragma once

#include <cstdio>

namespace SampleDrum {

constexpr int kColumns   = 8;
constexpr int kRows      = 4;
constexpr int kCellCount = kColumns * kRows;

inline constexpr bool isCellIndex (int cell) { return cell >= 0 && cell < kCellCount; }
inline constexpr int cellIndex (int column, int row) { return column + row * kColumns; }
inline constexpr int columnOfCell (int cell) { return cell % kColumns; }
inline constexpr int rowOfCell (int cell) { return cell / kColumns; }

//------------------------------------------------------------------------
// The drum map
//------------------------------------------------------------------------

/** One column: the note that triggers it and what the panel calls it. */
struct DrumColumn
{
	int         note;
	const char* name;       // never null
};

/** THE GENERAL MIDI PERCUSSION MAP, eight of it, left to right - the
    DEFAULT note of each column and the drum it is named after.

    Every drum sequencer and e-kit already sends these numbers, so a new
    instance plays any GM pattern with no setup. Each column's note is
    now a parameter (kColumnNoteBase + column, the box at the top of the
    column) and these are its defaults; the NAMES stay fixed, because a
    column is still "the kick column" when it is moved to note 35.

    42 AND 46 ARE CHOKED - see chokes() below. They are next to each other
    on the panel for that reason. */
inline constexpr DrumColumn kDrumMap[kColumns] = {
	{ 36, "Kick"      },
	{ 38, "Snare"     },
	{ 39, "Clap"      },
	{ 42, "Closed HH" },
	{ 46, "Open HH"   },
	{ 45, "Low Tom"   },
	{ 50, "High Tom"  },
	{ 49, "Crash"     },
};

/** Which column a note plays IN THE DEFAULT MAP, or -1. The DSP does not
    use this - it uses the columns' current notes - but the defaults are
    worth pinning in the tests on their own. */
inline constexpr int defaultColumnForNote (int note)
{
	for (int c = 0; c < kColumns; ++c)
		if (kDrumMap[c].note == note)
			return c;
	return -1;
}

//------------------------------------------------------------------------
// Note numbers and names
//------------------------------------------------------------------------

constexpr int kMinNote = 0;
constexpr int kMaxNote = 127;

/** The octave convention: MIDI 60 is C3 and 36 - the GM kick - is C1,
    which is what Cubase, Logic and most drum-map tables print. (Some
    hosts call 60 "C4"; the NUMBER is shown first so nobody has to know
    which convention is in use.) */
constexpr int kOctaveOfNoteZero = -2;

/** "36 C1", "42 F#1", "0 C-2". ONE copy of the formatting, called by the
    host's parameter text and by the panel's note box, so the two can never
    show a column's note differently.

    THE NUMBER COMES FIRST, deliberately: the host turns this text back
    into a value by reading the leading number (the validator checks that
    round trip), and "C1 (36)" would not survive it. */
inline void formatNote (int note, char* out, int size)
{
	static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F",
	                                        "F#", "G", "G#", "A", "A#", "B" };
	if (out == nullptr || size <= 0)
		return;
	if (note < kMinNote || note > kMaxNote)
	{
		std::snprintf (out, static_cast<size_t> (size), "%d", note);
		return;
	}
	std::snprintf (out, static_cast<size_t> (size), "%d %s%d", note, kNames[note % 12],
	               note / 12 + kOctaveOfNoteZero);
}

//------------------------------------------------------------------------
// The velocity bands
//------------------------------------------------------------------------

/** MIDI velocity 1..127 to a row, 0 (top, softest) .. 3 (bottom, hardest).

    FOUR EQUAL BANDS OF 32, with the first one short by one because
    velocity 0 is not a note:

        row 0    1 -  31
        row 1   32 -  63
        row 2   64 -  95
        row 3   96 - 127

    Integer division by 32 gives exactly that. Anything below 1 is -1, and
    the caller plays nothing: a MIDI note-on at velocity 0 IS a note-off,
    and hosts that forward it rather than converting it must not get a
    whisper-quiet hit. Anything above 127 is clamped into the top band. */
inline constexpr int rowForVelocity (int velocity127)
{
	if (velocity127 < 1)
		return -1;
	const int row = velocity127 / 32;
	return (row >= kRows) ? kRows - 1 : row;
}

/** The bottom and top of a row's band, for the panel's row labels. */
inline constexpr int bandLow (int row)  { return (row <= 0) ? 1 : row * 32; }
inline constexpr int bandHigh (int row) { return (row >= kRows - 1) ? 127 : row * 32 + 31; }

/** VST3's velocity is 0..1; MIDI's is 0..127. Rounded, not truncated:
    a host that sent 96 as 96/127 = 0.7559 must come back as 96, and
    truncating 95.99999 would drop it into the band above's neighbour. */
inline int velocityTo127 (float velocity01)
{
	if (!(velocity01 > 0.f))
		return 0;
	const int v = static_cast<int> (velocity01 * 127.f + 0.5f);
	return (v > 127) ? 127 : v;
}

//------------------------------------------------------------------------
/** Which cell a hit in `column` at `row` actually plays, given which
    cells hold a loaded sample (`loaded` is a bit per cell index).

    THE ASKED-FOR ROW WHEN IT IS LOADED. Otherwise the NEAREST LOADED ROW
    IN THE SAME COLUMN, so a column with a single sample in it plays that
    sample at every velocity - which is what anyone who has dropped one
    kick on the grid expects. A TIE goes to the SOFTER row (the one
    above): when a fallback is a guess, the guess that errs quiet is the
    one that does not make somebody jump.

    Returns -1 when the column is empty, and the hit plays nothing. */
inline constexpr int chooseCell (int column, int row, unsigned int loaded)
{
	if (column < 0 || column >= kColumns || row < 0 || row >= kRows)
		return -1;

	for (int distance = 0; distance < kRows; ++distance)
	{
		const int softer = row - distance;
		if (softer >= 0 && (loaded & (1u << cellIndex (column, softer))) != 0u)
			return cellIndex (column, softer);

		const int harder = row + distance;
		if (harder < kRows && (loaded & (1u << cellIndex (column, harder))) != 0u)
			return cellIndex (column, harder);
	}
	return -1;
}

static_assert (kCellCount <= 32, "chooseCell's loaded mask is one bit per cell");

//------------------------------------------------------------------------
// The choke
//------------------------------------------------------------------------

/** True when a hit on `hitColumn` must cut off what `playingColumn` is
    playing.

    ONE PAIR, ONE DIRECTION: the closed hi-hat cuts the open one, as the
    pedal does on a real kit. BY COLUMN, not by note: the hat columns
    choke each other whatever notes they have been moved to. Not the other way round - an open hat struck
    while a closed one rings is two sounds, and a closed hat is too short
    for the difference to be heard anyway. A column never chokes itself:
    a second hit on the same drum OVERLAPS the first. */
inline constexpr bool chokes (int hitColumn, int playingColumn)
{
	return hitColumn == 3 && playingColumn == 4;
}

static_assert (kDrumMap[3].note == 42 && kDrumMap[4].note == 46,
               "chokes() names the hi-hat columns by position; move them together");

//------------------------------------------------------------------------
} // namespace SampleDrum
