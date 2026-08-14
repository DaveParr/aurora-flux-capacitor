#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../audio.h"

static const float kEps = 1e-6f;

TEST_CASE("scaleVolume - unity gain") {
    StereoFrame out = scaleVolume({0.5f, -0.3f}, 1.0f);
    CHECK(out.left  == doctest::Approx(0.5f).epsilon(kEps));
    CHECK(out.right == doctest::Approx(-0.3f).epsilon(kEps));
}

TEST_CASE("scaleVolume - silence at zero volume") {
    StereoFrame out = scaleVolume({0.5f, -0.3f}, 0.0f);
    CHECK(out.left  == doctest::Approx(0.f).epsilon(kEps));
    CHECK(out.right == doctest::Approx(0.f).epsilon(kEps));
}

TEST_CASE("scaleVolume - half volume") {
    StereoFrame out = scaleVolume({0.8f, 0.4f}, 0.5f);
    CHECK(out.left  == doctest::Approx(0.4f).epsilon(kEps));
    CHECK(out.right == doctest::Approx(0.2f).epsilon(kEps));
}
