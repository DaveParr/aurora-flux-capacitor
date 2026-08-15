#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../tape_voice.h"
#include <cmath>
#include <vector>

using namespace fluxcap;

TEST_CASE("ComputeModFreq - zero semitones -> zero mod freq") {
    CHECK(ComputeModFreq(0.0f, 48000.0f, kTapeVoiceBufferSize) == doctest::Approx(0.0f));
}

TEST_CASE("ComputeModFreq - correct magnitude for both directions") {
    // +12 semitones: ratio = 2.0, mod_freq = (2.0-1.0)*sr/bufferSize
    float upExpected = (2.0f - 1.0f) * 48000.0f / static_cast<float>(kTapeVoiceBufferSize);
    CHECK(ComputeModFreq(12.0f, 48000.0f, kTapeVoiceBufferSize) == doctest::Approx(upExpected));

    // -12 semitones: ratio = 0.5, mod_freq = |0.5-1.0|*sr/bufferSize = 0.5*sr/bufferSize
    float downExpected = 0.5f * 48000.0f / static_cast<float>(kTapeVoiceBufferSize);
    CHECK(ComputeModFreq(-12.0f, 48000.0f, kTapeVoiceBufferSize) == doctest::Approx(downExpected));

    // These must NOT be equal -- that was the bug.
    CHECK(upExpected != doctest::Approx(downExpected));
}

TEST_CASE("ComputeModFreq - matches the octave-up formula") {
    // one octave up: ratio = 2.0, mod_freq = (ratio - 1.0) * sr / bufferSize
    float expected = (2.0f - 1.0f) * 48000.0f / static_cast<float>(kTapeVoiceBufferSize);
    CHECK(ComputeModFreq(12.0f, 48000.0f, kTapeVoiceBufferSize) == doctest::Approx(expected));
}

TEST_CASE("ComputeDryMix - fully dry at zero semitones") {
    CHECK(ComputeDryMix(0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("ComputeDryMix - fully wet at or beyond the blend range") {
    CHECK(ComputeDryMix(1.0f) == doctest::Approx(0.0f));
    CHECK(ComputeDryMix(-1.0f) == doctest::Approx(0.0f));
    CHECK(ComputeDryMix(5.0f) == doctest::Approx(0.0f));
}

TEST_CASE("ComputeDryMix - halfway through the blend range") {
    CHECK(ComputeDryMix(0.5f) == doctest::Approx(0.5f));
    CHECK(ComputeDryMix(-0.5f) == doctest::Approx(0.5f));
}

TEST_CASE("TapeVoice - exact dry passthrough at zero semitones") {
    TapeVoice voice;
    voice.Init(48000.0f);
    for (int i = 0; i < 100; i++) {
        float in  = 0.37f * static_cast<float>((i % 7) - 3);
        float out = voice.Process(in, 0.0f);
        CHECK(out == doctest::Approx(in));
    }
}

TEST_CASE("TapeVoice - does not crash or produce NaN/Inf when pitch-shifting") {
    TapeVoice voice;
    voice.Init(48000.0f);
    for (int i = 0; i < 2000; i++) {
        float in  = 0.5f * static_cast<float>((i % 13) - 6);
        float out = voice.Process(in, 7.0f);
        CHECK(std::isfinite(out));
    }
}

// Counts zero crossings (rising only) over the given buffer, returns estimated
// frequency in Hz given the sample rate.
static float EstimateFrequencyHz(const float* buf, int n, float sampleRate)
{
    int crossings = 0;
    for (int i = 1; i < n; i++)
        if (buf[i - 1] <= 0.0f && buf[i] > 0.0f)
            crossings++;
    float durationSec = static_cast<float>(n) / sampleRate;
    return static_cast<float>(crossings) / durationSec;
}

TEST_CASE("TapeVoice - downward pitch shift produces correct frequency") {
    const float sr = 48000.0f;
    const float inputFreq = 440.0f;
    const int n = 48000; // 1 second, enough to let the crossfade settle

    TapeVoice voice;
    voice.Init(sr);

    std::vector<float> out(n);
    for (int i = 0; i < n; i++) {
        float in = sinf(2.0f * static_cast<float>(M_PI) * inputFreq * static_cast<float>(i) / sr);
        out[i] = voice.Process(in, -12.0f); // one octave down
    }

    // Measure over the second half only, after the crossfade has settled.
    float measured = EstimateFrequencyHz(out.data() + n/2, n/2, sr);
    float expected = inputFreq * 0.5f; // -12 semitones = half frequency

    CHECK(measured == doctest::Approx(expected).epsilon(0.05)); // 5% tolerance
}
