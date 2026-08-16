#pragma once
#include <cmath>
#include "Utility/dsp.h"

namespace fluxcap
{
/** Maps KNOB_MIX (0..1) + CV_MIX (additive, same pattern as WARP) to a
 *  combined, clamped dry/wet blend amount in [0, 1].
 */
inline float ComputeMix(float mix_knob, float mix_cv)
{
    return daisysp::fclamp(mix_knob + mix_cv, 0.0f, 1.0f);
}

/** Equal-power (constant-power) crossfade gains for a dry/wet blend
 *  amount in [0, 1]. At mix == 0.5, both gains are ~0.707 (not 0.5),
 *  so perceived loudness stays constant across the sweep instead of
 *  dipping in the middle, unlike a linear crossfade.
 *
 *  Equal-power crossfade is the right choice when dry and wet are
 *  decorrelated (the usual case once WARP/tape-stop are actually altering
 *  the signal), but note: when the effect is at rest (WARP centered, tape
 *  not stopped), wet is bit-identical to dry, and summing two identical
 *  signals via equal-power gains adds ~+3dB at mix ~= 0.5 rather than
 *  staying at unity. If this proves too hot on hardware, that's a tuning
 *  decision for hardware verification, not a defect in this formula.
 */
inline void ComputeMixGains(float mix, float *dry_gain, float *wet_gain)
{
    *dry_gain = cosf(mix * HALFPI_F);
    *wet_gain = sinf(mix * HALFPI_F);
}

// How aggressively the auto-wet curve (below) rises as speed departs from
// 1.0. Needs to be steep, not linear: StopAmplitude (tape_transport.h) also
// scales the wet signal by `speed`, so a linear `1 - speed` curve raises
// wetGain no faster than wetAmp shrinks the wet signal underneath it --
// their product never once beats the dry signal across the whole stop.
// Raising `speed` to a power > 1 makes the curve jump toward 1 quickly for
// any real departure from PLAY, so dry gets out of the way early enough for
// the wet path's own (correct) fade to actually be what's audible. Tunable
// by ear: higher values make the effect kick in even faster.
constexpr float kAutoWetSteepness = 8.0f;

// Auto-wet only exists to rescue MIX == 0, where FREEZE would otherwise be
// silent. Any other setting is untouched by transport speed, matching a
// physical pot: only its fully-counterclockwise position is a distinct
// mode. This needs to be generous, not a tight epsilon: libDaisy's own
// AnalogControl::Process() (ctrl.cpp) carries an acknowledged-but-unused
// BOTTOM_THRESH constant ("prevent bleed on the bottom of the pots/CVs"),
// i.e. a pot at its physical minimum is not guaranteed to read a clean
// 0.0 -- unlike kWarpDeadzoneSemitones (a mid-travel center deadzone,
// a different and generally better-behaved region of a pot's sweep),
// this threshold covers the far end of travel, where bleed is worse.
// Tunable: raise it further if MIX still reads above this at hardware
// full-left.
constexpr float kMixFullyDryThreshold = 0.03f;

/** Overrides the dry/wet blend to fully wet while TapeTransport's speed is
 *  away from 1.0 (PLAY), but ONLY when the user's MIX is at (or within
 *  `kMixFullyDryThreshold` of) fully dry -- without that, a user who never
 *  turns MIX up would never hear FREEZE do anything. Any other MIX setting
 *  returns `user_mix` completely unchanged at every speed: this is not a
 *  floor or a blend, it's a substitute for one specific setting FREEZE
 *  would otherwise make pointless.
 */
inline float ComputeEffectiveMix(float user_mix, float speed)
{
    if (user_mix >= kMixFullyDryThreshold)
        return user_mix;
    return 1.0f - powf(speed, kAutoWetSteepness);
}

/** True exactly when `mix` and `prev_mix` fall on opposite sides of
 *  `kMixFullyDryThreshold` -- i.e. the knob just crossed into or out of
 *  the auto-wet zone `ComputeEffectiveMix` special-cases. Uses the same
 *  `>=` boundary convention as `ComputeEffectiveMix` (exactly-at-threshold
 *  counts as "above"), so the two functions never disagree about where
 *  the line is.
 */
inline bool MixDryZoneCrossed(float mix, float prev_mix)
{
    bool wasDry = prev_mix < kMixFullyDryThreshold;
    bool isDry  = mix < kMixFullyDryThreshold;
    return wasDry != isDry;
}
} // namespace fluxcap
