# SampleDrum 1.0.0.0

A sample-based drum instrument for macOS, as a **VST3** and an
**Audio Unit**. The first release.

## What it is

An **8 × 4 grid of cells**, each holding one `.wav` you drop on it from the
Finder.

- **A column is a drum** — one MIDI note. The defaults are the General MIDI
  map (Kick 36, Snare 38, Clap 39, Closed HH 42, Open HH 46, Low Tom 45,
  High Tom 50, Crash 49), so any GM pattern plays the right drum with no
  setup. The note box at the top of each column changes it: click an end to
  step a semitone (Shift: an octave), drag, or scroll; Option-click goes back
  to the default. Two columns on one note both play, and both boxes turn
  amber.
- **A row is a velocity band** — 1–31, 32–63, 64–95 and 96–127, top to bottom,
  so the harder the hit, the lower the row. An empty cell borrows the nearest
  loaded cell in the same column, so one sample in a column plays at every
  velocity.
- **A level bar under every cell**, −40 to +12 dB, automatable; the bottom of
  the bar is silence. **Output Trim** sets the whole kit.
- **Closed hi-hat cuts open hi-hat**, as a pedal does.
- Click a cell to hear it. **Drag a cell onto another** to move it (onto a
  loaded cell, the two swap); hold **Option** or **Control** at the drop to
  copy. **Right-click** a cell to clear it.

Samples play once, to the end, at the file's own sample rate converted to the
host's; 32 voices.

## Installing

Download `SampleDrum-1.0.0.0.pkg` and open it. The installer offers each
format separately:

    /Library/Audio/Plug-Ins/VST3/SampleDrum.vst3
    /Library/Audio/Plug-Ins/Components/SampleDrum.component

Quit your DAW first - a running host holds the old copy open. Signed with a
Developer ID and notarised by Apple, so it installs without any
Privacy & Security detour.

Requires macOS 10.13 or later.

## Three things that will look like bugs and are not

* **Samples play at their own level, and hits add up.** A kick and a snare
  each normalised to 0 dBFS, struck together, reach **+6 dBFS** and clip.
  That is how drum samplers behave, and no headroom is hidden to disguise
  it. Turn **Output Trim** down (−6 dB covers two coincident full-scale
  hits), or use samples that are not normalised to full scale.
* **Velocity chooses the row, not the loudness.** Within a row a soft hit and
  a hard hit play at the same level; the four rows are where the dynamics
  live. The level bar sets how loud each cell is.
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
SHA-256: 93a38622e1028b43065cd536398000a3202c7e31d955dd796726f06ec02f39ba
```

---

Copyright 2026 A. E. Cobley. CC BY-SA 4.0.
VST is a trademark of Steinberg Media Technologies GmbH.
