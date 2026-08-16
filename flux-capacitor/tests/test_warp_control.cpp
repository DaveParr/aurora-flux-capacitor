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

TEST_CASE("ComputeWarpLedLevels - zero semitones -> all LEDs off") {
    WarpLedLevels levels = ComputeWarpLedLevels(0.0f);
    for (int i = 0; i < 3; i++) {
        CHECK(levels.up[i] == doctest::Approx(0.0f));
        CHECK(levels.down[i] == doctest::Approx(0.0f));
    }
}

TEST_CASE("ComputeWarpLedLevels - small upward shift lights only innermost up LED, partially") {
    // 2 semitones is halfway through the first 4-semitone band.
    WarpLedLevels levels = ComputeWarpLedLevels(2.0f);
    CHECK(levels.up[0] == doctest::Approx(0.5f));
    CHECK(levels.up[1] == doctest::Approx(0.0f));
    CHECK(levels.up[2] == doctest::Approx(0.0f));
    CHECK(levels.down[0] == doctest::Approx(0.0f));
    CHECK(levels.down[1] == doctest::Approx(0.0f));
    CHECK(levels.down[2] == doctest::Approx(0.0f));
}

TEST_CASE("ComputeWarpLedLevels - small downward shift lights only innermost down LED, partially") {
    WarpLedLevels levels = ComputeWarpLedLevels(-2.0f);
    CHECK(levels.down[0] == doctest::Approx(0.5f));
    CHECK(levels.down[1] == doctest::Approx(0.0f));
    CHECK(levels.down[2] == doctest::Approx(0.0f));
    CHECK(levels.up[0] == doctest::Approx(0.0f));
    CHECK(levels.up[1] == doctest::Approx(0.0f));
    CHECK(levels.up[2] == doctest::Approx(0.0f));
}

TEST_CASE("ComputeWarpLedLevels - band boundaries") {
    // Exactly 4 semitones: innermost LED fully lit, next band still zero.
    WarpLedLevels at4 = ComputeWarpLedLevels(4.0f);
    CHECK(at4.up[0] == doctest::Approx(1.0f));
    CHECK(at4.up[1] == doctest::Approx(0.0f));

    // Exactly 8 semitones: first two LEDs fully lit, third still zero.
    WarpLedLevels at8 = ComputeWarpLedLevels(8.0f);
    CHECK(at8.up[0] == doctest::Approx(1.0f));
    CHECK(at8.up[1] == doctest::Approx(1.0f));
    CHECK(at8.up[2] == doctest::Approx(0.0f));

    // Exactly 12 semitones: all three fully lit.
    WarpLedLevels at12 = ComputeWarpLedLevels(12.0f);
    CHECK(at12.up[0] == doctest::Approx(1.0f));
    CHECK(at12.up[1] == doctest::Approx(1.0f));
    CHECK(at12.up[2] == doctest::Approx(1.0f));
}

TEST_CASE("ComputeWarpLedLevels - beyond full scale clamps, does not overflow or wrap") {
    WarpLedLevels levels = ComputeWarpLedLevels(60.0f); // max WARP CV range
    CHECK(levels.up[0] == doctest::Approx(1.0f));
    CHECK(levels.up[1] == doctest::Approx(1.0f));
    CHECK(levels.up[2] == doctest::Approx(1.0f));
    CHECK(levels.down[0] == doctest::Approx(0.0f));

    WarpLedLevels negLevels = ComputeWarpLedLevels(-60.0f);
    CHECK(negLevels.down[0] == doctest::Approx(1.0f));
    CHECK(negLevels.down[1] == doctest::Approx(1.0f));
    CHECK(negLevels.down[2] == doctest::Approx(1.0f));
    CHECK(negLevels.up[0] == doctest::Approx(0.0f));
}
