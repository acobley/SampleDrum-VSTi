# Denormal and aliasing audit: the DXi-DEv plug-ins

30 October 2026. Covers FilterDrum, ForTran, MasterLoud, Project6, R2Synth,
SampleDrum, SpyBand and VocalFilter, as they stood on their current branches
that day. SpaceDub was audited and fixed separately; its fix is the model for
the denormal half of this document.

**This copy is for SampleDrum.** Its own section is in part C. The full audit, covering all eight plug-ins, is `~/DXi-DEv/AUDIT-denormals-aliasing.md`.

This document is meant to be worked from. Each project has its own copy,
`AUDIT-denormals-aliasing.md`, that holds the shared background, the shared
fixes and that project's own section, so a session working in one project
needs only that file.

---

## Summary

| Plug-in | Denormals | Aliasing | Priority |
|---|---|---|---|
| **MasterLoud** | **Problem.** Unflushed biquads go into permanent denormals once audio stops; CPU rises 4–5× and stays there (measured) | None | **1: fix** |
| **Project6** | **Problem.** Level smoothers aimed at 0 (any fader at the bottom) stick at a denormal for good; ~30× the cost of that loop (measured) | **Problem.** Linear interpolation when Varispeed speeds a loop up, or a file's rate is above the session's | **2: fix** |
| **R2Synth** | Handled | **Problem.** MS-20 and MS-20 Late filters at high resonance and bright cutoff: alias spurs at −36 to −47 dB at 1×, −63 to −70 dB at 2× (measured) | **3: fix** |
| **FilterDrum** | Handled | **Problem.** Self-oscillation above ~4 kHz: alias tones at −26 / −29 dB right beside a 6 kHz pitch (measured) | **4: fix** |
| **ForTran** | Minor: output gain smoother at 0 | **Problem.** Partials above Nyquist are not culled; notes from about D♯7 up alias | **5: fix** |
| **VocalFilter** | Minor: Mix and Trim smoothers stick when aimed at 0 | None (linear) | 6: tidy |
| **SampleDrum** | Handled | **Fixed 1 Nov.** Was: linear interpolation when a file's rate is above the session's | done |
| **SpyBand** | Handled | None that matters | none |

Every one of the eight lacks a flush-to-zero guard around `process()`. Add it
to all of them (section B1). It is cheap insurance, and for MasterLoud and
Project6 it is half of the fix.

The CPU figures were measured on x86_64, which is what an Intel Mac or Rosetta
runs. Apple Silicon handles denormals in hardware at much lower cost, so the
penalty there will be far smaller; that was not measured. ForTran, MasterLoud,
Project6, R2Synth and SampleDrum ship `arm64;x86_64`. FilterDrum, SpyBand and
VocalFilter build for the host architecture only. The aliasing figures do not
depend on the CPU.

---

## A. Background

### A1. Denormals, and the trap that keeps them forever

A denormal (subnormal) is a float or double too small for the normal exponent
range: below about 1.2 × 10⁻³⁸ for a float, or 2.2 × 10⁻³⁰⁸ for a double.
Arithmetic on denormals is slow on x86, often 10–100× the normal cost per
operation. Recursive DSP produces them whenever a signal decays towards zero:
filter state, feedback, envelopes and smoothers.

**The trap found in this audit: some decays never reach zero.** A one-pole
recursion `x += (target − x) × c` aimed at a target of **0** multiplies `x` by
`(1 − c)` each sample. In round-to-nearest arithmetic, once `x` is down at the
bottom of the denormal range, `x × c` rounds to zero and `x` **stops changing,
as a denormal, for ever**. Measured:

| Recursion | Becomes denormal after | Then |
|---|---|---|
| double, c = 0.001 (a ~20 ms smoother) | 708 042 samples (15 s at 48 kHz) | stuck at 2.5 × 10⁻³²¹ for good |
| double, c = 0.4 | 1 386 samples | stuck at 4.9 × 10⁻³²⁴ for good |
| double, c = 0.6 | 773 samples | reaches 0 at sample 813 |
| float, c = 0.001 | 87 292 samples | stuck at 7.0 × 10⁻⁴³ for good |

Any decay factor above 0.5, which is every smoother and almost every audio
filter pole, sticks rather than underflowing to zero. Biquads behave the same
way: their state settles into a permanent denormal limit cycle when the input
goes silent. So denormal cost is not a short burst while a tail dies away: it
starts some seconds after the signal stops and **lasts until the plug-in is
reset**.

### A2. Aliasing

Anything nonlinear (clipping, a diode or tanh, rectification, a waveshaper)
creates harmonics. Harmonics above Nyquist (half the sample rate) cannot exist
at that rate, so they fold back to `|k·f − m·fs|`: inharmonic tones that move
the wrong way when the pitch moves. Playback at a speed above 1 does the same
to everything in the recording above `Nyquist / speed`. So does generating
partials (additive synthesis) above Nyquist.

The fixes are:

* **oversampling:** run the nonlinear part at 2× or 4×, then low-pass and
  decimate;
* **antiderivative antialiasing (ADAA):** for a memoryless curve outside a
  feedback loop;
* **band-limiting at the source:** polyBLEP oscillators, culling partials above
  Nyquist, band-limited interpolation.

ADAA does not fit a nonlinearity solved *inside* a zero-delay feedback loop,
which is the case in R2Synth's and FilterDrum's MS-20 filters. For those,
oversampling is the practical answer.

---

## B. Shared fixes

### B1. Flush-to-zero for the length of `process()` (all eight)

Copy `source/DenormalGuard.h` from SpaceDub (`~/DXi-DEv/Spacedub-VSTi`). It is
standard-library only, so the SDK-free test suites can include it. Put one line
at the top of the processor's `process()`:

```cpp
#include "DenormalGuard.h"
...
tresult PLUGIN_API XxxProcessor::process (ProcessData& data)
{
	// Flush-to-zero for this call only; the host's FPU mode comes back on
	// return. See DenormalGuard.h.
	ScopedFlushDenormals noDenormals;
	...
```

Add the header to the target's source list in `CMakeLists.txt`. The guard sets
MXCSR FTZ + DAZ on x86_64, or FPCR FZ on arm64, and puts the host's mode back
in its destructor. It is never left on, because the FPU mode belongs to the
host's thread.

**This is not a replacement for B2/B3.** The DSP is also run from the tests,
from offline renders, and from any future caller that is not `process()`. Fix
the recursion as well, so the DSP is correct on its own.

Test (copy SpaceDub's `tests/DelayLineTests.cpp` check 12): with the guard alive,
`1e-37f × 0.01f == 0`; after it, the product is denormal again.

### B2. Snap smoothers to their target

Every `x += (target − x) × c` must snap when it gets close. MasterLoud already
does this (`MasterLoudDsp.cpp:432–437`):

```cpp
if (x != target)
{
	x += (target - x) * c;
	if (std::fabs (target - x) < 1e-9)
		x = target;                 // settled values are EXACT
}
```

That snap fixes the denormal and also makes a settled value exact, which keeps
"unity is bit-identical" tests honest. Some smoothers (R2Synth, FilterDrum,
SampleDrum) flush at `kDenormalFloor = 1e-25f` instead. That is also fine, but
it only works for a target of 0, while a snap works for every target.

### B3. Flush recursive filter state

Below a floor far under audibility, set state to zero. Use 1e-30 for doubles
(R2Synth's reasoning: far above the denormal range, and low enough not to
disturb a filter at a very low cutoff whose first pole runs at ~1e-15) and
1e-25 to 1e-30 for floats:

```cpp
z1 = ...; z2 = ...;
if (z1 > -1e-30 && z1 < 1e-30) z1 = 0.0;
if (z2 > -1e-30 && z2 < 1e-30) z2 = 0.0;
```

Then render something loud above the floor and confirm it is bit-identical to
before. Only values that were already below 1e-30 may change.

### B4. Oversampling an instrument voice

An instrument makes its own signal, so it needs **no upsampler**. Run the whole
voice (oscillators, filter, envelopes) at `N × fs`, then low-pass and decimate
once at the output. In outline:

1. `setSampleRate (fs × N)` on the voice DSP; every coefficient is already
   computed from the sample rate, so nothing else changes. **Keep user-facing
   ceilings (maximum cutoff and the like) tied to the host rate**, or the knob's
   top end moves.
2. Render `N` samples per output sample into a scratch buffer, sized in
   `setupProcessing` and never allocated on the audio thread.
3. Decimate with a half-band filter per 2× stage. A polyphase IIR half-band
   (allpass pairs, as in Laurent de Soras's HIIR) costs a few multiplies, adds
   no latency worth reporting, and reaches −100 dB rejection with about 8
   coefficients. A linear-phase FIR half-band is the alternative if phase
   matters; report its latency through `getLatencySamples`.
4. Expose the factor (Off / 2× / 4×) as a parameter or a fixed build constant.
   2× is enough for the cases measured here.

For an *effect* (a nonlinearity on the host's input), upsample first with the
same half-band, run the stage, then decimate.

### B5. Band-limited sample playback

Linear interpolation does nothing to stop aliasing when the read head moves
faster than 1 source sample per output sample. Measured with the exact
interpolation the plug-ins use: a source tone at `f` playing at speed `s`, with
`s·f` above Nyquist, comes out folded at **−0.5 to −3 dB**, i.e. barely
attenuated. Two ways to fix it:

* **When the ratio is fixed** (a file whose rate differs from the session's):
  resample the whole file once, at load or in `setupProcessing`, off the audio
  thread, with a proper windowed-sinc resampler. The audio thread then plays at
  a step of exactly 1, which is bit-identical to the file and costs nothing.
* **When the ratio varies** (tempo fitting): use windowed-sinc interpolation
  whose cutoff scales by `min (1, 1 / speed)`. A 16–32 tap Kaiser-windowed
  polyphase table, with the kernel stretched by `speed` when `speed > 1`, is
  standard. Or keep pre-filtered half-rate and quarter-rate copies of the
  sample (a mip-map, built at load), pick the copy by speed, and interpolate in
  that.

---

## C. This project's findings

### SampleDrum: aliasing, priority 7 (tidy); denormals handled

> **Status: fixed 1 Nov 2026** on SampleDrum branch `bandlimited-src` (see its `PORTING-NOTES.md` §13). This is not load-time resampling as suggested below: above a step of 1 a voice reads through a band-limited sinc bank, the same design as Project6's but longer. For a 96 kHz file at 48 kHz, 30 kHz → 18 kHz went from 0 dB to −98 dB. Steps of 1 or below are byte-identical to before.

**Denormals: handled.** `Smoother::next` flushes at 1e-25
(`SampleDrumDsp.cpp:46–51`). Voices end at the end of the file or the end of
the choke fade, and there is no other recursion. Add B1.

**Aliasing finding.** Playback interpolates linearly
(`SampleDrumDsp.cpp:437–452`) at `step = sourceRate / sessionRate`
(`:399`). There is no pitch control, so `step` differs from 1 only for sample
rate conversion:

* A **96 kHz file in a 48 kHz session** (step 2) folds everything between 24 and
  48 kHz in the recording straight down at nearly full level. A 96 kHz cymbal or
  hat recording does have energy there.
* 48 kHz in 44.1 kHz (step 1.088) folds only 22.05–24 kHz, which is minor.
* Steps below 1 (playing slower) cause mild imaging, which linear
  interpolation partly suppresses.

**Fix.** B5, fixed ratio: resample each sample to the session rate when it is
loaded and when `setupProcessing` changes the rate, off the audio thread, with
a windowed-sinc resampler. The voice then always runs at a step of exactly 1,
which the tests already hold to be bit-identical to the file. This also removes
the per-sample interpolation.

**Test.** Load a 96 kHz file holding a 30 kHz sine into a 48 kHz session.
Assert the output at the folded 18 kHz is below −80 dB. Today it is 0 dB:
at a step of exactly 2, linear interpolation is plain decimation.

---

## D. How this was measured

All measurements used each project's own SDK-free DSP sources, compiled
standalone with `c++ -std=c++17 -O2`. Nothing was changed in any project to
take them.

* **CPU:** x86_64 Linux. Wall time per second of 48 kHz audio, with and
  without MXCSR FTZ+DAZ set for the run.
* **Aliasing:** a 4-term Blackman-Harris FFT of the steady part of a render.
  "Worst spur" means the strongest bin more than 6 bins from any harmonic of
  the fundamental below 24 kHz, relative to the fundamental. "4×" and "2×" mean
  the same DSP rendered at 192 / 96 kHz and analysed below 24 kHz only, i.e.
  what oversampling with an ideal decimator would give. For FilterDrum, the
  level at each predicted fold frequency was read directly, because the noise
  excitation hides a spur search.
* **Smoother sticking:** `x += (0 − x) × c`, iterated 10⁸ times in double and
  float.

The harnesses are not kept in the projects. Turn the tests listed in each
section into the project's own `tests/DspTests.cpp` checks as the fixes go in,
so the numbers above become regression assertions.
