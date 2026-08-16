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
 */
inline void ComputeMixGains(float mix, float *dry_gain, float *wet_gain)
{
    *dry_gain = cosf(mix * HALFPI_F);
    *wet_gain = sinf(mix * HALFPI_F);
}
} // namespace fluxcap
