# SampleDrum designs

Polivoks black panel shared with R2Synth, ForTran and SpyBand: matte black, white silkscreen, outlined section
titles, knurled knobs, slide faders, red jewel lamps, small OLEDs behind glass, and 70s sci-fi lettering
throughout (SAMPLE in white, DRUM in the accent). All three have the **nearly-new** finish: faint haze,
a few hairlines, a little dust on the glass.

The isolator follows PORTING-NOTES §14 (branch `output-isolator`): it comes after the voices and before
Output Trim, gains run from kill to +6 dB, Bass Freq is 50–500 Hz and Treble Freq is 1.5–12 kHz. The demo
settings are `ISO` in `sd.js`: bass +4 dB at 200 Hz, treble −18 dB at 3.50 kHz.

- `SampleDrum-Polivoks-1-KitGrid-NearlyNew.png`: the 8 × 4 grid as in the plug-in. Each cell is a colour OLED
  with its file name and waveform, coloured by drum family (kick amber, snare/clap blue, hats mint, toms violet,
  crash white). An LED level ladder under each cell, with 0 dB marked and the part above 0 dB in amber. The note
  box ("‹ 36 C1 ›") heads each column. Shows sounding cells with a playhead, empty cells naming the row they
  borrow from, and one "file not found" cell. An **Isolator** section (a response screen showing the low, mid and high bands with both crossovers, and Bass, Bass Freq,
  Treble and Treble Freq knobs) above Output (trim, stereo meter, over and bypass lamps, hat choke).
- `SampleDrum-Polivoks-2-ChannelStrips-NearlyNew.png`: each drum is a mixer-style strip. Note box at the top,
  then the four velocity cells soft to hard, each with a level knob and a dB readout, and a hit lamp at the
  bottom. All screens amber. Master section: last-hit scope, the **Isolator** (response screen, Bass and Treble level and frequency knobs),
  then output trim, meter, choke and bypass.
- `SampleDrum-Polivoks-3-PadsScope-NearlyNew.png`: display first. A large white OLED shows the cell last
  clicked or hit. Below it, the grid as rubber-capped pads with a red lamp strip that lights on each hit, a
  silkscreened waveform, and a mini slide fader for each cell's level. The top row follows the signal: Sample, then **Isolator**, then Output. Monochrome white screens, red accents.

Sources are in `source/` (`pv.js` is ForTran's kit with the wordmark words, a voice-meter segment count and the
divider position made parameters; `sd.js` adds the kit model, waveforms, cells, note box and level controls;
`rbfont.js` gains ↑ and #):

    python3 render.py d1 d2 d3
    SEED=7  python3 weather.py d1-clean.png d1-layout.json out 0 SampleDrum-Polivoks-1-KitGrid-NearlyNew.png
    SEED=14 python3 weather.py d2-clean.png d2-layout.json out 0 SampleDrum-Polivoks-2-ChannelStrips-NearlyNew.png
    SEED=21 python3 weather.py d3-clean.png d3-layout.json out 0 SampleDrum-Polivoks-3-PadsScope-NearlyNew.png

Mode `0` (nearly new) is new in this copy of `weather.py`. Modes 1 to 3 (studio-worn, road case, barn find)
still work.
