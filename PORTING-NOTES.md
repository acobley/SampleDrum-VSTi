# SampleDrum — engineering notes

**SampleDrum is not a port.** It was scaffolded blank on 2026-09-21 from
`~/DXi-DEv/vst3-port-template`, with the non-editor source files taken
from FilterDrum's own blank scaffold (commit `b2e1890`) and renamed.
Nothing is implemented yet: the DSP renders silence and there is one
placeholder parameter, which is enough to exercise the bus layout, the
parameter plumbing, the event input and the state stream — exactly what
the SDK validator checks.

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
| Editor now or later | **Later** | Porting guide step 6: get a silent plug-in validating with processor, controller and entry and nothing else, while there is little code to search. `createView` is not overridden, so hosts show their generic parameter list. |

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

`source/SampleDrumDsp.{h,cpp}` includes nothing under `pluginterfaces/`
or `public.sdk/`, and must not acquire one.

1. **It is the only thing that executes during development.** The
   assistant session reaches this Mac through a Linux VM and cannot run
   cmake, Xcode, the SDK validator or `auval`. `tests/DspTests.cpp`
   compiles straight against `SampleDrumDsp.cpp` with plain `c++`, so the
   numbers are tested before anything is built. An SDK header in that
   file kills the suite.
2. **Processor and controller do not share memory.** Anything a panel
   displays that the DSP computes must come from one shared function both
   call. `dbToGain()` / `gainToDb()` in `SampleDrumDsp.h` are the first.

---

## 4. Measured output level of the default patch

    48 kHz, 4096-sample block, default patch (trim 0 dB)
      L  -inf dBFS (exact silence)
      R  -inf dBFS (exact silence)

Correct for a silent scaffold. The trim stage on its own is
**bit-identical** at 0 dB at every rate from 44.1 k to 192 k, and the
negative control (half gain) is confirmed *not* identical.

**The first real DSP must repeat this measurement** and change the
assertion in `measureDefaultLevel()` to insist on a peak comfortably
below 0 dBFS. A default patch that clips masks every other fault.

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
- **Sample data is never loaded on the audio thread** — noted in
  `renderVoices()` for the sampler to come.

## 6. Deliberate omissions, and what bites when they are undone

| Omitted | Trap when adding it |
|---|---|
| `getProcessContextRequirements` | Since VST3 3.7 the ProcessContext is opt-in with no flags by default. Anything reading tempo silently gets 120 in every host and the validator prints `- None`. The exact override to add is in the banner of `SampleDrumProcessor.h`. |
| Processor ↔ controller messages | A `sendMessage` from `process()` is silently discarded by the host's connection proxy. UI thread only (`setActive`, `setState`, `notify`); per-block values go out as read-only parameters from id 1001. See `SampleDrumIDs.h`. |
| Editor | `editorDestroyed` is called from `~EditorView()`, where `dynamic_cast` to the derived editor returns null — compare upcast pointers. See `SampleDrumController.h`; `FilterDrumController.cpp` is the worked example and `SpyBandControls.{h,cpp}` lifts across with a namespace rename. |

---

## 7. Verification actually run (2026-09-21, from the Linux VM)

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

1. `cd ~/DXi-DEv/SampleDrum-VSTi && ./setup-xcode.sh`
2. Build Release; the SDK validator runs in the post-build step.
3. `auval -v aumu SmDr AECo`
4. Record both results here, then start on `renderVoices()`.
