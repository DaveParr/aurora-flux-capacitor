#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../tape_transport.h"
#include <cmath>

using namespace fluxcap;

TEST_CASE("StopSemitones - full speed is zero shift") {
    CHECK(StopSemitones(1.0f) == doctest::Approx(0.0f));
}

TEST_CASE("StopSemitones - half speed is one octave down") {
    CHECK(StopSemitones(0.5f) == doctest::Approx(-12.0f));
}

TEST_CASE("StopSemitones - near-zero speed stays finite") {
    CHECK(std::isfinite(StopSemitones(0.0f)));
    CHECK(StopSemitones(0.0f) < -100.0f); // deep, but finite
}

TEST_CASE("StopAmplitude - linear, matches speed exactly") {
    CHECK(StopAmplitude(1.0f) == doctest::Approx(1.0f));
    CHECK(StopAmplitude(0.5f) == doctest::Approx(0.5f));
    CHECK(StopAmplitude(0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("TapeTransport - starts at full speed (PLAY)") {
    TapeTransport transport;
    transport.Init();
    CHECK(transport.Speed() == doctest::Approx(1.0f));
}

TEST_CASE("TapeTransport - freeze edge ramps speed down and snaps to exactly zero") {
    TapeTransport transport;
    transport.Init();
    transport.Update(/*freeze_edge=*/true, /*gate_high=*/false, 0.1f);
    CHECK(transport.Speed() < 1.0f); // moved off full speed immediately

    for (int i = 0; i < 200; i++)
        transport.Update(false, false, 0.1f);

    CHECK(transport.Speed() == 0.0f); // exact snap, not just close
}

TEST_CASE("TapeTransport - second freeze edge mid-ramp reverses back to exactly one") {
    TapeTransport transport;
    transport.Init();
    transport.Update(true, false, 0.1f);
    for (int i = 0; i < 5; i++)
        transport.Update(false, false, 0.1f);
    CHECK(transport.Speed() > 0.0f);
    CHECK(transport.Speed() < 1.0f);

    transport.Update(true, false, 0.1f); // catch it mid-ramp, reverse

    for (int i = 0; i < 200; i++)
        transport.Update(false, false, 0.1f);

    CHECK(transport.Speed() == 1.0f);
}

TEST_CASE("TapeTransport - gate high forces stop regardless of button target") {
    TapeTransport transport;
    transport.Init(); // button target stays PLAY, never toggled

    for (int i = 0; i < 200; i++)
        transport.Update(false, /*gate_high=*/true, 0.1f);

    CHECK(transport.Speed() == 0.0f);
}

TEST_CASE("TapeTransport - releasing gate hands control back to button's last toggle") {
    TapeTransport transport;
    transport.Init(); // button target == PLAY

    for (int i = 0; i < 200; i++)
        transport.Update(false, true, 0.1f); // gate forces stop
    CHECK(transport.Speed() == 0.0f);

    for (int i = 0; i < 200; i++)
        transport.Update(false, false, 0.1f); // gate released, button target is PLAY
    CHECK(transport.Speed() == 1.0f);
}
