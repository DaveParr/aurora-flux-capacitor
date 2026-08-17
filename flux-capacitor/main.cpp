/** flux-capacitor
 *
 *  Phase 3: REFLECT-controlled wow (slow sine pitch drift) and
 *  BLUR-controlled flutter (fast filtered-noise pitch jitter), summed
 *  into the WARP + tape-stop pitch pipeline. True stereo. LED_1-6 show
 *  the WARP pitch shift as a bar-graph, its brightness breathing with
 *  the wow/flutter amount. FREEZE drives a tape-stop with a MIX dry/wet
 *  blend; only when MIX is dialed fully to 0 does FREEZE auto-override
 *  to fully wet while actively stopping/stopped/starting (otherwise the
 *  effect would be silent), settling back to 0 once play resumes -- any
 *  other MIX setting is untouched by FREEZE. LED_FREEZE tracks the
 *  tape-stop fade, briefly flashing white whenever MIX crosses into or
 *  out of that fully-dry auto-wet zone.
 *  Phase 4 adds a stereo tape delay (KNOB_TIME/CV_TIME, log-mapped
 *  1ms-2s) between the pitch stage and the MIX blend: repeats darken
 *  via a fixed feedback lowpass, lengthen and slow as FREEZE brakes the
 *  tape, and wobble in sync with the same wow/flutter signal already
 *  applied to pitch.
 *  Phase 5a adds ATMOSPHERE (KNOB_ATMOSPHERE/CV_ATMOSPHERE) as a single
 *  tape-coloration control: it darkens the wet signal (tone lowpass,
 *  additionally darkened by tape-stop speed), adds tape-style
 *  saturation, and raises TapeDelay's feedback amount, all together.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase3-wow-flutter-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase4-tape-delay-design.md, and
 *  docs/superpowers/specs/2026-08-17-flux-capacitor-phase5-atmosphere-design.md
 */
#include "aurora.h"
#include "tape_voice.h"
#include "warp_control.h"
#include "tape_transport.h"
#include "mix_control.h"
#include "wow_flutter.h"
#include "dsp_util.h"
#include "tape_delay.h"
#include "atmosphere_control.h"

using namespace daisy;
using namespace aurora;
using namespace fluxcap;

Hardware        hw;
TapeVoice       voiceL, voiceR;
TapeTransport   transport;
WowFlutter      wowFlutter;
OnePoleSmoother warpSmoother;
OnePoleSmoother mixSmoother;
OnePoleSmoother reflectSmoother;
OnePoleSmoother blurSmoother;
float           warpSmoothCoeff    = 0.0f;
float           mixSmoothCoeff     = 0.0f;
float           stopRampCoeff      = 0.0f;
float           reflectSmoothCoeff = 0.0f;
float           blurSmoothCoeff    = 0.0f;

// SDRAM-placed backing storage for TapeDelay -- see tape_delay.h's
// class comment and the design spec's "Buffer sizing and memory
// placement". DSY_SDRAM_BSS is already available here transitively via
// aurora.h -> daisy_seed.h -> daisy.h -> dev/sdram.h; no new include
// is needed.
TapeDelayLine DSY_SDRAM_BSS delayLineL, delayLineR;
TapeDelay     delayL, delayR;
OnePoleSmoother timeSmoother;
float           timeSmoothCoeff = 0.0f;
OnePoleSmoother atmosphereSmoother;
float           atmosphereSmoothCoeff = 0.0f;
OnePoleSmoother atmosphereLpfL, atmosphereLpfR; // audio-rate tone filters, one per channel

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
    float wetAmp = StopAmplitude(speed);

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

    float rawWowRateHz = ComputeWowRateHz(hw.GetKnobValue(KNOB_REFLECT), hw.GetCvValue(CV_REFLECT));
    float wowRateHz     = reflectSmoother.Process(rawWowRateHz, reflectSmoothCoeff);

    float rawFlutterDepth = ComputeFlutterDepthSemitones(hw.GetKnobValue(KNOB_BLUR), hw.GetCvValue(CV_BLUR));
    float flutterDepth     = blurSmoother.Process(rawFlutterDepth, blurSmoothCoeff);

    float rawDelaySeconds = ComputeDelayTimeSeconds(hw.GetKnobValue(KNOB_TIME), hw.GetCvValue(CV_TIME));
    float delaySeconds     = timeSmoother.Process(rawDelaySeconds, timeSmoothCoeff);

    float rawAtmosphere = ComputeAtmosphereAmount(hw.GetKnobValue(KNOB_ATMOSPHERE), hw.GetCvValue(CV_ATMOSPHERE));
    float atmosphere     = atmosphereSmoother.Process(rawAtmosphere, atmosphereSmoothCoeff);
    float atmosphereLpfCoeff = ComputeAtmosphereLpfCoeff(atmosphere, speed, hw.AudioSampleRate());
    float atmosphereDrive    = ComputeSaturationDrive(atmosphere);

    delayL.Update(delaySeconds, speed, atmosphere);
    delayR.Update(delaySeconds, speed, atmosphere);

    for (size_t i = 0; i < size; i++)
    {
        float wobble     = wowFlutter.Process(wowRateHz, flutterDepth);
        // wobble also feeds TapeVoice::ComputeDryMix (via totalSemis) --
        // near WARP-centered, this incidentally chorus/AM-modulates the
        // signal instead of shifting pitch alone, since a small nonzero
        // semitone value there partially unblends dry and wet rather than
        // passing through clean. Kept deliberately (reads as authentic
        // tape wobble character); revisit only if it proves undesirable
        // on hardware -- the clean fix decouples TapeVoice's dry-blend
        // decision from its pitch-shift amount, a Phase-1 interface change.
        float totalSemis = warpSemis + StopSemitones(speed) + wobble;

        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        wetL = atmosphereLpfL.Process(ApplySaturation(wetL, atmosphereDrive), atmosphereLpfCoeff);
        wetR = atmosphereLpfR.Process(ApplySaturation(wetR, atmosphereDrive), atmosphereLpfCoeff);

        wetL = delayL.Process(wetL, wobble);
        wetR = delayR.Process(wetR, wobble);

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
    // (not pitch/amplitude glide -- TapeVoice's crossfade, TapeTransport's
    // own ramp, and WowFlutter's per-sample oscillator/filter handle those).
    constexpr float kWarpSmoothTimeSeconds    = 0.02f;
    constexpr float kMixSmoothTimeSeconds     = 0.02f;
    constexpr float kReflectSmoothTimeSeconds = 0.02f;
    constexpr float kBlurSmoothTimeSeconds    = 0.02f;
    // fonepole's "time" parameter is an exponential time constant (tau), not a
    // fixed ramp duration -- see dsp.h's fonepole doc comment. TapeTransport
    // snaps to its target once within kSnapEpsilon (0.001), which an
    // exponential decay reaches at t = tau * ln(1/kSnapEpsilon) = tau * ln(1000).
    // Solving for a ~1.5s settle time: tau = 1.5 / ln(1000) ~= 0.217s.
    constexpr float kStopRampTimeSeconds = 0.217f;
    // Brief, fixed-duration white flash on LED_FREEZE when MIX crosses the
    // fully-dry auto-wet boundary -- not a fade, just a short blink.
    constexpr float kMixEdgeFlashSeconds = 0.15f;
    constexpr float kTimeSmoothTimeSeconds = 0.08f; // slower than the other 0.02f smoothers --
    // TIME's sweep is meant to audibly warble, not click; see the design spec.
    constexpr float kAtmosphereSmoothTimeSeconds = 0.02f; // same as WARP/MIX/REFLECT/BLUR --
    // unlike TIME, a fast ATMOSPHERE turn isn't meant to read as
    // intentional analog character, so it should just track cleanly.
    warpSmoothCoeff     = 1.0f / (kWarpSmoothTimeSeconds * hw.AudioCallbackRate());
    mixSmoothCoeff      = 1.0f / (kMixSmoothTimeSeconds * hw.AudioCallbackRate());
    stopRampCoeff       = 1.0f / (kStopRampTimeSeconds * hw.AudioCallbackRate());
    reflectSmoothCoeff  = 1.0f / (kReflectSmoothTimeSeconds * hw.AudioCallbackRate());
    blurSmoothCoeff     = 1.0f / (kBlurSmoothTimeSeconds * hw.AudioCallbackRate());
    mixFlashBlocksTotal = static_cast<int>(kMixEdgeFlashSeconds * hw.AudioCallbackRate());
    timeSmoothCoeff     = 1.0f / (kTimeSmoothTimeSeconds * hw.AudioCallbackRate());
    atmosphereSmoothCoeff = 1.0f / (kAtmosphereSmoothTimeSeconds * hw.AudioCallbackRate());

    voiceL.Init(hw.AudioSampleRate());
    voiceR.Init(hw.AudioSampleRate());
    transport.Init();
    wowFlutter.Init(hw.AudioSampleRate());
    warpSmoother.Init(0.0f);
    mixSmoother.Init(0.0f);
    reflectSmoother.Init(kWowRateMinHz);
    blurSmoother.Init(0.0f);
    delayL.Init(&delayLineL, hw.AudioSampleRate());
    delayR.Init(&delayLineR, hw.AudioSampleRate());
    timeSmoother.Init(0.0f);
    atmosphereSmoother.Init(0.0f);
    atmosphereLpfL.Init(0.0f);
    atmosphereLpfR.Init(0.0f);

    hw.StartAudio(AudioCallback);

    // WARP pitch-shift bar-graph: LED_4/5/6 light amber outward for an
    // upward shift, LED_3/2/1 light cyan outward for a downward shift,
    // brightness breathing with the wow/flutter modulation amount. When
    // WARP is centered, all 6 LEDs instead show a cyan-to-amber gradient
    // across the bar (a "centered" rest indicator), crossfading smoothly
    // into the normal bar-graph as the knob leaves center.
    // LED_FREEZE lights red, brightness tracking (1 - tape speed), except
    // it briefly flashes white whenever MIX crosses the fully-dry
    // auto-wet boundary (see MixDryZoneCrossed) -- unaffected by
    // wow/flutter or the centered gradient either way, since it
    // represents transport/MIX state, not the pitch-shift display.
    // See docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md,
    // docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md, and
    // docs/superpowers/specs/2026-08-16-flux-capacitor-phase3-wow-flutter-design.md
    constexpr float kWarpUpColor[3]   = {1.0f, 0.4f, 0.0f}; // amber
    constexpr float kWarpDownColor[3] = {0.0f, 0.6f, 1.0f}; // cyan
    const Leds      upLeds[3]         = {LED_4, LED_5, LED_6};
    const Leds      downLeds[3]       = {LED_3, LED_2, LED_1};

    while (1)
    {
        WarpLedLevels levels     = ComputeWarpLedLevels(warpSmoother.Value());
        float          ledScale   = ComputeWowFlutterLedScale(wowFlutter.Value());
        float          centerGlow = ComputeWarpCenterGlow(warpSmoother.Value());
        hw.ClearLeds();
        for (int i = 0; i < 3; i++)
        {
            // Physical left-to-right gradient position: down[0..2] map to
            // LED_3,LED_2,LED_1 (indices 2,1,0), up[0..2] map to
            // LED_4,LED_5,LED_6 (indices 3,4,5). See ComputeWarpCenterGradientColor.
            float gradUp[3], gradDown[3];
            ComputeWarpCenterGradientColor(3 + i, kWarpDownColor, kWarpUpColor, gradUp);
            ComputeWarpCenterGradientColor(2 - i, kWarpDownColor, kWarpUpColor, gradDown);

            hw.SetLed(upLeds[i],
                      (kWarpUpColor[0] * levels.up[i] * (1.0f - centerGlow) + gradUp[0] * centerGlow) * ledScale,
                      (kWarpUpColor[1] * levels.up[i] * (1.0f - centerGlow) + gradUp[1] * centerGlow) * ledScale,
                      (kWarpUpColor[2] * levels.up[i] * (1.0f - centerGlow) + gradUp[2] * centerGlow) * ledScale);
            hw.SetLed(downLeds[i],
                      (kWarpDownColor[0] * levels.down[i] * (1.0f - centerGlow) + gradDown[0] * centerGlow) * ledScale,
                      (kWarpDownColor[1] * levels.down[i] * (1.0f - centerGlow) + gradDown[1] * centerGlow) * ledScale,
                      (kWarpDownColor[2] * levels.down[i] * (1.0f - centerGlow) + gradDown[2] * centerGlow) * ledScale);
        }
        if (mixFlashBlocksRemaining > 0)
            hw.SetLed(LED_FREEZE, 1.0f, 1.0f, 1.0f); // white: MIX just crossed the fully-dry auto-wet boundary
        else
            hw.SetLed(LED_FREEZE, 1.0f - transport.Speed(), 0.0f, 0.0f);
        hw.WriteLeds();
    }
}
