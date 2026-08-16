#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../dsp_util.h"

using namespace fluxcap;

TEST_CASE("OnePoleSmoother - first call moves partway toward target") {
    OnePoleSmoother smoother;
    smoother.Init(0.0f);
    float result = smoother.Process(12.0f, 0.5f);
    // fonepole: out += coeff * (target - out) = 0 + 0.5*(12-0) = 6
    CHECK(result == doctest::Approx(6.0f));
}

TEST_CASE("OnePoleSmoother - converges toward target over repeated calls") {
    OnePoleSmoother smoother;
    smoother.Init(0.0f);
    float result = 0.0f;
    for (int i = 0; i < 30; i++)
        result = smoother.Process(12.0f, 0.5f);
    CHECK(result == doctest::Approx(12.0f).epsilon(0.01));
}

TEST_CASE("OnePoleSmoother - Value reflects last Process result") {
    OnePoleSmoother smoother;
    smoother.Init(2.0f);
    CHECK(smoother.Value() == doctest::Approx(2.0f));
    smoother.Process(2.0f, 0.5f);
    CHECK(smoother.Value() == doctest::Approx(2.0f));
}
