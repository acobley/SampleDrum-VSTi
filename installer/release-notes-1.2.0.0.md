# SampleDrum 1.2.0.0

A sample-based drum instrument for macOS, as a **VST3** and an
**Audio Unit**.

## What changed since 1.1.0.0

- **Every drum has its own level, SOLO and MUTE**, at the foot of its
  channel strip, as on a mixing desk.
  - **Drum level**, −40 to +12 dB, sets the whole drum *after* its four cell
    levels, so you can balance the kick against the kit without upsetting
    the balance between its soft and hard hits. The bottom of its travel is
    silence ("OFF"). Option-click or double-click for 0 dB.
  - **SOLO** (the amber lamp) and **MUTE** (the red lamp) are switches: click
    to turn on, click again to turn off. Solo several drums and they all
    play. **Mute wins**: a drum that is soloed and muted stays silent.
    Drums silenced by someone else's solo show their drum level dimmed.
  - Mutes and solos take **5 ms** - fast enough to land on the beat, slow
    enough not to click - and all of them, levels included, are
    **automatable**.
- **The panel is 60 pixels taller** to make room; the output meter grew
  with it.
- **56 factory kits** - 808, 909, acoustic, boomy, crisp, distorted, dull,
  lo-fi, metallic, muffled, punchy, tight, vintage drum machine and warm,
  four of each - every cell filled, samples included, so they play on any
  Mac. They take about 900 MB; untick the choice if you don't want them. They appear in your host's preset
  browser for both the VST3 and the Audio Unit, installed as their own
  choice in the installer.
- **macOS 12 or later is now required** (1.1.0.0 ran on 10.13). Current
  Xcode no longer builds for anything older.

**What to expect.** The 24 new controls are new parameters. A project saved
with 1.1.0.0 opens with every drum at 0 dB, nothing soloed and nothing
muted, and sounds exactly as it did.

**A muted drum is still played, silently.** Its **HIT** lamp still flashes
and its cells still light, so you can see the MIDI arriving; unmute it in
the middle of a cymbal and the cymbal comes back where it has got to. It
still uses voices, and a muted closed hi-hat still cuts the open one - so a
soloed open hat is choked exactly as it is in the full mix. Clicking a cell
of a muted drum plays nothing: the click goes through the strip, as a hit
would.

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
  of its travel is silence ("OFF"). Each drum's strip ends in a **drum
  level**, **SOLO** and **MUTE**. **Output Trim** sets the whole kit, after
  the **output isolator** (Bass and Treble, each from kill to +6 dB).
- **Closed hi-hat cuts open hi-hat**, as a pedal does.

Samples play once, to the end, at the file's own sample rate converted to the
host's; 32 voices.

## Installing

Download `SampleDrum-1.2.0.0.pkg` and open it. The installer offers each
format separately:

    /Library/Audio/Plug-Ins/VST3/SampleDrum.vst3
    /Library/Audio/Plug-Ins/Components/SampleDrum.component

and the factory kits, for both formats, as a third choice:

    /Library/Audio/Presets/AE Cobley/SampleDrum

Quit your DAW first - a running host holds the old copy open. Signed with a
Developer ID and notarised by Apple, so it installs without any
Privacy & Security detour.

Requires **macOS 12 (Monterey) or later**; the installer refuses anything older.

## Three things that will look like bugs and are not

* **Samples play at their own level, and hits add up.** A kick and a snare
  each normalised to 0 dBFS, struck together, reach **+6 dBFS** and clip —
  the **OVER** lamp will tell you. That is how drum samplers behave, and no
  headroom is hidden to disguise it. Turn **Output Trim** down (−6 dB covers
  two coincident full-scale hits), or use samples that are not normalised to
  full scale. Boosting **Bass** or **Treble** adds level too.
* **Velocity chooses the row, not the loudness.** Within a row a soft hit and
  a hard hit play at the same level; the four rows are where the dynamics
  live. The level knobs set how loud each cell is, and each drum's level
  how loud the drum is.
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
sudo rm -rf "/Library/Audio/Presets/AE Cobley/SampleDrum"
```

Your projects keep their samples; nothing else is installed.

## Checksum

```
SHA-256: <paste from: shasum -a 256 installer/SampleDrum-1.2.0.0.pkg>
```

---

Copyright 2026 A. E. Cobley. CC BY-SA 4.0.
VST is a trademark of Steinberg Media Technologies GmbH.
