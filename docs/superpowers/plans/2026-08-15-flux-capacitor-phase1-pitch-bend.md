# Flux Capacitor Phase 1: Base Pitch Bend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement WARP knob/CV pitch bend for the Aurora, with truly continuous (not semitone-stepped) pitch tracking, as a new `flux-capacitor/` firmware project.

**Architecture:** A `TapeVoice` class (one instance per audio channel) adapts DaisySP's `PitchShifter` mechanism — two `DelayLine` taps crossfaded via `Phasor`-driven LFOs, so each tap periodically resets to zero delay while the other is audible (glitch-free, indefinite pitch shift) — but computes the crossfade rate from a continuous semitone value instead of `PitchShifter`'s quantized integer-semitone lookup table. A separate `WarpControl` module maps `KNOB_WARP` + `CV_WARP` (via `hw.GetWarpVoct()`) to that semitone value, with one-pole smoothing to damp ADC/knob jitter. `main.cpp` wires these into `AudioCallback` for true stereo processing.

**Tech Stack:** C++14, DaisySP (`DelayLine`, `Phasor`, `dsp.h` utilities), Aurora SDK (`aurora.h`), doctest for host-side unit tests, ARM GCC (`gcc-arm-none-eabi`) for the device build.

**Spec:** [docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md](../specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md)

## Global Constraints

- Sample rate: 48 kHz nominal (Aurora hardware default).
- KNOB_WARP maps to a fixed ±12 semitone range for Phase 1 — no Shift+knob secondary pitch-bend-range page (that's Phase 5).
- True stereo: two independent `TapeVoice` instances (`voiceL`, `voiceR`), not mono-summed.
- Pure logic (control mapping, pitch-shift math) lives in host-testable headers, separate from hardware glue in `main.cpp` — same pattern as `hello-aurora`'s `colour.h`/`audio.h`.
- Out of scope: FREEZE/tape-stop transport, wow/flutter, tape delay, saturation/filtering (ATMOSPHERE), REVERSE mode, Shift+knob secondary pages. Do not add stubs or hooks for these — they get their own specs later.
- New project directory: `flux-capacitor/`, following `hello-aurora/`'s structure (`main.cpp`, `Makefile` including `../config.mk`, `tests/` with a doctest suite built via plain `g++`).
- Design refinement beyond the spec (confirmed with the user during planning): at `semitones == 0` (WARP centered), `TapeVoice::Process` must blend to a clean dry passthrough rather than locking onto a fixed ~170ms delay tap (DaisySP's `PitchShifter` does this at `transpose == 0`, since its crossfade phasors freeze when the mod frequency is zero). The blend crosses over linearly between 0 and ±1 semitone.

---

## Task 1: Submodule setup + WARP control mapping (`warp_control.h`)

**Files:**
- Create: `flux-capacitor/warp_control.h`
- Create: `flux-capacitor/tests/Makefile`
- Create: `flux-capacitor/tests/test_warp_control.cpp`

**Interfaces:**
- Produces (used by Task 3, `main.cpp`):
  - `namespace fluxcap { constexpr float kWarpKnobRangeSemitones = 12.0f; }`
  - `float fluxcap::ComputeWarpSemitones(float knobValue, float warpVoctSemitones)` — pure function, no smoothing. `knobValue` is 0..1 (from `hw.GetKnobValue(KNOB_WARP)`), `warpVoctSemitones` is a continuous semitone offset (from `hw.GetWarpVoct()`). Returns their sum, with `knobValue` mapped from 0..1 to ±`kWarpKnobRangeSemitones` (knob=0.5 → 0 semitones).
  - `class fluxcap::WarpSmoother` — stateful one-pole smoother.
    - `void Init(float initial = 0.0f)`
    - `float Process(float target, float coeff)` — applies one `fonepole` step toward `target`, returns the new smoothed value.
    - `float Value() const`

- [ ] **Step 1: Initialize the Aurora-SDK's nested submodules**

The Aurora-SDK submodule's own `libDaisy`/`DaisySP` submodules aren't checked out yet in this repo (they're present in a sibling `aurora` repo but not here). `Utility/dsp.h`, needed for `fmap`/`fonepole`/`fclamp`, lives in the `DaisySP` submodule.

Run: `git submodule update --init --recursive`

Expected: `lib/Aurora-SDK/libs/DaisySP/Source/Utility/dsp.h` and `lib/Aurora-SDK/libs/libDaisy/` now contain real files (not empty directories). Verify with:

```bash
test -f lib/Aurora-SDK/libs/DaisySP/Source/Utility/dsp.h && echo OK
```

- [ ] **Step 2: Create the flux-capacitor project directory and write the failing test**

Create `flux-capacitor/tests/test_warp_control.cpp`:

```cpp
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

Create `flux-capacitor/tests/Makefile`:

```makefile
CXX      = g++
CXXFLAGS = -std=c++14 -Wall -I.. -I../../lib/Aurora-SDK/libs/DaisySP/Source

DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
DOCTEST_H   = doctest.h

all: $(DOCTEST_H) test_warp_control
	./test_warp_control

$(DOCTEST_H):
	curl -sSL $(DOCTEST_URL) -o $@

test_warp_control: test_warp_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f test_warp_control $(DOCTEST_H)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd flux-capacitor/tests && make`
Expected: FAIL — compile error, `../warp_control.h` does not exist yet.

- [ ] **Step 4: Write `warp_control.h`**

Create `flux-capacitor/warp_control.h`:

```cpp
#pragma once
#include "Utility/dsp.h"

namespace fluxcap
{
constexpr float kWarpKnobRangeSemitones = 12.0f;

/** Maps KNOB_WARP (0..1, center = 0.5) + a continuous CV semitone offset
 *  (e.g. from hw.GetWarpVoct()) to a combined, unsmoothed semitone value.
 */
inline float ComputeWarpSemitones(float knobValue, float warpVoctSemitones)
{
    float knobSemis = daisysp::fmap(
        knobValue, -kWarpKnobRangeSemitones, kWarpKnobRangeSemitones);
    return knobSemis + warpVoctSemitones;
}

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
} // namespace fluxcap
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd flux-capacitor/tests && make`
Expected: PASS — all `test_warp_control` cases succeed.

- [ ] **Step 6: Commit**

Note: `git submodule update --init --recursive` only populates `lib/Aurora-SDK`'s own nested submodules (`libDaisy`, `DaisySP`) in its working tree — it does not change this repo's tracked gitlink for `lib/Aurora-SDK` (still pinned at its existing commit), so there's nothing new to stage from that step.

```bash
git add flux-capacitor/warp_control.h flux-capacitor/tests/Makefile flux-capacitor/tests/test_warp_control.cpp
git commit -m "flux-capacitor: add WARP knob/CV semitone mapping with tests"
```

---

## Task 2: Continuous-pitch tape voice (`tape_voice.h`)

**Files:**
- Create: `flux-capacitor/tape_voice.h`
- Create: `flux-capacitor/tests/test_tape_voice.cpp`
- Modify: `flux-capacitor/tests/Makefile`

**Interfaces:**
- Consumes: nothing from Task 1 (independent of `warp_control.h`).
- Produces (used by Task 3, `main.cpp`):
  - `namespace fluxcap { constexpr size_t kTapeVoiceBufferSize = 16384; }`
  - `namespace fluxcap { constexpr float kDryBlendRangeSemitones = 1.0f; }`
  - `float fluxcap::ComputeModFreq(float semitones, float sampleRate, size_t bufferSize)` — pure function; crossfade rate in Hz for the given semitone offset (symmetric in sign, always ≥ 0).
  - `float fluxcap::ComputeDryMix(float semitones)` — pure function; 1.0 at `semitones == 0`, linearly falling to 0.0 by `±kDryBlendRangeSemitones`.
  - `class fluxcap::TapeVoice`
    - `void Init(float sample_rate)`
    - `float Process(float in, float semitones)` — one call per audio sample. Returns the processed (or dry-blended) sample.

- [ ] **Step 1: Write the failing tests**

Create `flux-capacitor/tests/test_tape_voice.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../tape_voice.h"

using namespace fluxcap;

TEST_CASE("ComputeModFreq - zero semitones -> zero mod freq") {
    CHECK(ComputeModFreq(0.0f, 48000.0f, kTapeVoiceBufferSize) == doctest::Approx(0.0f));
}

TEST_CASE("ComputeModFreq - symmetric for positive and negative semitones") {
    float up   = ComputeModFreq(12.0f, 48000.0f, kTapeVoiceBufferSize);
    float down = ComputeModFreq(-12.0f, 48000.0f, kTapeVoiceBufferSize);
    CHECK(up == doctest::Approx(down));
    CHECK(up > 0.0f);
}

TEST_CASE("ComputeModFreq - matches the octave-up formula") {
    // one octave up: ratio = 2.0, mod_freq = (ratio - 1.0) * sr / bufferSize
    float expected = (2.0f - 1.0f) * 48000.0f / static_cast<float>(kTapeVoiceBufferSize);
    CHECK(ComputeModFreq(12.0f, 48000.0f, kTapeVoiceBufferSize) == doctest::Approx(expected));
}

TEST_CASE("ComputeDryMix - fully dry at zero semitones") {
    CHECK(ComputeDryMix(0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("ComputeDryMix - fully wet at or beyond the blend range") {
    CHECK(ComputeDryMix(1.0f) == doctest::Approx(0.0f));
    CHECK(ComputeDryMix(-1.0f) == doctest::Approx(0.0f));
    CHECK(ComputeDryMix(5.0f) == doctest::Approx(0.0f));
}

TEST_CASE("ComputeDryMix - halfway through the blend range") {
    CHECK(ComputeDryMix(0.5f) == doctest::Approx(0.5f));
    CHECK(ComputeDryMix(-0.5f) == doctest::Approx(0.5f));
}

TEST_CASE("TapeVoice - exact dry passthrough at zero semitones") {
    TapeVoice voice;
    voice.Init(48000.0f);
    for (int i = 0; i < 100; i++) {
        float in  = 0.37f * static_cast<float>((i % 7) - 3);
        float out = voice.Process(in, 0.0f);
        CHECK(out == doctest::Approx(in));
    }
}

TEST_CASE("TapeVoice - does not crash or produce NaN/Inf when pitch-shifting") {
    TapeVoice voice;
    voice.Init(48000.0f);
    for (int i = 0; i < 2000; i++) {
        float in  = 0.5f * static_cast<float>((i % 13) - 6);
        float out = voice.Process(in, 7.0f);
        CHECK(std::isfinite(out));
    }
}
```

Update `flux-capacitor/tests/Makefile` to add the `test_tape_voice` target, which needs `Control/phasor.cpp` linked in (`Phasor::Process`/`SetFreq` are defined out-of-line):

```makefile
CXX      = g++
CXXFLAGS = -std=c++14 -Wall -I.. -I../../lib/Aurora-SDK/libs/DaisySP/Source

DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
DOCTEST_H   = doctest.h

DAISYSP_SRC = ../../lib/Aurora-SDK/libs/DaisySP/Source/Control/phasor.cpp

all: $(DOCTEST_H) test_warp_control test_tape_voice
	./test_warp_control
	./test_tape_voice

$(DOCTEST_H):
	curl -sSL $(DOCTEST_URL) -o $@

test_warp_control: test_warp_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_tape_voice: test_tape_voice.cpp $(DAISYSP_SRC) $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DAISYSP_SRC)

clean:
	rm -f test_warp_control test_tape_voice $(DOCTEST_H)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd flux-capacitor/tests && make`
Expected: FAIL — compile error, `../tape_voice.h` does not exist yet.

- [ ] **Step 3: Write `tape_voice.h`**

Create `flux-capacitor/tape_voice.h`:

```cpp
#pragma once
#include <cmath>
#include <cstddef>
#include "Utility/dsp.h"
#include "Utility/delayline.h"
#include "Control/phasor.h"

namespace fluxcap
{
// Matches DaisySP's PitchShifter SHIFT_BUFFER_SIZE -- TapeVoice is adapted
// from that mechanism (two crossfaded delay taps), driven by a continuous
// semitone value instead of a quantized lookup table.
constexpr size_t kTapeVoiceBufferSize = 16384;

// Semitone offset (either direction) over which Process blends from a
// clean dry passthrough (at 0) to full crossfaded tape-voice processing
// (at or beyond this threshold). Without this blend, semitones == 0 would
// lock the crossfade onto a single fixed ~170ms delay tap instead of
// passing audio through cleanly -- the same characteristic DaisySP's
// PitchShifter has at transpose == 0, since its crossfade phasors freeze
// when the mod frequency is zero.
constexpr float kDryBlendRangeSemitones = 1.0f;

/** Crossfade rate (Hz) for the two delay taps, given a semitone offset.
 *  Always >= 0; direction (which tap sweeps toward/away from zero delay)
 *  is handled separately by TapeVoice via the sign of semitones.
 */
inline float ComputeModFreq(float semitones, float sampleRate, size_t bufferSize)
{
    float ratio = powf(2.0f, fabsf(semitones) / 12.0f); // always >= 1.0
    return ((ratio - 1.0f) * sampleRate) / static_cast<float>(bufferSize);
}

/** Dry/wet blend weight for the dry signal: 1.0 at semitones == 0, falling
 *  linearly to 0.0 by +/-kDryBlendRangeSemitones.
 */
inline float ComputeDryMix(float semitones)
{
    return daisysp::fclamp(
        1.0f - (fabsf(semitones) / kDryBlendRangeSemitones), 0.0f, 1.0f);
}

/** Continuous-pitch tape-style voice.
 *
 *  Adapted from DaisySP's PitchShifter: two overlapping DelayLine taps,
 *  crossfaded via Phasor-driven LFOs so each tap periodically resets to
 *  zero delay while the other is audible (glitch-free, indefinite pitch
 *  shift). Unlike PitchShifter, the crossfade rate is computed directly
 *  from a continuous semitone value instead of a quantized lookup table,
 *  so pitch tracks smoothly rather than stepping. Blends to a dry
 *  passthrough near semitones == 0 (see kDryBlendRangeSemitones).
 */
class TapeVoice
{
  public:
    void Init(float sample_rate)
    {
        sr_             = sample_rate;
        last_semitones_ = 0.0f;
        mod_freq_       = 0.0f;
        shift_up_       = true;
        for (int i = 0; i < 2; i++)
        {
            gain_[i] = 0.0f;
            delay_[i].Init();
            phasor_[i].Init(sr_, 0.0f, i == 0 ? 0.0f : PI_F);
        }
    }

    float Process(float in, float semitones)
    {
        UpdateSemitones(semitones);

        float fade1 = phasor_[0].Process();
        float fade2 = phasor_[1].Process();

        if (shift_up_)
        {
            fade1 = 1.0f - fade1;
            fade2 = 1.0f - fade2;
        }

        float mod0 = fade1 * static_cast<float>(kTapeVoiceBufferSize - 1);
        float mod1 = fade2 * static_cast<float>(kTapeVoiceBufferSize - 1);

        gain_[0] = sinf(fade1 * PI_F);
        gain_[1] = sinf(fade2 * PI_F);

        delay_[0].Write(in);
        delay_[1].Write(in);
        delay_[0].SetDelay(mod0);
        delay_[1].SetDelay(mod1);

        float wet = (delay_[0].Read() * gain_[0]) + (delay_[1].Read() * gain_[1]);

        float dryMix = ComputeDryMix(semitones);
        return in * dryMix + wet * (1.0f - dryMix);
    }

  private:
    void UpdateSemitones(float semitones)
    {
        if (semitones == last_semitones_)
            return;
        last_semitones_ = semitones;

        shift_up_ = semitones > 0.0f;
        mod_freq_ = ComputeModFreq(semitones, sr_, kTapeVoiceBufferSize);

        phasor_[0].SetFreq(mod_freq_);
        phasor_[1].SetFreq(mod_freq_);
    }

    daisysp::DelayLine<float, kTapeVoiceBufferSize> delay_[2];
    daisysp::Phasor                                 phasor_[2];
    float                                            gain_[2] = {0.0f, 0.0f};
    float                                            sr_ = 48000.0f;
    float                                             mod_freq_ = 0.0f;
    float                                             last_semitones_ = 0.0f;
    bool                                               shift_up_ = true;
};
} // namespace fluxcap
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd flux-capacitor/tests && make`
Expected: PASS — all `test_warp_control` and `test_tape_voice` cases succeed.

- [ ] **Step 5: Commit**

```bash
git add flux-capacitor/tape_voice.h flux-capacitor/tests/Makefile flux-capacitor/tests/test_tape_voice.cpp
git commit -m "flux-capacitor: add continuous-pitch TapeVoice with tests"
```

---

## Task 3: Hardware glue (`main.cpp`) and device build

**Files:**
- Create: `flux-capacitor/main.cpp`
- Create: `flux-capacitor/Makefile`

**Interfaces:**
- Consumes:
  - `fluxcap::ComputeWarpSemitones(float, float) -> float` (Task 1, `warp_control.h`)
  - `fluxcap::WarpSmoother` — `Init(float)`, `Process(float, float) -> float` (Task 1, `warp_control.h`)
  - `fluxcap::TapeVoice` — `Init(float)`, `Process(float, float) -> float` (Task 2, `tape_voice.h`)
  - `aurora::Hardware` — `hw.Init()`, `hw.StartAudio(cb)`, `hw.ProcessAllControls()`, `hw.GetKnobValue(int)`, `hw.GetWarpVoct()`, `hw.AudioCallbackRate()`, `hw.AudioSampleRate()`, `aurora::KNOB_WARP` (all from `lib/Aurora-SDK/include/aurora.h`, already used by `hello-aurora/main.cpp`)
- Produces: `flux-capacitor.bin` via `make build PROJECT=flux-capacitor` (no further Phase 1 tasks depend on this; it's the deliverable).

This task's deliverable is hardware, so there's no host-side unit test — verification is a successful device build plus manual hardware confirmation.

- [ ] **Step 1: Write `main.cpp`**

Create `flux-capacitor/main.cpp`:

```cpp
/** flux-capacitor
 *
 *  Phase 1: WARP knob/CV pitch bend via a continuous, glitch-free tape
 *  voice pitch engine (no semitone stepping). True stereo.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md
 */
#include "aurora.h"
#include "tape_voice.h"
#include "warp_control.h"

using namespace daisy;
using namespace aurora;
using namespace fluxcap;

Hardware     hw;
TapeVoice    voiceL, voiceR;
WarpSmoother warpSmoother;
float        warpSmoothCoeff = 0.0f;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();

    float rawSemitones = ComputeWarpSemitones(hw.GetKnobValue(KNOB_WARP), hw.GetWarpVoct());
    float semitones     = warpSmoother.Process(rawSemitones, warpSmoothCoeff);

    for (size_t i = 0; i < size; i++)
    {
        out[0][i] = voiceL.Process(in[0][i], semitones);
        out[1][i] = voiceR.Process(in[1][i], semitones);
    }
}

int main(void)
{
    hw.Init();

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

- [ ] **Step 2: Write `Makefile`**

Create `flux-capacitor/Makefile` (mirrors `hello-aurora/Makefile`):

```makefile
include ../config.mk

C_DEFS += -DFIRMWARE_VERSION=\"$(FIRMWARE_VERSION)\"

# Project Name
TARGET = flux-capacitor

# Build Project for Daisy Bootloader
APP_TYPE = BOOT_SRAM

# Sources
CPP_SOURCES = main.cpp

# Aurora BSP header
C_INCLUDES += -I$(AURORA_SDK_DIR)/include/

# Library Locations
LIBDAISY_DIR ?= $(AURORA_SDK_DIR)/libs/libDaisy
DAISYSP_DIR  ?= $(AURORA_SDK_DIR)/libs/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
```

- [ ] **Step 3: Build libDaisy and DaisySP (one-time, if not already built)**

This requires the ARM toolchain (`gcc-arm-none-eabi`) to be installed per the repo README. If it's not available in this environment, stop here and hand this step to the user rather than guessing around a missing toolchain.

Run (from repo root): `make libdaisy`
Expected: builds `lib/Aurora-SDK/libs/libDaisy/build/libdaisy.a` and `lib/Aurora-SDK/libs/DaisySP/build/libdaisysp.a` without error.

- [ ] **Step 4: Build the firmware**

Run (from repo root): `make build PROJECT=flux-capacitor`
Expected: builds `flux-capacitor/build/flux-capacitor-dev.bin` (or `flux-capacitor-<version>.bin`) without error.

- [ ] **Step 5: Re-run the full host test suite**

Run: `cd flux-capacitor/tests && make`
Expected: PASS — confirms Task 1/2 changes weren't disturbed while adding the device-only `main.cpp`.

- [ ] **Step 6: Commit**

```bash
git add flux-capacitor/main.cpp flux-capacitor/Makefile
git commit -m "flux-capacitor: add hardware glue and device build for WARP pitch bend"
```

- [ ] **Step 7: Manual hardware verification (human-required)**

This step needs physical hardware and can't be automated:

1. Flash: `make flash PROJECT=flux-capacitor MOUNT=/media/YOUR_USER/YOUR_DRIVE` (adjust `MOUNT`), then follow the repo README's eject/insert/power-cycle steps.
2. Patch a slow LFO or manual CV sweep into `CV_WARP`. Confirm pitch glides continuously with no audible stepping.
3. Turn `KNOB_WARP` with no CV patched. Confirm the center pitch shifts as expected, and that centering the knob (no CV) gives a clean, effectively-dry sound rather than an audible fixed echo.
4. Confirm no zipper noise from knob/CV jitter at a static setting.

If any of these fail, that's new information for a follow-up fix — not a sign this plan's tasks were done wrong, since this step is exploratory by nature (first time this DSP runs on real hardware/ADCs).
