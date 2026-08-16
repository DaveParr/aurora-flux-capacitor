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
} // namespace fluxcap
