# Flux Capacitor — Phase 4: Tape Delay

## Context

This is Phase 4 of the retro-tape effects firmware described in
[`retro_tape_firmware_design.md`](../../../retro_tape_firmware_design.md).
That document lists five phases; this spec covers Phase 4 ("tape-style
delay"). ATMOSPHERE saturation/lowpass control, REVERSE mode, and
Shift+knob pages (Phase 5) remain out of scope and will get their own
spec.

Builds on Phase 1
([`2026-08-15-flux-capacitor-phase1-pitch-bend-design.md`](2026-08-15-flux-capacitor-phase1-pitch-bend-design.md)),
the WARP LED feedback spec
([`2026-08-16-flux-capacitor-warp-led-feedback-design.md`](2026-08-16-flux-capacitor-warp-led-feedback-design.md)),
Phase 2
([`2026-08-16-flux-capacitor-phase2-tape-stop-design.md`](2026-08-16-flux-capacitor-phase2-tape-stop-design.md)),
and Phase 3
([`2026-08-16-flux-capacitor-phase3-wow-flutter-design.md`](2026-08-16-flux-capacitor-phase3-wow-flutter-design.md)).
`TapeVoice`, `TapeTransport`, `WowFlutter`, `OnePoleSmoother`, and the
existing MIX dry/wet blend are reused unchanged; this phase adds a new
stage between `TapeVoice` and the MIX blend, and reuses `WowFlutter`'s
existing per-sample output as a second modulation target rather than
adding a new modulation source.

Phase 3's implementation (branch `flux-capacitor-wow-flutter`) is
complete and review-clean but not yet merged to `main`. This phase's
worktree/branch (`flux-capacitor-tape-delay`) branches from
`flux-capacitor-wow-flutter` directly rather than waiting on that
merge.

## Signal chain placement

```
Input -> TapeVoice (WARP + stop + wow/flutter pitch, unchanged) -> TapeDelay -> MIX dry/wet (unchanged)
```

`TapeDelay` sits on the already pitch-shifted signal, matching the
parent doc's pseudocode (`tapeDelay.Process(shifted, ...)`, where
`shifted` is the pitch shifter's output). MIX continues to blend
against the raw input exactly as today — `TapeDelay`'s output simply
replaces `TapeVoice`'s output as the "wet" signal feeding
`ComputeMixGains`. This is a stereo pair, `delayL`/`delayR`, mirroring
`voiceL`/`voiceR`: each channel is an independent delay with its own
feedback loop, not cross-coupled — the simplest option that preserves
the existing stereo image, matching how `TapeVoice` already treats L/R
as two independent mono voices rather than introducing new
cross-channel wiring.

## Buffer sizing and memory placement

TIME's maximum (2 seconds, see below) at 48kHz is 96,000 samples.
`DelayLine<float, N>` stores `N` samples as `T` (4 bytes for `float`),
so one channel's buffer is 384KB; two channels (stereo) is ~750KB.
`TapeVoice` already uses 65,536 of Aurora's 128KB DTCMRAM budget for
its two `DelayLine<float, 4096>` taps (see `tape_voice.h`) — there is
nowhere near enough DTCMRAM headroom left for `TapeDelay`'s buffers.

The Daisy Seed (Aurora's underlying hardware) has a separate 64MB
external SDRAM region, exposed by libDaisy via a linker-section
attribute macro:

```cpp
// lib/Aurora-SDK/libs/libDaisy/src/dev/sdram.h
#define DSY_SDRAM_BSS __attribute__((section(".sdram_bss")))
```

`TapeDelay`'s two `DelayLine<float, kTapeDelayMaxSamples>` members are
declared with this attribute, placing ~750KB total in the 64MB SDRAM
region — trivial headroom there, and it does not compete with
`TapeVoice`'s DTCMRAM usage at all. `kTapeDelayMaxSamples` is a fixed
compile-time constant (96,000, i.e. 2s at a nominal 48kHz), following
`tape_voice.h`'s existing precedent of sizing `DelayLine`'s template
parameter as a fixed constant rather than dynamically from
`hw.AudioSampleRate()`.

## Control mapping: TIME -> delay time

```cpp
constexpr float kDelayTimeMinSeconds = 0.001f;
constexpr float kDelayTimeMaxSeconds = 2.0f;

inline float ComputeDelayTimeSeconds(float time_knob, float time_cv)
{
    float combined = daisysp::fclamp(time_knob + time_cv, 0.0f, 1.0f);
    return daisysp::fmap(combined, kDelayTimeMinSeconds, kDelayTimeMaxSeconds, daisysp::Mapping::LOG);
}
```

`CV_TIME` sums with `KNOB_TIME` the same additive way established by
`CV_WARP`/`KNOB_WARP` (Phase 1), `CV_MIX`/`KNOB_MIX` (Phase 2), and
`CV_REFLECT`/`CV_BLUR` (Phase 3). Log curve for the same reason
`ComputeWowRateHz` uses one: a time/rate control reads more musically
on a log scale — the gap between 1ms and 10ms should feel comparable
to the gap between 1s and 2s, not swamped by it. TIME and its CV are
completely unused by the firmware before this phase, confirmed by
grepping `main.cpp` for `KNOB_TIME`/`CV_TIME` — no existing behavior to
preserve or conflict with.

The resulting base delay time is smoothed at control rate before
reaching `TapeDelay`, using the same `OnePoleSmoother` pattern as
`warpSmoother`/`mixSmoother`/`reflectSmoother`/`blurSmoother`, but with
a slower time constant:

```cpp
constexpr float kTimeSmoothTimeSeconds = 0.08f; // slower than the other 0.02f smoothers
```

The other control smoothers use 0.02s because they damp knob jitter
without being heard as anything other than "clean". TIME is different:
jumping the delay length on a live delay line produces an audible
pitch/scrub warble as the read head sweeps to its new position — this
is genuine, expected tape-echo character (real tape delay units warble
when you turn their time knob), not a glitch to eliminate. A 0.02s
constant would make that sweep fast enough to read as a click/zipper
artifact instead of a warble; 0.08s keeps TIME responsive while giving
the sweep enough duration to sound intentional.

## Speed coupling (control rate)

```cpp
inline float ComputeDelaySamples(float base_seconds, float speed, float sample_rate)
{
    float base_samples = base_seconds * sample_rate;
    return daisysp::fclamp(base_samples / daisysp::fmax(speed, kMinStopSpeed),
                            0.0f, static_cast<float>(kTapeDelayMaxSamples - 1));
}
```

Reuses `tape_transport.h`'s existing `kMinStopSpeed` constant (already
used by `StopSemitones` to avoid `log2f(0)`) rather than introducing a
second floor constant for the same purpose. As `speed` falls during a
tape-stop, `base_samples / speed` grows, stretching the delay time —
echoes lengthen and slow as the tape brakes, matching the parent doc's
"delay tails slow during stop." Near a full stop the computed value
would be enormous; the `fclamp` upper bound catches this and pins the
delay at `kTapeDelayMaxSamples - 1`, which reads as "tails freeze into
one long, frozen echo" rather than any error condition or discontinuity
— a real, intentional edge-of-range behavior rather than a corner case
to special-case around.

This computation happens once per audio block in `TapeDelay::Update`
(alongside consuming the smoothed base delay time), not per-sample —
matching how `TapeTransport::Update` and the other control-rate
`Update`/`Compute*` calls already run at block rate in `main.cpp`.

## Wow/flutter coupling (audio rate)

`WowFlutter::Process` already computes one combined wow+flutter
semitone offset per sample for `TapeVoice`. `TapeDelay` reuses that
exact same value as a second modulation target — no second `WowFlutter`
instance, no second modulation source to tune or keep in phase. This
is also physically honest: on a real tape machine, one wobbling
transport drives pitch and echo-timing deviation together, from the
same underlying speed variation.

```cpp
inline float ApplyWobbleToDelaySamples(float delay_samples, float wobble_semitones)
{
    float ratio = powf(2.0f, wobble_semitones / 12.0f);
    return delay_samples * ratio;
}
```

Called once per sample inside `TapeDelay::Process`, after `Update`'s
control-rate target has already folded in TIME and speed. The result
feeds `DelayLine::SetDelay` directly — `SetDelay(float)` already
performs linear interpolation between adjacent samples (see
`delayline.h`), the same fractional-delay mechanism `TapeVoice`
already relies on for its own crossfaded taps, so no new interpolation
logic is needed here.

## Feedback path

```cpp
constexpr float kTapeDelayFeedback = 0.35f; // fixed; no control surface yet

constexpr float kTapeDelayFeedbackLpfTauSeconds = 0.001f; // ~159 Hz -3dB point, audio-rate coeff
```

Aurora exposes six knobs, and by this phase all six have an assigned
primary function (WARP, TIME, BLUR, REFLECT, MIX — ATMOSPHERE remains
reserved for Phase 5's saturation/tone work per the parent doc's own
phase split). There is no free control for feedback amount yet, so
`kTapeDelayFeedback` is a fixed constant tuned for a handful of
audible, decaying repeats — the same "fixed for now, matches an
established codebase precedent of deferring a knob until a later
phase's scope calls for it" reasoning Phase 2 applied to the stop-ramp
rate and Phase 3 applied to wow's depth. A feedback control (e.g. an
ATMOSPHERE secondary/Shift page) can be added later without changing
`TapeDelay`'s public interface.

Undamped feedback would let high-frequency content build up
indefinitely at the repeat rate, both harsh-sounding and a stability
risk near `kTapeDelayFeedback`'s upper range. A single fixed-cutoff
one-pole lowpass in the feedback loop is enough to keep repeats
decaying and to darken them tape-style. This reuses `OnePoleSmoother`
as an **audio-rate filter**, the same repurposing trick
`WowFlutter`'s flutter noise LPF already established:

```cpp
float wet      = delay_.Read();
float filtered = feedback_lpf_.Process(wet, feedback_lpf_coeff_);
delay_.Write(in + filtered * kTapeDelayFeedback);
```

**Same `fonepole` time-constant gotcha Phase 3 had to document
explicitly applies here**: `feedback_lpf_coeff_` must be computed
against `hw.AudioSampleRate()` (this filter runs per audio sample), not
`hw.AudioCallbackRate()` (the control-rate block frequency the other
`OnePoleSmoother` instances use) — `kTapeDelayFeedbackLpfTauSeconds` is
an exponential time constant, not a directly-pluggable cutoff
frequency or settle duration.

No saturation this phase — deferred to Phase 5 alongside the rest of
ATMOSPHERE's tone-shaping role, per the parent doc's own phase
breakdown.

## `TapeDelay` class

```cpp
class TapeDelay
{
  public:
    void Init(float sample_rate)
    {
        sr_ = sample_rate;
        delay_.Init();
        feedback_lpf_.Init(0.0f);
        feedback_lpf_coeff_ = 1.0f / (kTapeDelayFeedbackLpfTauSeconds * sample_rate);
        target_samples_     = 0.0f;
    }

    // base_seconds: TIME knob+CV, already control-rate smoothed by the
    // caller. speed: TapeTransport::Speed(). Called once per audio block.
    void Update(float base_seconds, float speed)
    {
        target_samples_ = ComputeDelaySamples(base_seconds, speed, sr_);
    }

    // wobble_semitones: WowFlutter::Process's return value for this
    // sample -- same value TapeVoice already consumed for pitch this
    // sample. Must be called once per audio sample.
    float Process(float in, float wobble_semitones)
    {
        float samples = ApplyWobbleToDelaySamples(target_samples_, wobble_semitones);
        delay_.SetDelay(samples);

        float wet      = delay_.Read();
        float filtered = feedback_lpf_.Process(wet, feedback_lpf_coeff_);
        delay_.Write(in + filtered * kTapeDelayFeedback);

        return wet;
    }

  private:
    daisysp::DelayLine<float, kTapeDelayMaxSamples> DSY_SDRAM_BSS delay_;
    OnePoleSmoother                                                feedback_lpf_; // audio-rate LPF, not a control smoother
    float                                                          feedback_lpf_coeff_ = 0.0f;
    float                                                          target_samples_      = 0.0f;
    float                                                          sr_                  = 48000.0f;
};
```

Lives in a new `tape_delay.h`, following `wow_flutter.h`/
`tape_transport.h`'s precedent of pairing pure control-mapping
functions (`ComputeDelayTimeSeconds`, `ComputeDelaySamples`,
`ApplyWobbleToDelaySamples`) with the stateful class that consumes
them in one file, since `TapeDelay` carries real per-sample state like
its stateful siblings (and unlike stateless `warp_control.h`).

## Audio callback integration

```cpp
float rawDelaySeconds = ComputeDelayTimeSeconds(hw.GetKnobValue(KNOB_TIME), hw.GetCvValue(CV_TIME));
float delaySeconds     = timeSmoother.Process(rawDelaySeconds, timeSmoothCoeff);

delayL.Update(delaySeconds, speed);
delayR.Update(delaySeconds, speed);

for (size_t i = 0; i < size; i++)
{
    float wobble     = wowFlutter.Process(wowRateHz, flutterDepth);
    float totalSemis = warpSemis + StopSemitones(speed) + wobble;

    float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
    float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

    wetL = delayL.Process(wetL, wobble);
    wetR = delayR.Process(wetR, wobble);

    out[0][i] = in[0][i] * dryGain + wetL * wetGain;
    out[1][i] = in[1][i] * dryGain + wetR * wetGain;
}
```

`delayL`/`delayR` join `voiceL`/`voiceR`/`transport`/`wowFlutter` as
top-level globals; `timeSmoother`/`timeSmoothCoeff` join the existing
smoother/coefficient pairs, initialized the same way in `main()`
(`timeSmoother.Init(0.0f)`, `timeSmoothCoeff = 1.0f /
(kTimeSmoothTimeSeconds * hw.AudioCallbackRate())`). `delayL.Init`/
`delayR.Init` are called alongside `voiceL.Init`/`voiceR.Init` in
`main()`. `wobble` (already computed this sample for `totalSemis`) is
passed straight through to both `TapeDelay::Process` calls — no
recomputation. `TapeDelay::Update` is called once per block, immediately
after `speed` is available, mirroring where `ComputeMix`/
`ComputeEffectiveMix` already run at block rate today. No changes to
`ComputeMixGains`, `dryGain`/`wetGain`, or the MIX auto-wet/LED-flash
logic — `TapeDelay` only changes what `wetL`/`wetR` mean before that
blend, not how the blend itself works.

No LED changes this phase: the parent doc's control table has no LED
tied to TIME or delay feedback, and neither existing LED display
(WARP bar-graph, LED_FREEZE) represents anything TapeDelay changes.

## Testing plan

Host-testable (doctest, same pattern as `test_wow_flutter.cpp` /
`test_tape_voice.cpp` — `TapeDelay` is buildable host-side exactly like
`TapeVoice` since both depend only on `Utility/delayline.h` and
`Utility/dsp.h`, not any hardware-specific header):

- `ComputeDelayTimeSeconds`: `knob=0` -> `kDelayTimeMinSeconds`;
  `knob=1` -> `kDelayTimeMaxSeconds`; combined value clamps to `[0, 1]`
  before the log mapping; result always within `[kDelayTimeMinSeconds,
  kDelayTimeMaxSeconds]`.
- `ComputeDelaySamples`: `speed=1.0` -> `base_seconds * sample_rate`
  (unscaled); `speed < 1.0` increases the result (echoes stretch);
  result never exceeds `kTapeDelayMaxSamples - 1` even for
  pathologically small `speed` (verifies the clamp actually catches the
  near-zero-speed blowup case, not just typical values); `speed = 0`
  and `speed` below `kMinStopSpeed` are both clamped identically since
  `ComputeDelaySamples` floors through `kMinStopSpeed` before dividing.
- `ApplyWobbleToDelaySamples`: `wobble_semitones = 0` -> input unchanged
  (`ratio == 1.0`); `wobble_semitones = +12` -> doubles the sample
  count (`ratio == 2.0`, one octave up in tape-speed terms); `-12` ->
  halves it; matches the same `powf(2.0f, semitones / 12.0f)` relation
  `tape_voice.h`'s `ComputeModFreq` already uses for its own
  ratio-from-semitones conversion.
- `TapeDelay`: feed a unit impulse, assert the first echo peak lands at
  the expected sample offset for a known `Update(base_seconds, speed=1.0)`
  target (accounting for `DelayLine`'s linear interpolation smearing
  it slightly across two samples); assert successive echo peaks decay
  (each strictly quieter than the last, consistent with
  `kTapeDelayFeedback < 1.0`); assert output stays bounded (no runaway
  growth) across many blocks with `kTapeDelayFeedback` at its
  configured value; assert `speed` well below 1.0 measurably lengthens
  the gap between echo peaks relative to `speed = 1.0`.

Hardware verification (acceptance criteria for Phase 4):

- With TIME at minimum: a short, tight slapback echo. Sweeping TIME
  toward maximum smoothly lengthens the echo spacing up to a
  multi-second delay, log-curved (feels like even musical steps across
  the knob's travel, not front-loaded).
- Turning TIME while audio is playing produces an audible warble/pitch
  sweep on the repeats as the read head moves to the new time, not a
  click or dropout.
- Repeats are audibly darker than the dry signal and decay to silence
  over a handful of repeats at the fixed feedback amount — no runaway
  buildup, no harsh aliasing/ringing.
- Engaging FREEZE audibly stretches and slows any currently-decaying
  echo tail as the tape brakes, eventually freezing into one long
  sustained echo near a full stop, then returning to normal echo
  spacing as playback resumes.
- REFLECT/BLUR's wow/flutter is now audible on the echo repeats
  themselves (a wavering echo pitch/timing), not just on the direct
  pitch-shifted signal, and the two stay in sync (same underlying
  wobble source) rather than drifting independently.
- WARP bend, tape-stop, and MIX dry/wet all continue to work normally
  and audibly compose with the new delay stage.
- No audible pop, glitch, or CPU-starvation artifact (dropouts,
  crackle) introduced by the new SDRAM-resident delay buffers or the
  added per-sample `powf` call in `ApplyWobbleToDelaySamples`.

## Out of scope (deferred to later phases per the parent doc)

ATMOSPHERE-controlled feedback amount, tone shaping, and saturation;
REVERSE mode (which the parent doc notes affects delay read
direction); Shift+knob secondary pages; cross-channel (ping-pong)
feedback; any dedicated feedback-amount knob/CV (fixed constant for
now, matching Phase 2's stop-ramp-rate and Phase 3's wow-depth
precedent of leaving a parameter fixed until a later phase's scope
calls for it).
