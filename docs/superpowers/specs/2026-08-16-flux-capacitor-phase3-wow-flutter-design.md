# Flux Capacitor — Phase 3: Wow and Flutter

## Context

This is Phase 3 of the retro-tape effects firmware described in
[`retro_tape_firmware_design.md`](../../../retro_tape_firmware_design.md).
That document lists five phases; this spec covers Phase 3 ("tape wow
and flutter"). Tape delay (Phase 4) and ATMOSPHERE saturation/lowpass,
REVERSE mode, and Shift+knob pages (Phase 5) remain out of scope and
will get their own specs.

Builds on Phase 1
([`2026-08-15-flux-capacitor-phase1-pitch-bend-design.md`](2026-08-15-flux-capacitor-phase1-pitch-bend-design.md)),
the WARP LED feedback spec
([`2026-08-16-flux-capacitor-warp-led-feedback-design.md`](2026-08-16-flux-capacitor-warp-led-feedback-design.md)),
and Phase 2
([`2026-08-16-flux-capacitor-phase2-tape-stop-design.md`](2026-08-16-flux-capacitor-phase2-tape-stop-design.md)),
all already shipped. `TapeVoice`, `TapeTransport`, `ComputeWarpSemitones`,
`ComputeMix`/`ComputeMixGains`, and `OnePoleSmoother` are reused
unchanged; this phase adds a new modulation source and layers its
output into the existing combined-semitone pitch pipeline.

## Why this is pitch-only, and why it's a third term in the same sum

The parent doc's pseudocode imagines wow/flutter modulating a stock
DaisySP `PitchShifter`'s internal flutter parameter, an effective
`speed` variable, and (in Phase 4) a delay read position. This
codebase's `TapeVoice` is a custom adaptation of `PitchShifter` that
takes a single continuous semitone value per sample and has no
internal flutter parameter of its own — and there's no delay line yet
(Phase 4). So the only modulation target that exists right now is the
same combined-semitone pipeline `warpSemis` and `StopSemitones(speed)`
already feed into:

```cpp
float wobble     = wowFlutter.Process(wowRateHz, flutterDepth);
float totalSemis = warpSemis + StopSemitones(speed) + wobble;
```

No amplitude modulation this phase (the parent doc lists it only as
optional for tape-stop, not wow/flutter) — keeping this phase to one
new signal (pitch) rather than two keeps the scope matched to the
phase's own title.

## Wow: a slow sine LFO, rate-controlled, fixed depth

```cpp
constexpr float kWowRateMinHz      = 0.1f;
constexpr float kWowRateMaxHz      = 2.0f;
constexpr float kWowDepthSemitones = 0.15f; // fixed; not knob-controlled

inline float ComputeWowRateHz(float reflect_knob, float reflect_cv)
{
    float combined = daisysp::fclamp(reflect_knob + reflect_cv, 0.0f, 1.0f);
    return daisysp::fmap(combined, kWowRateMinHz, kWowRateMaxHz, daisysp::Mapping::LOG);
}
```

`CV_REFLECT` sums with `KNOB_REFLECT` the same additive way `CV_MIX`
sums with `KNOB_MIX` (Phase 2) and `CV_WARP` sums with `KNOB_WARP`
(Phase 1) — both already-established knob+CV combine patterns in this
codebase. The log curve (rather than linear) is used because rate
knobs read more musically on a log scale — turning REFLECT from 0.1 Hz
to 0.2 Hz and from 1 Hz to 2 Hz should feel like comparable steps, not
wildly different ones.

Per the parent doc's control table, `KNOB_REFLECT`/`CV_REFLECT`
control wow **rate** only. There is no wow-depth knob in this phase:
`kWowDepthSemitones` is a fixed constant, so wow modulation is always
present at some rate — REFLECT changes how fast it wobbles, never
whether it wobbles. This matches the phase's confirmed design and
mirrors Phase 2's fixed (non-knob-controlled) stop ramp rate.

Wow itself is a `daisysp::Oscillator` in `WAVE_SIN` mode:

```cpp
wow_osc_.SetFreq(rate_hz);
float wow = wow_osc_.Process() * kWowDepthSemitones;
```

## Flutter: filtered white noise, depth-controlled, fixed band

```cpp
constexpr float kFlutterDepthMaxSemitones = 0.3f;

inline float ComputeFlutterDepthSemitones(float blur_knob, float blur_cv)
{
    float combined = daisysp::fclamp(blur_knob + blur_cv, 0.0f, 1.0f);
    return combined * kFlutterDepthMaxSemitones;
}
```

Same additive knob+CV combine pattern as wow/MIX/WARP, this time
linear (not log) since it's a depth, not a rate — matching
`ComputeMix`'s linear treatment of its own combined value.

Flutter's jitter comes from `daisysp::WhiteNoise` run through the
existing `OnePoleSmoother` used as an **audio-rate lowpass** — a
different role than its other two uses in this codebase
(`warpSmoother`/`mixSmoother` damp control-rate knob jitter at
`AudioCallbackRate()`). Reusing the class instead of adding a new
filter type keeps this phase from introducing a fourth DSP primitive
for one job `fonepole` already does.

```cpp
constexpr float kFlutterLpfTauSeconds = 0.015f; // ~10.6 Hz -3dB point
// cutoff_hz ~= 1 / (2 * PI_F * kFlutterLpfTauSeconds)
```

**This is the same fonepole gotcha Phase 2's ramp constant had to
document explicitly, and it bit this codebase for real once before**:
`fonepole`'s `coeff` parameter is `1.0f / (tau_seconds * rate)`, where
`tau_seconds` is an *exponential time constant*, not a settle duration
or a directly-pluggable cutoff frequency. Unlike `warpSmoother` and
`mixSmoother` (which use this coefficient against
`hw.AudioCallbackRate()`, the control-rate block frequency), the
flutter lowpass runs **per audio sample**, so its coefficient must be
computed against `hw.AudioSampleRate()` instead:

```cpp
flutter_lpf_coeff_ = 1.0f / (kFlutterLpfTauSeconds * sample_rate);
```

This coefficient is fixed and baked into `WowFlutter::Init()` — no
knob controls flutter's rate/bandwidth in this phase, only its depth
via BLUR.

```cpp
float noise    = flutter_noise_.Process();               // WhiteNoise, ~[-1, 1]
float filtered = flutter_lpf_.Process(noise, flutter_lpf_coeff_); // reused as an audio-rate LPF
float flutter  = filtered * flutter_depth_semitones;
```

## `WowFlutter` class

```cpp
class WowFlutter
{
  public:
    void Init(float sample_rate)
    {
        wow_osc_.Init(sample_rate);
        wow_osc_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        flutter_noise_.Init();
        flutter_lpf_.Init(0.0f);
        flutter_lpf_coeff_ = 1.0f / (kFlutterLpfTauSeconds * sample_rate);
        last_value_        = 0.0f;
    }

    // rate_hz, flutter_depth_semitones: already control-rate smoothed by
    // the caller (main.cpp), same as warpSemis/mix before reaching here.
    // Must be called once per audio sample -- it advances the wow
    // oscillator's phase and the flutter noise/filter state.
    float Process(float rate_hz, float flutter_depth_semitones)
    {
        wow_osc_.SetFreq(rate_hz);
        float wow = wow_osc_.Process() * kWowDepthSemitones;

        float noise    = flutter_noise_.Process();
        float filtered = flutter_lpf_.Process(noise, flutter_lpf_coeff_);
        float flutter  = filtered * flutter_depth_semitones;

        last_value_ = wow + flutter;
        return last_value_;
    }

    // Last combined semitone offset, read from the main loop for LED display.
    float Value() const { return last_value_; }

  private:
    daisysp::Oscillator wow_osc_;
    daisysp::WhiteNoise  flutter_noise_;
    OnePoleSmoother       flutter_lpf_; // reused as an audio-rate LPF, not a control smoother
    float                 flutter_lpf_coeff_ = 0.0f;
    float                 last_value_        = 0.0f;
};
```

Lives in a new `wow_flutter.h`, alongside `ComputeWowRateHz`,
`ComputeFlutterDepthSemitones`, and the LED helper below — following
`tape_transport.h`'s precedent of pairing pure control-mapping
functions with the stateful class that consumes them in one file,
since (like `TapeTransport`, and unlike stateless `warp_control.h`/
`mix_control.h`) `WowFlutter` carries real per-sample state.

## Audio callback integration

```cpp
float rawWowRateHz = ComputeWowRateHz(hw.GetKnobValue(KNOB_REFLECT), hw.GetCvValue(CV_REFLECT));
float wowRateHz     = reflectSmoother.Process(rawWowRateHz, reflectSmoothCoeff);

float rawFlutterDepth = ComputeFlutterDepthSemitones(hw.GetKnobValue(KNOB_BLUR), hw.GetCvValue(CV_BLUR));
float flutterDepth     = blurSmoother.Process(rawFlutterDepth, blurSmoothCoeff);

for (size_t i = 0; i < size; i++)
{
    float wobble     = wowFlutter.Process(wowRateHz, flutterDepth);
    float totalSemis = warpSemis + StopSemitones(speed) + wobble;

    float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
    float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

    out[0][i] = in[0][i] * dryGain + wetL * wetGain;
    out[1][i] = in[1][i] * dryGain + wetR * wetGain;
}
```

`reflectSmoother`/`blurSmoother` are two more `OnePoleSmoother`
instances (the same shared class `warpSmoother`/`mixSmoother` already
use), with coefficients computed the same
`1.0f / (seconds * hw.AudioCallbackRate())` way, against a proposed
`kReflectSmoothTimeSeconds = kBlurSmoothTimeSeconds = 0.02f` (matching
`kWarpSmoothTimeSeconds`/`kMixSmoothTimeSeconds`). `wowFlutter.Process`
itself moves inside the per-sample loop — unlike `warpSemis`/`mix`,
which are computed once per block, wow/flutter's oscillator phase and
filter state must advance every sample.

**Performance note for hardware verification**: `TapeVoice::UpdateSemitones`
currently short-circuits (`if (semitones == last_semitones_) return;`),
skipping its `powf()`-based `ComputeModFreq` call whenever the combined
semitone value repeats across samples — true today whenever WARP is
centered and the tape isn't stopping. Once `wobble` is summed in,
`totalSemis` changes on essentially every sample (both from the
continuously-varying wow sine and the flutter noise), so this
short-circuit stops firing in practice and `ComputeModFreq` runs every
sample for both voices. This is expected to be well within the H750's
budget at 48 kHz, but is worth confirming has no audible/measurable
cost during hardware verification rather than pre-optimizing here.

## LED integration: brightness scaling on the pitch-shift bar-graph

```cpp
constexpr float kWowFlutterLedMaxSemitones      = kWowDepthSemitones + kFlutterDepthMaxSemitones; // 0.45f
constexpr float kWowFlutterLedMinBrightnessScale = 0.5f;

inline float ComputeWowFlutterLedScale(float combined_semitones)
{
    float normalized = daisysp::fclamp(
        combined_semitones / kWowFlutterLedMaxSemitones, -1.0f, 1.0f); // -1..1
    return daisysp::fmap(
        (normalized + 1.0f) * 0.5f, kWowFlutterLedMinBrightnessScale, 1.0f);
}
```

In the main loop, after computing `WarpLedLevels` as today:

```cpp
float ledScale = ComputeWowFlutterLedScale(wowFlutter.Value());
for (int i = 0; i < 3; i++)
{
    hw.SetLed(upLeds[i],
              kWarpUpColor[0] * levels.up[i] * ledScale,
              kWarpUpColor[1] * levels.up[i] * ledScale,
              kWarpUpColor[2] * levels.up[i] * ledScale);
    hw.SetLed(downLeds[i],
              kWarpDownColor[0] * levels.down[i] * ledScale,
              kWarpDownColor[1] * levels.down[i] * ledScale,
              kWarpDownColor[2] * levels.down[i] * ledScale);
}
```

Applies uniformly to `LED_1`-`LED_6` (the WARP pitch-shift bar-graph)
only — `LED_FREEZE` stays driven purely by `1.0f - transport.Speed()`,
unrelated to this scale, since it represents transport state rather
than the pitch-shift display. `wowFlutter.Value()` is read from the
main loop the same way `transport.Speed()` and `warpSmoother.Value()`
already are, despite being written from the audio callback — an
existing, accepted pattern in this codebase, not something this phase
changes. Because `levels.up[i]`/`levels.down[i]` are already `0.0f`
wherever no bar-graph LED is lit, multiplying by `ledScale` is a no-op
there — no extra "is anything lit" branch needed.

## Testing plan

Host-testable (doctest, same pattern as `test_warp_control.cpp` /
`test_mix_control.cpp`):

- `ComputeWowRateHz`: `knob=0` → `kWowRateMinHz`; `knob=1` →
  `kWowRateMaxHz`; combined value clamps to `[0, 1]` before the log
  mapping; result always within `[kWowRateMinHz, kWowRateMaxHz]`.
- `ComputeFlutterDepthSemitones`: `knob=0` → `0`; `knob=1` →
  `kFlutterDepthMaxSemitones`; additive knob+CV, clamped.
- `ComputeWowFlutterLedScale`: `combined=0` → the breathing range's
  midpoint, `0.75` (halfway between `kWowFlutterLedMinBrightnessScale`
  and `1.0`) — the LEDs breathe symmetrically between 50% and 100%
  brightness as the wobble signal swings positive/negative, they are
  *not* pinned to full brightness at rest; `combined =
  +kWowFlutterLedMaxSemitones` → `1.0`; `combined =
  -kWowFlutterLedMaxSemitones` → `kWowFlutterLedMinBrightnessScale`;
  values beyond the max clamp rather than overflow.
- `WowFlutter`: lighter-touch than the pure functions above, since its
  output mixes a deterministic oscillator with nondeterministic noise.
  Assert what's actually guaranteed: output stays bounded within
  `+/-(kWowDepthSemitones + kFlutterDepthMaxSemitones)` across many
  `Process()` calls; `Value()` always equals the most recent
  `Process()` return; `flutter_depth_semitones = 0` collapses flutter's
  contribution to exactly `0` regardless of the noise/filter state
  (isolates the wow-only sine, which *is* fully checkable against
  `sinf` at a known phase).

Hardware verification (acceptance criteria for Phase 3):

- With REFLECT at minimum: a slow, smooth pitch drift (roughly 0.1 Hz)
  is audible even with BLUR at 0.
- Sweeping REFLECT from minimum to maximum audibly speeds up the wow
  cycle, log-curved (feels like even musical steps, not front-loaded).
- With BLUR at 0: no audible fast jitter, only wow. Raising BLUR adds
  an increasingly audible fast, irregular (not periodic-sounding)
  flutter on top.
- WARP bend and tape-stop still work normally and audibly compose with
  wow/flutter (e.g. a WARP offset plus wow/flutter still center-wobbles
  around the bent pitch, not the unbent one).
- LED_1-6 visibly breathe/flicker in brightness in sync with the
  audible wobble whenever a bar-graph LED is lit (WARP off-center);
  LED_FREEZE's brightness continues to track `1 - speed` only, with no
  visible wow/flutter flicker.
- No audible pop, glitch, or CPU-starvation artifact (dropouts,
  crackle) introduced by `TapeVoice`'s short-circuit no longer firing
  every sample.

## Out of scope (deferred to later phases per the parent doc)

Tape delay and its wow/flutter-modulated read position, amplitude
modulation from wow/flutter, ATMOSPHERE saturation/lowpass filtering,
REVERSE mode, Shift+knob secondary pages, wow-depth and flutter-rate
knob control (both fixed constants for now, matching Phase 2's
precedent of leaving TIME/ramp-rate fixed until a later phase's scope
calls for it).
