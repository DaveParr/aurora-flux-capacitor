# Hello Aurora: Knob Colour Control & Audio Passthrough — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add knob-driven LED colour control and MIX-knob audio passthrough to the hello-aurora starter project, with host-side unit tests for the pure logic modules.

**Architecture:** Two header-only pure-C++ modules (`colour.h`, `audio.h`) with zero Aurora/Daisy dependencies are tested on the host with doctest; `main.cpp` is the only file that touches the SDK and wires the two modules into the hardware loop.

**Tech Stack:** C++14, ARM GCC (embedded build), host g++ (tests), doctest (single-header, downloaded at test build time), libDaisy / Aurora SDK.

## Global Constraints

- All new source lives under `hello-aurora/`
- `colour.h` and `audio.h` must NOT include any Aurora or Daisy headers
- Tests compile with `g++` on the host — no ARM toolchain required
- Embedded build uses existing `hello-aurora/Makefile` unchanged
- Use `hw.GetButton()` — not `hw.GetSwitch()` (stale comment in aurora.h)
- Bottom LED red channel is silently dropped by the SDK — pass full RGB, accept the hardware behaviour

---

### Task 1: colour.h (TDD)

**Files:**
- Create: `hello-aurora/tests/Makefile`
- Create: `hello-aurora/tests/test_colour.cpp`
- Create: `hello-aurora/colour.h`

**Interfaces:**
- Produces:
  ```cpp
  struct Rgb { float r, g, b; };
  Rgb hsvToRgb(float h, float s, float v);
  ```
  `h` in `[0, 1]` — value 1.0 wraps to red. `s` and `v` are `[0, 1]`.

---

- [ ] **Step 1: Create `hello-aurora/tests/Makefile`**

```makefile
CXX      = g++
CXXFLAGS = -std=c++14 -Wall -I..

DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
DOCTEST_H   = doctest.h

all: $(DOCTEST_H) test_colour test_audio
	./test_colour
	./test_audio

$(DOCTEST_H):
	curl -sSL $(DOCTEST_URL) -o $@

test_colour: test_colour.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_audio: test_audio.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f test_colour test_audio $(DOCTEST_H)
```

- [ ] **Step 2: Write failing tests in `hello-aurora/tests/test_colour.cpp`**

```cpp
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
```

- [ ] **Step 3: Create stub `hello-aurora/colour.h` so tests compile but fail**

```cpp
#pragma once

struct Rgb { float r, g, b; };

inline Rgb hsvToRgb(float h, float s, float v)
{
    return {0.f, 0.f, 0.f};
}
```

- [ ] **Step 4: Run tests — verify they FAIL**

```bash
make -C hello-aurora/tests test_colour && ./hello-aurora/tests/test_colour
```

Expected output contains:
```
[doctest] test cases: 7 | 0 passed | 7 failed
```

- [ ] **Step 5: Implement `hsvToRgb` in `hello-aurora/colour.h`**

Replace the stub body with the full 6-sector HSV algorithm:

```cpp
#pragma once

struct Rgb { float r, g, b; };

inline Rgb hsvToRgb(float h, float s, float v)
{
    h = h - static_cast<int>(h);   // wrap to [0, 1)
    if (h < 0.f) h += 1.f;

    int   i = static_cast<int>(h * 6.f);
    float f = h * 6.f - i;
    float p = v * (1.f - s);
    float q = v * (1.f - f * s);
    float t = v * (1.f - (1.f - f) * s);

    switch (i % 6)
    {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        case 5: return {v, p, q};
    }
    return {0.f, 0.f, 0.f};
}
```

- [ ] **Step 6: Run tests — verify they PASS**

```bash
make -C hello-aurora/tests test_colour && ./hello-aurora/tests/test_colour
```

Expected:
```
[doctest] test cases: 7 | 7 passed | 0 failed | 0 skipped
```

- [ ] **Step 7: Commit**

```bash
git add hello-aurora/colour.h hello-aurora/tests/Makefile hello-aurora/tests/test_colour.cpp
git commit -m "feat: add colour.h with hsvToRgb and host-side tests"
```

---

### Task 2: audio.h (TDD)

**Files:**
- Create: `hello-aurora/tests/test_audio.cpp`
- Create: `hello-aurora/audio.h`

**Interfaces:**
- Consumes: nothing from prior tasks
- Produces:
  ```cpp
  struct StereoFrame { float left, right; };
  StereoFrame scaleVolume(StereoFrame in, float volume);
  ```

---

- [ ] **Step 1: Write failing tests in `hello-aurora/tests/test_audio.cpp`**

```cpp
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
```

- [ ] **Step 2: Create stub `hello-aurora/audio.h` so tests compile but fail**

```cpp
#pragma once

struct StereoFrame { float left, right; };

inline StereoFrame scaleVolume(StereoFrame in, float volume)
{
    return {0.f, 0.f};
}
```

- [ ] **Step 3: Run tests — verify they FAIL**

```bash
make -C hello-aurora/tests test_audio && ./hello-aurora/tests/test_audio
```

Expected output contains:
```
[doctest] test cases: 3 | 0 passed | 3 failed
```

- [ ] **Step 4: Implement `scaleVolume` in `hello-aurora/audio.h`**

```cpp
#pragma once

struct StereoFrame { float left, right; };

inline StereoFrame scaleVolume(StereoFrame in, float volume)
{
    return {in.left * volume, in.right * volume};
}
```

- [ ] **Step 5: Run tests — verify they PASS**

```bash
make -C hello-aurora/tests test_audio && ./hello-aurora/tests/test_audio
```

Expected:
```
[doctest] test cases: 3 | 3 passed | 0 failed | 0 skipped
```

- [ ] **Step 6: Run full test suite to confirm both modules still pass**

```bash
make -C hello-aurora/tests
```

Expected:
```
[doctest] test cases: 7 | 7 passed | 0 failed | 0 skipped
[doctest] test cases: 3 | 3 passed | 0 failed | 0 skipped
```

- [ ] **Step 7: Commit**

```bash
git add hello-aurora/audio.h hello-aurora/tests/test_audio.cpp
git commit -m "feat: add audio.h with scaleVolume and host-side tests"
```

---

### Task 3: Wire into main.cpp and verify embedded build

**Files:**
- Modify: `hello-aurora/main.cpp`

**Interfaces:**
- Consumes:
  ```cpp
  // from colour.h
  struct Rgb { float r, g, b; };
  Rgb hsvToRgb(float h, float s, float v);

  // from audio.h
  struct StereoFrame { float left, right; };
  StereoFrame scaleVolume(StereoFrame in, float volume);
  ```

**Knob → LED mapping:**

| Index | Knob | LED (numbered) | Bottom LED |
|-------|------|----------------|------------|
| 0 | `KNOB_TIME` | `LED_1` | `LED_BOT_1` |
| 1 | `KNOB_REFLECT` | `LED_2` | `LED_BOT_2` |
| 2 | `KNOB_MIX` | `LED_3` | `LED_BOT_3` |
| 3 | `KNOB_ATMOSPHERE` | `LED_4` | — |
| 4 | `KNOB_BLUR` | `LED_5` | — |
| 5 | `KNOB_WARP` | `LED_6` | — |

`KNOB_MIX` also drives audio volume. `LED_BOT_*` red is silently dropped by the SDK.

---

- [ ] **Step 1: Replace `hello-aurora/main.cpp` with the full wired implementation**

```cpp
/** hello-aurora
 *
 *  Knob colour control and audio passthrough.
 *  Each knob drives the hue of its paired LED via HSV.
 *  MIX knob also controls audio volume.
 *  FREEZE and REVERSE button LEDs light white when pressed.
 *  Bottom LEDs driven by first 3 knobs for physical position mapping.
 */
#include "aurora.h"
#include "colour.h"
#include "audio.h"

using namespace daisy;
using namespace aurora;

Hardware hw;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();
    float volume = hw.GetKnobValue(KNOB_MIX);
    for (size_t i = 0; i < size; i++)
    {
        StereoFrame frame = scaleVolume({in[0][i], in[1][i]}, volume);
        out[0][i] = frame.left;
        out[1][i] = frame.right;
    }
}

int main(void)
{
    hw.Init();
    hw.StartAudio(AudioCallback);

    const Leds knobLeds[KNOB_LAST] = {
        LED_1,           // KNOB_TIME
        LED_2,           // KNOB_REFLECT
        LED_3,           // KNOB_MIX
        LED_4,           // KNOB_ATMOSPHERE
        LED_5,           // KNOB_BLUR
        LED_6,           // KNOB_WARP
    };

    const Leds  botLeds[3]  = { LED_BOT_1, LED_BOT_2, LED_BOT_3 };
    const int   botKnobs[3] = { KNOB_TIME, KNOB_REFLECT, KNOB_MIX };

    while (1)
    {
        hw.ClearLeds();

        for (int i = 0; i < KNOB_LAST; i++)
        {
            Rgb c = hsvToRgb(hw.GetKnobValue(i), 1.f, 1.f);
            hw.SetLed(knobLeds[i], c.r, c.g, c.b);
        }

        for (int i = 0; i < 3; i++)
        {
            Rgb c = hsvToRgb(hw.GetKnobValue(botKnobs[i]), 1.f, 1.f);
            hw.SetLed(botLeds[i], c.r, c.g, c.b);
        }

        if (hw.GetButton(SW_FREEZE).Pressed())
            hw.SetLed(LED_FREEZE, 1.f, 1.f, 1.f);

        if (hw.GetButton(SW_REVERSE).Pressed())
            hw.SetLed(LED_REVERSE, 1.f, 1.f, 1.f);

        hw.WriteLeds();
    }
}
```

- [ ] **Step 2: Build the embedded firmware — verify it compiles**

```bash
make -C hello-aurora
```

Expected: build completes with no errors, ending with a line like:
```
arm-none-eabi-objcopy -O binary build/hello-aurora.elf build/hello-aurora.bin
```

- [ ] **Step 3: Commit**

```bash
git add hello-aurora/main.cpp
git commit -m "feat: wire knob colour control and audio passthrough into hello-aurora"
```

---

## Post-Flash Verification Checklist

After flashing `hello-aurora/build/hello-aurora.bin` to the module:

- [ ] Turn each of the 6 knobs — confirm LED colour changes across the rainbow
- [ ] Send audio in — confirm audio passes through
- [ ] Turn MIX knob down — confirm audio volume decreases
- [ ] Hold FREEZE button — confirm LED_FREEZE lights white
- [ ] Hold REVERSE button — confirm LED_REVERSE lights white
- [ ] Note which bottom LED responds to KNOB_TIME, KNOB_REFLECT, and KNOB_MIX — update `context.md` with findings
