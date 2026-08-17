# Flux Capacitor — Phase 5a: ATMOSPHERE Coloration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire up `KNOB_ATMOSPHERE`/`CV_ATMOSPHERE` for the first time, adding tone-shaping lowpass and saturation to the main signal path and `TapeDelay`'s feedback loop, plus a variable feedback-amount control for `TapeDelay` (replacing the fixed `kTapeDelayFeedback` constant from Phase 4).

**Architecture:** One new host-testable header (`atmosphere_control.h`), following the existing `mix_control.h`/`warp_control.h` pattern of pure, stateless control-mapping functions — no owned state, since the audio-rate filter state it feeds reuses the existing `OnePoleSmoother` class (already doing double duty as both a control-rate smoother and an audio-rate filter elsewhere in this codebase). `TapeDelay::Update` gains a third `atmosphere` parameter; its feedback loop gains a saturation step ahead of the existing feedback LPF. `main.cpp` reads the new knob/CV, computes one combined "atmosphere" scalar, and applies saturate-then-lowpass to the wet signal between `TapeVoice` and `TapeDelay`.

**Tech Stack:** C++14, DaisySP (`Utility/dsp.h`: `fclamp`, `fmax`, `fmap`, `fonepole`, `SoftClip`), doctest for host-side tests, Aurora SDK (`aurora.h`) for `KNOB_ATMOSPHERE`/`CV_ATMOSPHERE`.

**Spec:** [`docs/superpowers/specs/2026-08-17-flux-capacitor-phase5-atmosphere-design.md`](../specs/2026-08-17-flux-capacitor-phase5-atmosphere-design.md)

## Global Constraints

- `namespace fluxcap` for the new header, matching every existing one.
- `#pragma once` + `#include "Utility/dsp.h"` only — `atmosphere_control.h` must stay host-testable like `mix_control.h`/`warp_control.h` (no libDaisy includes).
- `kAtmosphereMin = 0.0f`, `kAtmosphereMax = 1.0f` — `ComputeAtmosphereAmount` clamps knob+CV to this range, same additive pattern as WARP/MIX/REFLECT/BLUR/TIME.
- `kAtmosphereLpfTauMinSeconds = 0.000005f` (near-bypass, ~32 kHz), `kAtmosphereLpfTauMaxSeconds = 0.0001f` (dark, ~1.6 kHz), `kStopLpfTauSeconds = 0.00015f` (~1.1 kHz stop-darkening floor) — all provisional, explicitly deferred to a later tuning pass per the spec.
- `kSaturationDriveMin = 1.0f`, `kSaturationDriveMax = 4.0f`.
- `kAtmosphereFeedbackMin = 0.15f`, `kAtmosphereFeedbackMax = 0.55f` — replaces `tape_delay.h`'s fixed `kTapeDelayFeedback = 0.35f` (removed in Task 2).
- Saturate-then-filter ordering everywhere coloration is applied (main path and `TapeDelay`'s feedback loop) — locked in during design review.
- `TapeDelay::Update`'s new third parameter is `atmosphere` (the same smoothed 0..1 scalar driving the main-path tone/saturation), consumed once per block into `drive_`/`feedback_amount_` members, mirroring how `target_samples_` is already set in `Update` and consumed per-sample in `Process`.
- Test files follow the existing pattern exactly: `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include "doctest.h"` + `#include "../<header>.h"`, `using namespace fluxcap;`, `TEST_CASE`/`CHECK`/`doctest::Approx`.
- No LED changes, no REVERSE, no Shift+knob pages, no tuning-by-ear pass, no ping-pong feedback — all out of scope per the spec.

---

### Task 1: `atmosphere_control.h` — pure control-mapping functions

**Files:**
- Create: `flux-capacitor/atmosphere_control.h`
- Create: `flux-capacitor/tests/test_atmosphere_control.cpp`
- Modify: `flux-capacitor/tests/Makefile`

**Interfaces:**
- Consumes: `daisysp::fclamp`, `daisysp::fmax`, `daisysp::fmap` (default `Mapping::LINEAR`), `daisysp::SoftClip` (all `Utility/dsp.h`, already used elsewhere in this codebase).
- Produces: `fluxcap::ComputeAtmosphereAmount(float atmosphere_knob, float atmosphere_cv) -> float`; `fluxcap::ComputeAtmosphereLpfCoeff(float atmosphere, float speed, float sample_rate) -> float`; `fluxcap::ComputeSaturationDrive(float atmosphere) -> float`; `fluxcap::ApplySaturation(float in, float drive) -> float`; `fluxcap::ComputeAtmosphereFeedback(float atmosphere) -> float`. Task 2 (`tape_delay.h`) and Task 3 (`main.cpp`) call all five.

- [ ] **Step 1: Write the failing test**

Create `flux-capacitor/tests/test_atmosphere_control.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../atmosphere_control.h"
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
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd flux-capacitor/tests && make test_atmosphere_control
```

Expected: FAIL — compile error, `../atmosphere_control.h: No such file or directory`.

- [ ] **Step 3: Create `atmosphere_control.h`**

Create `flux-capacitor/atmosphere_control.h`:

```cpp
#pragma once
#include "Utility/dsp.h"

namespace fluxcap
{
constexpr float kAtmosphereMin = 0.0f;
constexpr float kAtmosphereMax = 1.0f;

/** Maps KNOB_ATMOSPHERE (0..1) + CV_ATMOSPHERE (additive, same pattern
 *  as WARP/MIX/REFLECT/BLUR/TIME) to a single 0..1 coloration amount
 *  that drives tone, saturation, and TapeDelay's feedback amount
 *  together as one "tape coloration" macro -- there's only one knob/CV
 *  pair for three effects, so they move together rather than fighting
 *  for one turn of travel.
 */
inline float ComputeAtmosphereAmount(float atmosphere_knob, float atmosphere_cv)
{
    return daisysp::fclamp(atmosphere_knob + atmosphere_cv, kAtmosphereMin, kAtmosphereMax);
}

// Tau range for the ATMOSPHERE-driven tone lowpass. Provisional --
// a later tuning pass is expected to revisit these by ear once the
// full coloration chain exists to audition against.
constexpr float kAtmosphereLpfTauMinSeconds = 0.000005f; // near-bypass (~32 kHz), atmosphere = 0
constexpr float kAtmosphereLpfTauMaxSeconds = 0.0001f;   // dark (~1.6 kHz), atmosphere = 1

// Tau floor applied as speed -> 0, independent of the ATMOSPHERE knob --
// "tape loses HF as it slows" (parent doc's Tape Speed Model), so a
// full brake always darkens even with ATMOSPHERE fully counterclockwise.
constexpr float kStopLpfTauSeconds = 0.00015f; // ~1.1 kHz

/** Tone-lowpass coefficient for an audio-rate OnePoleSmoother, folding
 *  in both the ATMOSPHERE-dialed darkening and speed-based tape-stop
 *  darkening as a single fmax of two candidate time constants (larger
 *  tau = darker) -- whichever is more extreme wins, so callers need
 *  only one filter instance, not two.
 */
inline float ComputeAtmosphereLpfCoeff(float atmosphere, float speed, float sample_rate)
{
    float atmosphereTau = daisysp::fmap(atmosphere, kAtmosphereLpfTauMinSeconds,
                                         kAtmosphereLpfTauMaxSeconds);
    float speedTau = daisysp::fmap(1.0f - daisysp::fclamp(speed, 0.0f, 1.0f),
                                    kAtmosphereLpfTauMinSeconds, kStopLpfTauSeconds);
    float tau = daisysp::fmax(atmosphereTau, speedTau);
    return 1.0f / (tau * sample_rate);
}

constexpr float kSaturationDriveMin = 1.0f; // atmosphere = 0: SoftClip(x) is ~identity for |x| << 1
constexpr float kSaturationDriveMax = 4.0f; // atmosphere = 1: audible soft-knee saturation

/** Pre-gain applied before daisysp::SoftClip, scaling with ATMOSPHERE
 *  amount so the same knob drives saturation drive as well as tone.
 */
inline float ComputeSaturationDrive(float atmosphere)
{
    return daisysp::fmap(atmosphere, kSaturationDriveMin, kSaturationDriveMax);
}

/** Thin wrapper around daisysp::SoftClip -- ApplySaturation(0, drive)
 *  == 0 for any drive, so saturation fades to silence exactly when its
 *  input does (no separate stop-envelope handling needed for it).
 */
inline float ApplySaturation(float in, float drive)
{
    return daisysp::SoftClip(in * drive);
}

// Replaces tape_delay.h's fixed kTapeDelayFeedback = 0.35f (Phase 4).
constexpr float kAtmosphereFeedbackMin = 0.15f;
constexpr float kAtmosphereFeedbackMax = 0.55f; // stays comfortably under 1.0; SoftClip in the loop bounds it further

/** Maps ATMOSPHERE amount to TapeDelay's feedback amount. */
inline float ComputeAtmosphereFeedback(float atmosphere)
{
    return daisysp::fmap(atmosphere, kAtmosphereFeedbackMin, kAtmosphereFeedbackMax);
}
} // namespace fluxcap
```

- [ ] **Step 4: Run it to verify it passes**

```bash
cd flux-capacitor/tests && make test_atmosphere_control && ./test_atmosphere_control
```

Expected: PASS — all 12 test cases pass, 0 failures.

- [ ] **Step 5: Add the new binary to `flux-capacitor/tests/Makefile`**

Add `test_atmosphere_control` to the `all` target's prerequisite list and run line, its build rule, and the `clean` rule (no extra DaisySP source needed — `atmosphere_control.h` only touches header-only `Utility/dsp.h` functions, same as `mix_control.h`/`warp_control.h`):

```makefile
all: $(DOCTEST_H) test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter test_tape_delay test_atmosphere_control
	./test_warp_control
	./test_tape_voice
	./test_dsp_util
	./test_mix_control
	./test_tape_transport
	./test_wow_flutter
	./test_tape_delay
	./test_atmosphere_control
```

```makefile
test_atmosphere_control: test_atmosphere_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<
```

```makefile
clean:
	rm -f test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter test_tape_delay test_atmosphere_control $(DOCTEST_H)
```

- [ ] **Step 6: Run the full host test suite**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all eight test binaries build and pass.

- [ ] **Step 7: Commit**

```bash
git add flux-capacitor/atmosphere_control.h flux-capacitor/tests/test_atmosphere_control.cpp \
        flux-capacitor/tests/Makefile
git commit -m "flux-capacitor: add ATMOSPHERE control mapping (tone, saturation, feedback amount)"
```

---

### Task 2: Extend `TapeDelay` — variable feedback amount + feedback-loop saturation

**Files:**
- Modify: `flux-capacitor/tape_delay.h`
- Modify: `flux-capacitor/tests/test_tape_delay.cpp`

**Interfaces:**
- Consumes: `fluxcap::ComputeSaturationDrive`, `fluxcap::ApplySaturation`, `fluxcap::ComputeAtmosphereFeedback` (Task 1, `atmosphere_control.h`).
- Produces: `fluxcap::TapeDelay::Update(float base_seconds, float speed, float atmosphere)` — signature change from Phase 4's two-parameter version. `TapeDelay::Process`'s public signature (`float Process(float in, float wobble_semitones)`) and return value (`in + wet`) are unchanged. Task 3 (`main.cpp`) calls the new three-parameter `Update`.

- [ ] **Step 1: Write the failing/updated tests**

In `flux-capacitor/tests/test_tape_delay.cpp`, add the include and update every existing `delay.Update(...)` call site to pass a third `atmosphere` argument. None of the existing timing-only tests (position/arrival-sample checks) depend on `atmosphere`'s value — `TapeDelay::Process`'s output tap (`in + wet`) stays the raw, unsaturated read regardless of `atmosphere`; only what gets written back into the loop changes. Pass `0.0f` in all of them except the impulse-decay test, which now needs its magic-number threshold updated (see below) since feedback-loop saturation changes the decay-per-repeat math even at `atmosphere=0`.

Add the include:

```cpp
#include "../atmosphere_control.h"
```

Update the impulse-decay test's `Update` call and its second-repeat threshold:

```cpp
TEST_CASE("TapeDelay - impulse produces decaying, bounded repeats at the expected spacing") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 1.0f, 0.0f); // target 100 samples, atmosphere off

    float peaks[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int   repeat   = 0;
    float maxAbs   = 0.0f;

    for (int i = 0; i < 800 && repeat < 4; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        maxAbs    = daisysp::fmax(maxAbs, fabsf(wet));

        int expectedCenter = (repeat + 1) * 100;
        if (i >= expectedCenter - 5 && i <= expectedCenter + 15) {
            if (fabsf(wet) > peaks[repeat])
                peaks[repeat] = fabsf(wet);
            if (i == expectedCenter + 15)
                repeat++;
        }
    }

    CHECK(peaks[0] == doctest::Approx(1.0f));  // first repeat: unscaled impulse
    CHECK(peaks[1] < peaks[0]);                  // each repeat strictly quieter than the last
    CHECK(peaks[2] < peaks[1]);
    CHECK(peaks[3] < peaks[2]);
    CHECK(maxAbs == doctest::Approx(1.0f));      // never exceeds the original impulse

    // Pins kTapeDelayFeedbackLpfTauSeconds against regressing to a
    // slower value: at the correct tau and atmosphere=0 (feedback=0.15,
    // drive=1.0), the second repeat's peak is ~4.9% of the first; at a
    // rejected slower tau it would be ~0.24% -- still a >20x margin, so
    // 0.02f cleanly separates the two.
    CHECK(peaks[1] > 0.02f);
}
```

Update the three timing-only tests' `Update` calls (no assertion changes — see rationale above):

```cpp
TEST_CASE("TapeDelay - per-sample delay-position slew is bounded even for a huge target jump") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(kDelayTimeMinSeconds, 1.0f, 0.0f); // tiny target
    delay.Process(0.0f, 0.0f);                // settle near the tiny target
    delay.Update(kDelayTimeMaxSeconds, 1.0f, 0.0f); // huge jump in target (~96000 samples)

    float before = delay.CurrentDelaySamples();
    delay.Process(0.0f, 0.0f);
    float after = delay.CurrentDelaySamples();

    CHECK(fabsf(after - before) <= kMaxDelaySlewSamplesPerSample + 1e-4f);
}

TEST_CASE("TapeDelay - lower speed lengthens the gap between repeats") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 0.5f, 0.0f); // target 200 samples (100 / 0.5)

    int firstRepeatSample = -1;
    for (int i = 0; i < 400; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        if (i > 0 && fabsf(wet) > 0.5f) {
            firstRepeatSample = i;
            break;
        }
    }
    CHECK(firstRepeatSample == 200);
}

TEST_CASE("TapeDelay - zero wobble and full speed reproduce ComputeDelaySamples exactly") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(50.0f / 48000.0f, 1.0f, 0.0f); // target 50 samples

    int firstRepeatSample = -1;
    for (int i = 0; i < 200; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        if (i > 0 && fabsf(wet) > 0.5f) {
            firstRepeatSample = i;
            break;
        }
    }
    CHECK(firstRepeatSample == 50);
}

TEST_CASE("TapeDelay - wobble affects delay timing as expected") {
    TapeDelayLine linePos;
    TapeDelayLine lineNeg;
    TapeDelay     delayPos;
    TapeDelay     delayNeg;
    delayPos.Init(&linePos, 48000.0f);
    delayNeg.Init(&lineNeg, 48000.0f);
    delayPos.Update(100.0f / 48000.0f, 1.0f, 0.0f); // target 100 samples
    delayNeg.Update(100.0f / 48000.0f, 1.0f, 0.0f);

    int posRepeatSample = -1;
    int negRepeatSample = -1;

    for (int i = 0; i < 400; i++) {
        float in = (i == 0) ? 1.0f : 0.0f;
        float posWet = delayPos.Process(in, 12.0f); // +12 semitones = 2x speed -> half delay
        float negWet = delayNeg.Process(in, -12.0f); // -12 semitones = 0.5x speed -> 2x delay

        if (posRepeatSample < 0 && i > 0 && fabsf(posWet) > 0.5f)
            posRepeatSample = i;
        if (negRepeatSample < 0 && i > 0 && fabsf(negWet) > 0.5f)
            negRepeatSample = i;
    }

    CHECK(posRepeatSample == 50);
    CHECK(negRepeatSample == 200);
}
```

Add two new tests covering the `atmosphere` parameter's effect:

```cpp
TEST_CASE("TapeDelay - higher atmosphere sustains more repeats above a noise floor") {
    TapeDelayLine lineLow, lineHigh;
    TapeDelay     delayLow, delayHigh;
    delayLow.Init(&lineLow, 48000.0f);
    delayHigh.Init(&lineHigh, 48000.0f);
    delayLow.Update(100.0f / 48000.0f, 1.0f, 0.0f);
    delayHigh.Update(100.0f / 48000.0f, 1.0f, 1.0f);

    auto countRepeatsAboveFloor = [](TapeDelay &delay) {
        int   repeatsAbove = 0;
        int   repeat       = 0;
        float peak         = 0.0f;
        for (int i = 0; i < 3000 && repeat < 10; i++) {
            float in  = (i == 0) ? 1.0f : 0.0f;
            float wet = delay.Process(in, 0.0f);
            int   expectedCenter = (repeat + 1) * 100;
            if (i >= expectedCenter - 5 && i <= expectedCenter + 15) {
                if (fabsf(wet) > peak)
                    peak = fabsf(wet);
                if (i == expectedCenter + 15) {
                    if (peak > 0.01f)
                        repeatsAbove++;
                    peak = 0.0f;
                    repeat++;
                }
            }
        }
        return repeatsAbove;
    };

    CHECK(countRepeatsAboveFloor(delayHigh) > countRepeatsAboveFloor(delayLow));
}

TEST_CASE("TapeDelay - output stays bounded across many repeats even at maximum atmosphere") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 1.0f, 1.0f); // max atmosphere: highest feedback + drive

    float maxAbs = 0.0f;
    for (int i = 0; i < 5000; i++) {
        float in  = (i == 0) ? 1.0f : 0.0f;
        float wet = delay.Process(in, 0.0f);
        maxAbs    = daisysp::fmax(maxAbs, fabsf(wet));
    }
    // SoftClip in the feedback loop bounds the sustained tail well
    // under the original impulse -- 1.05 gives headroom over the
    // observed ~1.0 ceiling without loosening the check to meaninglessness.
    CHECK(maxAbs <= 1.05f);
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd flux-capacitor/tests && make test_tape_delay
```

Expected: FAIL — compile error, `no matching function for call to 'fluxcap::TapeDelay::Update(float, float)'` (too few arguments) or similar, since `tape_delay.h` still has the two-parameter `Update`.

- [ ] **Step 3: Update `tape_delay.h`**

Add the include, alongside the existing ones:

```cpp
#include "atmosphere_control.h"
```

Remove the fixed feedback constant (now computed from `atmosphere` instead):

```cpp
constexpr float kTapeDelayFeedback = 0.35f; // fixed; no control surface yet
```

Add two members to `TapeDelay`'s private section, alongside the existing ones:

```cpp
float drive_           = 0.0f;
float feedback_amount_ = 0.0f;
```

Change `Update`'s signature and body:

```cpp
    // base_seconds/speed: as before. atmosphere: smoothed ATMOSPHERE
    // amount, same value driving the main-path tone/saturation this
    // block. Called once per audio block.
    void Update(float base_seconds, float speed, float atmosphere)
    {
        target_samples_  = ComputeDelaySamples(base_seconds, speed, sr_);
        drive_           = ComputeSaturationDrive(atmosphere);
        feedback_amount_ = ComputeAtmosphereFeedback(atmosphere);
    }
```

Change `Process`'s feedback-path lines (saturate before the existing feedback LPF, use `feedback_amount_` instead of the removed constant):

```cpp
        float wet       = delay_->Read();
        float saturated = ApplySaturation(wet, drive_);
        float filtered  = feedback_lpf_.Process(saturated, feedback_lpf_coeff_);
        delay_->Write(in + filtered * feedback_amount_);

        return in + wet;
```

(The rest of `Process` — delay-position slew limiting, `SetDelay` — is unchanged; only the feedback write side changes, not the read/output side.)

- [ ] **Step 4: Run it to verify it passes**

```bash
cd flux-capacitor/tests && make test_tape_delay && ./test_tape_delay
```

Expected: PASS — all 12 test cases pass, 0 failures.

- [ ] **Step 5: Run the full host test suite**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all eight test binaries build and pass.

- [ ] **Step 6: Commit**

```bash
git add flux-capacitor/tape_delay.h flux-capacitor/tests/test_tape_delay.cpp
git commit -m "flux-capacitor: extend TapeDelay with atmosphere-driven feedback amount and saturation"
```

---

### Task 3: Wire ATMOSPHERE into `main.cpp`

**Files:**
- Modify: `flux-capacitor/main.cpp`

**Interfaces:**
- Consumes: `fluxcap::ComputeAtmosphereAmount`, `fluxcap::ComputeAtmosphereLpfCoeff`, `fluxcap::ComputeSaturationDrive`, `fluxcap::ApplySaturation` (Task 1, `atmosphere_control.h`); `fluxcap::TapeDelay::Update`'s new three-parameter signature (Task 2); `fluxcap::OnePoleSmoother` (existing, `dsp_util.h`); `hw.GetKnobValue(KNOB_ATMOSPHERE)`, `hw.GetCvValue(CV_ATMOSPHERE)` (Aurora SDK, already transitively available via the existing `#include "aurora.h"`).

This task has no new pure logic to unit-test — it's hardware glue, verified by the firmware compiling and (separately, per the spec's hardware acceptance criteria) on the module itself.

- [ ] **Step 1: Add the include and new globals**

In `flux-capacitor/main.cpp`, add the include alongside the others:

```cpp
#include "atmosphere_control.h"
```

Add these globals alongside the existing smoother declarations (`OnePoleSmoother warpSmoother; ... OnePoleSmoother timeSmoother;`):

```cpp
OnePoleSmoother atmosphereSmoother;
float           atmosphereSmoothCoeff = 0.0f;
OnePoleSmoother atmosphereLpfL, atmosphereLpfR; // audio-rate tone filters, one per channel
```

- [ ] **Step 2: Compute ATMOSPHERE at control rate and pass it into `TapeDelay::Update`, in `AudioCallback`**

In `flux-capacitor/main.cpp`'s `AudioCallback`, replace:

```cpp
    delayL.Update(delaySeconds, speed);
    delayR.Update(delaySeconds, speed);
```

with:

```cpp
    float rawAtmosphere = ComputeAtmosphereAmount(hw.GetKnobValue(KNOB_ATMOSPHERE), hw.GetCvValue(CV_ATMOSPHERE));
    float atmosphere     = atmosphereSmoother.Process(rawAtmosphere, atmosphereSmoothCoeff);
    float atmosphereLpfCoeff = ComputeAtmosphereLpfCoeff(atmosphere, speed, hw.AudioSampleRate());
    float atmosphereDrive    = ComputeSaturationDrive(atmosphere);

    delayL.Update(delaySeconds, speed, atmosphere);
    delayR.Update(delaySeconds, speed, atmosphere);
```

(`atmosphereLpfCoeff`/`atmosphereDrive` are computed once per block, same as `wowRateHz`/`flutterDepth` above them, then reused for every sample in the block below.)

- [ ] **Step 3: Apply saturate-then-lowpass between `TapeVoice` and `TapeDelay`, in the per-sample loop**

Still in `AudioCallback`, inside the per-sample `for` loop, change:

```cpp
        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        wetL = delayL.Process(wetL, wobble);
        wetR = delayR.Process(wetR, wobble);
```

to:

```cpp
        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        wetL = atmosphereLpfL.Process(ApplySaturation(wetL, atmosphereDrive), atmosphereLpfCoeff);
        wetR = atmosphereLpfR.Process(ApplySaturation(wetR, atmosphereDrive), atmosphereLpfCoeff);

        wetL = delayL.Process(wetL, wobble);
        wetR = delayR.Process(wetR, wobble);
```

- [ ] **Step 4: Initialize the new smoother and filters in `main()`**

In `flux-capacitor/main.cpp`'s `main()`, add alongside the existing smoothing-time constants:

```cpp
    constexpr float kAtmosphereSmoothTimeSeconds = 0.02f; // same as WARP/MIX/REFLECT/BLUR --
    // unlike TIME, a fast ATMOSPHERE turn isn't meant to read as
    // intentional analog character, so it should just track cleanly.
```

and alongside the existing coefficient assignments:

```cpp
    atmosphereSmoothCoeff = 1.0f / (kAtmosphereSmoothTimeSeconds * hw.AudioCallbackRate());
```

and alongside the existing `Init` calls:

```cpp
    atmosphereSmoother.Init(0.0f);
    atmosphereLpfL.Init(0.0f);
    atmosphereLpfR.Init(0.0f);
```

- [ ] **Step 5: Update the file header comment**

At the top of `flux-capacitor/main.cpp`, extend the doc comment's phase description, following the existing pattern each prior phase used:

```cpp
 *  Phase 5a adds ATMOSPHERE (KNOB_ATMOSPHERE/CV_ATMOSPHERE) as a single
 *  tape-coloration control: it darkens the wet signal (tone lowpass,
 *  additionally darkened by tape-stop speed), adds tape-style
 *  saturation, and raises TapeDelay's feedback amount, all together.
```

and add to the `See` list:

```cpp
 *  docs/superpowers/specs/2026-08-17-flux-capacitor-phase5-atmosphere-design.md
```

- [ ] **Step 6: Confirm the SDK libraries are built (one-time; skip if already built)**

```bash
ls lib/Aurora-SDK/libs/libDaisy/build/libdaisy.a lib/Aurora-SDK/libs/DaisySP/build/libdaisysp.a
```

If either is missing:

```bash
make libdaisy
make -C lib/Aurora-SDK/libs/DaisySP GCC_PATH=$(grep '^GCC_PATH' config.mk | sed 's/.*?= *//')
```

- [ ] **Step 7: Build the firmware to confirm it compiles**

```bash
make build PROJECT=flux-capacitor
```

Expected: builds cleanly, producing `flux-capacitor/build/flux-capacitor-<version>.bin` with no errors.

- [ ] **Step 8: Run the full host test suite one more time**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all eight test binaries build and pass.

- [ ] **Step 9: Commit**

```bash
git add flux-capacitor/main.cpp
git commit -m "flux-capacitor: wire ATMOSPHERE control into main.cpp"
```

- [ ] **Step 10: Hardware verification (manual, per the spec's acceptance criteria)**

Flash to the module (see the `run-aurora` skill for mount/flash steps) and check:
- With ATMOSPHERE fully counterclockwise: the signal path sounds effectively as it did before this phase — no audible darkening, saturation, or feedback-level change versus the Phase 4 baseline.
- Turning ATMOSPHERE clockwise smoothly and audibly darkens and warms the wet signal (both the direct pitch-shifted signal and the delay repeats), with increasing saturation character, and the delay's repeats become both more numerous and darker.
- No zipper noise, click, or discontinuity as ATMOSPHERE is turned while audio is playing.
- Engaging FREEZE darkens the tape even with ATMOSPHERE fully counterclockwise, audibly progressing as speed falls toward a full stop.
- No harsh aliasing, runaway feedback buildup, or instability at ATMOSPHERE's maximum, even with TIME set for a short slapback.
- WARP bend, tape-stop, MIX dry/wet, wow/flutter, and TapeDelay's TIME/speed coupling all continue to work normally and audibly compose with the new coloration stage.

This step has no automated pass/fail — record the outcome in conversation with the user rather than checking the box unattended.

---
