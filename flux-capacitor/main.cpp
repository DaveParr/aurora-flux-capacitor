/** flux-capacitor
 *
 *  Phase 1: WARP knob/CV pitch bend via a continuous, glitch-free tape
 *  voice pitch engine (no semitone stepping). True stereo.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md
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

    // Phase 1 has no ongoing LED behavior; clear whatever state a previous
    // firmware or the bootloader left lit so the module doesn't look hung.
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

    while (1) {}
}
