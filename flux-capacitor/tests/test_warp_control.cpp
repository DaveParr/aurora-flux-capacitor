#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../warp_control.h"

using namespace fluxcap;

TEST_CASE("ComputeWarpSemitones - knob centered, zero CV -> zero semitones") {
    CHECK(ComputeWarpSemitones(0.5f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("ComputeWarpSemitones - knob extremes map to +-12 semitones") {
    CHECK(ComputeWarpSemitones(0.0f, 0.0f) == doctest::Approx(-12.0f));
    CHECK(ComputeWarpSemitones(1.0f, 0.0f) == doctest::Approx(12.0f));
}

TEST_CASE("ComputeWarpSemitones - knob and CV sum") {
    CHECK(ComputeWarpSemitones(0.5f, 7.0f) == doctest::Approx(7.0f));
    CHECK(ComputeWarpSemitones(1.0f, 5.0f) == doctest::Approx(17.0f));
    CHECK(ComputeWarpSemitones(0.0f, 3.0f) == doctest::Approx(-9.0f));
}

TEST_CASE("ComputeWarpSemitones - near-center knob/CV noise snaps to exactly zero") {
    // Small combined values inside the deadzone must snap to bit-exact 0.0f.
    CHECK(ComputeWarpSemitones(0.5f, 0.0f) == 0.0f);
    // A slightly off-center knob: knobSemis = fmap(0.501, -12, 12) is tiny
    // but nonzero; combined with a small CV offset it should still land
    // inside the +-0.05 semitone deadzone and snap to zero.
    CHECK(ComputeWarpSemitones(0.501f, 0.0f) == 0.0f);
    CHECK(ComputeWarpSemitones(0.5f, 0.02f) == 0.0f);
    CHECK(ComputeWarpSemitones(0.4999f, -0.01f) == 0.0f);
}

TEST_CASE("ComputeWarpSemitones - values outside the deadzone are unaffected") {
    CHECK(ComputeWarpSemitones(0.5f, 0.1f) == doctest::Approx(0.1f));
    CHECK(ComputeWarpSemitones(0.5f, -0.06f) == doctest::Approx(-0.06f));
    CHECK(ComputeWarpSemitones(0.5f, 7.0f) == doctest::Approx(7.0f));
}

TEST_CASE("WarpSmoother - first call moves partway toward target") {
    WarpSmoother smoother;
    smoother.Init(0.0f);
    float result = smoother.Process(12.0f, 0.5f);
    // fonepole: out += coeff * (target - out) = 0 + 0.5*(12-0) = 6
    CHECK(result == doctest::Approx(6.0f));
}

TEST_CASE("WarpSmoother - converges toward target over repeated calls") {
    WarpSmoother smoother;
    smoother.Init(0.0f);
    float result = 0.0f;
    for (int i = 0; i < 30; i++)
        result = smoother.Process(12.0f, 0.5f);
    CHECK(result == doctest::Approx(12.0f).epsilon(0.01));
}

TEST_CASE("WarpSmoother - Value reflects last Process result") {
    WarpSmoother smoother;
    smoother.Init(2.0f);
    CHECK(smoother.Value() == doctest::Approx(2.0f));
    smoother.Process(2.0f, 0.5f);
    CHECK(smoother.Value() == doctest::Approx(2.0f));
}
