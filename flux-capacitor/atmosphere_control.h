#pragma once
#include "Utility/dsp.h"

namespace fluxcap
{
constexpr float kAtmosphereMin = 0.0f;
constexpr float kAtmosphereMax = 1.0f;

/** Maps KNOB_ATMOSPHERE (0..1) + CV_ATMOSPHERE (additive, same pattern
 *  as WARP/MIX/REFLECT/BLUR/TIME) to a single 0..1 coloration amount
 *  that drives tone, saturation, and TapeDelay's feedback amount
 *  together as one "tape coloration" macro -- there's only one knob/CV
 *  pair for three effects, so they move together rather than fighting
 *  for one turn of travel.
 */
inline float ComputeAtmosphereAmount(float atmosphere_knob, float atmosphere_cv)
{
    return daisysp::fclamp(atmosphere_knob + atmosphere_cv, kAtmosphereMin, kAtmosphereMax);
}

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

// Tau floor applied as speed -> 0, independent of the ATMOSPHERE knob --
// "tape loses HF as it slows" (parent doc's Tape Speed Model), so a
// full brake always darkens even with ATMOSPHERE fully counterclockwise.
constexpr float kStopLpfTauSeconds = 0.00015f; // ~1.1 kHz

/** Tone-lowpass coefficient for an audio-rate OnePoleSmoother, folding
 *  in both the ATMOSPHERE-dialed darkening and speed-based tape-stop
 *  darkening as a single fmax of two candidate time constants (larger
 *  tau = darker) -- whichever is more extreme wins, so callers need
 *  only one filter instance, not two.
 */
inline float ComputeAtmosphereLpfCoeff(float atmosphere, float speed, float sample_rate)
{
    float atmosphereTau = daisysp::fmap(atmosphere, kAtmosphereLpfTauMinSeconds,
                                         kAtmosphereLpfTauMaxSeconds);
    float speedTau = daisysp::fmap(1.0f - daisysp::fclamp(speed, 0.0f, 1.0f),
                                    kAtmosphereLpfTauMinSeconds, kStopLpfTauSeconds);
    float tau = daisysp::fmax(atmosphereTau, speedTau);
    // Defensive clamp: guarantees a valid fonepole coefficient (<=1.0)
    // regardless of the tau constants above, at any sample rate this
    // ever runs at -- see the stability-floor comment above.
    return daisysp::fmin(1.0f, 1.0f / (tau * sample_rate));
}

// Provisional -- see the tau constants' comment above; the tuning pass
// is expected to revisit this range by ear too.
constexpr float kSaturationDriveMin = 1.0f; // atmosphere = 0: SoftClip(x) is ~identity for |x| << 1
constexpr float kSaturationDriveMax = 4.0f; // atmosphere = 1: audible soft-knee saturation

/** Pre-gain applied before daisysp::SoftClip, scaling with ATMOSPHERE
 *  amount so the same knob drives saturation drive as well as tone.
 */
inline float ComputeSaturationDrive(float atmosphere)
{
    return daisysp::fmap(atmosphere, kSaturationDriveMin, kSaturationDriveMax);
}

/** Thin wrapper around daisysp::SoftClip -- ApplySaturation(0, drive)
 *  == 0 for any drive, so saturation fades to silence exactly when its
 *  input does (no separate stop-envelope handling needed for it).
 */
inline float ApplySaturation(float in, float drive)
{
    return daisysp::SoftClip(in * drive);
}

// Replaces the fixed kTapeDelayFeedback = 0.35f left in place by Phase 4.
// kAtmosphereFeedbackMin matches that old constant exactly, so ATMOSPHERE
// fully counterclockwise reproduces Phase 4's feedback behavior byte-for-
// byte -- unlike saturation (see ApplySaturation's doc comment above),
// this one dimension of "no change at zero" is fully achievable, so it
// is, rather than leaving an unnecessary gap alongside the necessary one.
// Provisional -- see the tau constants' comment above; the tuning pass
// revisits this range too.
constexpr float kAtmosphereFeedbackMin = 0.35f;
constexpr float kAtmosphereFeedbackMax = 0.55f; // stays comfortably under 1.0; SoftClip in the loop bounds it further

/** Maps ATMOSPHERE amount to TapeDelay's feedback amount. */
inline float ComputeAtmosphereFeedback(float atmosphere)
{
    return daisysp::fmap(atmosphere, kAtmosphereFeedbackMin, kAtmosphereFeedbackMax);
}
} // namespace fluxcap
