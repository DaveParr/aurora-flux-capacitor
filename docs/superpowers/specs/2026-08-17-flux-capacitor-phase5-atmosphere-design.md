# Flux Capacitor — Phase 5a: ATMOSPHERE Coloration

## Context

This is a sub-project of Phase 5 ("Polish and Modes") from
[`retro_tape_firmware_design.md`](../../../retro_tape_firmware_design.md).
That phase's scope note bundles four independent pieces of work
(ATMOSPHERE, REVERSE mode, Shift+knob secondary pages, a tuning pass)
plus an optional stretch item (ping-pong delay feedback). This spec
covers only the ATMOSPHERE knob/CV: tone-shaping lowpass, saturation
(main path and `TapeDelay`'s feedback loop), a feedback-*amount*
control for `TapeDelay` (replacing the fixed `kTapeDelayFeedback`
constant left in place by Phase 4), and tape-stop-specific HF loss.
REVERSE mode, Shift+knob pages, the tuning pass, and ping-pong feedback
remain out of scope and will get their own specs; the tuning pass in
particular is expected to revisit every constant introduced here by
ear, once this and the other Phase 5 sub-projects exist to tune
against.

Builds on Phase 4
([`2026-08-16-flux-capacitor-phase4-tape-delay-design.md`](2026-08-16-flux-capacitor-phase4-tape-delay-design.md)),
which is merged to `main`. `TapeVoice`, `TapeTransport`, `WowFlutter`,
`OnePoleSmoother`, `TapeDelay`, and the existing MIX dry/wet blend are
all reused; this phase adds a new coloration stage between `TapeVoice`
and `TapeDelay` on the main path, and extends `TapeDelay` itself for
the feedback-loop changes.

Aurora exposes `KNOB_ATMOSPHERE`/`CV_ATMOSPHERE` (see `aurora.h`); both
are read nowhere in `main.cpp` today, confirmed by grep.

## Signal chain placement

```
Input -> TapeVoice (WARP + stop + wow/flutter pitch, unchanged)
      -> saturate (ATMOSPHERE) -> tone lowpass (ATMOSPHERE + speed)
      -> TapeDelay (feedback loop gets its own saturate -> filter -> feedback-amount)
      -> MIX dry/wet (unchanged)
```

Coloration sits on the wet signal after `TapeVoice`, before
`TapeDelay` — so `TapeDelay`'s repeats inherit the same darkened/
saturated character as whatever seeds them, matching how a real tape
echo's repeats are colored by the same record/playback chain as the
direct signal. Saturating before filtering (both on the main path and
inside `TapeDelay`'s feedback loop) lets the lowpass smooth the
saturation's added harmonics after the fact, the same way a real tape
machine's own frequency response tames record-head saturation
artifacts — a single consistent ordering rule applied in both places.

MIX, `ComputeMixGains`, and the auto-wet/LED-flash logic are
unchanged: coloration only changes what feeds into `TapeDelay`, not
how the dry/wet blend itself works.

## Control mapping: ATMOSPHERE -> a single coloration amount

There is one knob/CV pair and three things it drives (tone-lowpass
cutoff, saturation drive, `TapeDelay` feedback amount). Every existing
control in this codebase (WARP, TIME, MIX, REFLECT, BLUR) maps one
knob+CV pair to a single scalar; ATMOSPHERE follows the same pattern,
combining knob and CV into one 0–1 "atmosphere" amount that then drives
all three effects together as one coloration macro — the knob reads as
"more tape" in one direction, not three semi-independent controls
fighting for one turn of travel:

```cpp
constexpr float kAtmosphereMin = 0.0f;
constexpr float kAtmosphereMax = 1.0f;

inline float ComputeAtmosphereAmount(float atmosphere_knob, float atmosphere_cv)
{
    return daisysp::fclamp(atmosphere_knob + atmosphere_cv, kAtmosphereMin, kAtmosphereMax);
}
```

Smoothed at control rate before use, same `OnePoleSmoother` pattern and
time constant as `warpSmoother`/`mixSmoother`/`reflectSmoother`/
`blurSmoother`:

```cpp
constexpr float kAtmosphereSmoothTimeSeconds = 0.02f;
```

0.02s (not TIME's slower 0.08s) because, unlike TIME, nothing about
ATMOSPHERE changing quickly is meant to read as intentional analog
character — a fast knob turn should just track cleanly, the same
reasoning already applied to WARP/MIX/REFLECT/BLUR.

## Tone lowpass: `ComputeAtmosphereLpfCoeff`

Expressed as a time constant (tau), matching this codebase's existing
convention (`kTapeDelayFeedbackLpfTauSeconds`, `kStopRampTimeSeconds`)
rather than introducing Hz-based math or a second cutoff-to-coefficient
formula:

```cpp
// Tau range for the ATMOSPHERE-driven tone lowpass. Provisional --
// the later tuning-pass sub-project is expected to revisit these by
// ear once the full coloration chain exists to audition against.
//
// kAtmosphereLpfTauMinSeconds is a stability floor, not just a taste
// choice: OnePoleSmoother's fonepole backing (out += coeff*(in-out))
// is only a valid lowpass for coeff in (0, 1] -- coeff > 2 diverges
// outright, and 1 < coeff < 2 is stable but resonant, not a lowpass.
// Since coeff = 1/(tau*sample_rate), tau must never fall below one
// sample period at the *fastest* sample rate this runs at (48kHz on
// Aurora); 0.0000210f gives coeff ~= 0.992 at 48kHz, just inside the
// valid range with a small margin. An earlier value (0.000005f, chosen
// from the continuous-time cutoff formula 1/(2*pi*tau) alone without
// checking it against fonepole's discrete-time stability bound) gave
// coeff ~= 4.17 -- unconditionally divergent, driving the filter to
// NaN within milliseconds regardless of where the ATMOSPHERE knob sat,
// since atmosphereSmoother ramps up from 0.0f at power-on and sweeps
// through the unstable region on every boot. Caught in the final
// whole-branch review, not by any single task's tests, because the
// bug only appears when the pure formula, the real 48kHz sample rate,
// and fonepole's actual recurrence are considered together.
// ComputeAtmosphereLpfCoeff's fmin(1.0f, ...) clamp below is a second,
// independent line of defense against the same class of bug.
constexpr float kAtmosphereLpfTauMinSeconds = 0.0000210f; // near-bypass (coeff ~= 0.99 at 48kHz), atmosphere = 0
constexpr float kAtmosphereLpfTauMaxSeconds = 0.0001f;   // dark (~1.6 kHz), atmosphere = 1

// Tau floor applied as speed -> 0, independent of the ATMOSPHERE knob
// -- "tape loses HF as it slows" per the parent doc's Tape Speed Model,
// so a full brake always darkens even with ATMOSPHERE fully
// counterclockwise. Same order of magnitude as kAtmosphereLpfTauMaxSeconds
// on purpose: stop-darkening should read as "at least as dark as full
// ATMOSPHERE", not a separate, more extreme effect.
constexpr float kStopLpfTauSeconds = 0.00015f; // ~1.1 kHz

inline float ComputeAtmosphereLpfCoeff(float atmosphere, float speed, float sample_rate)
{
    float atmosphereTau = daisysp::fmap(atmosphere, kAtmosphereLpfTauMinSeconds,
                                         kAtmosphereLpfTauMaxSeconds);
    // speed = 1.0 -> no extra darkening (tau floor sits at the bright end);
    // speed -> kMinStopSpeed -> tau rises to kStopLpfTauSeconds.
    float speedTau = daisysp::fmap(1.0f - daisysp::fclamp(speed, 0.0f, 1.0f),
                                    kAtmosphereLpfTauMinSeconds, kStopLpfTauSeconds);
    float tau = daisysp::fmax(atmosphereTau, speedTau); // larger tau = darker; take whichever is darker
    // Defensive clamp: guarantees a valid fonepole coefficient (<=1.0)
    // regardless of the tau constants above, at any sample rate this
    // ever runs at -- see the stability-floor comment above.
    return daisysp::fmin(1.0f, 1.0f / (tau * sample_rate));
}
```

A single `fmax`-of-two-taus rather than a second filter instance: one
`OnePoleSmoother` per channel, reused for both the ATMOSPHERE-dialed
darkening and the stop-brake darkening, since only the more extreme of
the two needs to win at any moment. Per-channel state (`atmosphereLpfL`/
`atmosphereLpfR`, both `OnePoleSmoother`) lives in `main.cpp` as
audio-rate filters, the same repurposing trick `TapeDelay`'s
`feedback_lpf_` and `WowFlutter`'s flutter-noise LPF already establish.
Same `fonepole` time-constant gotcha documented in the Phase 3/4 specs
applies here: the coefficient must be computed against
`hw.AudioSampleRate()` (this filter runs per audio sample), not
`hw.AudioCallbackRate()`.

## Saturation: `ApplySaturation`

`daisysp::SoftClip` (`Utility/dsp.h`) already exists in this codebase's
dependency tree — no new library code:

```cpp
// kSaturationDriveMin = 1.0f is a deliberate compromise, not a fully
// "clean" bypass: SoftClip's soft knee starts compressing well below
// unity (SoftLimit(1.0) ~= 0.778, a full-scale peak ~-2.2dB with a few
// percent third-harmonic), so there is no drive value that is
// simultaneously unity-gain AND distortion-free at typical signal
// levels -- lowering drive avoids the distortion but loses gain
// instead (e.g. drive=0.25 keeps compression under ~0.2dB but quiets
// the signal by ~12dB). 1.0f keeps unity gain and accepts the small,
// provisional softening; true zero-effect transparency at atmosphere=0
// (e.g. via a dry/wet blend of this stage) is left to the tuning pass.
constexpr float kSaturationDriveMin = 1.0f;
constexpr float kSaturationDriveMax = 4.0f; // atmosphere = 1: audible soft-knee saturation on typical signal levels

inline float ComputeSaturationDrive(float atmosphere)
{
    return daisysp::fmap(atmosphere, kSaturationDriveMin, kSaturationDriveMax);
}

inline float ApplySaturation(float in, float drive)
{
    return daisysp::SoftClip(in * drive);
}
```

`SoftClip` is an odd, monotonic, bounded (±1 beyond ±3) waveshaper —
`ApplySaturation(0, drive) == 0` for any drive, so saturation fades to
silence exactly when its input does. This is what lets the main-path
saturation ride `wetAmp`'s existing tape-stop fade for free (see
"Audio callback integration" below): no separate stop-envelope logic
needed for saturation, unlike the tone lowpass which needs its own
explicit speed input.

## `TapeDelay` changes: feedback amount + feedback-loop saturation

```cpp
// Replaces the fixed kTapeDelayFeedback = 0.35f left in place by Phase 4.
// kAtmosphereFeedbackMin matches that old constant exactly, so ATMOSPHERE
// fully counterclockwise reproduces Phase 4's feedback behavior byte-for-
// byte -- unlike saturation (see ApplySaturation's doc comment above),
// this one dimension of "no change at zero" is fully achievable, so it
// is, rather than leaving an unnecessary gap alongside the necessary one.
constexpr float kAtmosphereFeedbackMin = 0.35f;
constexpr float kAtmosphereFeedbackMax = 0.55f; // stays comfortably under 1.0; SoftClip in the loop bounds it further

inline float ComputeAtmosphereFeedback(float atmosphere)
{
    return daisysp::fmap(atmosphere, kAtmosphereFeedbackMin, kAtmosphereFeedbackMax);
}
```

`TapeDelay::Update` gains a third parameter; `drive_` and
`feedback_amount_` become members set once per block, consumed
per-sample in `Process` — the same control-rate-in/audio-rate-out
pattern `target_samples_` already follows:

```cpp
// base_seconds/speed: as before. atmosphere: smoothed ATMOSPHERE
// amount, same value driving the main-path tone/saturation this block.
void Update(float base_seconds, float speed, float atmosphere)
{
    target_samples_  = ComputeDelaySamples(base_seconds, speed, sr_);
    drive_           = ComputeSaturationDrive(atmosphere);
    feedback_amount_ = ComputeAtmosphereFeedback(atmosphere);
}
```

`Process`'s feedback path (saturate, then the existing Phase-4 feedback
LPF, then scale by the now-variable `feedback_amount_`):

```cpp
float wet       = delay_->Read();
float saturated = ApplySaturation(wet, drive_);
float filtered  = feedback_lpf_.Process(saturated, feedback_lpf_coeff_);
delay_->Write(in + filtered * feedback_amount_);
```

The rest of `TapeDelay::Process` (delay-position slew limiting,
`SetDelay`, the `in + wet` return value) is unchanged — this only
touches the write side of the feedback loop, not the read/output side.

## Audio callback integration

```cpp
// New globals, alongside the existing smoother/coefficient pairs.
OnePoleSmoother atmosphereSmoother;
float           atmosphereSmoothCoeff = 0.0f;
OnePoleSmoother atmosphereLpfL, atmosphereLpfR; // audio-rate tone filters, one per channel
```

```cpp
float rawAtmosphere = ComputeAtmosphereAmount(hw.GetKnobValue(KNOB_ATMOSPHERE), hw.GetCvValue(CV_ATMOSPHERE));
float atmosphere     = atmosphereSmoother.Process(rawAtmosphere, atmosphereSmoothCoeff);
float atmosphereLpfCoeff = ComputeAtmosphereLpfCoeff(atmosphere, speed, hw.AudioSampleRate());
float atmosphereDrive    = ComputeSaturationDrive(atmosphere);

delayL.Update(delaySeconds, speed, atmosphere);
delayR.Update(delaySeconds, speed, atmosphere);

for (size_t i = 0; i < size; i++)
{
    float wobble     = wowFlutter.Process(wowRateHz, flutterDepth);
    float totalSemis = warpSemis + StopSemitones(speed) + wobble;

    float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
    float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

    wetL = atmosphereLpfL.Process(ApplySaturation(wetL, atmosphereDrive), atmosphereLpfCoeff);
    wetR = atmosphereLpfR.Process(ApplySaturation(wetR, atmosphereDrive), atmosphereLpfCoeff);

    wetL = delayL.Process(wetL, wobble);
    wetR = delayR.Process(wetR, wobble);

    out[0][i] = in[0][i] * dryGain + wetL * wetGain;
    out[1][i] = in[1][i] * dryGain + wetR * wetGain;
}
```

`atmosphereLpfCoeff`/`atmosphereDrive` are computed once per block (both
depend only on control-rate values: `atmosphere` and, for the coeff,
`speed`), then reused for every sample in the block — same
once-per-block-then-reused pattern `wowRateHz`/`flutterDepth` already
follow before entering the per-sample loop. `atmosphereSmoother.Init(0.0f)`
and `atmosphereLpfL.Init(0.0f)`/`atmosphereLpfR.Init(0.0f)` join the
other `Init` calls in `main()`; `atmosphereSmoothCoeff = 1.0f /
(kAtmosphereSmoothTimeSeconds * hw.AudioCallbackRate())` joins the other
coefficient assignments.

No LED changes — out of scope per this project's earlier decomposition
question; `LED_BOT_1-3` remain unused.

## Testing plan

Host-testable (doctest, new `test_atmosphere_control.cpp` following
`test_mix_control.cpp`/`test_warp_control.cpp`'s pattern for a
stateless, pure-function header):

- `ComputeAtmosphereAmount`: `knob=0,cv=0` -> `0.0`; `knob=1,cv=0` ->
  `1.0`; combined value clamps to `[0, 1]` (e.g. `knob=1, cv=1` does
  not exceed `1.0`; `knob=0, cv=-1` does not go below `0.0`).
- `ComputeAtmosphereLpfCoeff`: `atmosphere=0, speed=1.0` -> tau at
  `kAtmosphereLpfTauMinSeconds` (near-bypass, largest coefficient);
  `atmosphere=1, speed=1.0` -> tau at `kAtmosphereLpfTauMaxSeconds`;
  `atmosphere=0, speed` near `kMinStopSpeed` -> tau driven up toward
  `kStopLpfTauSeconds` by the speed term alone, confirming stop-darkening
  works even at `atmosphere=0`; `atmosphere=1, speed` near
  `kMinStopSpeed` -> tau is the larger (darker) of the two candidate
  taus, not their sum or average.
- `ComputeSaturationDrive`/`ComputeAtmosphereFeedback`: `atmosphere=0`
  -> min value (`kSaturationDriveMin`/`kAtmosphereFeedbackMin`);
  `atmosphere=1` -> max value; monotonic increasing in between.
- `ApplySaturation`: input `0` -> output `0` for any drive (silence
  stays silent, verifies the "rides wetAmp's fade for free" claim
  above); small input with `drive=kSaturationDriveMin` is
  near-identity (`|ApplySaturation(x, 1.0) - x|` small for `|x| <
  0.1`); large input stays bounded to `[-1, 1]` regardless of drive;
  odd-symmetric (`ApplySaturation(-x, d) == -ApplySaturation(x, d)`).

Extend `test_tape_delay.cpp` for the new `Update` parameter and
feedback-loop changes:

- Feeding an impulse through `TapeDelay` with `atmosphere=1` produces a
  measurably larger sustained feedback tail (more repeats above a noise
  floor) than `atmosphere=0`, confirming `feedback_amount_` actually
  varies the decay.
- Output stays bounded (no runaway growth) across many blocks even at
  `atmosphere=1` (`kAtmosphereFeedbackMax` combined with the in-loop
  `SoftClip`), the same stability property Phase 4's existing feedback
  test already checks at the old fixed constant.
- `atmosphere=0` reduces feedback-loop saturation to
  `kSaturationDriveMin`, distinguishing "feedback amount changed" from
  "saturation changed" as separate, independently-verifiable effects of
  the same `atmosphere` input.

Hardware verification (acceptance criteria for this sub-project):

- With ATMOSPHERE fully counterclockwise: `TapeDelay`'s feedback amount
  matches Phase 4's old fixed constant exactly (`kAtmosphereFeedbackMin
  = 0.35f`), so repeat count/decay is unchanged from the Phase 4
  baseline. Saturation is **not** bit-identical to a bypass at this
  setting: `SoftClip`'s soft knee has no drive value that is
  simultaneously unity-gain and distortion-free at typical signal
  levels (`kSaturationDriveMin = 1.0f` gives a full-scale peak roughly
  −2.2 dB with a few percent third-harmonic; a lower drive avoids the
  distortion but loses gain instead). Expect subtle softening on hot
  peaks, not silence-identical bypass — full transparency at this
  setting (e.g. via a dry/wet blend of the saturation stage) is
  deferred to the tuning pass, which can judge by ear whether it's
  worth the added complexity.
- Turning ATMOSPHERE clockwise smoothly and audibly darkens and warms
  the wet signal (both the direct pitch-shifted signal and the delay
  repeats), with increasing saturation character, and the delay's
  repeats become both more numerous and darker.
- No zipper noise, click, or discontinuity as ATMOSPHERE is turned
  while audio is playing.
- Engaging FREEZE darkens the tape even with ATMOSPHERE fully
  counterclockwise, audibly progressing as speed falls toward a full
  stop.
- No harsh aliasing, runaway feedback buildup, or instability at
  ATMOSPHERE's maximum, even with TIME set for a short slapback (the
  fastest repeat rate, and therefore the fastest feedback buildup rate).
- WARP bend, tape-stop, MIX dry/wet, wow/flutter, and TapeDelay's
  TIME/speed coupling all continue to work normally and audibly compose
  with the new coloration stage.

## Out of scope (deferred to later Phase 5 sub-projects)

REVERSE mode; Shift+knob secondary pages (including Shift+ATMOSPHERE
for an independently-adjustable saturation amount, which depends on
this sub-project's primary ATMOSPHERE function landing first); the
tuning pass revisiting every constant introduced here by ear; LED
feedback for ATMOSPHERE (`LED_BOT_1-3` remain unused); cross-channel
(ping-pong) delay feedback.
