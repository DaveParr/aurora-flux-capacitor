#pragma once
#include "Utility/dsp.h"

namespace fluxcap
{
/** One-pole smoother, used to damp ADC/knob jitter on a control-rate
 *  value before it reaches audio-rate processing (e.g. WARP semitones,
 *  MIX blend amount). Not used for TapeTransport's speed ramp, which
 *  is a separate target-chasing smoother with its own snap-to-target
 *  behavior.
 */
class OnePoleSmoother
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
