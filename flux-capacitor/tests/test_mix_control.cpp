#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../mix_control.h"

using namespace fluxcap;

TEST_CASE("ComputeMix - knob and CV sum, clamped to [0, 1]") {
    CHECK(ComputeMix(0.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(ComputeMix(1.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(ComputeMix(0.5f, 0.2f) == doctest::Approx(0.7f));
    CHECK(ComputeMix(0.9f, 0.5f) == doctest::Approx(1.0f)); // clamped high
    CHECK(ComputeMix(0.1f, -0.5f) == doctest::Approx(0.0f)); // clamped low
}

TEST_CASE("ComputeMixGains - mix 0 is fully dry") {
    float dryGain, wetGain;
    ComputeMixGains(0.0f, &dryGain, &wetGain);
    CHECK(dryGain == doctest::Approx(1.0f));
    CHECK(wetGain == doctest::Approx(0.0f));
}

TEST_CASE("ComputeMixGains - mix 1 is fully wet") {
    float dryGain, wetGain;
    ComputeMixGains(1.0f, &dryGain, &wetGain);
    CHECK(dryGain == doctest::Approx(0.0f));
    CHECK(wetGain == doctest::Approx(1.0f));
}

TEST_CASE("ComputeMixGains - mix 0.5 is equal-power, not linear 0.5/0.5") {
    float dryGain, wetGain;
    ComputeMixGains(0.5f, &dryGain, &wetGain);
    CHECK(dryGain == doctest::Approx(0.70710678f));
    CHECK(wetGain == doctest::Approx(0.70710678f));
    CHECK(dryGain != doctest::Approx(0.5f));
}
