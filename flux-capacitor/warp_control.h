#pragma once
#include <cmath>
#include "Utility/dsp.h"

namespace fluxcap
{
constexpr float kWarpKnobRangeSemitones = 12.0f;

// Small combined-semitone deadzone around zero. KNOB_WARP spans +-12
// semitones, so even a few percent of realistic ADC/mechanical error at
// the physical center produces a nonzero-but-tiny semitone value -- which
// without this deadzone would resolve to an audibly non-dry, slowly
// sweeping detuned result instead of a clean passthrough.
constexpr float kWarpDeadzoneSemitones = 0.05f;

constexpr float kWarpLedFullScaleSemitones = kWarpKnobRangeSemitones; // 12.0f
constexpr float kWarpLedBandSemitones      = kWarpLedFullScaleSemitones / 3.0f; // 4.0f

/** Per-LED brightness (0..1) for the WARP pitch-shift bar-graph.
 *  up[0]/down[0] are the innermost LEDs (nearest zero shift), [2] the
 *  outermost. Exactly one side is nonzero at a time -- semitones > 0
 *  populates `up`, semitones < 0 populates `down`, semitones == 0
 *  leaves both all-zero.
 */
struct WarpLedLevels
{
    float up[3];
    float down[3];
};

/** Maps a combined WARP semitone value (post-deadzone, post-smoothing)
 *  to the 6-LED bar-graph's per-LED brightness. Full scale is +-12
 *  semitones (kWarpLedFullScaleSemitones), split into three 4-semitone
 *  bands per side; brightness fades continuously within a band. Values
 *  beyond full scale clamp at full brightness rather than overflowing.
 */
inline WarpLedLevels ComputeWarpLedLevels(float semitones)
{
    WarpLedLevels levels = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    float magnitude = fabsf(semitones);
    float *side = (semitones > 0.0f) ? levels.up : levels.down;
    for (int i = 0; i < 3; i++)
    {
        float bandStart = static_cast<float>(i) * kWarpLedBandSemitones;
        side[i] = daisysp::fclamp(
            (magnitude - bandStart) / kWarpLedBandSemitones, 0.0f, 1.0f);
    }
    return levels;
}

/** Maps KNOB_WARP (0..1, center = 0.5) + a continuous CV semitone offset
 *  (e.g. from hw.GetWarpVoct()) to a combined, unsmoothed semitone value.
 *  Snaps small combined values to exactly 0.0f (see kWarpDeadzoneSemitones)
 *  so realistic knob/CV noise near center still resolves to a bit-exact
 *  dry passthrough.
 */
inline float ComputeWarpSemitones(float knobValue, float warpVoctSemitones)
{
    float knobSemis = daisysp::fmap(
        knobValue, -kWarpKnobRangeSemitones, kWarpKnobRangeSemitones);
    float total = knobSemis + warpVoctSemitones;
    if (fabsf(total) < kWarpDeadzoneSemitones)
        return 0.0f;
    return total;
}

} // namespace fluxcap
