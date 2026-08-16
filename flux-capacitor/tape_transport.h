#pragma once
#include <cmath>
#include "Utility/dsp.h"

namespace fluxcap
{
// Floor applied before log2f in StopSemitones to avoid log2(0) = -inf.
constexpr float kMinStopSpeed = 0.0001f;

/** Converts a tape transport speed (0..1, nominal = 1.0) to a semitone
 *  offset, using the physically-correct tape relationship: halving
 *  playback speed drops pitch exactly one octave. speed == 1.0 -> 0
 *  semitones (no shift). Floors speed at kMinStopSpeed before taking
 *  log2 so speed == 0.0 (fully stopped) returns a large-but-finite
 *  negative value instead of -inf.
 */
inline float StopSemitones(float speed)
{
    return 12.0f * log2f(daisysp::fmax(speed, kMinStopSpeed));
}

/** Converts tape transport speed (0..1) to a wet-signal amplitude gain.
 *  Linear for now (amplitude == speed): simplest defensible curve,
 *  independently tunable later without touching TapeTransport or the
 *  MIX blend.
 */
inline float StopAmplitude(float speed)
{
    return speed;
}

/** Continuous tape-transport speed (0..1) that one-pole-smooths toward
 *  a 0/1 target, snapping to the target once close enough so PLAY
 *  (1.0) and STOPPED (0.0) are reached exactly rather than approached
 *  asymptotically. Collapses the "STOPPING vs STOPPED" / "STARTING vs
 *  PLAY" distinction into "target is 0" / "target is 1": catching
 *  freeze_edge mid-ramp just flips the target and Update() reverses
 *  direction on its own.
 */
class TapeTransport
{
  public:
    void Init(float initial_speed = 1.0f)
    {
        speed_         = initial_speed;
        button_target_ = initial_speed;
    }

    /** freeze_edge: true on the block SW_FREEZE was just pressed --
     *  flips the button's target (0 <-> 1).
     *  gate_high: current GATE_FREEZE level. Forces the target to 0
     *  while true, overriding the button; when it goes false, control
     *  reverts to the button's last toggled target. Unpatched gates
     *  read low (GateIn's pulldown), so this is a no-op when nothing
     *  is patched into GATE_FREEZE.
     *  ramp_coeff: one-pole coefficient (1.0f / (seconds * blockRate)).
     */
    void Update(bool freeze_edge, bool gate_high, float ramp_coeff)
    {
        if (freeze_edge)
            button_target_ = (button_target_ >= 0.5f) ? 0.0f : 1.0f;

        float target = gate_high ? 0.0f : button_target_;

        daisysp::fonepole(speed_, target, ramp_coeff);

        if (fabsf(speed_ - target) < kSnapEpsilon)
            speed_ = target;
    }

    float Speed() const { return speed_; }

  private:
    static constexpr float kSnapEpsilon = 0.001f;

    float speed_         = 1.0f;
    float button_target_ = 1.0f;
};
} // namespace fluxcap
