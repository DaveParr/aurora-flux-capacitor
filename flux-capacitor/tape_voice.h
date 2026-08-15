#pragma once
#include <cmath>
#include <cstddef>
#include "Utility/dsp.h"
#include "Utility/delayline.h"
#include "Control/phasor.h"

namespace fluxcap
{
// Chosen to fit two stereo TapeVoice instances' DelayLines comfortably within
// Aurora's 128KB DTCMRAM budget (each DelayLine<float,N> is N*4 bytes; two
// per voice, two voices for stereo = 16*N bytes total). 4096 samples is the
// maximum tap length (~85ms at 48kHz); the actual crossfade period is
// 1/mod_freq, which varies with the semitone value and only approaches this
// maximum at extreme settings. Uses 65,536 bytes total, leaving headroom for
// the rest of .bss and future phases.
constexpr size_t kTapeVoiceBufferSize = 4096;

// Semitone offset (either direction) over which Process blends from a
// clean dry passthrough (at 0) to full crossfaded tape-voice processing
// (at or beyond this threshold). Without this blend, semitones == 0 would
// lock the crossfade onto a single fixed ~43ms delay tap instead of
// passing audio through cleanly -- the same characteristic DaisySP's
// PitchShifter has at transpose == 0, since its crossfade phasors freeze
// when the mod frequency is zero.
constexpr float kDryBlendRangeSemitones = 1.0f;

/** Crossfade rate (Hz) for the two delay taps, given a semitone offset.
 *  Always >= 0: the magnitude is the pitch ratio's deviation from 1.0
 *  (i.e. how far the ratio implied by semitones is from unity), taken
 *  after applying the sign of semitones to the exponent so up- and
 *  down-shifts get their correct, generally different, rates. Direction
 *  (which tap sweeps toward/away from zero delay) is handled separately
 *  by TapeVoice via the sign of semitones.
 */
inline float ComputeModFreq(float semitones, float sampleRate, size_t bufferSize)
{
    float ratio = powf(2.0f, semitones / 12.0f); // < 1.0 for downshifts
    return (fabsf(ratio - 1.0f) * sampleRate) / static_cast<float>(bufferSize);
}

/** Dry/wet blend weight for the dry signal: 1.0 at semitones == 0, falling
 *  linearly to 0.0 by +/-kDryBlendRangeSemitones.
 */
inline float ComputeDryMix(float semitones)
{
    return daisysp::fclamp(
        1.0f - (fabsf(semitones) / kDryBlendRangeSemitones), 0.0f, 1.0f);
}

/** Continuous-pitch tape-style voice.
 *
 *  Adapted from DaisySP's PitchShifter: two overlapping DelayLine taps,
 *  crossfaded via Phasor-driven LFOs so each tap periodically resets to
 *  zero delay while the other is audible (glitch-free, indefinite pitch
 *  shift). Unlike PitchShifter, the crossfade rate is computed directly
 *  from a continuous semitone value instead of a quantized lookup table,
 *  so pitch tracks smoothly rather than stepping. Blends to a dry
 *  passthrough near semitones == 0 (see kDryBlendRangeSemitones).
 */
class TapeVoice
{
  public:
    void Init(float sample_rate)
    {
        sr_             = sample_rate;
        last_semitones_ = 0.0f;
        mod_freq_       = 0.0f;
        shift_up_       = true;
        for (int i = 0; i < 2; i++)
        {
            gain_[i] = 0.0f;
            delay_[i].Init();
            phasor_[i].Init(sr_, 0.0f, i == 0 ? 0.0f : PI_F);
        }
    }

    float Process(float in, float semitones)
    {
        UpdateSemitones(semitones);

        float fade1 = phasor_[0].Process();
        float fade2 = phasor_[1].Process();

        if (shift_up_)
        {
            fade1 = 1.0f - fade1;
            fade2 = 1.0f - fade2;
        }

        float mod0 = fade1 * static_cast<float>(kTapeVoiceBufferSize - 1);
        float mod1 = fade2 * static_cast<float>(kTapeVoiceBufferSize - 1);

        gain_[0] = sinf(fade1 * PI_F);
        gain_[1] = sinf(fade2 * PI_F);

        delay_[0].Write(in);
        delay_[1].Write(in);
        delay_[0].SetDelay(mod0);
        delay_[1].SetDelay(mod1);

        float wet = (delay_[0].Read() * gain_[0]) + (delay_[1].Read() * gain_[1]);

        float dryMix = ComputeDryMix(semitones);
        return in * dryMix + wet * (1.0f - dryMix);
    }

  private:
    void UpdateSemitones(float semitones)
    {
        if (semitones == last_semitones_)
            return;
        last_semitones_ = semitones;

        shift_up_ = semitones > 0.0f;
        mod_freq_ = ComputeModFreq(semitones, sr_, kTapeVoiceBufferSize);

        phasor_[0].SetFreq(mod_freq_);
        phasor_[1].SetFreq(mod_freq_);
    }

    daisysp::DelayLine<float, kTapeVoiceBufferSize> delay_[2];
    daisysp::Phasor                                 phasor_[2];
    float                                            gain_[2] = {0.0f, 0.0f};
    float                                            sr_ = 48000.0f;
    float                                             mod_freq_ = 0.0f;
    float                                             last_semitones_ = 0.0f;
    bool                                               shift_up_ = true;
};
} // namespace fluxcap
