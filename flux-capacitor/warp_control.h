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

/** One-pole smoother, used to damp ADC/knob jitter on the combined
 *  semitone value before it reaches TapeVoice. This is separate from
 *  TapeVoice's own crossfade mechanism, which is what makes pitch
 *  changes glitch-free.
 */
class WarpSmoother
{
  public:
    void Init(float initial = 0.0f) { value_ = initial; }

    float Process(float target, float coeff)
    {
        daisysp::fonepole(value_, target, coeff);
        return value_;
    }

    float Value() const { return value_; }

  private:
    float value_ = 0.0f;
};
} // namespace fluxcap
