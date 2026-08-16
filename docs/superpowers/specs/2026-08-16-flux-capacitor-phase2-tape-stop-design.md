# Flux Capacitor — Phase 2: Tape Stop + MIX Dry/Wet

## Context

This is Phase 2 of the retro-tape effects firmware described in
[`retro_tape_firmware_design.md`](../../../retro_tape_firmware_design.md).
That document lists five phases; this spec covers Phase 2 ("tape stop")
and pulls the MIX knob's dry/wet role forward from the doc's original
signal-flow pseudocode, since tape-stop is the first effect in this
firmware that produces a signal genuinely different from its input —
the natural point to introduce a bypass-style blend. Later phases (wow
and flutter, tape delay, polish/modes including REVERSE and Shift+knob
pages) remain out of scope and will get their own specs.

Builds on Phase 1
([`2026-08-15-flux-capacitor-phase1-pitch-bend-design.md`](2026-08-15-flux-capacitor-phase1-pitch-bend-design.md))
and the WARP LED feedback spec
([`2026-08-16-flux-capacitor-warp-led-feedback-design.md`](2026-08-16-flux-capacitor-warp-led-feedback-design.md)),
both already shipped. `TapeVoice` and `ComputeWarpSemitones` are reused
unchanged; this phase adds a new transport/speed layer, a new MIX
blend, and LED feedback for `SW_FREEZE`/`GATE_FREEZE`.

## Approach: continuous speed chasing a target, not a rigid FSM

The parent doc's pseudocode describes a four-state machine (`PLAY →
STOPPING → STOPPED → STARTING → PLAY`) with separate ramp logic per
transition. This spec simplifies that to a single continuous `speed`
(0..1) that one-pole-smooths toward a `target` (0 or 1) — the same
`fonepole` mechanism Phase 1's `WarpSmoother` already uses for
WARP semitones, with a snap-to-target epsilon so `speed` reaches
*exactly* 0.0 or 1.0 rather than approaching it asymptotically forever.

This collapses "STOPPING vs STOPPED" and "STARTING vs PLAY" into
"target is 0" / "target is 1" respectively, which has a real behavioral
payoff: catching `SW_FREEZE` mid-ramp just flips `target` and the
existing smoother reverses direction on its own — no extra logic to
detect which of the four states you're currently in before deciding
how to transition out of it.

```cpp
class TapeTransport
{
  public:
    void Init(float initial_speed = 1.0f);

    // freeze_edge: true on the block SW_FREEZE was just pressed (toggles target).
    // gate_high: current GATE_FREEZE level; forces target = 0 while true,
    //            overriding the button, and releases control back to the
    //            button's last toggle when it goes low. Unpatched gates
    //            depend on the external inverting input circuit's idle
    //            state (GateIn has no internal pull resistor), expected
    //            to read as "not high", so this is a no-op when nothing
    //            is patched.
    void Update(bool freeze_edge, bool gate_high, float ramp_coeff);

    float Speed() const;   // 0..1, current smoothed transport speed

  private:
    float speed_        = 1.0f;
    float button_target_ = 1.0f;  // 0 or 1; flipped by freeze_edge
};
```

`ramp_coeff` is a single fixed constant tuned for roughly a 1.5 second
stop/start settle time (same `1.0f / (seconds * hw.AudioCallbackRate())`
pattern `main.cpp` already uses for `warpSmoothCoeff`). Note that the
`seconds` value in this formula is `fonepole`'s exponential time
constant τ, not the settle duration itself — since `TapeTransport`
snaps to its target once within `kSnapEpsilon` (0.001), the actual
settle time is `τ · ln(1/kSnapEpsilon) ≈ τ · 6.9`, so implementers
should size `seconds` accordingly (τ ≈ 0.217s for a ~1.5s settle), not
plug the desired settle time in directly. No knob controls ramp rate in
this phase — TIME stays unused until Phase 4's delay, matching the
parent doc's own phasing (Shift+TIME ramp-curve control is Phase 5
Polish).

### Speed → pitch and amplitude

```cpp
constexpr float kMinStopSpeed = 0.0001f; // floor before log2, avoids -inf

inline float StopSemitones(float speed)
{
    return 12.0f * log2f(fmaxf(speed, kMinStopSpeed));
}

inline float StopAmplitude(float speed)
{
    return speed; // linear for now; easy to retune independently later
}
```

`StopSemitones` **corrects** the parent doc's pseudocode, which computes
`SpeedToSemitones(transport.speed - 1.0f)` — undefined at `speed == 0`
(`log2(-1)`) and backwards at `speed == 1` in general. The physically
correct tape relationship is `semitones = 12·log2(speed)`: halving
playback speed drops pitch exactly one octave (−12 semitones), and
`speed == 1.0` gives `0` (no shift), consistent with Phase 1's
convention.

`StopSemitones(speed)` is summed with `ComputeWarpSemitones(...)`
(Phase 1) into one combined value fed into the existing, unmodified
`TapeVoice::Process()` — so WARP bend and tape-stop compose naturally,
per the same "effectiveSemitones = bend + speed-derived" idea the
parent doc's pseudocode describes. Because this reuses `TapeVoice`
as-is rather than writing a new speed→crossfade-rate formula,
**Phase 1's fabsf-placement trap does not recur here**: `ComputeModFreq`
already takes `fabsf(ratio - 1.0f)` internally on the *combined* signed
semitone value, so direction (`shift_up_`) and magnitude are handled
correctly regardless of how large or negative the combined value gets
as `speed → 0`.

No lowpass/filtering during stop in this phase, even though the parent
doc lists it as "optional" for tape-stop — it overlaps with
ATMOSPHERE's tone-shaping role, explicitly slated for Phase 5 Polish.
Adding it here would blur that boundary for a phase that's already
introducing two new pieces of state (transport + mix).

## MIX dry/wet blend

```cpp
constexpr float kHalfPi = 1.57079632679f;

inline float ComputeMix(float mix_knob, float mix_cv)
{
    return fclamp(mix_knob + mix_cv, 0.0f, 1.0f); // additive, same pattern as WARP
}

inline void ComputeMixGains(float mix, float *dry_gain, float *wet_gain)
{
    *dry_gain = cosf(mix * kHalfPi);
    *wet_gain = sinf(mix * kHalfPi);
}
```

Equal-power (constant-power) crossfade, not linear — at `mix == 0.5`,
`dryGain == wetGain ≈ 0.707`, not `0.5/0.5`, so perceived loudness
doesn't dip through the middle of the sweep. `mix == 0`: dry only,
`SW_FREEZE`/tape-stop has **no audible effect at all**, matching the
"MIX at 0 = effect off" behavior implied by the parent doc's original
pseudocode. `mix == 1`: fully wet.

`CV_MIX` sums with `KNOB_MIX` immediately (not deferred), fulfilling
the parent doc's control-mapping table (`CV_MIX: Dry/wet modulation`)
in this same phase rather than pushing it to a later pass, mirroring
how `CV_WARP` already sums with `KNOB_WARP` in Phase 1.

`dryGain`/`wetGain` are computed once per audio block from the smoothed
mix value (control-rate), then applied per-sample — the same
control-rate/audio-rate split Phase 1 already uses for `semitones`.

### Refactor: generalize `WarpSmoother` → `OnePoleSmoother`

`WarpSmoother` (in `warp_control.h`) is a thin, purpose-named wrapper
around `fonepole` with no WARP-specific logic. MIX needs an identical
smoother. Rather than duplicate the wrapper, this phase generalizes it
into a small reusable `OnePoleSmoother` (moved to a shared location,
e.g. `dsp_util.h`), used for both `warpSmoother` and a new
`mixSmoother`. This is a targeted cleanup enabled by touching this code
for MIX, not a speculative new abstraction — `TapeTransport`'s own
speed-ramp smoothing is a separate, stateful concern (target-chasing
with snap epsilon) and is not folded into this shared smoother.

## LED feedback: `LED_FREEZE`

```cpp
float stopBrightness = 1.0f - transport.Speed();
hw.SetLed(LED_FREEZE, stopBrightness, 0.0f, 0.0f); // red
```

Brightness is driven directly by `1 - speed` — no separate pulse LFO.
Off during PLAY (`speed == 1`), rises smoothly as tape slows, full red
exactly when `speed == 0` (STOPPED) — visually mirrors the audible
fade rather than blinking independently of it. Red is chosen to be
visually distinct from WARP's amber/cyan bar-graph (`LED_1`-`LED_6`,
unaffected by this phase). `LED_REVERSE`/`SW_REVERSE`/`LED_BOT_1-3`
remain untouched — REVERSE mode stays out of scope, deferred with the
rest of Phase 5.

## Audio callback (updated shape)

```cpp
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();

    float rawWarpSemis = ComputeWarpSemitones(hw.GetKnobValue(KNOB_WARP), hw.GetWarpVoct());
    float warpSemis     = warpSmoother.Process(rawWarpSemis, warpSmoothCoeff);

    bool freezeEdge = hw.GetButton(SW_FREEZE).RisingEdge();
    bool gateHigh   = hw.GetGateState(GATE_FREEZE);
    transport.Update(freezeEdge, gateHigh, stopRampCoeff);
    float speed = transport.Speed();

    float totalSemis = warpSemis + StopSemitones(speed);
    float wetAmp      = StopAmplitude(speed);

    float rawMix = ComputeMix(hw.GetKnobValue(KNOB_MIX), hw.GetCvValue(CV_MIX));
    float mix     = mixSmoother.Process(rawMix, mixSmoothCoeff);
    float dryGain, wetGain;
    ComputeMixGains(mix, &dryGain, &wetGain);

    for (size_t i = 0; i < size; i++)
    {
        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        out[0][i] = in[0][i] * dryGain + wetL * wetGain;
        out[1][i] = in[1][i] * dryGain + wetR * wetGain;
    }
}
```

## Testing plan

Host-testable (doctest, same pattern as `test_warp_control.cpp` /
`test_tape_voice.cpp`):

- `StopSemitones`: `speed = 1.0` → `0`; `speed = 0.5` → `-12`; behaves
  sanely (no NaN/inf) as `speed → 0`.
- `StopAmplitude`: `1.0 → 1.0`, `0.0 → 0.0`, monotonic in between.
- `ComputeMix`: additive knob+CV, clamped to `[0, 1]`.
- `ComputeMixGains`: `mix = 0` → `dryGain = 1, wetGain = 0`; `mix = 1`
  → the reverse; `mix = 0.5` → both ≈ `0.707` (catches an accidental
  regression to linear 0.5/0.5 mixing).
- `TapeTransport`:
  - Starts at `speed = 1.0` (PLAY).
  - `freeze_edge` → target flips to 0; repeated `Update()` calls ramp
    `speed` down and snap to exactly `0.0`.
  - A second `freeze_edge` mid-ramp reverses direction; `speed` ramps
    back up and snaps to exactly `1.0`.
  - `gate_high = true` forces `speed` toward `0` regardless of the
    button's target; `gate_high` returning `false` hands control back
    to the button's last toggle.

Hardware verification (acceptance criteria for Phase 2):

- With MIX at max: pressing `SW_FREEZE` mid-play smoothly winds pitch
  down and fades to full silence within the ramp time; pressing it
  again ramps cleanly back to normal play, no pop or glitch.
- With MIX at 0: `SW_FREEZE` has no audible effect at all, ever.
- Catching `SW_FREEZE` mid-ramp reverses direction smoothly.
- `LED_FREEZE` brightness visually tracks the audible fade — off in
  play, exactly full brightness when silent.
- Patching a gate into `GATE_FREEZE` forces stop while high and
  overrides the button; releases cleanly back to button control when
  it goes low.
- WARP bend still works normally and composes with tape-stop's pitch
  drop (e.g. dialing in a WARP offset while stopped/stopping shifts
  the stopped pitch accordingly).

## Out of scope (deferred to later phases per the parent doc)

Wow and flutter, tape delay, ATMOSPHERE saturation/lowpass filtering
(including stop-time filtering), REVERSE mode, Shift+knob secondary
pages, stop/start ramp-rate control (fixed constant for now).
