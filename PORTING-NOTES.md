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
without the content installed. **There is no installer yet**; when one is
built, fill that folder from a `samples/` directory as an optional
component, exactly as Project6's `build-installer.sh` does, and have it
refuse to run if its destination and `kFactorySampleRoot` disagree.
**Decide before the first release** — once a project is saved referring to
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

