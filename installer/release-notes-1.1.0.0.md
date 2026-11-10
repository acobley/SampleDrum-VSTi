# SampleDrum 1.1.0.0

A sample-based drum instrument for macOS, as a **VST3** and an
**Audio Unit**.

## What changed since 1.0.1.0

- **A new panel.** SampleDrum now has a black instrument panel with
  knurled knobs, red jewel lamps, amber screens behind glass and 70s sci-fi
  lettering, lightly worn. Each drum is a **channel strip**: its note box at
  the top, its four velocity cells soft to hard, a **level knob** under each
  cell with its value beside it, and a **HIT** lamp at the foot. Everything
  you could do before still works the same way: drop a `.wav` on a cell,
  click to hear it, drag to move (Option or Control to copy), right-click
  to clear.
- **Cells show their sample.** Each loaded cell draws the file's waveform
  and name; a struck cell lights up with a playhead running across the
  waveform. An empty cell says which row a hit there plays instead
  ("PLAYS 64-95 ↑"), so the fallback is no longer a guess.
- **An output isolator**, as on a DJ mixer: **Bass** and **Treble**, each
  from **kill** up to +6 dB, with their crossovers (**Bass Freq** 50–500 Hz,
  default 200 Hz; **Treble Freq** 1.5–12 kHz, default 3.5 kHz). It splits the
  kit into three bands that add back up exactly, so at its defaults it is
  **bit-for-bit transparent**; kill the bass and the kick's body goes while
  its click stays, kill the treble and the hats go. It sits after the
  voices and before Output Trim. A screen shows its response, live.
- **A master strip**: the **last hit** (the drum, its velocity band, the
  file, the velocity and the waveform), the isolator, **Output Trim**, a
  **peak meter** with an **OVER** lamp that lights when the output passes
  0 dBFS, and **BYPASS**. A voice meter runs along the top.

**What to expect.** The four isolator controls are new, automatable
parameters. A project saved with 1.0.x opens with the isolator flat and
sounds exactly as it did; nothing else about kits or saved projects has
changed. The cell levels are the same parameters as before, now on knobs:
drag up and down (Shift for fine), use the wheel, and Option-click or
double-click for 0 dB.

**One colour has changed.** When two columns are set to the same note
(both play), their note boxes now turn **red**; amber is the screens'
normal colour.

## What it is

An **8 × 4 grid of cells**, each holding one `.wav` you drop on it from the
Finder.

- **A column is a drum** — one MIDI note. The defaults are the General MIDI
  map (Kick 36, Snare 38, Clap 39, Closed HH 42, Open HH 46, Low Tom 45,
  High Tom 50, Crash 49), so any GM pattern plays the right drum with no
  setup. The note box at the top of each column changes it: click an end to
  step a semitone (Shift: an octave), drag, or scroll; Option-click goes back
  to the default.
- **A row is a velocity band** — 1–31, 32–63, 64–95 and 96–127, top to bottom,
  so the harder the hit, the lower the row. An empty cell borrows the nearest
  loaded cell in the same column, so one sample in a column plays at every
  velocity.
- **A level knob under every cell**, −40 to +12 dB, automatable; the bottom
  of its travel is silence ("OFF"). **Output Trim** sets the whole kit.
- **Closed hi-hat cuts open hi-hat**, as a pedal does.

Samples play once, to the end, at the file's own sample rate converted to the
host's; 32 voices.

## Installing

Download `SampleDrum-1.1.0.0.pkg` and open it. The installer offers each
format separately:

    /Library/Audio/Plug-Ins/VST3/SampleDrum.vst3
    /Library/Audio/Plug-Ins/Components/SampleDrum.component

Quit your DAW first - a running host holds the old copy open. Signed with a
Developer ID and notarised by Apple, so it installs without any
Privacy & Security detour.

Requires macOS 10.13 or later.

## Three things that will look like bugs and are not

* **Samples play at their own level, and hits add up.** A kick and a snare
  each normalised to 0 dBFS, struck together, reach **+6 dBFS** and clip —
  the **OVER** lamp will tell you. That is how drum samplers behave, and no
  headroom is hidden to disguise it. Turn **Output Trim** down (−6 dB covers
  two coincident full-scale hits), or use samples that are not normalised to
  full scale. Boosting **Bass** or **Treble** adds level too.
* **Velocity chooses the row, not the loudness.** Within a row a soft hit and
  a hard hit play at the same level; the four rows are where the dynamics
  live. The level knobs set how loud each cell is.
* **Your samples are saved inside the project.** Every file you drop is
  embedded byte for byte, so the project opens on another Mac that does not
  have them — and project files grow by the size of the samples. The
  embedded copy wins: if you edit a sample on disk, drop it on the cell again
  to bring the new version in.

## Known gaps

* **WAV only**, up to 20 seconds per file (PCM 8/16/24/32-bit, 32/64-bit
  float, any channel count).
* **One stereo output.** No per-drum outputs yet.
* **No pan or tuning per cell.**
* **Note-off is ignored** — every hit plays to the end of its sample.
* **A cell dragged from another SampleDrum window** is read from disk, not
  from the other window's project, so on a Mac without the file it comes
  across as "file not found".

## Uninstalling

A `.pkg` never will, so:

```sh
sudo rm -rf /Library/Audio/Plug-Ins/VST3/SampleDrum.vst3
sudo rm -rf /Library/Audio/Plug-Ins/Components/SampleDrum.component
```

Your projects keep their samples; nothing else is installed.

## Checksum

```
SHA-256: 71c27f831cb4a3cb34a80e6562f0861ccd7b030834db6c412eb6fa4a95454906
```

---

Copyright 2026 A. E. Cobley. CC BY-SA 4.0.
VST is a trademark of Steinberg Media Technologies GmbH.
