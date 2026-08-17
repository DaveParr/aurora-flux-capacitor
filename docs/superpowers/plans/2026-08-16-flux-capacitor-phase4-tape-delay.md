# Flux Capacitor — Phase 4: Tape Delay Implementation Plan

> **Post-implementation note:** This plan's code samples reflect the design as originally written and do not include two corrections made during review: (1) `ApplyWobbleToDelaySamples` divides by the wobble ratio, not multiplies; (2) `TapeDelay::Process` slew-limits the delay-line position and returns `in + wet`, not just `wet`. The design spec's "Wow/flutter coupling" and "`TapeDelay` class" sections have been updated to match. Trust `flux-capacitor/tape_delay.h` over this document's code blocks.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a stereo, wow/flutter-and-speed-coupled tape delay to the `flux-capacitor` firmware, sitting between the existing WARP/tape-stop/wow-flutter pitch stage (`TapeVoice`) and the MIX dry/wet blend — wiring up `KNOB_TIME`/`CV_TIME` for the first time.

**Architecture:** One new host-testable header (`tape_delay.h`), following the existing `tape_transport.h`/`wow_flutter.h` pattern of "pure mapping functions + a stateful class in one file." `TapeDelay` wraps a single `DelayLine` with a damped feedback loop; because its backing buffer must live in Aurora's external SDRAM (`DSY_SDRAM_BSS`, only available via a libDaisy include chain the host build can't compile), `TapeDelay` takes a pointer to caller-owned storage in `Init` rather than embedding the `DelayLine` as a member — `main.cpp` declares the SDRAM-placed buffers as globals and owns them, keeping `tape_delay.h` itself free of any hardware-specific include. Speed coupling and wow/flutter coupling both reuse existing state (`TapeTransport::Speed()`, `WowFlutter::Process`'s per-sample return value) rather than introducing new modulation sources.

**Tech Stack:** C++14, DaisySP (`Utility/dsp.h`: `fclamp`, `fmax`, `fmap`, `fonepole`; `Utility/delayline.h`: `DelayLine`), doctest for host-side tests, Aurora SDK (`aurora.h`) for hardware glue, libDaisy (`dev/sdram.h`, via `aurora.h`) for `DSY_SDRAM_BSS`.

**Spec:** [`docs/superpowers/specs/2026-08-16-flux-capacitor-phase4-tape-delay-design.md`](../specs/2026-08-16-flux-capacitor-phase4-tape-delay-design.md)

## Global Constraints

- `namespace fluxcap` for the new header, matching every existing one.
- `#pragma once` + explicit `#include <cmath>` and `#include <cstddef>` at the top (matching `tape_voice.h`'s explicit-include convention for the same `size_t`/`powf` needs), plus `"Utility/dsp.h"` and `"Utility/delayline.h"`.
- `tape_delay.h` must never `#include` anything from libDaisy (only DaisySP's `Utility` path) — this is what keeps it host-testable. The one exception, `DSY_SDRAM_BSS`, stays entirely in `main.cpp`, which already has it transitively via `aurora.h` → `daisy_seed.h` → `daisy.h` → `dev/sdram.h`.
- `kDelayTimeMinSeconds = 0.001f`, `kDelayTimeMaxSeconds = 2.0f`, `kTapeDelayMaxSamples = 96000` (2s at a nominal 48kHz, fixed compile-time constant per `tape_voice.h`'s precedent — not derived from `hw.AudioSampleRate()`).
- `kTapeDelayFeedback = 0.35f` — fixed constant, no control surface this phase.
- `kTapeDelayFeedbackLpfTauSeconds = 0.00005f` (**not** a rounder-looking value like `0.001f` — verified empirically in the spec that a slower tau smears repeats together at TIME's shortest setting instead of just darkening them; this exact value is load-bearing).
- `ComputeDelayTimeSeconds`/`ComputeDelaySamples` are additive knob+CV, clamped, log-curved for the time mapping — same pattern as `ComputeWowRateHz`.
- `ComputeDelaySamples` reuses `tape_transport.h`'s existing `kMinStopSpeed` constant as its near-zero-speed floor — no second floor constant.
- `TapeDelay::Init` takes a `TapeDelayLine *` (caller-owned) plus `sample_rate`, **not** a plain `DelayLine` member — see Architecture above.
- No ATMOSPHERE control, no saturation, no cross-channel (ping-pong) feedback, no REVERSE changes — all deferred per the spec's "Out of scope" section.
- Test files follow the existing pattern exactly: `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include "doctest.h"` + `#include "../<header>.h"`, `using namespace fluxcap;`, `TEST_CASE`/`CHECK`/`doctest::Approx`.
- The new test binary must be added to `flux-capacitor/tests/Makefile`'s `all` target (both the build prerequisite list and the `./test_x` run line) and its `clean` rule, following the existing targets. `tape_delay.h` needs no extra DaisySP `.cpp` source to link (unlike `test_tape_voice`/`test_wow_flutter`, which need `phasor.cpp`/`oscillator.cpp`) — `DelayLine` and `fonepole` are header-only.

---

### Task 1: Delay time/speed/wobble math and `TapeDelay` class (`tape_delay.h`)

**Files:**
- Create: `flux-capacitor/tape_delay.h`
- Create: `flux-capacitor/tests/test_tape_delay.cpp`
- Modify: `flux-capacitor/tests/Makefile`

**Interfaces:**
- Consumes: `daisysp::fclamp`, `daisysp::fmax`, `daisysp::fmap`, `daisysp::Mapping::LOG`, `daisysp::fonepole` (all from `Utility/dsp.h`, already used elsewhere in this codebase); `daisysp::DelayLine<T, N>` (`Utility/delayline.h`, already used by `tape_voice.h`); `fluxcap::kMinStopSpeed` (`tape_transport.h`); `fluxcap::OnePoleSmoother` (`dsp_util.h`).
- Produces: `fluxcap::ComputeDelayTimeSeconds(float time_knob, float time_cv) -> float`; `fluxcap::ComputeDelaySamples(float base_seconds, float speed, float sample_rate) -> float`; `fluxcap::ApplyWobbleToDelaySamples(float delay_samples, float wobble_semitones) -> float`; `fluxcap::TapeDelayLine` (alias for `daisysp::DelayLine<float, kTapeDelayMaxSamples>`); `fluxcap::TapeDelay` — `void Init(TapeDelayLine *delay_line, float sample_rate)`, `void Update(float base_seconds, float speed)`, `float Process(float in, float wobble_semitones)`. Task 2's `main.cpp` wiring calls all of these.

- [ ] **Step 1: Write the failing test**

Create `flux-capacitor/tests/test_tape_delay.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../tape_delay.h"
#include <cmath>

using namespace fluxcap;

TEST_CASE("ComputeDelayTimeSeconds - knob endpoints map to the range") {
    CHECK(ComputeDelayTimeSeconds(0.0f, 0.0f) == doctest::Approx(kDelayTimeMinSeconds));
    CHECK(ComputeDelayTimeSeconds(1.0f, 0.0f) == doctest::Approx(kDelayTimeMaxSeconds));
}

TEST_CASE("ComputeDelayTimeSeconds - combined knob+CV clamps to [0,1] before mapping") {
    CHECK(ComputeDelayTimeSeconds(0.9f, 0.5f) == doctest::Approx(kDelayTimeMaxSeconds)); // clamped high
    CHECK(ComputeDelayTimeSeconds(0.1f, -0.5f) == doctest::Approx(kDelayTimeMinSeconds)); // clamped low
}

TEST_CASE("ComputeDelayTimeSeconds - result always within [min, max]") {
    for (float knob = 0.0f; knob <= 1.0f; knob += 0.1f)
    {
        float seconds = ComputeDelayTimeSeconds(knob, 0.0f);
        CHECK(seconds >= kDelayTimeMinSeconds);
        CHECK(seconds <= kDelayTimeMaxSeconds);
    }
}

TEST_CASE("ComputeDelaySamples - speed 1.0 is unscaled base_seconds * sample_rate") {
    CHECK(ComputeDelaySamples(0.002f, 1.0f, 48000.0f) == doctest::Approx(96.0f));
}

TEST_CASE("ComputeDelaySamples - speed below 1.0 lengthens the result") {
    float atFull = ComputeDelaySamples(0.002f, 1.0f, 48000.0f);
    float atHalf = ComputeDelaySamples(0.002f, 0.5f, 48000.0f);
    CHECK(atHalf == doctest::Approx(atFull * 2.0f));
}

TEST_CASE("ComputeDelaySamples - clamps to buffer capacity for near-zero speed") {
    float result = ComputeDelaySamples(kDelayTimeMaxSeconds, 0.0f, 48000.0f);
    CHECK(result == doctest::Approx(static_cast<float>(kTapeDelayMaxSamples - 1)));
}

TEST_CASE("ComputeDelaySamples - speed=0 and speed below kMinStopSpeed clamp identically") {
    float atZero  = ComputeDelaySamples(0.01f, 0.0f, 48000.0f);
    float atFloor = ComputeDelaySamples(0.01f, kMinStopSpeed / 2.0f, 48000.0f);
    CHECK(atZero == doctest::Approx(atFloor));
}

TEST_CASE("ApplyWobbleToDelaySamples - zero wobble leaves samples unchanged") {
    CHECK(ApplyWobbleToDelaySamples(100.0f, 0.0f) == doctest::Approx(100.0f));
}

TEST_CASE("ApplyWobbleToDelaySamples - +12 semitones doubles, -12 halves") {
    CHECK(ApplyWobbleToDelaySamples(100.0f, 12.0f) == doctest::Approx(200.0f));
    CHECK(ApplyWobbleToDelaySamples(100.0f, -12.0f) == doctest::Approx(50.0f));
}

TEST_CASE("TapeDelay - impulse produces decaying, bounded repeats at the expected spacing") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 1.0f); // target 100 samples

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
}

TEST_CASE("TapeDelay - lower speed lengthens the gap between repeats") {
    TapeDelayLine line;
    TapeDelay     delay;
    delay.Init(&line, 48000.0f);
    delay.Update(100.0f / 48000.0f, 0.5f); // target 200 samples (100 / 0.5)

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
    delay.Update(50.0f / 48000.0f, 1.0f); // target 50 samples

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
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd flux-capacitor/tests && make test_tape_delay
```

Expected: FAIL — compile error, `../tape_delay.h: No such file or directory`.

- [ ] **Step 3: Create `tape_delay.h`**

Create `flux-capacitor/tape_delay.h`:

```cpp
#pragma once
#include <cmath>
#include <cstddef>
#include "Utility/dsp.h"
#include "Utility/delayline.h"
#include "tape_transport.h"
#include "dsp_util.h"

namespace fluxcap
{
constexpr float kDelayTimeMinSeconds = 0.001f;
constexpr float kDelayTimeMaxSeconds = 2.0f;

// 2 seconds at a nominal 48kHz -- TapeVoice's DelayLine sizing precedent
// (fixed compile-time constant, not derived from hw.AudioSampleRate()).
constexpr size_t kTapeDelayMaxSamples = 96000;

constexpr float kTapeDelayFeedback = 0.35f; // fixed; no control surface yet

// ~3.2 kHz -3dB point. Verified empirically (host-side probe) to settle
// within ~10 samples at 48kHz -- comfortably inside even TIME's shortest
// (1ms / 48-sample) gap between repeats, so it darkens each repeat
// without smearing it into the next. A slower tau (e.g. 0.001s / ~159Hz)
// was tried first and rejected: it spread each repeat's energy across
// ~150 samples with no return to silence between taps, even at TIME's
// minimum -- a diffuse wash, not the tight slapback this phase wants.
constexpr float kTapeDelayFeedbackLpfTauSeconds = 0.00005f;

/** Maps KNOB_TIME (0..1) + CV_TIME (additive, same pattern as
 *  WARP/MIX/REFLECT/BLUR) to a base delay time in seconds, log-curved
 *  (like ComputeWowRateHz) so short slapback times and long ambient
 *  tails both feel like even steps across the knob's travel.
 */
inline float ComputeDelayTimeSeconds(float time_knob, float time_cv)
{
    float combined = daisysp::fclamp(time_knob + time_cv, 0.0f, 1.0f);
    return daisysp::fmap(combined, kDelayTimeMinSeconds, kDelayTimeMaxSeconds, daisysp::Mapping::LOG);
}

/** Converts a base delay time (seconds) to a sample count, scaled by
 *  tape transport speed the same way pitch is (halving speed doubles
 *  the effective delay time) so echoes lengthen and slow as the tape
 *  brakes. Floors speed at tape_transport.h's kMinStopSpeed (same
 *  constant StopSemitones uses, avoiding a second near-zero-speed
 *  floor) before dividing, then clamps to the buffer's actual capacity
 *  -- near a full stop this pins the delay at its longest possible
 *  value rather than overflowing, reading as "tails freeze into one
 *  long echo" rather than any error condition.
 */
inline float ComputeDelaySamples(float base_seconds, float speed, float sample_rate)
{
    float base_samples = base_seconds * sample_rate;
    return daisysp::fclamp(base_samples / daisysp::fmax(speed, kMinStopSpeed),
                            0.0f, static_cast<float>(kTapeDelayMaxSamples - 1));
}

/** Applies a wow/flutter semitone offset (the same per-sample value
 *  TapeVoice already consumes for pitch) to a delay-sample count as a
 *  multiplicative ratio -- physically honest, since a real tape
 *  transport's speed wobble changes pitch and echo timing together
 *  from the same underlying variation. Matches tape_voice.h's
 *  ComputeModFreq ratio-from-semitones relation.
 */
inline float ApplyWobbleToDelaySamples(float delay_samples, float wobble_semitones)
{
    float ratio = powf(2.0f, wobble_semitones / 12.0f);
    return delay_samples * ratio;
}

using TapeDelayLine = daisysp::DelayLine<float, kTapeDelayMaxSamples>;

/** Single-channel tape-style delay: a DelayLine with a damped feedback
 *  loop. Its backing buffer must live in Aurora's external SDRAM
 *  (DSY_SDRAM_BSS) -- TapeVoice's DelayLines already use most of the
 *  128KB DTCMRAM budget -- but that attribute lives in a libDaisy
 *  header the host-side doctest build can't compile. So TapeDelay
 *  takes a pointer to caller-owned storage instead of embedding the
 *  DelayLine as a member, keeping this header on DaisySP-only includes
 *  and just as host-testable as TapeVoice/WowFlutter. main.cpp declares
 *  the actual SDRAM-placed buffers and owns their lifetime.
 */
class TapeDelay
{
  public:
    // delay_line: caller-owned storage (e.g. an SDRAM-placed global in
    // main.cpp, or a plain stack object in a host test -- TapeDelay
    // doesn't know or care which). Caller must keep it alive for as
    // long as this TapeDelay is used.
    void Init(TapeDelayLine *delay_line, float sample_rate)
    {
        delay_ = delay_line;
        delay_->Init();
        sr_ = sample_rate;
        feedback_lpf_.Init(0.0f);
        feedback_lpf_coeff_ = 1.0f / (kTapeDelayFeedbackLpfTauSeconds * sample_rate);
        target_samples_     = 0.0f;
    }

    // base_seconds: TIME knob+CV, already control-rate smoothed by the
    // caller. speed: TapeTransport::Speed(). Called once per audio block.
    void Update(float base_seconds, float speed)
    {
        target_samples_ = ComputeDelaySamples(base_seconds, speed, sr_);
    }

    // wobble_semitones: WowFlutter::Process's return value for this
    // sample -- same value TapeVoice already consumed for pitch this
    // sample. Must be called once per audio sample.
    float Process(float in, float wobble_semitones)
    {
        float samples = ApplyWobbleToDelaySamples(target_samples_, wobble_semitones);
        delay_->SetDelay(samples);

        float wet      = delay_->Read();
        float filtered = feedback_lpf_.Process(wet, feedback_lpf_coeff_);
        delay_->Write(in + filtered * kTapeDelayFeedback);

        return wet;
    }

  private:
    TapeDelayLine  *delay_ = nullptr;
    OnePoleSmoother feedback_lpf_; // audio-rate LPF, not a control smoother
    float           feedback_lpf_coeff_ = 0.0f;
    float           target_samples_      = 0.0f;
    float           sr_                  = 48000.0f;
};
} // namespace fluxcap
```

- [ ] **Step 4: Run it to verify it passes**

```bash
cd flux-capacitor/tests && make test_tape_delay && ./test_tape_delay
```

Expected: PASS — all 11 test cases / 18 assertions pass. (This exact header and test were verified together during planning: 11 test cases, 18 assertions, 0 failures.)

- [ ] **Step 5: Update `flux-capacitor/tests/Makefile`**

Add `test_tape_delay` to the `all` target's prerequisite list and run line, and add its build rule (no extra DaisySP source needed — `DelayLine` and `fonepole` are header-only, unlike `test_tape_voice`/`test_wow_flutter`):

```makefile
all: $(DOCTEST_H) test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter test_tape_delay
	./test_warp_control
	./test_tape_voice
	./test_dsp_util
	./test_mix_control
	./test_tape_transport
	./test_wow_flutter
	./test_tape_delay
```

```makefile
test_tape_delay: test_tape_delay.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<
```

```makefile
clean:
	rm -f test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter test_tape_delay $(DOCTEST_H)
```

- [ ] **Step 6: Run the full host test suite**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all seven test binaries build and pass.

- [ ] **Step 7: Commit**

```bash
git add flux-capacitor/tape_delay.h flux-capacitor/tests/test_tape_delay.cpp \
        flux-capacitor/tests/Makefile
git commit -m "flux-capacitor: add TapeDelay with speed/wow-flutter-coupled delay time"
```

---

### Task 2: Wire `TapeDelay` and TIME into `main.cpp`

**Files:**
- Modify: `flux-capacitor/main.cpp`

**Interfaces:**
- Consumes: `fluxcap::TapeDelay`, `fluxcap::TapeDelayLine`, `fluxcap::ComputeDelayTimeSeconds` (Task 1); `fluxcap::OnePoleSmoother` (existing, `dsp_util.h`); existing `fluxcap::TapeVoice`, `fluxcap::TapeTransport`, `fluxcap::WowFlutter`, `fluxcap::ComputeMix`/`ComputeMixGains`/`ComputeEffectiveMix`/`MixDryZoneCrossed`, `fluxcap::ComputeWarpSemitones`, `fluxcap::ComputeWarpLedLevels`/`ComputeWarpCenterGlow`/`ComputeWarpCenterGradientColor`, `fluxcap::ComputeWowRateHz`/`ComputeFlutterDepthSemitones`/`ComputeWowFlutterLedScale` (all unchanged); `hw.GetKnobValue(KNOB_TIME)`, `hw.GetCvValue(CV_TIME)`, `DSY_SDRAM_BSS` (Aurora SDK / libDaisy, both already transitively available via the existing `#include "aurora.h"`).

This task has no new pure logic to unit-test — it's hardware glue, verified by the firmware compiling and (separately, per the spec's hardware acceptance criteria) on the module itself. Each step is a self-contained edit followed by a compile check.

- [ ] **Step 1: Add the include, SDRAM-placed buffers, and `TapeDelay` globals**

In `flux-capacitor/main.cpp`, add the include alongside the others:

```cpp
#include "tape_delay.h"
```

Add these globals alongside the existing `TapeVoice voiceL, voiceR;` / `WowFlutter wowFlutter;` declarations:

```cpp
// SDRAM-placed backing storage for TapeDelay -- see tape_delay.h's
// class comment and the design spec's "Buffer sizing and memory
// placement". DSY_SDRAM_BSS is already available here transitively via
// aurora.h -> daisy_seed.h -> daisy.h -> dev/sdram.h; no new include
// is needed.
TapeDelayLine DSY_SDRAM_BSS delayLineL, delayLineR;
TapeDelay     delayL, delayR;
OnePoleSmoother timeSmoother;
float           timeSmoothCoeff = 0.0f;
```

- [ ] **Step 2: Compute TIME and update `TapeDelay` at control rate, in `AudioCallback`**

In `flux-capacitor/main.cpp`'s `AudioCallback`, add this block after `speed`/`wetAmp` are computed (needed by `delayL.Update`/`delayR.Update`) and before the per-sample `for` loop:

```cpp
    float rawDelaySeconds = ComputeDelayTimeSeconds(hw.GetKnobValue(KNOB_TIME), hw.GetCvValue(CV_TIME));
    float delaySeconds     = timeSmoother.Process(rawDelaySeconds, timeSmoothCoeff);

    delayL.Update(delaySeconds, speed);
    delayR.Update(delaySeconds, speed);
```

- [ ] **Step 3: Route `TapeDelay::Process` between `TapeVoice::Process` and the MIX blend**

Still in `AudioCallback`, inside the per-sample `for` loop, change:

```cpp
        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        out[0][i] = in[0][i] * dryGain + wetL * wetGain;
        out[1][i] = in[1][i] * dryGain + wetR * wetGain;
```

to:

```cpp
        float wetL = voiceL.Process(in[0][i], totalSemis) * wetAmp;
        float wetR = voiceR.Process(in[1][i], totalSemis) * wetAmp;

        wetL = delayL.Process(wetL, wobble);
        wetR = delayR.Process(wetR, wobble);

        out[0][i] = in[0][i] * dryGain + wetL * wetGain;
        out[1][i] = in[1][i] * dryGain + wetR * wetGain;
```

(`wobble` is the existing per-sample `float wobble = wowFlutter.Process(wowRateHz, flutterDepth);` computed earlier in this same loop — reused as-is, no recomputation.)

- [ ] **Step 4: Initialize the new smoother and `TapeDelay` instances in `main()`**

In `flux-capacitor/main.cpp`'s `main()`, add alongside the existing smoothing-time constants:

```cpp
    constexpr float kTimeSmoothTimeSeconds = 0.08f; // slower than the other 0.02f smoothers --
    // TIME's sweep is meant to audibly warble, not click; see the design spec.
```

and alongside the existing coefficient assignments:

```cpp
    timeSmoothCoeff = 1.0f / (kTimeSmoothTimeSeconds * hw.AudioCallbackRate());
```

and alongside the existing `voiceL.Init(...)` / `wowFlutter.Init(...)` calls:

```cpp
    delayL.Init(&delayLineL, hw.AudioSampleRate());
    delayR.Init(&delayLineR, hw.AudioSampleRate());
    timeSmoother.Init(0.0f);
```

- [ ] **Step 5: Update the file header comment**

At the top of `flux-capacitor/main.cpp`, extend the doc comment's phase description and spec list to mention Phase 4, following the existing pattern each prior phase used:

```cpp
 *  Phase 4 adds a stereo tape delay (KNOB_TIME/CV_TIME, log-mapped
 *  1ms-2s) between the pitch stage and the MIX blend: repeats darken
 *  via a fixed feedback lowpass, lengthen and slow as FREEZE brakes the
 *  tape, and wobble in sync with the same wow/flutter signal already
 *  applied to pitch.
```

and add to the `See` list:

```cpp
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase4-tape-delay-design.md
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

Expected: builds cleanly, producing `flux-capacitor/build/flux-capacitor-<version>.bin` with no errors — confirms `main.cpp`'s new include, `KNOB_TIME`/`CV_TIME` usage, the `DSY_SDRAM_BSS`-placed globals, and `tape_delay.h` all compile together against the real Aurora SDK (not just the host-side doctest stubs).

- [ ] **Step 8: Run the full host test suite one more time**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all seven test binaries build and pass.

- [ ] **Step 9: Commit**

```bash
git add flux-capacitor/main.cpp
git commit -m "flux-capacitor: wire TapeDelay and TIME control into main.cpp"
```

- [x] **Step 10: Hardware verification (manual, per the spec's acceptance criteria)** — passed 2026-08-17.

Flash to the module (see the `run-aurora` skill for mount/flash steps) and check:
- With TIME at minimum: a short, tight slapback echo. Sweeping TIME toward maximum smoothly lengthens the echo spacing up to a multi-second delay, log-curved.
- Turning TIME while audio is playing produces an audible warble/pitch sweep on the repeats, not a click or dropout.
- Repeats are audibly darker than the dry signal and decay to silence over a handful of repeats — no runaway buildup, no harsh aliasing/ringing.
- Engaging FREEZE audibly stretches and slows any currently-decaying echo tail, eventually freezing into one long sustained echo near a full stop, then returning to normal spacing as playback resumes.
- REFLECT/BLUR's wow/flutter is now audible on the echo repeats themselves, in sync with the direct pitch-shifted signal's own wobble (not drifting independently).
- WARP bend, tape-stop, and MIX dry/wet all continue to work normally and audibly compose with the new delay stage.
- No audible pop, glitch, or CPU-starvation artifact (dropouts, crackle).

This step has no automated pass/fail — record the outcome in conversation with the user rather than checking the box unattended.

---
