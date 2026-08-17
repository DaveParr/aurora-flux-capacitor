#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../atmosphere_control.h"
#include "../tape_transport.h"
#include <cmath>

using namespace fluxcap;

TEST_CASE("ComputeAtmosphereAmount - knob and CV sum, clamped to [0, 1]") {
    CHECK(ComputeAtmosphereAmount(0.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(ComputeAtmosphereAmount(1.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(ComputeAtmosphereAmount(0.5f, 0.2f) == doctest::Approx(0.7f));
    CHECK(ComputeAtmosphereAmount(0.9f, 0.5f) == doctest::Approx(1.0f)); // clamped high
    CHECK(ComputeAtmosphereAmount(0.1f, -0.5f) == doctest::Approx(0.0f)); // clamped low
}

TEST_CASE("ComputeAtmosphereLpfCoeff - atmosphere=0, speed=1.0 is near-bypass") {
    float coeff = ComputeAtmosphereLpfCoeff(0.0f, 1.0f, 48000.0f);
    CHECK(coeff == doctest::Approx(1.0f / (kAtmosphereLpfTauMinSeconds * 48000.0f)));
}

TEST_CASE("ComputeAtmosphereLpfCoeff - atmosphere=1, speed=1.0 uses the dark tau, not the stop tau") {
    float coeff = ComputeAtmosphereLpfCoeff(1.0f, 1.0f, 48000.0f);
    CHECK(coeff == doctest::Approx(1.0f / (kAtmosphereLpfTauMaxSeconds * 48000.0f)));
}

TEST_CASE("ComputeAtmosphereLpfCoeff - speed near kMinStopSpeed darkens even at atmosphere=0") {
    // kMinStopSpeed (0.0001) is close enough to 0 that speedTau lands
    // within 1% of kStopLpfTauSeconds -- a generous epsilon accounting
    // for that residual, not floating-point slop.
    float coeff = ComputeAtmosphereLpfCoeff(0.0f, 0.0001f, 48000.0f);
    CHECK(coeff == doctest::Approx(1.0f / (kStopLpfTauSeconds * 48000.0f)).epsilon(0.01));
}

TEST_CASE("ComputeAtmosphereLpfCoeff - at max atmosphere and near-zero speed, the darker (stop) tau wins, not the sum") {
    // kStopLpfTauSeconds (0.00015) > kAtmosphereLpfTauMaxSeconds (0.0001),
    // so fmax should pick the stop term even with ATMOSPHERE maxed --
    // matching the atmosphere=0 case above, not their sum or average.
    float atZeroAtmosphere = ComputeAtmosphereLpfCoeff(0.0f, 0.0001f, 48000.0f);
    float atMaxAtmosphere  = ComputeAtmosphereLpfCoeff(1.0f, 0.0001f, 48000.0f);
    CHECK(atMaxAtmosphere == doctest::Approx(atZeroAtmosphere));
}

TEST_CASE("ComputeAtmosphereLpfCoeff - higher atmosphere never brightens (coeff never increases)") {
    float prevCoeff = ComputeAtmosphereLpfCoeff(0.0f, 1.0f, 48000.0f);
    for (float a = 0.1f; a <= 1.0f; a += 0.1f) {
        float coeff = ComputeAtmosphereLpfCoeff(a, 1.0f, 48000.0f);
        CHECK(coeff <= prevCoeff);
        prevCoeff = coeff;
    }
}

TEST_CASE("ComputeAtmosphereLpfCoeff - coefficient never exceeds fonepole's stable range at any supported sample rate") {
    for (float sample_rate : {48000.0f, 96000.0f}) {
        for (float a = 0.0f; a <= 1.0f; a += 0.1f) {
            for (float speed : {0.0f, kMinStopSpeed, 0.5f, 1.0f}) {
                float coeff = ComputeAtmosphereLpfCoeff(a, speed, sample_rate);
                CHECK(coeff <= 1.0f);
                CHECK(coeff > 0.0f);
            }
        }
    }
}

TEST_CASE("ComputeSaturationDrive - endpoints and monotonic in between") {
    CHECK(ComputeSaturationDrive(0.0f) == doctest::Approx(kSaturationDriveMin));
    CHECK(ComputeSaturationDrive(1.0f) == doctest::Approx(kSaturationDriveMax));
    CHECK(ComputeSaturationDrive(0.5f) > ComputeSaturationDrive(0.0f));
    CHECK(ComputeSaturationDrive(0.5f) < ComputeSaturationDrive(1.0f));
}

TEST_CASE("ComputeAtmosphereFeedback - endpoints and monotonic in between") {
    CHECK(ComputeAtmosphereFeedback(0.0f) == doctest::Approx(kAtmosphereFeedbackMin));
    CHECK(ComputeAtmosphereFeedback(1.0f) == doctest::Approx(kAtmosphereFeedbackMax));
    CHECK(ComputeAtmosphereFeedback(0.5f) > ComputeAtmosphereFeedback(0.0f));
    CHECK(ComputeAtmosphereFeedback(0.5f) < ComputeAtmosphereFeedback(1.0f));
}

TEST_CASE("ApplySaturation - zero input is zero output for any drive") {
    CHECK(ApplySaturation(0.0f, kSaturationDriveMin) == doctest::Approx(0.0f));
    CHECK(ApplySaturation(0.0f, kSaturationDriveMax) == doctest::Approx(0.0f));
}

TEST_CASE("ApplySaturation - near-identity for small input at minimum drive") {
    float x = 0.01f;
    CHECK(fabsf(ApplySaturation(x, kSaturationDriveMin) - x) < 0.001f);
}

TEST_CASE("ApplySaturation - bounded to [-1, 1] regardless of drive") {
    CHECK(fabsf(ApplySaturation(10.0f, kSaturationDriveMax)) <= 1.0f);
    CHECK(fabsf(ApplySaturation(-10.0f, kSaturationDriveMax)) <= 1.0f);
}

TEST_CASE("ApplySaturation - odd-symmetric") {
    CHECK(ApplySaturation(-0.5f, 2.0f) == doctest::Approx(-ApplySaturation(0.5f, 2.0f)));
}
