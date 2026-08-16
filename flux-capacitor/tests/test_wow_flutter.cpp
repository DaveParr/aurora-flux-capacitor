#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../wow_flutter.h"
#include <cmath>

using namespace fluxcap;

TEST_CASE("ComputeWowRateHz - knob endpoints map to the Hz range") {
    CHECK(ComputeWowRateHz(0.0f, 0.0f) == doctest::Approx(kWowRateMinHz));
    CHECK(ComputeWowRateHz(1.0f, 0.0f) == doctest::Approx(kWowRateMaxHz));
}

TEST_CASE("ComputeWowRateHz - combined knob+CV clamps to [0,1] before mapping") {
    CHECK(ComputeWowRateHz(0.9f, 0.5f) == doctest::Approx(kWowRateMaxHz)); // clamped high
    CHECK(ComputeWowRateHz(0.1f, -0.5f) == doctest::Approx(kWowRateMinHz)); // clamped low
}

TEST_CASE("ComputeWowRateHz - result always within [min, max]") {
    for (float knob = 0.0f; knob <= 1.0f; knob += 0.1f)
    {
        float hz = ComputeWowRateHz(knob, 0.0f);
        CHECK(hz >= kWowRateMinHz);
        CHECK(hz <= kWowRateMaxHz);
    }
}

TEST_CASE("ComputeFlutterDepthSemitones - knob endpoints") {
    CHECK(ComputeFlutterDepthSemitones(0.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(ComputeFlutterDepthSemitones(1.0f, 0.0f) == doctest::Approx(kFlutterDepthMaxSemitones));
}

TEST_CASE("ComputeFlutterDepthSemitones - additive knob+CV, clamped") {
    CHECK(ComputeFlutterDepthSemitones(0.5f, 0.2f) == doctest::Approx(0.7f * kFlutterDepthMaxSemitones));
    CHECK(ComputeFlutterDepthSemitones(0.9f, 0.5f) == doctest::Approx(kFlutterDepthMaxSemitones)); // clamped high
    CHECK(ComputeFlutterDepthSemitones(0.1f, -0.5f) == doctest::Approx(0.0f)); // clamped low
}

TEST_CASE("ComputeWowFlutterLedScale - rest (0) is the breathing midpoint, not full brightness") {
    CHECK(ComputeWowFlutterLedScale(0.0f) == doctest::Approx(0.75f));
}

TEST_CASE("ComputeWowFlutterLedScale - positive extreme is full brightness") {
    CHECK(ComputeWowFlutterLedScale(kWowFlutterLedMaxSemitones) == doctest::Approx(1.0f));
}

TEST_CASE("ComputeWowFlutterLedScale - negative extreme is the brightness floor") {
    CHECK(ComputeWowFlutterLedScale(-kWowFlutterLedMaxSemitones) == doctest::Approx(kWowFlutterLedMinBrightnessScale));
}

TEST_CASE("ComputeWowFlutterLedScale - values beyond the max clamp rather than overflow") {
    CHECK(ComputeWowFlutterLedScale(kWowFlutterLedMaxSemitones * 2.0f) == doctest::Approx(1.0f));
    CHECK(ComputeWowFlutterLedScale(-kWowFlutterLedMaxSemitones * 2.0f) == doctest::Approx(kWowFlutterLedMinBrightnessScale));
}

TEST_CASE("ComputeWowFlutterLedScale - wow alone (full swing, no flutter) reaches the LED range's extremes") {
    // kWowFlutterLedMaxSemitones is normalized against kWowDepthSemitones
    // alone (not wow+flutter combined) -- wow's own full swing, which is
    // the common case (flutter sits near its own mean most of the time),
    // must reach the LED range's documented extremes on its own.
    CHECK(ComputeWowFlutterLedScale(kWowDepthSemitones) == doctest::Approx(1.0f));
    CHECK(ComputeWowFlutterLedScale(-kWowDepthSemitones) == doctest::Approx(kWowFlutterLedMinBrightnessScale));
}

TEST_CASE("WowFlutter - flutter depth 0 isolates a pure wow sine at a known phase") {
    WowFlutter wf;
    const float sampleRate = 48000.0f;
    const float rateHz     = 1.0f;
    wf.Init(sampleRate);

    // Oscillator starts at phase_ == 0.0f (see Oscillator::Init), and
    // Process() reads sinf(phase_) *before* advancing phase -- so the
    // first sample is always sinf(0) == 0 regardless of rate.
    float first = wf.Process(rateHz, 0.0f);
    CHECK(first == doctest::Approx(0.0f));

    // Checking the *second* sample (a tiny phase increment) is a
    // vacuous test: at that magnitude (~2e-5), doctest's default
    // Approx tolerance can't tell kWowDepthSemitones (correct,
    // SetAmp(1.0f) behavior) apart from what a regressed default
    // amp_ = 0.5 would produce (~1e-5) -- both pass. Instead, advance
    // to a quarter cycle (sampleRate / (4 * rateHz) = 12000 samples),
    // where sinf(phase) ~= 1.0 and the wow contribution should peak
    // at essentially kWowDepthSemitones. Empirically (verified via a
    // host-side run), the accumulated phase at sample 12000 lands
    // close enough to pi/2 that the result matches kWowDepthSemitones
    // to full float precision (diff == 0.0f) -- so a tight epsilon of
    // 0.0001f is comfortably valid, while still being ~750x tighter
    // than the ~0.075 (50%) gap a missing SetAmp(1.0f) would produce.
    const long quarterCycleSamples = static_cast<long>(sampleRate / (4.0f * rateHz));
    float      atQuarterCycle      = 0.0f;
    for (long i = 1; i < quarterCycleSamples; i++)
    {
        atQuarterCycle = wf.Process(rateHz, 0.0f);
    }
    CHECK(atQuarterCycle == doctest::Approx(kWowDepthSemitones).epsilon(0.0001));
}

TEST_CASE("WowFlutter - Value reflects the last Process result") {
    WowFlutter wf;
    wf.Init(48000.0f);
    float result = wf.Process(1.0f, 0.1f);
    CHECK(wf.Value() == doctest::Approx(result));
}

TEST_CASE("WowFlutter - output stays bounded across many samples") {
    WowFlutter wf;
    wf.Init(48000.0f);
    for (int i = 0; i < 10000; i++)
    {
        float value = wf.Process(1.0f, kFlutterDepthMaxSemitones);
        CHECK(fabsf(value) <= (kWowDepthSemitones + kFlutterDepthMaxSemitones) + 0.01f);
    }
}

TEST_CASE("WowFlutter - flutter's own magnitude reaches a meaningful fraction of its depth, and never exceeds it") {
    // The bounded-output test above has enough slack (kWowDepthSemitones +
    // kFlutterDepthMaxSemitones + 0.01f) that it would still pass even if
    // flutter were entirely broken or missing -- exactly what shipped
    // before the makeup-gain fix. This test isolates flutter's own
    // contribution (by subtracting a parallel, independently-run wow-only
    // reference oscillator from WowFlutter's combined output) so it can
    // actually fail if the makeup gain regresses.
    WowFlutter wf;
    const float sampleRate = 48000.0f;
    const float rateHz     = 1.0f;
    wf.Init(sampleRate);

    daisysp::Oscillator wowRef;
    wowRef.Init(sampleRate);
    wowRef.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    wowRef.SetAmp(1.0f);
    wowRef.SetFreq(rateHz);

    // Empirically (host-side run, same deterministic WhiteNoise seed),
    // 5000 samples at kFlutterDepthMaxSemitones reaches an isolated
    // flutter peak of ~0.189 semitones (~63% of the 0.3 max) -- well
    // above a conservative 50% floor -- and never exceeds 0.3 (the
    // Fix-1 clamp guarantees this structurally, not just empirically).
    float maxAbsFlutter = 0.0f;
    for (int i = 0; i < 5000; i++)
    {
        float combined    = wf.Process(rateHz, kFlutterDepthMaxSemitones);
        float wowOnly      = wowRef.Process() * kWowDepthSemitones;
        float flutterOnly = combined - wowOnly;
        float absFlutter  = fabsf(flutterOnly);
        if (absFlutter > maxAbsFlutter)
            maxAbsFlutter = absFlutter;
        CHECK(absFlutter <= kFlutterDepthMaxSemitones + 1e-4f);
    }
    CHECK(maxAbsFlutter >= 0.5f * kFlutterDepthMaxSemitones);
}
