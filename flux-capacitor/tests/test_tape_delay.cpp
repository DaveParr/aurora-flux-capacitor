#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../tape_delay.h"
#include <cmath>

using namespace fluxcap;

TEST_CASE("ComputeDelayTimeSeconds - knob endpoints map to the range") {
    CHECK(ComputeDelayTimeSeconds(0.0f, 0.0f) == doctest::Approx(kDelayTimeMinSeconds));
    CHECK(ComputeDelayTimeSeconds(1.0f, 0.0f) == doctest::Approx(kDelayTimeMaxSeconds));
}

TEST_CASE("ComputeDelayTimeSeconds - combined knob+CV clamps to [0,1] before mapping") {
    CHECK(ComputeDelayTimeSeconds(0.9f, 0.5f) == doctest::Approx(kDelayTimeMaxSeconds)); // clamped high
    CHECK(ComputeDelayTimeSeconds(0.1f, -0.5f) == doctest::Approx(kDelayTimeMinSeconds)); // clamped low
}

TEST_CASE("ComputeDelayTimeSeconds - result always within [min, max]") {
    for (float knob = 0.0f; knob <= 1.0f; knob += 0.1f)
    {
        float seconds = ComputeDelayTimeSeconds(knob, 0.0f);
        CHECK(seconds >= kDelayTimeMinSeconds);
        CHECK(seconds <= kDelayTimeMaxSeconds);
    }
}

TEST_CASE("ComputeDelaySamples - speed 1.0 is unscaled base_seconds * sample_rate") {
    CHECK(ComputeDelaySamples(0.002f, 1.0f, 48000.0f) == doctest::Approx(96.0f));
}

TEST_CASE("ComputeDelaySamples - speed below 1.0 lengthens the result") {
    float atFull = ComputeDelaySamples(0.002f, 1.0f, 48000.0f);
    float atHalf = ComputeDelaySamples(0.002f, 0.5f, 48000.0f);
    CHECK(atHalf == doctest::Approx(atFull * 2.0f));
}

TEST_CASE("ComputeDelaySamples - clamps to buffer capacity for near-zero speed") {
    float result = ComputeDelaySamples(kDelayTimeMaxSeconds, 0.0f, 48000.0f);
    CHECK(result == doctest::Approx(static_cast<float>(kTapeDelayMaxSamples - 1)));
}

TEST_CASE("ComputeDelaySamples - speed=0 and speed below kMinStopSpeed clamp identically") {
    float atZero  = ComputeDelaySamples(0.01f, 0.0f, 48000.0f);
    float atFloor = ComputeDelaySamples(0.01f, kMinStopSpeed / 2.0f, 48000.0f);
    CHECK(atZero == doctest::Approx(atFloor));
}

TEST_CASE("ApplyWobbleToDelaySamples - zero wobble leaves samples unchanged") {
    CHECK(ApplyWobbleToDelaySamples(100.0f, 0.0f) == doctest::Approx(100.0f));
}

TEST_CASE("ApplyWobbleToDelaySamples - +12 semitones halves, -12 doubles") {
    CHECK(ApplyWobbleToDelaySamples(100.0f, 12.0f) == doctest::Approx(50.0f));
    CHECK(ApplyWobbleToDelaySamples(100.0f, -12.0f) == doctest::Approx(200.0f));
}

TEST_CASE("TapeDelay - impulse produces decaying, bounded repeats at the expected spacing") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 1.0f); // target 100 samples

    float peaks[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int   repeat   = 0;
    float maxAbs   = 0.0f;

    for (int i = 0; i < 800 && repeat < 4; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        maxAbs    = daisysp::fmax(maxAbs, fabsf(wet));

        int expectedCenter = (repeat + 1) * 100;
        if (i >= expectedCenter - 5 && i <= expectedCenter + 15) {
            if (fabsf(wet) > peaks[repeat])
                peaks[repeat] = fabsf(wet);
            if (i == expectedCenter + 15)
                repeat++;
        }
    }

    CHECK(peaks[0] == doctest::Approx(1.0f));  // first repeat: unscaled impulse
    CHECK(peaks[1] < peaks[0]);                  // each repeat strictly quieter than the last
    CHECK(peaks[2] < peaks[1]);
    CHECK(peaks[3] < peaks[2]);
    CHECK(maxAbs == doctest::Approx(1.0f));      // never exceeds the original impulse

    // Pins kTapeDelayFeedbackLpfTauSeconds against regressing to a
    // slower value (e.g. the rejected 0.001f) that would smear repeats
    // together instead of darkening them: at the correct tau, the
    // second repeat's peak is ~14.6% of the first; at the rejected
    // tau it would be ~0.7%. All the *ordering* assertions above this
    // one pass at either tau, so without this the tau has zero
    // regression coverage despite being documented load-bearing.
    CHECK(peaks[1] > 0.05f);
}

TEST_CASE("TapeDelay - per-sample delay-position slew is bounded even for a huge target jump") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(kDelayTimeMinSeconds, 1.0f); // tiny target
    delay.Process(0.0f, 0.0f);                // settle near the tiny target
    delay.Update(kDelayTimeMaxSeconds, 1.0f); // huge jump in target (~96000 samples)

    float before = delay.CurrentDelaySamples();
    delay.Process(0.0f, 0.0f);
    float after = delay.CurrentDelaySamples();

    CHECK(fabsf(after - before) <= kMaxDelaySlewSamplesPerSample + 1e-4f);
}

TEST_CASE("TapeDelay - lower speed lengthens the gap between repeats") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 0.5f); // target 200 samples (100 / 0.5)

    int firstRepeatSample = -1;
    for (int i = 0; i < 400; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        if (i > 0 && fabsf(wet) > 0.5f) {
            firstRepeatSample = i;
            break;
        }
    }
    CHECK(firstRepeatSample == 200);
}

TEST_CASE("TapeDelay - zero wobble and full speed reproduce ComputeDelaySamples exactly") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(50.0f / 48000.0f, 1.0f); // target 50 samples

    int firstRepeatSample = -1;
    for (int i = 0; i < 200; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        if (i > 0 && fabsf(wet) > 0.5f) {
            firstRepeatSample = i;
            break;
        }
    }
    CHECK(firstRepeatSample == 50);
}

TEST_CASE("TapeDelay - wobble affects delay timing as expected") {
    TapeDelayLine linePos;
    TapeDelayLine lineNeg;
    TapeDelay     delayPos;
    TapeDelay     delayNeg;
    delayPos.Init(&linePos, 48000.0f);
    delayNeg.Init(&lineNeg, 48000.0f);
    delayPos.Update(100.0f / 48000.0f, 1.0f); // target 100 samples
    delayNeg.Update(100.0f / 48000.0f, 1.0f);

    // Positive wobble (higher speed) should shorten the delay;
    // negative wobble (lower speed) should lengthen it.
    int posRepeatSample = -1;
    int negRepeatSample = -1;

    for (int i = 0; i < 400; i++) {
        float in = (i == 0) ? 1.0f : 0.0f;
        float posWet = delayPos.Process(in, 12.0f); // +12 semitones = 2x speed -> half delay
        float negWet = delayNeg.Process(in, -12.0f); // -12 semitones = 0.5x speed -> 2x delay

        if (posRepeatSample < 0 && i > 0 && fabsf(posWet) > 0.5f)
            posRepeatSample = i;
        if (negRepeatSample < 0 && i > 0 && fabsf(negWet) > 0.5f)
            negRepeatSample = i;
    }

    // Positive wobble halves the delay (100 -> 50 samples)
    // Negative wobble doubles the delay (100 -> 200 samples)
    CHECK(posRepeatSample == 50);
    CHECK(negRepeatSample == 200);
}
