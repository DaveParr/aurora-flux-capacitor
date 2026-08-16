#pragma once
#include <cmath>
#include <cstddef>
#include "Utility/dsp.h"
#include "Utility/delayline.h"
#include "tape_transport.h"
#include "dsp_util.h"

namespace fluxcap
{
constexpr float kDelayTimeMinSeconds = 0.001f;
constexpr float kDelayTimeMaxSeconds = 2.0f;

// 2 seconds at a nominal 48kHz -- TapeVoice's DelayLine sizing precedent
// (fixed compile-time constant, not derived from hw.AudioSampleRate()).
constexpr size_t kTapeDelayMaxSamples = 96000;

constexpr float kTapeDelayFeedback = 0.35f; // fixed; no control surface yet

// ~3.2 kHz -3dB point. Verified empirically (host-side probe) to settle
// within ~10 samples at 48kHz -- comfortably inside even TIME's shortest
// (1ms / 48-sample) gap between repeats, so it darkens each repeat
// without smearing it into the next. A slower tau (e.g. 0.001s / ~159Hz)
// was tried first and rejected: it spread each repeat's energy across
// ~150 samples with no return to silence between taps, even at TIME's
// minimum -- a diffuse wash, not the tight slapback this phase wants.
constexpr float kTapeDelayFeedbackLpfTauSeconds = 0.00005f;

/** Maps KNOB_TIME (0..1) + CV_TIME (additive, same pattern as
 *  WARP/MIX/REFLECT/BLUR) to a base delay time in seconds, log-curved
 *  (like ComputeWowRateHz) so short slapback times and long ambient
 *  tails both feel like even steps across the knob's travel.
 */
inline float ComputeDelayTimeSeconds(float time_knob, float time_cv)
{
    float combined = daisysp::fclamp(time_knob + time_cv, 0.0f, 1.0f);
    return daisysp::fmap(combined, kDelayTimeMinSeconds, kDelayTimeMaxSeconds, daisysp::Mapping::LOG);
}

/** Converts a base delay time (seconds) to a sample count, scaled by
 *  tape transport speed the same way pitch is (halving speed doubles
 *  the effective delay time) so echoes lengthen and slow as the tape
 *  brakes. Floors speed at tape_transport.h's kMinStopSpeed (same
 *  constant StopSemitones uses, avoiding a second near-zero-speed
 *  floor) before dividing, then clamps to the buffer's actual capacity
 *  -- near a full stop this pins the delay at its longest possible
 *  value rather than overflowing, reading as "tails freeze into one
 *  long echo" rather than any error condition.
 */
inline float ComputeDelaySamples(float base_seconds, float speed, float sample_rate)
{
    float base_samples = base_seconds * sample_rate;
    return daisysp::fclamp(base_samples / daisysp::fmax(speed, kMinStopSpeed),
                            0.0f, static_cast<float>(kTapeDelayMaxSamples - 1));
}

/** Applies a wow/flutter semitone offset (the same per-sample value
 *  TapeVoice already consumes for pitch) to a delay-sample count as a
 *  multiplicative ratio -- physically honest, since a real tape
 *  transport's speed wobble changes pitch and echo timing together
 *  from the same underlying variation. Matches tape_voice.h's
 *  ComputeModFreq ratio-from-semitones relation.
 */
inline float ApplyWobbleToDelaySamples(float delay_samples, float wobble_semitones)
{
    float ratio = powf(2.0f, wobble_semitones / 12.0f);
    return delay_samples * ratio;
}

using TapeDelayLine = daisysp::DelayLine<float, kTapeDelayMaxSamples>;

/** Single-channel tape-style delay: a DelayLine with a damped feedback
 *  loop. Its backing buffer must live in Aurora's external SDRAM
 *  (DSY_SDRAM_BSS) -- TapeVoice's DelayLines already use most of the
 *  128KB DTCMRAM budget -- but that attribute lives in a libDaisy
 *  header the host-side doctest build can't compile. So TapeDelay
 *  takes a pointer to caller-owned storage instead of embedding the
 *  DelayLine as a member, keeping this header on DaisySP-only includes
 *  and just as host-testable as TapeVoice/WowFlutter. main.cpp declares
 *  the actual SDRAM-placed buffers and owns their lifetime.
 */
class TapeDelay
{
  public:
    // delay_line: caller-owned storage (e.g. an SDRAM-placed global in
    // main.cpp, or a plain stack object in a host test -- TapeDelay
    // doesn't know or care which). Caller must keep it alive for as
    // long as this TapeDelay is used.
    void Init(TapeDelayLine *delay_line, float sample_rate)
    {
        delay_ = delay_line;
        delay_->Init();
        sr_ = sample_rate;
        feedback_lpf_.Init(0.0f);
        feedback_lpf_coeff_ = 1.0f / (kTapeDelayFeedbackLpfTauSeconds * sample_rate);
        target_samples_     = 0.0f;
    }

    // base_seconds: TIME knob+CV, already control-rate smoothed by the
    // caller. speed: TapeTransport::Speed(). Called once per audio block.
    void Update(float base_seconds, float speed)
    {
        target_samples_ = ComputeDelaySamples(base_seconds, speed, sr_);
    }

    // wobble_semitones: WowFlutter::Process's return value for this
    // sample -- same value TapeVoice already consumed for pitch this
    // sample. Must be called once per audio sample.
    float Process(float in, float wobble_semitones)
    {
        float samples = ApplyWobbleToDelaySamples(target_samples_, wobble_semitones);
        delay_->SetDelay(samples);

        float wet      = delay_->Read();
        float filtered = feedback_lpf_.Process(wet, feedback_lpf_coeff_);
        delay_->Write(in + filtered * kTapeDelayFeedback);

        return wet;
    }

  private:
    TapeDelayLine  *delay_ = nullptr;
    OnePoleSmoother feedback_lpf_; // audio-rate LPF, not a control smoother
    float           feedback_lpf_coeff_ = 0.0f;
    float           target_samples_      = 0.0f;
    float           sr_                  = 48000.0f;
};
} // namespace fluxcap
