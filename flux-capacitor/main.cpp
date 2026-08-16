/** flux-capacitor
 *
 *  Phase 1: WARP knob/CV pitch bend via a continuous, glitch-free tape
 *  voice pitch engine (no semitone stepping). True stereo. LED_1-6 show
 *  the current pitch shift as a symmetric bar-graph.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md
 *  and docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md
 */
#include "aurora.h"
#include "tape_voice.h"
#include "warp_control.h"

using namespace daisy;
using namespace aurora;
using namespace fluxcap;

Hardware     hw;
TapeVoice    voiceL, voiceR;
WarpSmoother warpSmoother;
float        warpSmoothCoeff = 0.0f;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();

    float rawSemitones = ComputeWarpSemitones(hw.GetKnobValue(KNOB_WARP), hw.GetWarpVoct());
    float semitones     = warpSmoother.Process(rawSemitones, warpSmoothCoeff);

    for (size_t i = 0; i < size; i++)
    {
        out[0][i] = voiceL.Process(in[0][i], semitones);
        out[1][i] = voiceR.Process(in[1][i], semitones);
    }
}

int main(void)
{
    hw.Init();
    hw.ClearLeds();
    hw.WriteLeds();

    // One-pole smoothing time constant for WARP knob/CV jitter damping
    // (not pitch glide -- TapeVoice's crossfade mechanism handles that).
    constexpr float kWarpSmoothTimeSeconds = 0.02f;
    warpSmoothCoeff = 1.0f / (kWarpSmoothTimeSeconds * hw.AudioCallbackRate());

    voiceL.Init(hw.AudioSampleRate());
    voiceR.Init(hw.AudioSampleRate());
    warpSmoother.Init(0.0f);

    hw.StartAudio(AudioCallback);

    // WARP pitch-shift bar-graph: LED_4/5/6 light amber outward for an
    // upward shift, LED_3/2/1 light cyan outward for a downward shift.
    // See docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md
    constexpr float kWarpUpColor[3]   = {1.0f, 0.4f, 0.0f}; // amber
    constexpr float kWarpDownColor[3] = {0.0f, 0.6f, 1.0f}; // cyan
    const Leds      upLeds[3]         = {LED_4, LED_5, LED_6};
    const Leds      downLeds[3]       = {LED_3, LED_2, LED_1};

    while (1)
    {
        WarpLedLevels levels = ComputeWarpLedLevels(warpSmoother.Value());
        hw.ClearLeds();
        for (int i = 0; i < 3; i++)
        {
            hw.SetLed(upLeds[i],
                      kWarpUpColor[0] * levels.up[i],
                      kWarpUpColor[1] * levels.up[i],
                      kWarpUpColor[2] * levels.up[i]);
            hw.SetLed(downLeds[i],
                      kWarpDownColor[0] * levels.down[i],
                      kWarpDownColor[1] * levels.down[i],
                      kWarpDownColor[2] * levels.down[i]);
        }
        hw.WriteLeds();
    }
}
