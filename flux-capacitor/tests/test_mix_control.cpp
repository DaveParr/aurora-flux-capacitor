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

TEST_CASE("ComputeEffectiveMix - at full speed (PLAY), effective mix is exactly the user's mix") {
    CHECK(ComputeEffectiveMix(0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(ComputeEffectiveMix(0.3f, 1.0f) == doctest::Approx(0.3f));
    CHECK(ComputeEffectiveMix(1.0f, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("ComputeEffectiveMix - MIX fully left sweeps to fully wet as speed drops to 0") {
    CHECK(ComputeEffectiveMix(0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(ComputeEffectiveMix(0.0f, 0.9f) == doctest::Approx(1.0f - powf(0.9f, kAutoWetSteepness)));
    CHECK(ComputeEffectiveMix(0.0f, 0.5f) == doctest::Approx(1.0f - powf(0.5f, kAutoWetSteepness)));
    CHECK(ComputeEffectiveMix(0.0f, 0.0f) == doctest::Approx(1.0f)); // fully stopped -> fully wet
}

TEST_CASE("ComputeEffectiveMix - any MIX setting above fully-left is completely untouched by speed") {
    // Auto-wet exists only to rescue MIX == 0 (otherwise FREEZE would be
    // silent). Any other setting -- including settings just barely above
    // zero -- must reproduce exactly the pre-auto-wet behavior: the user's
    // own mix, at every speed from full play down to fully stopped.
    for (float userMix : {0.05f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f}) {
        for (float speed : {1.0f, 0.9f, 0.5f, 0.1f, 0.0f}) {
            CHECK(ComputeEffectiveMix(userMix, speed) == doctest::Approx(userMix));
        }
    }
}

TEST_CASE("ComputeEffectiveMix - threshold boundary: just below triggers auto-wet, at/above passes through") {
    float justBelow = kMixFullyDryThreshold * 0.5f;
    float atThreshold = kMixFullyDryThreshold;
    CHECK(ComputeEffectiveMix(justBelow, 0.5f) == doctest::Approx(1.0f - powf(0.5f, kAutoWetSteepness)));
    CHECK(ComputeEffectiveMix(atThreshold, 0.5f) == doctest::Approx(atThreshold));
}

TEST_CASE("ComputeEffectiveMix - fully wet user setting is unaffected by speed") {
    CHECK(ComputeEffectiveMix(1.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(ComputeEffectiveMix(1.0f, 0.5f) == doctest::Approx(1.0f));
}

TEST_CASE("MixDryZoneCrossed - no crossing when both readings are on the same side") {
    CHECK(MixDryZoneCrossed(0.0f, 0.0f) == false);   // both dry
    CHECK(MixDryZoneCrossed(0.01f, 0.02f) == false); // both dry, moved but stayed under threshold
    CHECK(MixDryZoneCrossed(0.5f, 0.5f) == false);   // both above threshold
    CHECK(MixDryZoneCrossed(0.3f, 0.9f) == false);   // both above threshold, moved but stayed over
}

TEST_CASE("MixDryZoneCrossed - true when moving from above threshold to dry (knob goes below)") {
    CHECK(MixDryZoneCrossed(0.0f, 0.5f) == true);
    CHECK(MixDryZoneCrossed(0.01f, 0.05f) == true);
}

TEST_CASE("MixDryZoneCrossed - true when moving from dry to above threshold (knob goes above)") {
    CHECK(MixDryZoneCrossed(0.5f, 0.0f) == true);
    CHECK(MixDryZoneCrossed(0.05f, 0.01f) == true);
}

TEST_CASE("MixDryZoneCrossed - boundary is consistent with ComputeEffectiveMix's own threshold check") {
    // exactly at the threshold counts as "above" (ComputeEffectiveMix uses >=),
    // so moving from just-below to exactly-at-threshold is a crossing.
    CHECK(MixDryZoneCrossed(kMixFullyDryThreshold, kMixFullyDryThreshold * 0.5f) == true);
    CHECK(MixDryZoneCrossed(kMixFullyDryThreshold, kMixFullyDryThreshold) == false);
}

TEST_CASE("ComputeEffectiveMix - regression: with MIX at 0, the audible wet contribution "
          "actually exceeds dry for most of an active stop, not just at the extremes") {
    // This is the bug a linear (1 - speed) floor had: StopAmplitude also
    // scales the wet signal by `speed`, so wetGain*wetAmp never once beat
    // dryGain across the whole transition even though wetGain itself rose
    // toward 1. Assert the actual audible product wins well before the tail.
    for (float speed : {0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f}) {
        float effectiveMix = ComputeEffectiveMix(0.0f, speed);
        float dryGain, wetGain;
        ComputeMixGains(effectiveMix, &dryGain, &wetGain);
        float wetAmp = speed; // mirrors StopAmplitude(speed) from tape_transport.h
        float wetContribution = wetGain * wetAmp;
        CHECK(wetContribution > dryGain);
    }
}
