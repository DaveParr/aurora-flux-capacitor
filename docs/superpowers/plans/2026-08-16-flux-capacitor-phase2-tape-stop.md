# Flux Capacitor — Phase 2: Tape Stop + MIX Dry/Wet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add FREEZE-driven tape stop (continuous speed → pitch/amplitude) and a MIX-knob dry/wet blend to the `flux-capacitor` firmware, on top of the existing Phase 1 WARP pitch-bend engine.

**Architecture:** Three new pure, host-testable headers (`dsp_util.h`, `mix_control.h`, `tape_transport.h`) following the existing `warp_control.h` pattern of "pure mapping function + thin hardware glue in `main.cpp`." `TapeTransport` produces a continuous `speed` (0..1) that combines with WARP's semitone offset before hitting the existing, unmodified `TapeVoice`; `mix_control.h` computes an equal-power crossfade between the raw input and that combined wet signal.

**Tech Stack:** C++14, DaisySP (`Utility/dsp.h`: `fclamp`, `fmax`, `fonepole`, `HALFPI_F`), doctest for host-side tests, Aurora SDK (`aurora.h`) for hardware glue.

**Spec:** [`docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md`](../specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md)

## Global Constraints

- `namespace fluxcap` for all new headers, matching `warp_control.h`/`tape_voice.h`.
- `#pragma once` + explicit `#include <cmath>` and `"Utility/dsp.h"` at the top of every new header (existing headers include `<cmath>` explicitly even though `dsp.h` already pulls it in — follow that convention).
- `StopSemitones(speed) = 12.0f * log2f(daisysp::fmax(speed, kMinStopSpeed))` with `kMinStopSpeed = 0.0001f` — the physically-correct tape relationship (halving speed = −12 semitones), floored to avoid `log2(0)`.
- `StopAmplitude(speed) = speed` — linear, no other curve.
- MIX uses `HALFPI_F` (the bare macro from `dsp.h`, used unprefixed elsewhere in this codebase — see `tape_voice.h`'s use of `PI_F`), **not** `daisysp::HALFPI_F`.
- `ComputeMix` is additive knob+CV, clamped to `[0, 1]` via `daisysp::fclamp`, same pattern as `ComputeWarpSemitones`.
- No lowpass/filtering during stop, no stop-rate knob control, no REVERSE changes — all deferred per the spec's "Out of scope" section.
- Test files follow the existing pattern exactly: `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include "doctest.h"` + `#include "../<header>.h"`, `using namespace fluxcap;`, `TEST_CASE`/`CHECK`/`doctest::Approx`.
- Every new test binary must be added to `flux-capacitor/tests/Makefile`'s `all` target (both the build prerequisite list and the `./test_x` run line), following the existing `test_warp_control`/`test_tape_voice` targets.

---

### Task 1: Generalize `WarpSmoother` into `OnePoleSmoother` (`dsp_util.h`)

**Files:**
- Create: `flux-capacitor/dsp_util.h`
- Create: `flux-capacitor/tests/test_dsp_util.cpp`
- Modify: `flux-capacitor/warp_control.h` (remove the `WarpSmoother` class)
- Modify: `flux-capacitor/tests/test_warp_control.cpp` (remove the 3 `WarpSmoother` test cases)
- Modify: `flux-capacitor/main.cpp` (use `OnePoleSmoother` instead of `WarpSmoother`)
- Modify: `flux-capacitor/tests/Makefile`

**Interfaces:**
- Produces: `fluxcap::OnePoleSmoother` — `void Init(float initial = 0.0f)`, `float Process(float target, float coeff)`, `float Value() const`. Identical behavior/signature to the old `WarpSmoother`, just renamed and moved so `mixSmoother` (Task 4) can reuse it without duplicating the wrapper.

- [ ] **Step 1: Write the failing test**

Create `flux-capacitor/tests/test_dsp_util.cpp`:

```cpp
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
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd flux-capacitor/tests && make test_dsp_util
```

Expected: FAIL — compile error, `../dsp_util.h: No such file or directory`.

- [ ] **Step 3: Create `dsp_util.h`**

Create `flux-capacitor/dsp_util.h`:

```cpp
#pragma once
#include "Utility/dsp.h"

namespace fluxcap
{
/** One-pole smoother, used to damp ADC/knob jitter on a control-rate
 *  value before it reaches audio-rate processing (e.g. WARP semitones,
 *  MIX blend amount). Not used for TapeTransport's speed ramp, which
 *  is a separate target-chasing smoother with its own snap-to-target
 *  behavior.
 */
class OnePoleSmoother
{
  public:
    void Init(float initial = 0.0f) { value_ = initial; }

    float Process(float target, float coeff)
    {
        daisysp::fonepole(value_, target, coeff);
        return value_;
    }

    float Value() const { return value_; }

  private:
    float value_ = 0.0f;
};
} // namespace fluxcap
```

- [ ] **Step 4: Remove `WarpSmoother` from `warp_control.h`**

In `flux-capacitor/warp_control.h`, delete this class (it's replaced by `fluxcap::OnePoleSmoother` in `dsp_util.h`):

```cpp
/** One-pole smoother, used to damp ADC/knob jitter on the combined
 *  semitone value before it reaches TapeVoice. This is separate from
 *  TapeVoice's own crossfade mechanism, which is what makes pitch
 *  changes glitch-free.
 */
class WarpSmoother
{
  public:
    void Init(float initial = 0.0f) { value_ = initial; }

    float Process(float target, float coeff)
    {
        daisysp::fonepole(value_, target, coeff);
        return value_;
    }

    float Value() const { return value_; }

  private:
    float value_ = 0.0f;
};
```

- [ ] **Step 5: Update `test_warp_control.cpp`**

In `flux-capacitor/tests/test_warp_control.cpp`, delete these 3 `TEST_CASE`s (they move to `test_dsp_util.cpp` under the new name):

```cpp
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
```

- [ ] **Step 6: Update `main.cpp` to use `OnePoleSmoother`**

In `flux-capacitor/main.cpp`:
- Add `#include "dsp_util.h"` alongside the other includes.
- Change the global `WarpSmoother warpSmoother;` to `OnePoleSmoother warpSmoother;`.

- [ ] **Step 7: Update `flux-capacitor/tests/Makefile`**

Add `test_dsp_util` to the `all` target's prerequisite list and run line, and add its build rule:

```makefile
all: $(DOCTEST_H) test_warp_control test_tape_voice test_dsp_util
	./test_warp_control
	./test_tape_voice
	./test_dsp_util
```

```makefile
test_dsp_util: test_dsp_util.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<
```

```makefile
clean:
	rm -f test_warp_control test_tape_voice test_dsp_util $(DOCTEST_H)
```

- [ ] **Step 8: Run all host tests to verify they pass**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all of `test_warp_control`, `test_tape_voice`, `test_dsp_util` build and report all assertions passing (no `WarpSmoother` symbol errors from the removed tests).

- [ ] **Step 9: Commit**

```bash
git add flux-capacitor/dsp_util.h flux-capacitor/warp_control.h flux-capacitor/main.cpp \
        flux-capacitor/tests/test_dsp_util.cpp flux-capacitor/tests/test_warp_control.cpp \
        flux-capacitor/tests/Makefile
git commit -m "flux-capacitor: generalize WarpSmoother into reusable OnePoleSmoother"
```

---

### Task 2: MIX dry/wet math (`mix_control.h`)

**Files:**
- Create: `flux-capacitor/mix_control.h`
- Create: `flux-capacitor/tests/test_mix_control.cpp`
- Modify: `flux-capacitor/tests/Makefile`

**Interfaces:**
- Produces: `fluxcap::ComputeMix(float mix_knob, float mix_cv) -> float` (clamped `[0,1]`); `fluxcap::ComputeMixGains(float mix, float *dry_gain, float *wet_gain) -> void`. Task 4's `main.cpp` wiring calls both.

- [ ] **Step 1: Write the failing test**

Create `flux-capacitor/tests/test_mix_control.cpp`:

```cpp
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
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd flux-capacitor/tests && make test_mix_control
```

Expected: FAIL — compile error, `../mix_control.h: No such file or directory`.

- [ ] **Step 3: Create `mix_control.h`**

Create `flux-capacitor/mix_control.h`:

```cpp
#pragma once
#include <cmath>
#include "Utility/dsp.h"

namespace fluxcap
{
/** Maps KNOB_MIX (0..1) + CV_MIX (additive, same pattern as WARP) to a
 *  combined, clamped dry/wet blend amount in [0, 1].
 */
inline float ComputeMix(float mix_knob, float mix_cv)
{
    return daisysp::fclamp(mix_knob + mix_cv, 0.0f, 1.0f);
}

/** Equal-power (constant-power) crossfade gains for a dry/wet blend
 *  amount in [0, 1]. At mix == 0.5, both gains are ~0.707 (not 0.5),
 *  so perceived loudness stays constant across the sweep instead of
 *  dipping in the middle, unlike a linear crossfade.
 */
inline void ComputeMixGains(float mix, float *dry_gain, float *wet_gain)
{
    *dry_gain = cosf(mix * HALFPI_F);
    *wet_gain = sinf(mix * HALFPI_F);
}
} // namespace fluxcap
```

- [ ] **Step 4: Run it to verify it passes**

```bash
cd flux-capacitor/tests && make test_mix_control && ./test_mix_control
```

Expected: PASS — all assertions pass.

- [ ] **Step 5: Update `flux-capacitor/tests/Makefile`**

```makefile
all: $(DOCTEST_H) test_warp_control test_tape_voice test_dsp_util test_mix_control
	./test_warp_control
	./test_tape_voice
	./test_dsp_util
	./test_mix_control
```

```makefile
test_mix_control: test_mix_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<
```

```makefile
clean:
	rm -f test_warp_control test_tape_voice test_dsp_util test_mix_control $(DOCTEST_H)
```

- [ ] **Step 6: Run the full host test suite**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all four test binaries build and pass.

- [ ] **Step 7: Commit**

```bash
git add flux-capacitor/mix_control.h flux-capacitor/tests/test_mix_control.cpp \
        flux-capacitor/tests/Makefile
git commit -m "flux-capacitor: add ComputeMix/ComputeMixGains for MIX dry/wet blend"
```

---

### Task 3: Tape transport (`tape_transport.h`)

**Files:**
- Create: `flux-capacitor/tape_transport.h`
- Create: `flux-capacitor/tests/test_tape_transport.cpp`
- Modify: `flux-capacitor/tests/Makefile`

**Interfaces:**
- Consumes: `daisysp::fonepole`, `daisysp::fmax`, `daisysp::fclamp` (all from `Utility/dsp.h`, already used elsewhere in this codebase).
- Produces: `fluxcap::StopSemitones(float speed) -> float`; `fluxcap::StopAmplitude(float speed) -> float`; `fluxcap::TapeTransport` — `void Init(float initial_speed = 1.0f)`, `void Update(bool freeze_edge, bool gate_high, float ramp_coeff)`, `float Speed() const`. Task 4's `main.cpp` wiring calls all of these.

- [ ] **Step 1: Write the failing test**

Create `flux-capacitor/tests/test_tape_transport.cpp`:

```cpp
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
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd flux-capacitor/tests && make test_tape_transport
```

Expected: FAIL — compile error, `../tape_transport.h: No such file or directory`.

- [ ] **Step 3: Create `tape_transport.h`**

Create `flux-capacitor/tape_transport.h`:

```cpp
#pragma once
#include <cmath>
#include "Utility/dsp.h"

namespace fluxcap
{
// Floor applied before log2f in StopSemitones to avoid log2(0) = -inf.
constexpr float kMinStopSpeed = 0.0001f;

/** Converts a tape transport speed (0..1, nominal = 1.0) to a semitone
 *  offset, using the physically-correct tape relationship: halving
 *  playback speed drops pitch exactly one octave. speed == 1.0 -> 0
 *  semitones (no shift). Floors speed at kMinStopSpeed before taking
 *  log2 so speed == 0.0 (fully stopped) returns a large-but-finite
 *  negative value instead of -inf.
 */
inline float StopSemitones(float speed)
{
    return 12.0f * log2f(daisysp::fmax(speed, kMinStopSpeed));
}

/** Converts tape transport speed (0..1) to a wet-signal amplitude gain.
 *  Linear for now (amplitude == speed): simplest defensible curve,
 *  independently tunable later without touching TapeTransport or the
 *  MIX blend.
 */
inline float StopAmplitude(float speed)
{
    return speed;
}

/** Continuous tape-transport speed (0..1) that one-pole-smooths toward
 *  a 0/1 target, snapping to the target once close enough so PLAY
 *  (1.0) and STOPPED (0.0) are reached exactly rather than approached
 *  asymptotically. Collapses the "STOPPING vs STOPPED" / "STARTING vs
 *  PLAY" distinction into "target is 0" / "target is 1": catching
 *  freeze_edge mid-ramp just flips the target and Update() reverses
 *  direction on its own.
 */
class TapeTransport
{
  public:
    void Init(float initial_speed = 1.0f)
    {
        speed_         = initial_speed;
        button_target_ = initial_speed;
    }

    /** freeze_edge: true on the block SW_FREEZE was just pressed --
     *  flips the button's target (0 <-> 1).
     *  gate_high: current GATE_FREEZE level. Forces the target to 0
     *  while true, overriding the button; when it goes false, control
     *  reverts to the button's last toggled target. Unpatched gates
     *  read low (GateIn's pulldown), so this is a no-op when nothing
     *  is patched into GATE_FREEZE.
     *  ramp_coeff: one-pole coefficient (1.0f / (seconds * blockRate)).
     */
    void Update(bool freeze_edge, bool gate_high, float ramp_coeff)
    {
        if (freeze_edge)
            button_target_ = (button_target_ >= 0.5f) ? 0.0f : 1.0f;

        float target = gate_high ? 0.0f : button_target_;

        daisysp::fonepole(speed_, target, ramp_coeff);

        if (fabsf(speed_ - target) < kSnapEpsilon)
            speed_ = target;
    }

    float Speed() const { return speed_; }

  private:
    static constexpr float kSnapEpsilon = 0.001f;

    float speed_         = 1.0f;
    float button_target_ = 1.0f;
};
} // namespace fluxcap
```

- [ ] **Step 4: Run it to verify it passes**

```bash
cd flux-capacitor/tests && make test_tape_transport && ./test_tape_transport
```

Expected: PASS — all assertions pass.

- [ ] **Step 5: Update `flux-capacitor/tests/Makefile`**

```makefile
all: $(DOCTEST_H) test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport
	./test_warp_control
	./test_tape_voice
	./test_dsp_util
	./test_mix_control
	./test_tape_transport
```

```makefile
test_tape_transport: test_tape_transport.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<
```

```makefile
clean:
	rm -f test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport $(DOCTEST_H)
```

- [ ] **Step 6: Run the full host test suite**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all five test binaries build and pass.

- [ ] **Step 7: Commit**

```bash
git add flux-capacitor/tape_transport.h flux-capacitor/tests/test_tape_transport.cpp \
        flux-capacitor/tests/Makefile
git commit -m "flux-capacitor: add TapeTransport speed state machine and stop pitch/amplitude math"
```

---

### Task 4: Wire tape stop + MIX into `main.cpp`

**Files:**
- Modify: `flux-capacitor/main.cpp`

**Interfaces:**
- Consumes: `fluxcap::TapeTransport`, `fluxcap::StopSemitones`, `fluxcap::StopAmplitude` (Task 3); `fluxcap::ComputeMix`, `fluxcap::ComputeMixGains` (Task 2); `fluxcap::OnePoleSmoother` (Task 1); existing `fluxcap::ComputeWarpSemitones`, `fluxcap::TapeVoice`, `fluxcap::ComputeWarpLedLevels` (unchanged); `hw.GetButton(SW_FREEZE).RisingEdge()`, `hw.GetGateState(GATE_FREEZE)`, `hw.GetCvValue(CV_MIX)`, `hw.SetLed(LED_FREEZE, r, g, b)` (Aurora SDK, `aurora.h`).

This task has no new pure logic to unit-test — it's hardware glue, verified by the firmware compiling and (separately, per the spec's hardware acceptance criteria) on the module itself. Each step is a self-contained edit followed by a compile check.

- [ ] **Step 1: Replace `main.cpp`'s contents**

Replace the full contents of `flux-capacitor/main.cpp` with:

```cpp
/** flux-capacitor
 *
 *  Phase 2: FREEZE-driven tape stop with a MIX dry/wet blend, layered
 *  on top of Phase 1's WARP knob/CV pitch bend. True stereo. LED_1-6
 *  show the WARP pitch shift as a bar-graph; LED_FREEZE tracks the
 *  tape-stop fade.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md, and
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md
 */
#include "aurora.h"
#include "tape_voice.h"
#include "warp_control.h"
#include "tape_transport.h"
#include "mix_control.h"
#include "dsp_util.h"

using namespace daisy;
using namespace aurora;
using namespace fluxcap;

Hardware        hw;
TapeVoice       voiceL, voiceR;
TapeTransport   transport;
OnePoleSmoother warpSmoother;
OnePoleSmoother mixSmoother;
float           warpSmoothCoeff = 0.0f;
float           mixSmoothCoeff  = 0.0f;
float           stopRampCoeff   = 0.0f;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();

    float rawWarpSemis = ComputeWarpSemitones(hw.GetKnobValue(KNOB_WARP), hw.GetWarpVoct());
    float warpSemis     = warpSmoother.Process(rawWarpSemis, warpSmoothCoeff);

    bool freezeEdge = hw.GetButton(SW_FREEZE).RisingEdge();
    bool gateHigh   = hw.GetGateState(GATE_FREEZE);
    transport.Update(freezeEdge, gateHigh, stopRampCoeff);
    float speed = transport.Speed();

    float totalSemis = warpSemis + StopSemitones(speed);
    float wetAmp      = StopAmplitude(speed);

    float rawMix = ComputeMix(hw.GetKnobValue(KNOB_MIX), hw.GetCvValue(CV_MIX));
    float mix     = mixSmoother.Process(rawMix, mixSmoothCoeff);
    float dryGain, wetGain;
    ComputeMixGains(mix, &dryGain, &wetGain);

    for (size_t i = 0; i < size; i++)
    {
        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        out[0][i] = in[0][i] * dryGain + wetL * wetGain;
        out[1][i] = in[1][i] * dryGain + wetR * wetGain;
    }
}

int main(void)
{
    hw.Init();
    hw.ClearLeds();
    hw.WriteLeds();

    // One-pole smoothing time constants for control-rate jitter damping
    // (not pitch/amplitude glide -- TapeVoice's crossfade and
    // TapeTransport's own ramp handle those).
    constexpr float kWarpSmoothTimeSeconds = 0.02f;
    constexpr float kMixSmoothTimeSeconds  = 0.02f;
    constexpr float kStopRampTimeSeconds   = 1.5f;
    warpSmoothCoeff = 1.0f / (kWarpSmoothTimeSeconds * hw.AudioCallbackRate());
    mixSmoothCoeff  = 1.0f / (kMixSmoothTimeSeconds * hw.AudioCallbackRate());
    stopRampCoeff   = 1.0f / (kStopRampTimeSeconds * hw.AudioCallbackRate());

    voiceL.Init(hw.AudioSampleRate());
    voiceR.Init(hw.AudioSampleRate());
    transport.Init();
    warpSmoother.Init(0.0f);
    mixSmoother.Init(0.0f);

    hw.StartAudio(AudioCallback);

    // WARP pitch-shift bar-graph: LED_4/5/6 light amber outward for an
    // upward shift, LED_3/2/1 light cyan outward for a downward shift.
    // LED_FREEZE lights red, brightness tracking (1 - tape speed), so
    // it's off in normal play and full-bright exactly when stopped.
    // See docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md
    // and docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md
    constexpr float kWarpUpColor[3]   = {1.0f, 0.4f, 0.0f}; // amber
    constexpr float kWarpDownColor[3] = {0.0f, 0.6f, 1.0f}; // cyan
    const Leds      upLeds[3]         = {LED_4, LED_5, LED_6};
    const Leds      downLeds[3]       = {LED_3, LED_2, LED_1};

    while (1)
    {
        WarpLedLevels levels = ComputeWarpLedLevels(warpSmoother.Value());
        hw.ClearLeds();
        for (int i = 0; i < 3; i++)
        {
            hw.SetLed(upLeds[i],
                      kWarpUpColor[0] * levels.up[i],
                      kWarpUpColor[1] * levels.up[i],
                      kWarpUpColor[2] * levels.up[i]);
            hw.SetLed(downLeds[i],
                      kWarpDownColor[0] * levels.down[i],
                      kWarpDownColor[1] * levels.down[i],
                      kWarpDownColor[2] * levels.down[i]);
        }
        hw.SetLed(LED_FREEZE, 1.0f - transport.Speed(), 0.0f, 0.0f);
        hw.WriteLeds();
    }
}
```

- [ ] **Step 2: Confirm the SDK libraries are built (one-time; skip if already built)**

```bash
ls lib/Aurora-SDK/libs/libDaisy/build/libdaisy.a lib/Aurora-SDK/libs/DaisySP/build/libdaisysp.a
```

If either is missing:

```bash
make libdaisy
make -C lib/Aurora-SDK/libs/DaisySP GCC_PATH=$(grep '^GCC_PATH' config.mk | sed 's/.*?= *//')
```

- [ ] **Step 3: Build the firmware to confirm it compiles**

```bash
make build PROJECT=flux-capacitor
```

Expected: builds cleanly, producing `flux-capacitor/build/flux-capacitor-<version>.bin` with no errors — confirms `main.cpp`'s new includes, `SW_FREEZE`/`GATE_FREEZE`/`CV_MIX`/`LED_FREEZE` usage, and the new headers all compile together against the real Aurora SDK (not just the host-side doctest stubs).

- [ ] **Step 4: Run the full host test suite one more time**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all five test binaries build and pass.

- [ ] **Step 5: Commit**

```bash
git add flux-capacitor/main.cpp
git commit -m "flux-capacitor: wire tape stop transport and MIX dry/wet into main.cpp"
```

- [ ] **Step 6: Hardware verification (manual, per the spec's acceptance criteria)**

Flash to the module (see `run-aurora` skill for mount/flash steps) and check:
- With MIX at max: `SW_FREEZE` mid-play smoothly winds pitch down and fades to full silence within ~1.5s; pressing it again ramps cleanly back to normal play, no pop.
- With MIX at 0: `SW_FREEZE` has no audible effect at all.
- Catching `SW_FREEZE` mid-ramp reverses direction smoothly.
- `LED_FREEZE` brightness visually tracks the audible fade.
- A gate patched into `GATE_FREEZE` forces stop while high and overrides the button; releases cleanly on low.
- WARP bend still works and composes with tape-stop's pitch drop.

This step has no automated pass/fail — record the outcome in conversation with the user rather than checking the box unattended.

---
