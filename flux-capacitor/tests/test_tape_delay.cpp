#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../tape_delay.h"
#include "../atmosphere_control.h"
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
    delay.Update(100.0f / 48000.0f, 1.0f, 0.0f); // target 100 samples, atmosphere off

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
    // slower value: at the correct tau and atmosphere=0 (feedback=0.35,
    // drive=1.0), the second repeat's peak is ~11.3% of the first; at a
    // rejected slower tau it would be ~0.57% -- still a >19x margin, so
    // 0.02f cleanly separates the two.
    CHECK(peaks[1] > 0.02f);
}

TEST_CASE("TapeDelay - per-sample delay-position slew is bounded even for a huge target jump") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(kDelayTimeMinSeconds, 1.0f, 0.0f); // tiny target
    delay.Process(0.0f, 0.0f);                // settle near the tiny target
    delay.Update(kDelayTimeMaxSeconds, 1.0f, 0.0f); // huge jump in target (~96000 samples)

    float before = delay.CurrentDelaySamples();
    delay.Process(0.0f, 0.0f);
    float after = delay.CurrentDelaySamples();

    CHECK(fabsf(after - before) <= kMaxDelaySlewSamplesPerSample + 1e-4f);
}

TEST_CASE("TapeDelay - lower speed lengthens the gap between repeats") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 0.5f, 0.0f); // target 200 samples (100 / 0.5)

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
    delay.Update(50.0f / 48000.0f, 1.0f, 0.0f); // target 50 samples

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
    delayPos.Update(100.0f / 48000.0f, 1.0f, 0.0f); // target 100 samples
    delayNeg.Update(100.0f / 48000.0f, 1.0f, 0.0f);

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

    CHECK(posRepeatSample == 50);
    CHECK(negRepeatSample == 200);
}

TEST_CASE("TapeDelay - higher atmosphere sustains more repeats above a noise floor") {
    TapeDelayLine lineLow, lineHigh;
    TapeDelay     delayLow, delayHigh;
    delayLow.Init(&lineLow, 48000.0f);
    delayHigh.Init(&lineHigh, 48000.0f);
    delayLow.Update(100.0f / 48000.0f, 1.0f, 0.0f);
    delayHigh.Update(100.0f / 48000.0f, 1.0f, 1.0f);

    auto countRepeatsAboveFloor = [](TapeDelay &delay) {
        int   repeatsAbove = 0;
        int   repeat       = 0;
        float peak         = 0.0f;
        for (int i = 0; i < 3000 && repeat < 10; i++) {
            float in  = (i == 0) ? 1.0f : 0.0f;
            float wet = delay.Process(in, 0.0f);
            int   expectedCenter = (repeat + 1) * 100;
            if (i >= expectedCenter - 5 && i <= expectedCenter + 15) {
                if (fabsf(wet) > peak)
                    peak = fabsf(wet);
                if (i == expectedCenter + 15) {
                    if (peak > 0.01f)
                        repeatsAbove++;
                    peak = 0.0f;
                    repeat++;
                }
            }
        }
        return repeatsAbove;
    };

    CHECK(countRepeatsAboveFloor(delayHigh) > countRepeatsAboveFloor(delayLow));
}

TEST_CASE("TapeDelay - output stays bounded across many repeats even at maximum atmosphere") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 1.0f, 1.0f); // max atmosphere: highest feedback + drive

    float maxAbs = 0.0f;
    for (int i = 0; i < 5000; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        maxAbs    = daisysp::fmax(maxAbs, fabsf(wet));
    }
    // SoftClip in the feedback loop bounds the sustained tail well
    // under the original impulse -- 1.05 gives headroom over the
    // observed ~1.0 ceiling without loosening the check to meaninglessness.
    CHECK(maxAbs <= 1.05f);
}

TEST_CASE("TapeDelay - atmosphere's feedback-amount and saturation-drive effects are independently verifiable") {
    // At atmosphere=0, feedback amount is at its floor (kAtmosphereFeedbackMin)
    // independent of drive, and drive is at its own floor (kSaturationDriveMin)
    // independent of feedback -- these are two separate knobs on the same
    // input, not one combined effect.
    CHECK(ComputeAtmosphereFeedback(0.0f) == doctest::Approx(kAtmosphereFeedbackMin));
    CHECK(ComputeSaturationDrive(0.0f) == doctest::Approx(kSaturationDriveMin));
    CHECK(ComputeAtmosphereFeedback(1.0f) == doctest::Approx(kAtmosphereFeedbackMax));
    CHECK(ComputeSaturationDrive(1.0f) == doctest::Approx(kSaturationDriveMax));
}
