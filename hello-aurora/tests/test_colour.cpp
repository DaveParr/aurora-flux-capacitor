#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../colour.h"

static const float kEps = 1e-4f;

TEST_CASE("hsvToRgb - red at hue 0") {
    Rgb c = hsvToRgb(0.f, 1.f, 1.f);
    CHECK(c.r == doctest::Approx(1.f).epsilon(kEps));
    CHECK(c.g == doctest::Approx(0.f).epsilon(kEps));
    CHECK(c.b == doctest::Approx(0.f).epsilon(kEps));
}

TEST_CASE("hsvToRgb - yellow at hue 1/6") {
    Rgb c = hsvToRgb(1.f / 6.f, 1.f, 1.f);
    CHECK(c.r == doctest::Approx(1.f).epsilon(kEps));
    CHECK(c.g == doctest::Approx(1.f).epsilon(kEps));
    CHECK(c.b == doctest::Approx(0.f).epsilon(kEps));
}

TEST_CASE("hsvToRgb - green at hue 1/3") {
    Rgb c = hsvToRgb(1.f / 3.f, 1.f, 1.f);
    CHECK(c.r == doctest::Approx(0.f).epsilon(kEps));
    CHECK(c.g == doctest::Approx(1.f).epsilon(kEps));
    CHECK(c.b == doctest::Approx(0.f).epsilon(kEps));
}

TEST_CASE("hsvToRgb - cyan at hue 1/2") {
    Rgb c = hsvToRgb(0.5f, 1.f, 1.f);
    CHECK(c.r == doctest::Approx(0.f).epsilon(kEps));
    CHECK(c.g == doctest::Approx(1.f).epsilon(kEps));
    CHECK(c.b == doctest::Approx(1.f).epsilon(kEps));
}

TEST_CASE("hsvToRgb - blue at hue 2/3") {
    Rgb c = hsvToRgb(2.f / 3.f, 1.f, 1.f);
    CHECK(c.r == doctest::Approx(0.f).epsilon(kEps));
    CHECK(c.g == doctest::Approx(0.f).epsilon(kEps));
    CHECK(c.b == doctest::Approx(1.f).epsilon(kEps));
}

TEST_CASE("hsvToRgb - magenta at hue 5/6") {
    Rgb c = hsvToRgb(5.f / 6.f, 1.f, 1.f);
    CHECK(c.r == doctest::Approx(1.f).epsilon(kEps));
    CHECK(c.g == doctest::Approx(0.f).epsilon(kEps));
    CHECK(c.b == doctest::Approx(1.f).epsilon(kEps));
}

TEST_CASE("hsvToRgb - hue 1.0 wraps to red") {
    Rgb c = hsvToRgb(1.f, 1.f, 1.f);
    CHECK(c.r == doctest::Approx(1.f).epsilon(kEps));
    CHECK(c.g == doctest::Approx(0.f).epsilon(kEps));
    CHECK(c.b == doctest::Approx(0.f).epsilon(kEps));
}
