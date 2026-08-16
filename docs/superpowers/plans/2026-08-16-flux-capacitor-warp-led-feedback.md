# WARP LED Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Light `LED_1`–`LED_6` as a symmetric bar-graph showing the current WARP pitch shift's direction and magnitude, replacing Phase 1's "LEDs stay off" behavior.

**Architecture:** A pure, host-testable function `ComputeWarpLedLevels(float semitones)` in `warp_control.h` maps the combined semitone value to per-LED brightness. `main.cpp`'s `while(1)` loop calls it every iteration with the same smoothed semitone value the audio callback already computes (`warpSmoother.Value()`), and writes six LEDs via the existing `hw.SetLed`/`hw.WriteLeds` calls — no new shared state, no changes to the audio path.

**Tech Stack:** C++14, DaisySP (`Utility/dsp.h` for `fclamp`), doctest (host-side unit tests), Aurora SDK (`aurora.h` for `Hardware`, `Leds` enum).

**Spec:** `docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md`

## Global Constraints

- Full scale is ±12 semitones (`kWarpKnobRangeSemitones`, already defined in `warp_control.h`), split into three 4-semitone bands per side.
- Up-shift LEDs: `LED_4` (band 0–4), `LED_5` (band 4–8), `LED_6` (band 8–12). Down-shift LEDs: `LED_3` (band 0–4), `LED_2` (band 4–8), `LED_1` (band 8–12).
- Up-shift color: amber `(1.0, 0.4, 0.0)`. Down-shift color: cyan `(0.0, 0.6, 1.0)`.
- Brightness fades continuously within a band (not a hard on/off step); beyond ±12 semitones the side stays fully lit (clamped, no overflow indicator).
- `LED_FREEZE`, `LED_REVERSE`, `LED_BOT_1-3` are untouched — they stay off, as in Phase 1.
- No `Rgb`/color struct exists in `flux-capacitor/` (that type lived in `hello-aurora/colour.h`, a separate project). Do not introduce a dependency on `hello-aurora`; use plain `float` triples for color.

---

## File Structure

- Modify: `flux-capacitor/warp_control.h` — add `WarpLedLevels` struct, `kWarpLedFullScaleSemitones`/`kWarpLedBandSemitones` constants, and `ComputeWarpLedLevels()`.
- Modify: `flux-capacitor/tests/test_warp_control.cpp` — add test cases for `ComputeWarpLedLevels()`.
- Modify: `flux-capacitor/main.cpp` — replace the one-shot boot-time `ClearLeds()`/`WriteLeds()` with an ongoing `while(1)` loop that reads `warpSmoother.Value()`, computes LED levels, and writes the six LEDs.

No new files. `warp_control.h` already holds the pure semitone-mapping logic (`ComputeWarpSemitones`, `WarpSmoother`); the LED mapping is the same kind of pure function, so it belongs alongside them rather than in a new file.

---

## Task 1: `ComputeWarpLedLevels` pure function

**Files:**
- Modify: `flux-capacitor/warp_control.h`
- Test: `flux-capacitor/tests/test_warp_control.cpp`

**Interfaces:**
- Consumes: `kWarpKnobRangeSemitones` (already defined, `warp_control.h:7`), `daisysp::fclamp` (from `Utility/dsp.h`, already included).
- Produces: `struct WarpLedLevels { float up[3]; float down[3]; };` and `WarpLedLevels ComputeWarpLedLevels(float semitones)`, both in `namespace fluxcap`. `up[0]`/`down[0]` are the innermost LEDs (`LED_4`/`LED_3`), `up[2]`/`down[2]` the outermost (`LED_6`/`LED_1`). Task 2 consumes both.

- [ ] **Step 1: Write the failing tests**

Add to `flux-capacitor/tests/test_warp_control.cpp` (after the existing `WarpSmoother` test cases):

```cpp
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd flux-capacitor/tests && make`
Expected: compile error — `WarpLedLevels`/`ComputeWarpLedLevels` not declared. (If `doctest.h` hasn't been downloaded yet in this environment, the `Makefile`'s `curl` step fetches it automatically as part of `make`.)

- [ ] **Step 3: Implement `ComputeWarpLedLevels`**

Add to `flux-capacitor/warp_control.h`, after the existing `kWarpDeadzoneSemitones` constant and before `ComputeWarpSemitones`:

```cpp
constexpr float kWarpLedFullScaleSemitones = kWarpKnobRangeSemitones; // 12.0f
constexpr float kWarpLedBandSemitones      = kWarpLedFullScaleSemitones / 3.0f; // 4.0f

/** Per-LED brightness (0..1) for the WARP pitch-shift bar-graph.
 *  up[0]/down[0] are the innermost LEDs (nearest zero shift), [2] the
 *  outermost. Exactly one side is nonzero at a time -- semitones > 0
 *  populates `up`, semitones < 0 populates `down`, semitones == 0
 *  leaves both all-zero.
 */
struct WarpLedLevels
{
    float up[3];
    float down[3];
};

/** Maps a combined WARP semitone value (post-deadzone, post-smoothing)
 *  to the 6-LED bar-graph's per-LED brightness. Full scale is +-12
 *  semitones (kWarpLedFullScaleSemitones), split into three 4-semitone
 *  bands per side; brightness fades continuously within a band. Values
 *  beyond full scale clamp at full brightness rather than overflowing.
 */
inline WarpLedLevels ComputeWarpLedLevels(float semitones)
{
    WarpLedLevels levels = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    float magnitude = fabsf(semitones);
    float *side = (semitones > 0.0f) ? levels.up : levels.down;
    for (int i = 0; i < 3; i++)
    {
        float bandStart = static_cast<float>(i) * kWarpLedBandSemitones;
        side[i] = daisysp::fclamp(
            (magnitude - bandStart) / kWarpLedBandSemitones, 0.0f, 1.0f);
    }
    return levels;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd flux-capacitor/tests && make`
Expected: `test_warp_control` and `test_tape_voice` both build and all cases pass (doctest prints `[doctest] test cases: N | N passed`).

- [ ] **Step 5: Commit**

```bash
git add flux-capacitor/warp_control.h flux-capacitor/tests/test_warp_control.cpp
git commit -m "flux-capacitor: add ComputeWarpLedLevels for WARP bar-graph"
```

---

## Task 2: Wire the bar-graph into `main.cpp`'s LED loop

**Files:**
- Modify: `flux-capacitor/main.cpp`

**Interfaces:**
- Consumes: `fluxcap::WarpLedLevels`, `fluxcap::ComputeWarpLedLevels(float)` (Task 1). `warpSmoother.Value()` (existing global, `warp_control.h`'s `WarpSmoother::Value()`). `hw.SetLed(Leds, float, float, float)`, `hw.ClearLeds()`, `hw.WriteLeds()`, and the `Leds` enum (`LED_1`..`LED_6`) from `aurora.h`.
- Produces: nothing consumed by later tasks — this is the final integration point for this plan.

This task has no new pure logic to unit-test (the mapping function is already covered by Task 1's tests); its correctness is verified on hardware per the spec's acceptance criteria. There is no host-side test target for `main.cpp` in this codebase (it's the hardware entry point, excluded from `tests/Makefile` by construction — only `warp_control.h` and `tape_voice.h` have test binaries).

- [ ] **Step 1: Replace the one-shot LED clear with an ongoing bar-graph loop**

In `flux-capacitor/main.cpp`, replace:

```cpp
int main(void)
{
    hw.Init();

    // Phase 1 has no ongoing LED behavior; clear whatever state a previous
    // firmware or the bootloader left lit so the module doesn't look hung.
    hw.ClearLeds();
    hw.WriteLeds();

    // One-pole smoothing time constant for WARP knob/CV jitter damping
    // (not pitch glide -- TapeVoice's crossfade mechanism handles that).
    constexpr float kWarpSmoothTimeSeconds = 0.02f;
    warpSmoothCoeff = 1.0f / (kWarpSmoothTimeSeconds * hw.AudioCallbackRate());

    voiceL.Init(hw.AudioSampleRate());
    voiceR.Init(hw.AudioSampleRate());
    warpSmoother.Init(0.0f);

    hw.StartAudio(AudioCallback);

    while (1) {}
}
```

with:

```cpp
int main(void)
{
    hw.Init();
    hw.ClearLeds();
    hw.WriteLeds();

    // One-pole smoothing time constant for WARP knob/CV jitter damping
    // (not pitch glide -- TapeVoice's crossfade mechanism handles that).
    constexpr float kWarpSmoothTimeSeconds = 0.02f;
    warpSmoothCoeff = 1.0f / (kWarpSmoothTimeSeconds * hw.AudioCallbackRate());

    voiceL.Init(hw.AudioSampleRate());
    voiceR.Init(hw.AudioSampleRate());
    warpSmoother.Init(0.0f);

    hw.StartAudio(AudioCallback);

    // WARP pitch-shift bar-graph: LED_4/5/6 light amber outward for an
    // upward shift, LED_3/2/1 light cyan outward for a downward shift.
    // See docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md
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
        hw.WriteLeds();
    }
}
```

Also update the file header comment (currently `"Phase 1: WARP knob/CV pitch bend via a continuous, glitch-free tape voice pitch engine (no semitone stepping). True stereo."`) to mention the LED feedback:

```cpp
/** flux-capacitor
 *
 *  Phase 1: WARP knob/CV pitch bend via a continuous, glitch-free tape
 *  voice pitch engine (no semitone stepping). True stereo. LED_1-6 show
 *  the current pitch shift as a symmetric bar-graph.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md
 *  and docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md
 */
```

- [ ] **Step 2: Build the device firmware to verify it compiles**

Run: `make build PROJECT=flux-capacitor` (from the repo root; requires the ARM toolchain and `make libdaisy` to have been run once — see `README.md` if not already set up).
Expected: build succeeds, producing `flux-capacitor/build/flux-capacitor.bin`.

- [ ] **Step 3: Run the full host test suite once more to confirm nothing broke**

Run: `cd flux-capacitor/tests && make`
Expected: all test cases still pass (this task touches no logic covered by tests, only hardware glue — this step is a safety net, not new coverage).

- [ ] **Step 4: Commit**

```bash
git add flux-capacitor/main.cpp
git commit -m "flux-capacitor: light LED_1-6 as WARP pitch-shift bar-graph"
```

- [ ] **Step 5: Flash and verify on hardware**

Per the spec's acceptance criteria — not automatable, requires the physical module:

```bash
make flash PROJECT=flux-capacitor MOUNT=/media/YOUR_USER/YOUR_DRIVE
```

Then, with the module powered up from the flashed USB drive:
- Turn `KNOB_WARP` from center to fully clockwise: `LED_4 → LED_5 → LED_6` should light amber, in sequence, ending fully bright.
- Turn it fully counter-clockwise: `LED_3 → LED_2 → LED_1` should light cyan, in sequence.
- Patch a v/oct CV sweep into `CV_WARP` with the knob centered: same bar-graph behavior should be visible from CV alone.
- With the knob centered and no CV patched: all six LEDs off.

---

## Self-Review Notes

- **Spec coverage:** LED mapping/colors/bands (spec's "Approach" section) → Task 1. Main-loop wiring and `warpSmoother.Value()` reuse (spec's "Implementation shape") → Task 2. Testing plan's host-testable cases → Task 1 Step 1. Testing plan's hardware acceptance criteria → Task 2 Step 5. "Out of scope" section requires no task (nothing to build).
- **Placeholder scan:** no TBD/TODO; all steps include complete, concrete code.
- **Type consistency:** `WarpLedLevels`, `ComputeWarpLedLevels`, and `warpSmoother.Value()` are used identically across Task 1 and Task 2. Confirmed `daisysp::fclamp` and `constexpr float kWarpKnobRangeSemitones` already exist in `warp_control.h` (read the file directly before writing this plan) — no forward reference to an undefined symbol.
