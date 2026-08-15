# Flux Capacitor — Phase 1: Base Pitch Bend

## Context

This is Phase 1 of the retro-tape effects firmware described in
[`retro_tape_firmware_design.md`](../../../retro_tape_firmware_design.md).
That document lists five phases; this spec covers only Phase 1: "WARP
knob/CV → pitch bend, verify smooth pitch tracking." Later phases (tape
stop, wow/flutter, tape delay, polish/modes) are out of scope here and
will get their own specs once this one ships.

New project directory: `flux-capacitor/`, alongside the existing
`hello-aurora/` example, following the same structure (`main.cpp`, a
`Makefile` including `../config.mk`, and a `tests/` directory with
host-side doctest tests).

## Problem with the doc's literal pseudocode

The design doc's pseudocode calls `PitchShifter::SetTranspose()`.
Reading the actual DaisySP source
(`Source/Effects/pitchshifter.h`), the real API is
`SetTransposition(float)`, and critically: it computes its pitch ratio
from a 12-entry semitone lookup table indexed by
`(uint8_t)fabsf(transpose)` — the fractional part of `transpose` is
discarded. This means `PitchShifter` only supports **integer semitone
steps**, not the continuous "smooth pitch tracking" Phase 1 asks for.
`hw.GetWarpVoct()` already returns a continuous -60..+60 semitone float
from the CV input, so feeding it into `SetTransposition()` as-is would
produce audible stepping at semitone boundaries.

## Approach: `TapeVoice`

`PitchShifter` solves a hard problem well: indefinite pitch shift
without buffer overrun, using two overlapping `DelayLine` taps
crossfaded via `Phasor`-driven LFOs (each tap periodically resets to
zero delay while the other is audible, so there's no glitch from
running out of buffer). The only thing to change is how the crossfade
rate is computed.

`TapeVoice` copies that same two-tap/crossfade structure, replacing the
quantized lookup with a continuous formula:

```cpp
float ratio = powf(2.0f, semitones / 12.0f);       // continuous, no table
mod_freq_   = ((ratio - 1.0f) * sr_) / del_size_;   // same as PitchShifter, but ratio is continuous
```

Everything else — the two `DelayLine<float, SHIFT_BUFFER_SIZE>` taps,
the `Phasor`-driven crossfade, the sine-windowed gain on each tap — is
carried over from `PitchShifter` unchanged. This keeps the
well-tested glitch-free-looping behavior while removing the semitone
quantization.

One `TapeVoice` instance per audio channel (`voiceL`, `voiceR`), both
driven by the same `semitones` value each block, for true stereo
processing (the module's audio I/O is true stereo — Phase 1 does not
sum to mono like the doc's pseudocode does).

`speed`/`ratio` in `TapeVoice::Process()` is the same concept later
phases build on: Phase 2 ties tape-stop to this same speed value, and
Phase 4's tape delay reuses the delay-line-driven-by-speed idea. Phase
1 doesn't build a shared `TapeTransport` class yet — that's Phase 2 —
but `TapeVoice`'s interface (`Process(float in, float semitones)`) is
written to make that extension straightforward later.

## Control mapping

```cpp
float knobSemis = fmap(hw.GetKnobValue(KNOB_WARP), -12.f, 12.f); // knob=0.5 -> 0
float cvSemis   = hw.GetWarpVoct();                               // continuous, -60..+60
float semitones = ComputeWarpSemitones(knobSemis, cvSemis);       // sum + one-pole smoothing
```

`ComputeWarpSemitones()` is a pure free function (sum, then
`fonepole` smoothing to damp ADC/knob jitter — not for pitch glide,
which `TapeVoice`'s crossfade mechanism already handles glitch-free).
Pulling it out as a free function makes it host-testable without
hardware, following the pattern of `hello-aurora`'s `colour.h`/`audio.h`.

KNOB_WARP's secondary Shift function (selecting the pitch bend range,
per the doc's control table) is out of scope for Phase 1 — Shift+knob
pages are Phase 5. Phase 1 uses a fixed ±12 semitone knob range.

## Audio callback

```cpp
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();
    float semitones = ComputeWarpSemitones(
        fmap(hw.GetKnobValue(KNOB_WARP), -12.f, 12.f),
        hw.GetWarpVoct());

    for (size_t i = 0; i < size; i++)
    {
        out[0][i] = voiceL.Process(in[0][i], semitones);
        out[1][i] = voiceR.Process(in[1][i], semitones);
    }
}
```

## Testing plan

Host-testable (no hardware, doctest, same pattern as `hello-aurora/tests`):

- `ComputeWarpSemitones()` — knob/CV → semitones math: zero at knob
  center + zero CV, ±12 semitone knob range, summation with CV,
  smoothing behavior.
- `TapeVoice` core behavior — feed a synthetic sine buffer at a known
  frequency, verify the output frequency shifts by the expected ratio
  for a few `semitones` values, and verify no discontinuity/glitch
  across a crossfade cycle. Pure C++ (like DaisySP itself), no Daisy
  hardware needed.

Hardware verification (acceptance criteria for Phase 1):

- Patch a slow LFO or manual CV sweep into `CV_WARP`; pitch should
  glide continuously with no audible stepping.
- Turning `KNOB_WARP` shifts the center pitch as expected, independent
  of CV.
- No zipper noise from knob/CV jitter at a static setting.

## Out of scope (deferred to later phases per the parent doc)

FREEZE/tape-stop transport, wow/flutter, tape delay, saturation/
filtering (ATMOSPHERE), REVERSE mode, Shift+knob secondary pages.
