/** flux-capacitor
 *
 *  Phase 2: FREEZE-driven tape stop with a MIX dry/wet blend, layered
 *  on top of Phase 1's WARP knob/CV pitch bend. True stereo. LED_1-6
 *  show the WARP pitch shift as a bar-graph; LED_FREEZE tracks the
 *  tape-stop fade. Only when MIX is dialed fully to 0 does FREEZE
 *  auto-override to fully wet while actively stopping/stopped/starting
 *  (otherwise the effect would be silent), settling back to 0 once
 *  play resumes. Any other MIX setting is untouched by FREEZE.
 *  LED_FREEZE briefly flashes white whenever MIX crosses into or out
 *  of that fully-dry auto-wet zone, so the boundary is visible live.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md, and
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md
 */
#include "aurora.h"
#include "tape_voice.h"
#include "warp_control.h"
#include "tape_transport.h"
#include "mix_control.h"
#include "dsp_util.h"

using namespace daisy;
using namespace aurora;
using namespace fluxcap;

Hardware        hw;
TapeVoice       voiceL, voiceR;
TapeTransport   transport;
OnePoleSmoother warpSmoother;
OnePoleSmoother mixSmoother;
float           warpSmoothCoeff = 0.0f;
float           mixSmoothCoeff  = 0.0f;
float           stopRampCoeff   = 0.0f;

// MIX dry-zone-crossing white flash on LED_FREEZE (see MixDryZoneCrossed).
// prevMix/mixFlashBlocksRemaining are written in AudioCallback and read
// from the main loop -- same cross-context display-value read already
// used for warpSmoother.Value() and transport.Speed() below.
float prevMix                  = 0.0f;
int   mixFlashBlocksRemaining = 0;
int   mixFlashBlocksTotal     = 0;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();

    float rawWarpSemis = ComputeWarpSemitones(hw.GetKnobValue(KNOB_WARP), hw.GetWarpVoct());
    float warpSemis     = warpSmoother.Process(rawWarpSemis, warpSmoothCoeff);

    bool freezeEdge = hw.GetButton(SW_FREEZE).RisingEdge();
    bool gateHigh   = hw.GetGateState(GATE_FREEZE);
    transport.Update(freezeEdge, gateHigh, stopRampCoeff);
    float speed = transport.Speed();

    float totalSemis = warpSemis + StopSemitones(speed);
    float wetAmp      = StopAmplitude(speed);

    float rawMix = ComputeMix(hw.GetKnobValue(KNOB_MIX), hw.GetCvValue(CV_MIX));
    float mix     = mixSmoother.Process(rawMix, mixSmoothCoeff);

    if (MixDryZoneCrossed(mix, prevMix))
        mixFlashBlocksRemaining = mixFlashBlocksTotal;
    else if (mixFlashBlocksRemaining > 0)
        mixFlashBlocksRemaining--;
    prevMix = mix;

    float effectiveMix = ComputeEffectiveMix(mix, speed);
    float dryGain, wetGain;
    ComputeMixGains(effectiveMix, &dryGain, &wetGain);

    for (size_t i = 0; i < size; i++)
    {
        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        out[0][i] = in[0][i] * dryGain + wetL * wetGain;
        out[1][i] = in[1][i] * dryGain + wetR * wetGain;
    }
}

int main(void)
{
    hw.Init();
    hw.ClearLeds();
    hw.WriteLeds();

    // One-pole smoothing time constants for control-rate jitter damping
    // (not pitch/amplitude glide -- TapeVoice's crossfade and
    // TapeTransport's own ramp handle those).
    constexpr float kWarpSmoothTimeSeconds = 0.02f;
    constexpr float kMixSmoothTimeSeconds  = 0.02f;
    // fonepole's "time" parameter is an exponential time constant (tau), not a
    // fixed ramp duration -- see dsp.h's fonepole doc comment. TapeTransport
    // snaps to its target once within kSnapEpsilon (0.001), which an
    // exponential decay reaches at t = tau * ln(1/kSnapEpsilon) = tau * ln(1000).
    // Solving for a ~1.5s settle time: tau = 1.5 / ln(1000) ~= 0.217s.
    constexpr float kStopRampTimeSeconds   = 0.217f;
    // Brief, fixed-duration white flash on LED_FREEZE when MIX crosses the
    // fully-dry auto-wet boundary -- not a fade, just a short blink.
    constexpr float kMixEdgeFlashSeconds = 0.15f;
    warpSmoothCoeff = 1.0f / (kWarpSmoothTimeSeconds * hw.AudioCallbackRate());
    mixSmoothCoeff  = 1.0f / (kMixSmoothTimeSeconds * hw.AudioCallbackRate());
    stopRampCoeff   = 1.0f / (kStopRampTimeSeconds * hw.AudioCallbackRate());
    mixFlashBlocksTotal = static_cast<int>(kMixEdgeFlashSeconds * hw.AudioCallbackRate());

    voiceL.Init(hw.AudioSampleRate());
    voiceR.Init(hw.AudioSampleRate());
    transport.Init();
    warpSmoother.Init(0.0f);
    mixSmoother.Init(0.0f);

    hw.StartAudio(AudioCallback);

    // WARP pitch-shift bar-graph: LED_4/5/6 light amber outward for an
    // upward shift, LED_3/2/1 light cyan outward for a downward shift.
    // LED_FREEZE lights red, brightness tracking (1 - tape speed), so
    // it's off in normal play and full-bright exactly when stopped.
    // See docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md
    // and docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md
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
        if (mixFlashBlocksRemaining > 0)
            hw.SetLed(LED_FREEZE, 1.0f, 1.0f, 1.0f); // white: MIX just crossed the fully-dry auto-wet boundary
        else
            hw.SetLed(LED_FREEZE, 1.0f - transport.Speed(), 0.0f, 0.0f);
        hw.WriteLeds();
    }
}
