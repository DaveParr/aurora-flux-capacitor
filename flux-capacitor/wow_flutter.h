#pragma once
#include <cmath>
#include "Utility/dsp.h"
#include "Synthesis/oscillator.h"
#include "Noise/whitenoise.h"
#include "dsp_util.h"

namespace fluxcap
{
constexpr float kWowRateMinHz      = 0.1f;
constexpr float kWowRateMaxHz      = 2.0f;
constexpr float kWowDepthSemitones = 0.15f; // fixed; REFLECT controls rate only

constexpr float kFlutterDepthMaxSemitones = 0.3f; // BLUR's full-scale depth
constexpr float kFlutterLpfTauSeconds     = 0.015f; // ~10.6 Hz -3dB point; audio-rate coeff, not control-rate

// OnePoleSmoother's math preserves unity DC gain for a *slowly-varying
// control signal* (its normal job elsewhere in this codebase), but a fast,
// wideband noise input loses most of its energy to the same math -- a
// control-smoother's implicit "no gain compensation needed" assumption
// doesn't hold when it's repurposed as a noise filter. Empirically
// (measured via a host-side scratch run, same deterministic WhiteNoise
// seed as Init() always produces), the LPF'd noise peaks around 0.074 out
// of a full-scale +/-1 input at this coefficient, so this makeup gain
// restores peak magnitude to roughly full-scale before flutter depth is
// applied. See test_wow_flutter.cpp for the reproducible measurement.
constexpr float kFlutterMakeupGain = 13.5f;

constexpr float kWowFlutterLedMaxSemitones       = kWowDepthSemitones; // wow's own full swing alone reaches the LED extremes
constexpr float kWowFlutterLedMinBrightnessScale = 0.5f;

/** Maps KNOB_REFLECT (0..1) + CV_REFLECT (additive, same pattern as
 *  WARP/MIX) to a wow LFO rate in Hz, log-curved so the knob feels
 *  musically even across its range rather than front-loaded.
 */
inline float ComputeWowRateHz(float reflect_knob, float reflect_cv)
{
    float combined = daisysp::fclamp(reflect_knob + reflect_cv, 0.0f, 1.0f);
    return daisysp::fmap(combined, kWowRateMinHz, kWowRateMaxHz, daisysp::Mapping::LOG);
}

/** Maps KNOB_BLUR (0..1) + CV_BLUR (additive) to a flutter modulation
 *  depth in semitones, linearly from 0 to kFlutterDepthMaxSemitones.
 */
inline float ComputeFlutterDepthSemitones(float blur_knob, float blur_cv)
{
    float combined = daisysp::fclamp(blur_knob + blur_cv, 0.0f, 1.0f);
    return combined * kFlutterDepthMaxSemitones;
}

/** Maps a combined wow+flutter semitone offset to a WARP bar-graph
 *  brightness multiplier in [kWowFlutterLedMinBrightnessScale, 1.0].
 *  The LEDs breathe symmetrically around the range's midpoint (0.75)
 *  as the wobble signal swings positive/negative -- they are not
 *  pinned to full brightness at rest (combined == 0). Normalized
 *  against kWowDepthSemitones alone (not wow+flutter combined) so
 *  that wow's own swing -- the common case -- reaches the LED
 *  range's full extremes on its own; flutter adds jitter on top and
 *  clamps at the rails more often when it constructively adds to wow.
 */
inline float ComputeWowFlutterLedScale(float combined_semitones)
{
    float normalized = daisysp::fclamp(
        combined_semitones / kWowFlutterLedMaxSemitones, -1.0f, 1.0f); // -1..1
    return daisysp::fmap(
        (normalized + 1.0f) * 0.5f, kWowFlutterLedMinBrightnessScale, 1.0f);
}

/** Combined tape wow (slow sine) + flutter (fast filtered noise)
 *  pitch modulation source, in semitones. Wow depth is fixed
 *  (kWowDepthSemitones); flutter depth is caller-controlled per call
 *  (from BLUR). Both rate_hz and flutter_depth_semitones are expected
 *  to already be control-rate smoothed by the caller.
 */
class WowFlutter
{
  public:
    void Init(float sample_rate)
    {
        wow_osc_.Init(sample_rate);
        wow_osc_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        wow_osc_.SetAmp(1.0f); // Oscillator defaults to amp_ = 0.5; full range needed
                                // so kWowDepthSemitones is the true peak deviation.
        flutter_noise_.Init();
        flutter_lpf_.Init(0.0f);
        flutter_lpf_coeff_ = 1.0f / (kFlutterLpfTauSeconds * sample_rate);
        last_value_        = 0.0f;
    }

    // Must be called once per audio sample -- it advances both the wow
    // oscillator's phase and the flutter noise/filter state.
    float Process(float rate_hz, float flutter_depth_semitones)
    {
        wow_osc_.SetFreq(rate_hz);
        float wow = wow_osc_.Process() * kWowDepthSemitones;

        float noise    = flutter_noise_.Process();
        float filtered = flutter_lpf_.Process(noise, flutter_lpf_coeff_);
        float boosted  = daisysp::fclamp(filtered * kFlutterMakeupGain, -1.0f, 1.0f);
        float flutter  = boosted * flutter_depth_semitones;

        last_value_ = wow + flutter;
        return last_value_;
    }

    // Last combined semitone offset, read from the main loop for LED display.
    float Value() const { return last_value_; }

  private:
    daisysp::Oscillator wow_osc_;
    daisysp::WhiteNoise  flutter_noise_;
    OnePoleSmoother       flutter_lpf_; // reused as an audio-rate LPF, not a control smoother
    float                 flutter_lpf_coeff_ = 0.0f;
    float                 last_value_        = 0.0f;
};
} // namespace fluxcap
