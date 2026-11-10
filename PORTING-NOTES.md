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
| Drum map | **GM defaults**: Kick 36, Snare 38, Clap 39, Closed HH 42, Open HH 46, Low Tom 45, High Tom 50, Crash 49 — **editable per column since the fourth commit** (see below) | Any GM pattern plays the right drum with no setup. A note no column has is **ignored**, not folded onto a neighbour. |
| Column note (fourth commit) | **A note box at the top of each column** ("< 36 C1 >"): click an end to step a semitone (Shift: octave), drag up/down, scroll; Option-click back to the GM default. 8 parameters, ids 33–40, 0–127 in whole steps, defaults from `kDrumMap`; saved with the project; **not automatable** (re-mapping mid-song loses hits). **Two columns on one note both play** (layering) and both boxes turn amber. The choke stays **by column**, whatever notes the hats are on. Names stay fixed. Note names use 60 = C3 (36 = C1); the number is always shown first. | The number first because the host turns the text back into a value by reading the leading number — the validator's round trip. `noteFromNormalized` is the one rounding the processor, host text and panel share. |
| Velocity → row | **Four equal bands**, top to bottom: 1–31, 32–63, 64–95, 96–127 | `velocity / 32`. Velocity 0 plays nothing (it is a note-off). |
| Empty cell | Falls back to the **nearest loaded row in the same column**; a tie goes to the **softer** row | One sample in a column plays at every velocity. Erring quiet on a guess. Never borrows from another column. |
| Playback | **One-shot**, unity gain (velocity picks the row only, it does not scale level); note-off ignored; a second hit on the same drum **overlaps** | What was asked for. |
| Choke | **Closed HH cuts open HH**, 5 ms fade; not the reverse; no column chokes itself | As a hi-hat pedal does. |
| Polyphony | 32 voices + 8 spares; the oldest is stolen with a 5 ms fade | The spares let a stolen voice fade instead of clicking. |
| Sample rate | Linear interpolation to the host rate; step exactly 1 when rates match | At equal rates the output is the file's own samples, bit for bit. |
| Files | WAV only: PCM 8/16/24/32, float 32/64, extensible, any channel count (mono duplicated); **20 s cap** | Reader lifted from Project6 without the tempo/MIDI parts. |
| Portability (fifth change) | **Every loaded file is embedded in the project**, byte for byte, and the embedded copy wins over the disk; files under `/Users/Shared/SampleDrum/Samples` are factory content and never embedded. See §10. | As Project6 1.2.0.0: a project must open on another Mac. |
| Cell level (third commit) | **A level bar under every cell**, as under each Project6 pad: −40 to +12 dB, default 0 dB, **the bottom of the bar is silence**. 32 automatable parameters, ids 1–32 in cell order (`cellLevelParam (cell)`), titled e.g. "Kick 96-127 Level". Applies live to a ringing sample, smoothed like the trim. Drag with Shift for fine; while dragging, the cell reads out the level ("off" at the bottom). | Project6's range and law, for its reason: a component level that may need lifting, in front of the Output Trim that answers clipping. |
| Panel | Click a loaded cell = audition (**on press**); **drag a cell onto another = move, which swaps** the two (the cell level travels with the sample); **Option or Control held at the drop = copy**; **right-click / Control-click → "Clear cell (name)"**, and Option-click (on release, without moving) also empties; cells light while sounding and **flash on every hit** | Emptying needs a menu or a modifier, so a stray click cannot lose a sample. Option-click fires on *release* so Option-press-and-drag is a copy, not an emptied source. Audition stays on press (unlike Project6's pads, whose launch moved to release) because a one-shot heard while being picked up is harmless. Hit counting so a hi-hat shorter than one redraw still shows. |

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
  the project remembers it (the drive may just be unplugged). Since §10,
  "file not found" (`SampleStatus::Missing`) is told apart from "could not
  be read" — the first is the one a person can fix.
- **A cell's level is smoothed once per block, not once per voice.** The
  one-pole is advanced in closed form over the block and every voice on
  the cell ramps linearly between the two ends, so two voices on one cell
  hear the same gain and a hit starting mid-block joins the ramp where it
  is. It **arrives exactly** (snaps within 1e-6), so 0 dB stays
  bit-identical and the bottom of the bar is truly silent.
- **The host sees a number even at the bottom of a level bar** ("-40.0"),
  because the validator reads parameter text back through the default
  string-to-value conversion; only the panel says "off".
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

## 7. Verification actually run (from the Linux VM)

### Sixth change (2026-09-26) — cell-to-cell drag and clearing (§11)

All twelve `.cpp` files compile against the SDK and VSTGUI (no new
warnings); **0 unresolved `SampleDrum::` symbols** out of 68. DSP suite:
**405 checks, 0 failures** — adds the drag payload (round trip, trailing
NUL, every refusal: plain path, file URL, no/empty path, no cell, cell 32,
a 14-digit cell without overflow, junk, short or non-hex owner, Project6's
payload; and a cell drag is never read as a file drop) and the carry rule:
move, both halves of a swap, copy sharing bytes, a drop from outside
reading the disk, a clear letting go, a mismatched path carrying nothing,
the previous file lasting one message, `restore` forgetting, bad cells
refused. State suite unchanged: 21 / 0.

### Fifth change (2026-09-26) — projects that travel (§10)

All twelve `.cpp` files compile against the SDK and VSTGUI (no new
warnings; only the known `-Wsign-compare`); **0 unresolved `SampleDrum::`
symbols** out of 56. DSP suite: **369 checks, 0 failures** — adds
`isFactoryPath` (inside the root only; not the root, not a sibling whose
name starts with it), `readWholeFile` telling Ok / Missing / Unreadable
(empty file, directory) / TooBig apart at the exact ceiling, `Missing`
appended with the old values unchanged, and a file deleted from disk whose
kept bytes still decode to the same audio. One existing check changed on
purpose: a missing file now reports `Missing`, not `Unreadable`.

New **`tests/StateTests.cpp`** (needs the SDK; the command is in its
banner — add `-framework CoreFoundation` on macOS): **21 checks, 0
failures** — paths and files round-trip byte for byte with nothing left
unread; `readState` alone (the controller) stops with the audio unread; a
stream from before the block reads as before and every cell falls back to
disk; a truncated block keeps what came before the damage; negative and
2 GB lengths stop without allocating; a stream cut inside the paths is
not `complete`; an empty kit costs 132 bytes.


### Fourth commit — a note per column

All twelve `.cpp` files compile against the SDK and VSTGUI; **0
unresolved `SampleDrum::` symbols** out of 51. DSP suite: **341 checks,
0 failures** — adds note formatting (36 C1, 42 F#1, 60 C3, 0 C-2, 127 G8,
number first), GM defaults, a moved column (old note silent, new note
plays), a shared note layering both columns, the choke following
columns not notes, and clamping. A separate check compiled against the
SDK headers confirmed, for every note 0–127, that
`noteFromNormalized (normalizedFromNote (n)) == n` and the text's leading
number reads back as `n`; that each column's default is its GM note; and
that every table row's id equals its index with non-null title and units.

### Third commit — a level per cell

All eleven `.cpp` files compile against the SDK and VSTGUI; `nm -C`
shows **0 unresolved `SampleDrum::` symbols** out of 47. DSP suite:
**321 checks, 0 failures** — the 290 before, plus the level law at both
ends, silence at the bottom, a settled level scaling a hit, neighbours
untouched and still bit-identical, two voices sharing a cell's gain, and
a level yanked to off under a ringing sample at every rate from 44.1 k to
192 k (no step over ~1 % per sample; arrives at exactly 0, and back at
exactly 1). The output measurements in §4 are unchanged: levels default
to 0 dB.

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
   - rename a sample file on disk, reopen → **the cell still plays** (the
     project carries it); the tooltip still shows the old path;
   - **portability:** save a project, copy it to another Mac (or another
     user account) without the samples, open it → every cell plays;
   - a project saved *before* this change, with a file renamed → that
     cell shows its name in red with "file not found"; drop the file
     again, save, rename it once more, reopen → it plays;
   - edit a sample on disk after saving → the project still plays the
     old version until the file is dropped on the cell again;
   - Reaper: with a full 32-cell kit, check that parameter edits (undo
     points) don't feel slow;
   - **drag a cell onto an empty cell** → it moves, the source empties,
     its level bar goes with it; onto a loaded cell → they swap; hold
     Option (or Control) at the drop → copy; the cursor shows move/copy;
   - drag a cell onto itself → nothing lights, nothing happens;
   - press Option on a cell and drag → a copy, and the source is NOT
     emptied; Option-click without moving → empties;
   - right-click and Control-click a loaded cell → "Clear cell (name)";
     choosing it empties the cell and its level stays; close the editor
     with the menu open → no crash;
   - **portability of a move:** on a Mac with only the project (no
     samples), swap two cells, save, reopen → both still play;
   - two SampleDrum instances: drag a cell from one window to the other →
     the target takes it as a file (copy) and the source is untouched —
     and the target's own cell with that number is NOT moved;
   - drag a cell out to the Finder → nothing is created (the payload is
     text, not a file).
5. Record the results here.

## 10. Projects that travel: embedded files and a factory folder (2026-09-26)

A cell remembered its file by absolute path, and a path means nothing on
somebody else's Mac — a project handed over lost every cell. This follows
**Project6 1.2.0.0** (its PORTING-NOTES §21, commit `6594bec`) with the same
two mechanisms. Read §21 there for the options rejected; the one that
matters is that samples **must not** be copied into `SampleDrum.vst3`
(signature, `/Library` permissions, reinstalls, and the AU's separate
embedded copy of the VST3).

**Everything is embedded.** One block is appended to the state stream after
the paths: a cell count, then 32 length-prefixed blobs, each the cell's
`.wav` **exactly as it was on disk** (`writeEmbedded` / `readEmbedded` in
`SampleDrumState`). Zero length = nothing embedded: an empty cell, a factory
file, or a file that did not load.

**Factory files are not.** `kFactorySampleRoot` =
`/Users/Shared/SampleDrum/Samples` (`SampleDrumSample.h`). `/Users/Shared`
is the same path on every Mac, so a cell loaded from there needs nothing
carried. `isFactoryPath` is textual, so the answer is the same on a machine
without the content installed. **The installer (§12) fills that folder**
from `samples/` as an optional component, exactly as Project6's does, and
refuses to run if its destination and `kFactorySampleRoot` disagree.
**Permanent from the first release** — once a project is saved referring to
the root, moving it breaks those projects.

### The decisions inside it

- **`kStateVersion` stays 1.** This file's own rule: bump only for a change
  "stop early, keep defaults" cannot absorb. An appended block is exactly
  that — a reader without it stops after the paths and reads the disk. (This
  differs from Project6, which bumped to 10; there the version is
  informational, here a newer version is *refused*.)
- **Last in the stream**, so the controller's `setComponentState`, which
  calls `readState` alone, never reads the audio. Anything appended later
  goes **after** this block.
- **`StreamState::complete`** is set only when `readState` reached the end
  of the path block. The processor reads the embedded block only then —
  after an early stop the stream is mid-block and would be read as garbage.
  (Project6 doesn't have this guard; worth back-porting.)
- **The embedded copy wins.** `loadCell` decodes `mFiles[cell]` if the
  project carried it and reads the disk only if not. **A drop from the
  Finder always reads the disk afresh** (`notify` clears `mFiles[cell]`
  first) — that is how an edited sample is brought back in.
- **Bytes are held in memory** (`mFiles`, shared + immutable, under
  `mFilesLock`) rather than re-read at save time, since hosts call
  `getState` often (Reaper, for every undo point). `getState` snapshots the
  32 pointers under the lock and streams outside it. The audio thread never
  touches `mFiles`. Cost: the raw files in memory alongside the decoded
  float — small for drum hits; the 20 s cap bounds a stray long file.
- **Only a file that loaded is kept**, so a broken file is not carried to
  somebody else; the project keeps naming its path.
- **`SampleStatus::Missing`** ("file not found") is **appended**;
  `kLastSampleStatus` marks the end, and the controller's range check uses
  it (it hard-coded `NoFrames` before, and would have dropped the new
  status). `readWholeFile` `stat`s first to tell Missing from Unreadable.
- **Moved cells carry their bytes** (sixth change, §11): each cell message
  from a drag names its source cell (`kSampleDrumCellFromAttribute`), and
  `CellFiles::replace` carries the embedded copy across, keeping each
  cell's previous file for the second half of a swap.

### What it does not do

- **No relink.** A cell from a project saved before this change whose file
  is missing says "file not found"; drop the file again and the next save
  embeds it.
- **A drag from another SampleDrum instance** re-reads the path from disk
  (its embedded copy is in the other processor). On the machine that made
  the project the file is there; on a recipient's it may not be.
- **No switch to turn embedding off.** Measure in Reaper with a full kit of
  long samples before adding one; the format already allows "nothing
  embedded" per cell.

## 11. Cell-to-cell drag and clearing (2026-09-26, sixth change)

Ported from Project6's pad drag (`SpySampleSlot`, `moveSlot`), its
right-click "Clear pad" (commit `b8a3e5e`), and the embedded-copy carry
from 1.2.0.0 (`6594bec`). Where SampleDrum differs, it is said here.

**Gestures** — `SampleDrumCellView`:

- A left press **auditions at once** (not Option) and waits to see whether
  it is a click or a drag; past **4 px** it becomes a drag
  (`beginCellDrag`). Project6 moved its launch to release because a loop
  started by a pick-up would keep running; a one-shot doesn't.
- **Move = swap, Option or Control at the drop = copy**, read live in
  `onDragMove` so the cursor badge is right, and again at the drop.
  Control as well as Option because macOS turns a Control-*click* into a
  right-click, but a Control held *during* a drag arrives as Control.
- **Option-click empties on release**, not press — otherwise pressing
  Option to start a copy-drag would empty the source first. This is the
  one real conflict between the old gesture and the new one.
- **Right-click / Control-click** a loaded cell → `COptionMenu` with one
  entry, "Clear cell (*file name*)". The callback captures the
  **controller**, not the editor (the editor may be closed by the time the
  menu returns), and a `SharedPointer` to the view keeps it alive.

**The payload** — `encodeCellDrag` / `decodeCellDrag`, SDK-free in
`SampleDrumSample`: `SampleDrum/cell:<owner 16 hex>/<cell>\n<path>`, as text
with the NUL included (the macOS layer reads to a NUL). **The owner is new
relative to Project6**, whose payload has only the index: there, a pad
dragged from one Project6 window onto another calls `moveSlot (from, to)`
on the *target* instance, which moves the target's own pad `from`. Here a
foreign owner is treated as a file dropped from outside (copy, read from
disk). The owner is a random 64-bit id per controller (`instanceId()`), not
an address — sandboxed hosts can give two instances the same address.
**Back-port this to Project6.**

**The move** — `SampleDrumController::moveCell`: levels first (both read
before either is written, or the swap copies one value to both), each as a
full begin/perform/end gesture and only if it changed; then the paths
through the one door, `setCellPath (cell, path, carriedFrom)`. **The cell
level travels with the sample**, as Project6's pad settings do; on a
**clear the level stays** for the next file, as on a replacement. Nothing
has to be stopped by hand (Project6 stops pads first): replacing a cell's
buffer already ends its voices via `syncCells`.

**The carry** — `CellFiles` (SDK-free, `SampleDrumSample`), replacing the
processor's bare `mFiles` + lock so the rule can be unit-tested without a
host. `replace (cell, oldPath, newPath, from, fromPathNow)` carries
`from`'s bytes only if the path matches, from `from` as it is now or from
what it held before its last change — which is where the second half of a
swap finds its file. Only **one** previous file is kept, until the next
cell message. `restore` (setState) forgets them.

**What the processor does on a clear:** the message has no path, so
`replace` leaves the cell with nothing and the next save no longer carries
the file.

## 12. The installer and the release kit (2026-09-26)

`installer/` is ForTran 1.2.1.0's kit (itself FilterDrum's and Project6's),
with the factory-samples component from Project6 1.2.0.0 in place of
ForTran's presets. **Read `installer/README.md`**; this is only what differs.

| File | From | Changed |
|---|---|---|
| `build-installer.sh` | ForTran | names/ids; presets → **samples**: every `.wav` under `samples/` (sub-folders kept) → `SampleDrum-Samples.pkg` → `/Users/Shared/SampleDrum/Samples`, `--ownership recommended`; refuses if that is not `kFactorySampleRoot` in `SampleDrumSample.h`; `BEGIN SAMPLES`/`END SAMPLES` blocks stripped from the distribution when there are none. `root-samples` always exists, so the symlink guard scans it — ForTran's `${PRESET_COUNT:+…}` passed a missing directory to `find` (harmless there; worth fixing). |
| `build-release.sh`, `publish-release.sh`, `test-build-release.sh`, `test-publish.sh` | ForTran | **none** — they read `NAME=` from `build-installer.sh`. The preset step is skipped (no `tools/make-presets.py`). Notary profile: `project6-notary`, shared by every plug-in. |
| `verify-install.sh` | ForTran | names, `auval -v aumu SmDr AECo`; section 5 checks the factory samples are there and readable by every user. |
| `distribution.xml`, `resources/*.html`, `scripts-au/postinstall` | ForTran / Project6 | SampleDrum's prose: the +6 dBFS summing, velocity picks the row, samples embedded. No REAPER/pitch-wheel/Level-knob text carried over. |
| `release-notes-1.0.0.0.md` | new | `SHA-256: <paste from: …>` placeholder, filled by `publish-release.sh`. Assumes **no factory samples** ship; if `samples/` gets any, add them to *Installing* and *Uninstalling*. |
| `tools/run-tests.sh` | new | DspTests, StateTests (skipped without the SDK; CoreFoundation on macOS), `check-versions.py`, both stub suites. |
| `tools/check-versions.py` | ForTran | none. |
| `samples/README.md` | Project6 | `.wav` only. |
| `LICENSE` | Project6's header + ForTran's text | **CC BY-SA 4.0**, chosen 2026-09-26. The installer's licence pane needs it. |
| `CMakeLists.txt` | ForTran | **`PORT_UNIVERSAL` (arm64 + x86_64, before `project()`)** and **`-ffile-prefix-map` (before `add_subdirectory`)** — without them `build-release.sh` refuses the build. |
| `setup-xcode.sh` | ForTran | `--native` (one architecture, faster while iterating). |
| `.gitignore` | ForTran | packages, `.built-from`, signing material. |

**Verified from the Linux VM:** `tools/run-tests.sh` — DSP 405/0, State
21/0, versions agree, **test-build-release 22/0, test-publish 18/0**.
`build-installer.sh` run against stubbed `pkgbuild`/`productbuild`/
`pkgutil`/`codesign` and a fake build tree with the absolute
`plugin.vst3` symlink: with no samples → two packages, no samples choice,
distribution well-formed, every pkg-ref resolves; with `Kicks/k.WAV`,
`s.wav` and the README → three packages, the two `.wav` staged with their
folder and the README left out; with `kFactorySampleRoot` changed → refused.

**Not verified:** anything Apple — a real `pkgbuild`, signing,
notarisation, `spctl`, and whether the universal build and prefix map
compile clean on the Mac.

### To release 1.0.0.0

1. **Create the GitHub repository** and `git remote add origin …`; push
   `master`. `publish-release.sh` refuses without a reachable remote.
2. Do the §9 checks by hand in a host first — nothing in the kit tests the
   panel.
3. `installer/build-release.sh` (clean universal build, then one signed,
   notarised, stapled package), then install it and run
   `installer/verify-install.sh`.
4. `installer/publish-release.sh --dry-run`, then without `--dry-run`.
5. The package is under **Releases**, not in the file listing.


## 13. A sample above the session's rate: band-limited, not aliased (2026-11-01)

From the DXi-DEv audit (`AUDIT-denormals-aliasing.md`). A voice reads its file
at `step = sourceRate / session rate`, interpolating linearly. That is fine at a
step of 1 or below, and does nothing against aliasing above it. A 96 kHz file
in a 48 kHz session (step 2) folded everything in the recording between 24 and
48 kHz straight down at full level: a 30 kHz component came out at 18 kHz at
**0 dB**, because at a step of exactly 2 linear interpolation is plain
decimation. Cymbal and hat recordings made at 96 kHz do have energy there.

**Above a step of 1, a voice now reads through `SincBank`**
(`SampleDrumDsp.h`): a Kaiser-windowed sinc at 0.94 × the session's Nyquist,
10 zero-crossings either side, β = 9, from a polyphase bank (128 phases, two
rows interpolated, each row normalised to unity DC gain). This is the same
design as Project6's tempo-fit read, with a longer kernel. A file's rate is
fixed, so the bank is built once, and the extra taps buy a sharper transition.

* **Banks.** `setSampleRate` sizes eight bank slots, off the audio thread, and
  builds the steps for 88.2, 96, 176.4 and 192 kHz files in advance. Any other
  rate gets a bank when its first voice starts, in a slot no playing voice is
  using (about 8 k table lookups, once). With eight different high rates
  sounding at once, a voice falls back to the linear read.
* **Taps past either end of the file read as silence.** A drum hit is a
  one-shot.
* **At or below a step of 1 nothing changes.** Three renders, at steps 0.92,
  1.0 and 0.67, are byte-identical to the build before.

Measured, a 96 kHz file at 48 kHz:

* what would fold to 18 kHz comes out at −98 dB (0 dB before);
* to 16 kHz, −93 dB;
* 19 kHz passes at −0.4 dB, 20 kHz at −1.1.

The cost is in the taps: about 43 at a step of 2, capped at 65. With 28 voices
of 96 kHz files at 48 kHz on the arm64 Linux VM, CPU went from 1.7 to 32 ms per
second of audio. A few high-rate hits at once cost a fraction of that.

The resampling test's "does not overshoot a constant" now applies only to steps
of 1 or below. A constant that starts and stops dead is a step, and a
band-limited step rings by about 9 % at its ends (Gibbs). For a file above the
host's rate the test instead checks that the level in between is exact (unity
DC gain) and that the ringing is no more than Gibbs.

`DspTests` "a file above the host's rate is band-limited" checks:

* 30 kHz in a 96 kHz file at 48 kHz does not fold to 18 kHz (−98 dB; the linear
  read, as the negative control, gives −0.0);
* 10 kHz passes within 0.5 dB;
* a 64 kHz file, a rate built on demand, behaves the same;
* `ScopedFlushDenormals` (`source/DenormalGuard.h`, ForTran's copy with the
  compiler barriers) now wraps `SampleDrumProcessor::process`. Adding it adds a
  file to `source/`, so re-run `./setup-xcode.sh` before the next Xcode build.

## 14. The output isolator: bass and treble as a DJ mixer's (2026-11-10, branch `output-isolator`)

Four new controls on the **main (only) stereo output**, after the voices are
summed and **before Output Trim**: Bass, Bass Freq, Treble, Treble Freq. It
works like a DJ mixer's isolator, not a shelving EQ. The signal is split into
three bands whose sum is the input's magnitude exactly. Bass and treble each
have a gain from **kill** up to +6 dB, and the mid band stays at unity. Kill
both bands and the snare's body is what's left.

    voices ─► LR4 @ bass Hz ─┬─ low ─► allpass @ treble Hz ─► × bass ───┐
                             └─ rest ─► LR4 @ treble Hz ─┬─ mid ─► × 1 ─┼─► Σ ─► trim
                                                         └─ high ─► × treble

| | Decision | Why |
|---|---|---|
| Crossovers | **Linkwitz–Riley 4th order**: two Butterworth sections in series. The low band also goes through the second split's allpass. | LR4's low and high outputs are in phase and sum to an allpass, so there's no bump or hole at the crossover. The allpass keeps the low band in phase with the mid and high bands. At unity the whole thing is AP(bass Hz) × AP(treble Hz), which is flat at **any** two frequencies, even crossed. |
| Filters | **TPT state-variable filters in double**, 7 per channel. One SVF gives LP, HP and AP from a single state. | Stays stable and click-free while its frequency is swept, and the frequencies are automatable. Double precision because 50 Hz at 192 kHz is a very low normalised frequency. |
| Gains | −40 … +6 dB, **bottom = kill** (`isolatorGain`, exactly 0). Within **0.001 dB of 0 dB counts as exactly 1**. | The cell-level rule. The snap is not cosmetic: 0 dB on a −40…+6 range comes back from its normalised value as about 7e-15 dB (3e-6 dB via a host's float), and anything other than exactly 1 takes the filtered path (next row). The state test caught this. |
| 0 dB / 0 dB | **Dry, bit for bit.** When a gain leaves 0 dB, a **linear 20 ms crossfade** goes to the filtered path. It fades back to dry only once both gains have glided back to **exactly** 1. The filters run all the time. | The flat sum is still an allpass: the right level but a shifted phase. §4's promise is that a hit at default settings is the file's own samples. Fading only at unity means the two paths being mixed differ in phase only. |
| Frequencies | Bass **50–500 Hz** (default 200), Treble **1.5–12 kHz** (default 3500), **log taper** (`ParamType::LogFloat`, `logRangeToPlain/ToNormalized`). | The ranges don't overlap, so neither is ever clamped against the other; an automation lane that silently moved a second parameter would disagree with what is heard. 200 Hz splits a kick's body from the rest; 3.5 kHz splits a snare's body from hats and cymbals. |
| Smoothing | Gains and log2 frequency use one-pole glides (20 ms, as the trim) that **arrive** exactly. Coefficients are recomputed per sample, **only while a frequency is moving**. | No zipper noise, and nothing is spent when the frequencies are still. |
| Parameters | Ids **41–44** (`kBassGain`, `kBassFreq`, `kTrebleGain`, `kTrebleFreq`), **appended**, all **automatable**. `kStateVersion` stays 1. | Append, never insert. A 1.0.1.0 project (41 parameters) loads with the isolator at its defaults, i.e. flat and dry. |
| Host text | Frequencies in **whole Hz** ("3500"); gains as "+0.0" with a number even at the bottom. The panel shows "3.50 kHz" and "kill". | The validator reads text back through the leading number in the parameter's units, so "3.50 kHz" would come back as 3.5 Hz. |
| Denormals | Filter state is flushed below 1e-30 at the end of each block. | A tail ends in **exact zeros**, which is what the processor's silence check looks for. |

**The log taper in the controller.** `RangeParameter` is linear, so
`SampleDrumParameter` overrides `toPlain`/`toNormalized` for `LogFloat`, and
`fromString` goes through them. **Trap:** `RangeParameter`'s constructor
computes the default through its *own* (linear) `toNormalized`, because a
virtual call from a base constructor never reaches the override. The
subclass constructor sets `info.defaultNormalizedValue` again for
`LogFloat`.

**Panel.** Four `SpySlider`s to the right of Output Trim, after a gap. The
help line moved to its own row underneath, so the editor is 22 px taller.
**Option-click any `SpySlider` → default** (added to `SpySlider`, so the
trim gets it too; each slider's default comes from the table).

**Not on the aux outputs**, if per-pad aux outputs are ever added. Each bus
would need its own `Isolator`.

### Verification (from the Linux VM)

All twelve `.cpp` files compile against the SDK and VSTGUI with no new
warnings; **0 unresolved `SampleDrum::` symbols** out of 72.
`tools/run-tests.sh`: **all suites pass**.

**DSP: 552 checks, 0 failures** (412 before). At every rate from 44.1 kHz
to 192 kHz:

* 0/0 dB is bit-identical and dry;
* the filtered path, forced on at unity, is flat within **0.01 dB** from
  20 Hz to 20 kHz at the default frequencies and at both ends of the
  ranges;
* bass kill takes 50 Hz down by more than 40 dB, and 1 kHz moves less
  than 0.1 dB;
* treble kill takes 14 kHz down by more than 40 dB, and 1 kHz moves less
  than 0.1 dB;
* with both killed, the mid band is within 0.2 dB;
* +6 dB bass reads +6 at 30 Hz, and −12 dB treble reads −12 at 16 kHz;
* the kill follows its frequency: with bass at 50 Hz, 150 Hz is left
  alone; at 500 Hz, it's removed;
* a kill in and out under a 100 Hz sine never steps more than 1.5× the
  sine's own slope;
* both frequencies thrown end to end every 64 samples under noise stay
  finite and bounded, and L = R;
* an impulse tail ends in exact zeros;
* the isolator is wired into `render()`.

Negative controls run on throwaway copies of the source:

* without the low band's allpass, the flatness check fails (0.03 dB);
* with the crossfade replaced by a jump, the click check fails (about
  100× the sine's slope).

The second control **also caught a broken test**: the kill was being
triggered at `at == total / 4`, which is never a multiple of 256, so the
kill never happened.

**State: 29 checks, 0 failures** (21 before):

* a 41-parameter project reads with the isolator at its defaults;
* the defaults are 200 Hz and 3500 Hz through the log taper;
* a gain's default is exactly unity, even through a float;
* the DSP is handed the same Hz the host shows.

**Not verified here:** the build, the validator, `auval`, and the panel on
screen.

### To check by hand on the Mac

1. Re-run `./setup-xcode.sh --no-open`. No files were added, but it's
   harmless.
2. Build, then run the validator and `auval -v aumu SmDr AECo`.
3. In a host:
   - At defaults, a hit still nulls against the file.
   - Bass to the bottom (the panel says "kill"): the kick's body goes and
     the click stays.
   - Treble to kill: the hats go.
   - Both killed: the "telephone" mid band.
   - Sweep Bass Freq with bass killed: the cut follows the knob without
     zipper noise.
   - Automate all four: there are no clicks.
   - Option-click each slider (and the trim): it returns to its default.
   - The host's generic editor shows "200" and "3500" Hz; type
     "1000" into Bass Freq: it clamps to 500.
   - Open a 1.0.1.0 project: the isolator is flat.
4. Record the results here, then bump the version. **Done: checked on the
   Mac with the Polivoks panel (§16), released as 1.1.0.0.**

## 15. Panel designs (2026-11-10)

Three Polivoks black-panel mock-ups in `Designs/` (R2Synth / ForTran / SpyBand look, 70s sci-fi lettering),
all with a new **nearly-new** finish (`weather.py` mode `0`): 1 Kit Grid (colour OLED cells + LED level
ladders), 2 Channel Strips (a strip per drum, level knobs, amber OLEDs), 3 Pads + Scope (big white OLED of the
last cell, rubber pads with hit lamps, mini level faders). Each has an **Isolator** section (§14): a
three-band response screen plus Bass, Bass Freq, Treble and Treble Freq, placed before Output Trim. Sources and commands in `Designs/README.md`.
Design 2 is now the plug-in's panel: see §16.

## 16. The Polivoks panel (2026-11-10, branch `polivoks`, merged into `main`)

The editor is now design 2, **Channel Strips** (`Designs/SampleDrum-Polivoks-2-ChannelStrips-NearlyNew.png`),
1200 × 640, built **ForTran's way** (ForTran-VSTi README, "The panel"):

- **`resource/pv_panel.png`** — everything that never changes (boxes, titles, legends, knob scales, OLED bezels,
  the band in each cell's corner, the wordmark), 2x, from `tools/polivoks/d2p.js` with `window.STATIC`.
- **`resource/pv_wear.png`** — the **nearly-new** wear as a transparent layer over everything (`wear.py`).
- Everything that moves is drawn by views **at d2p.js's coordinates**. Move one, move both, then
  `tools/polivoks/make-panel.sh` (needs python3 + playwright + numpy/scipy/Pillow; the VM has no playwright,
  so the PNGs were rendered in the cloud workspace).

| Piece | File | Notes |
|---|---|---|
| Lettering | `SciFiLettering.{h,cpp}` | ForTran's, namespace changed, **`#` and `↑` added** (D#1, F#1, A#1, C#2; "PLAYS 64-95 ↑"). |
| Knob, lamp, text, OLED base, voice meter, overlay | `PvControls.{h,cpp}` | ForTran's subset; the voice meter takes a segment count and colours (32, amber); a lamp with no listener is an indicator. |
| Last hit, isolator response, output meter | `SampleDrumPvViews.{h,cpp}` | new; plus `Pv::drawGlass`, `Pv::drawThumbnail`. |
| Cells | `SampleDrumCellView` | same gestures; drawn as an amber OLED: waveform, name, lit border, **playhead**, "EMPTY / PLAYS a-b ↓" from `chooseCell`. The view is 4 px bigger than its screen for the border. |
| Note box | `SampleDrumNoteBox` | same control; amber lettering; a **shared note is now red** (amber is the normal colour here). |
| Cell level | `PvKnob` per cell | replaces the level bar (`SampleDrumLevelBar` and `SampleDrumControls` are now unused but still compiled — delete when convenient). Value printed beside it; "OFF" from `cellLevelGain`. Alt-click / double-click = default. |
| Isolator | 4 `PvKnob`s + response screen | "KILL" from `isolatorGain`; the curve is **`isolatorResponseDb`** (new, SDK-free): the LR4 network in the frequency domain, flat to 1e-9 dB at 0/0 (tested). |
| Trim, Bypass, Over | `PvKnob`, `PvLamp` (Bypass is the switch), `PvLamp` indicator | OVER lights 1.5 s after a peak ≥ 0 dBFS. |

**New data from the processor** (all on the UI thread, as §5 requires):

- **Thumbnails**: `makeThumbnail` (SDK-free, `SampleDrumSample`) — 96 min/max pairs as signed bytes, **absolute**
  (127 = full scale). Made once in `loadCell` from the decoded audio and sent on the **status message**
  (`Thumbnail`, `Seconds`), so it works from the embedded copy on a Mac without the files. The controller believes
  it only for the path it was made from, like the status.
- **Activity reply** gains `Peak` (L/R, max since the last reply — the audio thread raises an atomic, the reply
  `exchange(0)`s it), `Voices` (`SampleDrumDsp::voicesSounding`, an atomic set at the end of each block — the old
  `activeVoices()` reads the voice array and is **not** safe from the UI thread), `LastHit` (cell, velocity, a
  16-bit sequence, packed in one atomic word; set when a voice actually **starts**, so a note on an empty column is
  not a hit) and `Rate`. All optional on the reading side.
- The playheads are **estimated** in the editor from the hit time and the file's length; nothing extra is sent.

### Verification (from the Linux VM)

All 15 `.cpp` files compile against the SDK and VSTGUI with `-Wall -Wextra` (no warnings in our code);
**0 unresolved `SampleDrum::` symbols** out of 101. `tools/run-tests.sh`: **DSP 578 / 0** (adds `testPanelData`:
thumbnails, the response curve, voice count and last hit), **State 29 / 0**, versions agree, installer suites pass.

**Verified on the Mac (2026-11-10, Andy):** built, the panel looked at against the design, and checked working.

The hand checks, for the next change that touches the panel:

1. `./setup-xcode.sh --no-open` (**new source files and two new PNGs**), build, validator, `auval -v aumu SmDr AECo`.
2. The panel matches the design PNG; nothing is offset from the printed bezels/scales (a mismatch means d2p.js and
   `SampleDrumEditor.h` disagree).
3. Drop a .wav: name and waveform appear; an empty cell under it reads "PLAYS … ↓/↑"; a missing file shows red.
4. Play: cells light with a moving playhead, the column's HIT lamp, LAST HIT shows drum/band/file/VEL; click a cell
   → "CLICKED".
5. Knobs: drag, Shift-drag, wheel, Alt-click/double-click default; automation moves them; the level shows "OFF" at
   the bottom, the isolator "KILL".
6. Isolator curve follows all four knobs; two full-scale hits light OVER; BYPASS toggles and silences.
7. Two editors open on one instance, and a cell dragged between two instances, still behave as §9 says.

**Version: 1.1.0.0** — the isolator's four parameters and the new panel; a minor bump, as ForTran's
Polivoks panel was. Notes in `installer/release-notes-1.1.0.0.md`; `tools/check-versions.py` agrees.
