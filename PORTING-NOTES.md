# SampleDrum — engineering notes

**SampleDrum is not a port.** It was scaffolded blank on 2026-09-21 from
`~/DXi-DEv/vst3-port-template`, with the non-editor source files taken
from FilterDrum's own blank scaffold (commit `b2e1890`) and renamed.

**What it is now (second commit, same day):** a sample-based drum
instrument. An **8 × 4 grid of cells**, each holding one `.wav` dropped on
it from the Finder, as Project6 does. **A column is a drum** — one MIDI
note from the General MIDI map. **A row is a velocity band** — the harder
the hit, the lower the row. A note-on plays the cell for its column and
velocity, once, to the end.

Everything in `PORTING-GUIDE.md` (audio-thread allocation, denormals,
64-bit hosts, bounded state, the release tail) applies to the first real
DSP here exactly as it does to a port. Read it before writing
`renderVoices()`.

This file outlives the session. Add to it as you go, not at the end.

---

## 1. The four decisions

| | Value | Why |
|---|---|---|
| Effect or instrument | **Instrument** (`aumu`, `PlugType::kInstrumentDrum`) | A drum sampler plays from notes. Sets the AU type code, the event input, and the absence of an audio input bus. The PlugType string is *not* permanent — `kInstrumentSampler` is a free change later. |
| Bus layout | **One stereo output, no audio input** (0 in / 2 out) | Simplest thing that validates. Per-pad aux outputs can be *appended* later as extra buses — `Project6-VSTi` is the worked example. |
| Name and codes | `SampleDrum` / `SmDr` / `AECo` | See §2. `SmDr` checked against every other `au-info.plist` in `~/DXi-DEv`: `FDrm`, `FTrn`, `Prj6`, `SDub`, `SpyB`, `VcFl`. No clash. |
| Editor now or later | **Later**, then **now** in the second commit | The scaffold shipped without one per porting-guide step 6. The grid needed drag-and-drop, so the editor arrived with the sample engine. **If the first build's validator run is noisy, bisect the editor out first:** make `createView` return `nullptr`. |

### The kit's rules (second commit) — all in `source/SampleDrumKit.h`

| | Decision | Why |
|---|---|---|
| Grid | 8 columns × 4 rows; cell = column + row × 8, **saved in that order** | The state stream saves 32 paths by position. That order is permanent once a build ships. |
| Drum map | **Fixed GM**: Kick 36, Snare 38, Clap 39, Closed HH 42, Open HH 46, Low Tom 45, High Tom 50, Crash 49 | Any GM pattern plays the right drum with no setup. An unmapped note is **ignored**, not folded onto a neighbour. Making notes editable later is an *append* to the parameter table. |
| Velocity → row | **Four equal bands**, top to bottom: 1–31, 32–63, 64–95, 96–127 | `velocity / 32`. Velocity 0 plays nothing (it is a note-off). |
| Empty cell | Falls back to the **nearest loaded row in the same column**; a tie goes to the **softer** row | One sample in a column plays at every velocity. Erring quiet on a guess. Never borrows from another column. |
| Playback | **One-shot**, unity gain (velocity picks the row only, it does not scale level); note-off ignored; a second hit on the same drum **overlaps** | What was asked for. |
| Choke | **Closed HH cuts open HH**, 5 ms fade; not the reverse; no column chokes itself | As a hi-hat pedal does. |
| Polyphony | 32 voices + 8 spares; the oldest is stolen with a 5 ms fade | The spares let a stolen voice fade instead of clicking. |
| Sample rate | Linear interpolation to the host rate; step exactly 1 when rates match | At equal rates the output is the file's own samples, bit for bit. |
| Files | WAV only: PCM 8/16/24/32, float 32/64, extensible, any channel count (mono duplicated); **20 s cap** | Reader lifted from Project6 without the tempo/MIDI parts. |
| Panel | Click a loaded cell = audition; **Option-click = empty it**; cells light while sounding and **flash on every hit** | A modifier for clearing, so a stray click cannot lose a sample. Hit counting so a hi-hat shorter than one redraw still shows. |

---

## 2. Permanent identity — **never change any of this**

Hosts store these in the project file. Change one after a build has
shipped and every existing session silently loses the plug-in.

| | Value | Where |
|---|---|---|
| Processor class UID | `0xAFAD783D, 0xFCC97C1E, 0x28EF80EF, 0xD4DC5FCF` | `source/SampleDrumIDs.h` |
| Controller class UID | `0xDBD88CFF, 0x07124209, 0x6131B114, 0x20CECD1D` | `source/SampleDrumIDs.h` |
| AU type | `aumu` | `resource/au-info.plist` |
| AU subtype | `SmDr` | `resource/au-info.plist` |
| AU manufacturer | `AECo` | `resource/au-info.plist` — shared across all these plug-ins |
| VST3 bundle id | `audio.sampledrum.vst3` | `CMakeLists.txt` |
| AU bundle id | `audio.sampledrum.audiounit` | `CMakeLists.txt` |

Both UIDs were generated fresh from `os.urandom` and grepped against
every other project's IDs header: not reused.

Vendor: `AE Cobley` (no full stops) in `version.h`, `PLUGIN_COMPANY` and
the AU name `AE Cobley: SampleDrum` — the three must match.

Version: `1.0.0.0` in `CMakeLists.txt` and `version.h`; `1.0.0` in the plist.

Validate with: `auval -v aumu SmDr AECo`

---

## 3. Why the DSP is SDK-free

`source/SampleDrumDsp.{h,cpp}`, `SampleDrumSample.{h,cpp}` (the WAV
reader) and `SampleDrumKit.h` (the grid rules) include nothing under
`pluginterfaces/`, `public.sdk/` or `vstgui/`, and must not acquire one.

1. **It is the only thing that executes during development.** The
   assistant session reaches this Mac through a Linux VM and cannot run
   cmake, Xcode, the SDK validator or `auval`. `tests/DspTests.cpp`
   compiles straight against `SampleDrumDsp.cpp` with plain `c++`, so the
   numbers are tested before anything is built. An SDK header in that
   file kills the suite.
2. **Processor and controller do not share memory.** Anything a panel
   displays that the DSP computes must come from one shared function both
   call. `dbToGain()` in `SampleDrumDsp.h` is one; **every rule in
   `SampleDrumKit.h`** is another — the panel's column and row labels are
   computed from `kDrumMap`, `bandLow()` and `bandHigh()`, the same code
   that decides which cell plays.

---

## 4. Measured output levels

    48 kHz, trim 0 dB (the default)
      empty grid, kick at velocity 127     -inf dBFS (exact silence)
      one full-scale hit                   -0.00 dBFS   (unity: the file's own peak)
      two full-scale hits at once          +6.02 dBFS   <- CLIPS

**Read this before chasing any distortion.** SampleDrum plays samples at
unity and voices **sum**, so a kick and a snare that are each normalised
to 0 dBFS, hit together, reach +6 dBFS. That is how drum samplers
behave, and it was left that way deliberately, not "improved" with
hidden headroom. The fix is **Output Trim** (−6 dB covers two
coincident full-scale hits) or samples that are not normalised to
full scale. If a kit sounds distorted, check this first.

At equal file and host rates, a hit is **bit-identical** to the file at
every rate from 44.1 k to 192 k; the trim is bit-identical at 0 dB; both
negative controls (half gain) are confirmed *not* identical.

---

## 5. Traps handled in the scaffold

- **`kBypass` is 1000**, far past the end of the table; `paramDef()` is
  the only place `kParams` is indexed, and it range-checks.
- **Title and units are never null** to `RangeParameter` (including the
  Bypass parameter's units, `STR16 ("")`). A null presents as the
  *validator* segfaulting in the post-build step.
- **`setState` resets everything to defaults before reading the stream**,
  in both processor and controller, reading the identical layout — so a
  short stream cannot leave the previous project's values behind.
- **Append parameters, never insert.** The parameter count is written to
  the stream so old streams stay readable.
- **Parameter-only blocks** (`numSamples == 0`) apply parameters and
  return `kResultOk`.
- **64-bit hosts** are accepted; the DSP renders float into a scratch
  sized in `setupProcessing`, and `writeOutput` widens. The audio thread
  never allocates; a block larger than promised is refused rather than
  grown.
- **Bypass on an instrument is silence**, not a dry path.
- **Silence flags** are set while the output is silent — clear them the
  moment there are voices.
- **Files are opened only on the UI thread** (`notify`, `setState`), decoded
  to interleaved stereo float at load, and handed to the audio thread as
  immutable buffers.
- **The buffer handoff is lock-free** (Project6's pattern): the processor
  owns `shared_ptr`s, the DSP gets a release-stored bare pointer, and a
  replaced buffer goes on a retire list, freed once the block counter has
  moved on by 2. **The extra step here:** a *voice* keeps its pointer for
  as long as the sample plays, which can be many blocks, so
  `syncCells()` runs at the top of **every** `process()` — including
  zero-sample ones — and ends any voice whose cell changed under it
  within that block (fade if there are samples, stop if there are none).
  The tests poison the old buffer afterwards and check nothing reads it.
- **`setState` does not touch the voices.** It sets `mResetPending`, and
  the audio thread resets at the top of its next block; resetting from
  the UI thread while a block renders would be a data race.
- **Bypass stops voices** rather than pausing them — a paused voice would
  still hold a pointer `syncCells` had condemned.
- **Sample-accurate starts:** hits are queued with their offsets and
  rendered in segments; out-of-order host events are sorted.
- **A cell's status is only believed for the path it was reported for**
  — the processor's status message carries the path, because
  `setState` and `setComponentState` arrive in either order.
- **A failed load keeps its path**, shows it in red with the reason, and
  the project remembers it (the drive may just be unplugged).
- **Silence flags are set only when the block really is silent** —
  checked per block, not assumed.

## 6. Deliberate omissions, and what bites when they are undone

| Omitted | Trap when adding it |
|---|---|
| `getProcessContextRequirements` | Since VST3 3.7 the ProcessContext is opt-in with no flags by default. Anything reading tempo silently gets 120 in every host and the validator prints `- None`. The exact override to add is in the banner of `SampleDrumProcessor.h`. |
| Messages from `process()` | Still none, and must stay none: the host's connection proxy silently discards them. The four messages in `SampleDrumIDs.h` all travel on the UI thread; the lamps are a request from the editor's timer and a reply from `notify`. |
| Per-cell level / pan / tune | Would be parameters — **append** them after `kOutputTrim`, never insert. |
| Velocity → volume | Deliberately off (the rows do the velocity work). If added, it goes in `renderVoices`, and the unity bit-identity test must then be run at velocity 127. |

Traps already handled in the editor: `editorDestroyed` compares upcast
pointers (never `dynamic_cast` inside `~EditorView()`); the drop handler
trusts the dragged *content*, not the reported type (a Finder drag arrives
as a `file://` URL marked as text); `onDrop` clears its own highlight
because VSTGUI never calls `onDragLeave` after a drop.

---

## 7. Verification actually run (2026-09-21, from the Linux VM)

### Second commit — the drum engine and grid

All ten `.cpp` files compile against the SDK and VSTGUI headers
(`-Wall -Wextra`, no warnings in our code); `nm -C` shows **0 unresolved
`SampleDrum::` symbols** out of 44 referenced. DSP suite:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Isource tests/DspTests.cpp \
    source/SampleDrumDsp.cpp source/SampleDrumSample.cpp -o /tmp/dsptests && /tmp/dsptests
```

**290 checks, 0 failures.** It covers the drum map, every velocity-band
edge, fallback and tie-break, the WAV reader (16/24/8-bit PCM, float,
odd chunks, truncation, every refusal), drag-text parsing, bit-identity
of a hit, sample-accurate offsets, overlap, resampling length, the choke
(fade, no click, no reverse choke), replacing a cell mid-note (fade
within the block, instant on a zero-sample block, nothing reads the
retired buffer), the voice limit and stealing, degenerate input, and the
levels in §4. A 24-bit stereo file written by Python's `wave` module was
also loaded from disk through `loadWavFile`: loaded, 44100 frames, peak
0.5000.

**Not verified from here:** the build, the validator, `auval`, and
anything the panel does on screen (drawing, dragging from the Finder,
timers). See §9 for the checks to run by hand.

### First commit — the blank scaffold

Every `.cpp` compiled to an object file against FilterDrum's SDK
checkout, then symbols checked:

```sh
cd ~/DXi-DEv/SampleDrum-VSTi
SDK=~/DXi-DEv/FilterDrum-VSTi/external/vst3sdk
for f in source/*.cpp; do
  g++ -c -std=c++17 -Wall -Wextra -DLINUX=1 -DRELEASE=1 -I$SDK -I$SDK/vstgui4 -Isource \
      -o /tmp/$(basename $f .cpp).o $f || echo "FAILED $f"
done
nm -C /tmp/*.o | grep " U " | grep "SampleDrum::"   # each checked against the defined set
```

Result: all five `.cpp` compile; **0 unresolved `SampleDrum::` symbols**.
Only warnings: `-Wsign-compare` on `int < kNumParams` (same as FilterDrum;
harmless).

DSP suite:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Isource tests/DspTests.cpp source/SampleDrumDsp.cpp \
    -o /tmp/dsptests && /tmp/dsptests
```

Result: **62 checks, 0 failures.**

**Not verified from here:** cmake configure, the Xcode build, the SDK
validator, `auval`. Those need the Mac itself.

---

## 8. The SDK: in-tree

`./setup-xcode.sh` clones the VST3 SDK (~250 MB) into `external/`, which
is `.gitignore`d. **In-tree was chosen** over pointing
`VST3_SDK_ROOT` at a sibling project, so SampleDrum is self-contained and
can ship separately. (The Linux-side compile check above borrowed
FilterDrum's checkout only because nothing had been cloned yet.)

If a source file is added later and the build fails with *"Bundle does
not export the required 'GetPluginFactory' function"*, re-run
`./setup-xcode.sh --no-open` and build again.

## 9. Next steps on the Mac

1. `cd ~/DXi-DEv/SampleDrum-VSTi && ./setup-xcode.sh` (new source files
   were added, so re-run it even if a project already exists)
2. Build Release; the SDK validator runs in the post-build step.
3. `auval -v aumu SmDr AECo`
4. In a host, by hand:
   - drop a `.wav` on a cell → its name appears; drop an `.mp3` → the
     cell does not light green and refuses it;
   - click the cell → it plays and flashes; Option-click → it empties;
   - play notes 36/38/39/42/46/45/50/49 → the matching column flashes;
   - put different samples in one column's four rows and play soft to
     hard → the lit cell walks down the column;
   - open HH then closed HH → the open hat cuts off;
   - save, reopen the project → all cells come back with their files;
   - rename a sample file on disk, reopen → that cell shows its name in
     red with "could not be read".
5. Record the results here.
