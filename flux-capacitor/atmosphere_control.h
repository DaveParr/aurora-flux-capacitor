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
// a later tuning pass is expected to revisit these by ear once the
// full coloration chain exists to audition against.
constexpr float kAtmosphereLpfTauMinSeconds = 0.000005f; // near-bypass (~32 kHz), atmosphere = 0
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
    return 1.0f / (tau * sample_rate);
}

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

// Replaces tape_delay.h's fixed kTapeDelayFeedback = 0.35f (Phase 4).
constexpr float kAtmosphereFeedbackMin = 0.15f;
constexpr float kAtmosphereFeedbackMax = 0.55f; // stays comfortably under 1.0; SoftClip in the loop bounds it further

/** Maps ATMOSPHERE amount to TapeDelay's feedback amount. */
inline float ComputeAtmosphereFeedback(float atmosphere)
{
    return daisysp::fmap(atmosphere, kAtmosphereFeedbackMin, kAtmosphereFeedbackMax);
}
} // namespace fluxcap
