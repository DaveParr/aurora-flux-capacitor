# Flux Capacitor — Phase 3: Wow and Flutter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add tape-style wow (slow sine pitch drift, REFLECT-controlled rate) and flutter (fast filtered-noise pitch jitter, BLUR-controlled depth) to the `flux-capacitor` firmware, summed into the existing WARP + tape-stop pitch pipeline, with the modulation amount also breathing the WARP bar-graph LEDs' brightness.

**Architecture:** One new header, `wow_flutter.h`, following the `tape_transport.h` precedent of bundling pure control-mapping functions with the stateful class that consumes their output in a single file. `WowFlutter` wraps a `daisysp::Oscillator` (wow) and a `daisysp::WhiteNoise` run through the existing `OnePoleSmoother` reused as an audio-rate lowpass (flutter), producing one combined semitone offset per sample that sums into `TapeVoice`'s existing `totalSemis` input alongside `warpSemis` and `StopSemitones(speed)`. No new signal path — a third term in a sum that already exists.

**Tech Stack:** C++14, DaisySP (`Utility/dsp.h`: `fclamp`, `fmap`, `Mapping::LOG`; `Synthesis/oscillator.h`: `Oscillator`; `Noise/whitenoise.h`: `WhiteNoise`), doctest for host-side tests, Aurora SDK (`aurora.h`) for hardware glue.

**Spec:** [`docs/superpowers/specs/2026-08-16-flux-capacitor-phase3-wow-flutter-design.md`](../specs/2026-08-16-flux-capacitor-phase3-wow-flutter-design.md)

## Global Constraints

- `namespace fluxcap` for the new header, matching every other file in this codebase.
- `#pragma once` + explicit `#include <cmath>` and `"Utility/dsp.h"` at the top, following `tape_transport.h`'s convention.
- `kWowRateMinHz = 0.1f`, `kWowRateMaxHz = 2.0f` — wow rate range, per the spec.
- `kWowDepthSemitones = 0.15f` — fixed, not knob-controlled.
- `kFlutterDepthMaxSemitones = 0.3f` — BLUR's full-scale depth (linear 0..max).
- `kFlutterLpfTauSeconds = 0.015f` — the flutter lowpass's *exponential time constant*, not a settle duration or a cutoff frequency directly. Its coefficient **must** be computed against `hw.AudioSampleRate()` (it runs per audio sample), never `hw.AudioCallbackRate()` (the control-rate smoothers' rate) — this codebase has hit exactly this mixup before with `TapeTransport`'s ramp constant.
- `kWowFlutterLedMaxSemitones = kWowDepthSemitones + kFlutterDepthMaxSemitones` (`0.45f`), `kWowFlutterLedMinBrightnessScale = 0.5f` — LED brightness breathes in `[0.5, 1.0]`, with `0.0f` combined semitones landing at the *midpoint* `0.75`, not `1.0` (verified by hand against `ComputeWowFlutterLedScale`'s formula — the spec's testing-plan text was corrected during self-review to match).
- `ComputeWowRateHz`/`ComputeFlutterDepthSemitones` both combine knob+CV additively via `fclamp(knob + cv, 0.0f, 1.0f)` **before** any curve mapping, same pattern as `ComputeMix`.
- `daisysp::Oscillator` defaults to `amp_ = 0.5` (see `Synthesis/oscillator.h`'s `Init`) — `WowFlutter::Init` **must** call `wow_osc_.SetAmp(1.0f)` so `kWowDepthSemitones` is the true peak semitone deviation the spec's math assumes (`wow_osc_.Process() * kWowDepthSemitones`), not half of it. This is not mentioned explicitly in the spec's code sketch; it's required for the sketch's math to be correct and is caught here in the plan.
- `daisysp::WhiteNoise::Init` already defaults `amp_ = 1.0f` — no equivalent fix needed there.
- No amplitude or delay-time modulation, no wow-depth or flutter-rate knob control, no `LED_FREEZE` involvement — all deferred per the spec's "Out of scope" section.
- Test files follow the existing pattern exactly: `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include "doctest.h"` + `#include "../wow_flutter.h"`, `using namespace fluxcap;`, `TEST_CASE`/`CHECK`/`doctest::Approx`.
- `daisysp::Oscillator::Process()` is defined in `Synthesis/oscillator.cpp` (out-of-line, unlike `WhiteNoise`, which is fully inline) — the host test binary must compile and link that `.cpp`, the same way `test_tape_voice` already links `Control/phasor.cpp`.

---

### Task 1: Wow/flutter DSP and control mapping (`wow_flutter.h`)

**Files:**
- Create: `flux-capacitor/wow_flutter.h`
- Create: `flux-capacitor/tests/test_wow_flutter.cpp`
- Modify: `flux-capacitor/tests/Makefile`

**Interfaces:**
- Consumes: `daisysp::fclamp`, `daisysp::fmap`, `daisysp::Mapping::LOG` (`Utility/dsp.h`); `daisysp::Oscillator` (`Synthesis/oscillator.h`); `daisysp::WhiteNoise` (`Noise/whitenoise.h`); `fluxcap::OnePoleSmoother` (`dsp_util.h`, already exists).
- Produces: `fluxcap::ComputeWowRateHz(float reflect_knob, float reflect_cv) -> float`; `fluxcap::ComputeFlutterDepthSemitones(float blur_knob, float blur_cv) -> float`; `fluxcap::ComputeWowFlutterLedScale(float combined_semitones) -> float`; `fluxcap::WowFlutter` — `void Init(float sample_rate)`, `float Process(float rate_hz, float flutter_depth_semitones)`, `float Value() const`. Task 2's `main.cpp` wiring calls all of these.

- [ ] **Step 1: Write the failing tests**

Create `flux-capacitor/tests/test_wow_flutter.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../wow_flutter.h"
#include <cmath>

using namespace fluxcap;

TEST_CASE("ComputeWowRateHz - knob endpoints map to the Hz range") {
    CHECK(ComputeWowRateHz(0.0f, 0.0f) == doctest::Approx(kWowRateMinHz));
    CHECK(ComputeWowRateHz(1.0f, 0.0f) == doctest::Approx(kWowRateMaxHz));
}

TEST_CASE("ComputeWowRateHz - combined knob+CV clamps to [0,1] before mapping") {
    CHECK(ComputeWowRateHz(0.9f, 0.5f) == doctest::Approx(kWowRateMaxHz)); // clamped high
    CHECK(ComputeWowRateHz(0.1f, -0.5f) == doctest::Approx(kWowRateMinHz)); // clamped low
}

TEST_CASE("ComputeWowRateHz - result always within [min, max]") {
    for (float knob = 0.0f; knob <= 1.0f; knob += 0.1f)
    {
        float hz = ComputeWowRateHz(knob, 0.0f);
        CHECK(hz >= kWowRateMinHz);
        CHECK(hz <= kWowRateMaxHz);
    }
}

TEST_CASE("ComputeFlutterDepthSemitones - knob endpoints") {
    CHECK(ComputeFlutterDepthSemitones(0.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(ComputeFlutterDepthSemitones(1.0f, 0.0f) == doctest::Approx(kFlutterDepthMaxSemitones));
}

TEST_CASE("ComputeFlutterDepthSemitones - additive knob+CV, clamped") {
    CHECK(ComputeFlutterDepthSemitones(0.5f, 0.2f) == doctest::Approx(0.7f * kFlutterDepthMaxSemitones));
    CHECK(ComputeFlutterDepthSemitones(0.9f, 0.5f) == doctest::Approx(kFlutterDepthMaxSemitones)); // clamped high
    CHECK(ComputeFlutterDepthSemitones(0.1f, -0.5f) == doctest::Approx(0.0f)); // clamped low
}

TEST_CASE("ComputeWowFlutterLedScale - rest (0) is the breathing midpoint, not full brightness") {
    CHECK(ComputeWowFlutterLedScale(0.0f) == doctest::Approx(0.75f));
}

TEST_CASE("ComputeWowFlutterLedScale - positive extreme is full brightness") {
    CHECK(ComputeWowFlutterLedScale(kWowFlutterLedMaxSemitones) == doctest::Approx(1.0f));
}

TEST_CASE("ComputeWowFlutterLedScale - negative extreme is the brightness floor") {
    CHECK(ComputeWowFlutterLedScale(-kWowFlutterLedMaxSemitones) == doctest::Approx(kWowFlutterLedMinBrightnessScale));
}

TEST_CASE("ComputeWowFlutterLedScale - values beyond the max clamp rather than overflow") {
    CHECK(ComputeWowFlutterLedScale(kWowFlutterLedMaxSemitones * 2.0f) == doctest::Approx(1.0f));
    CHECK(ComputeWowFlutterLedScale(-kWowFlutterLedMaxSemitones * 2.0f) == doctest::Approx(kWowFlutterLedMinBrightnessScale));
}

TEST_CASE("WowFlutter - flutter depth 0 isolates a pure wow sine at a known phase") {
    WowFlutter wf;
    const float sampleRate = 48000.0f;
    const float rateHz     = 1.0f;
    wf.Init(sampleRate);

    // Oscillator starts at phase_ == 0.0f (see Oscillator::Init), and
    // Process() reads sinf(phase_) *before* advancing phase -- so the
    // first sample is always sinf(0) == 0 regardless of rate.
    float first = wf.Process(rateHz, 0.0f);
    CHECK(first == doctest::Approx(0.0f));

    float phaseInc = (TWOPI_F * rateHz) / sampleRate;
    float expected = sinf(phaseInc) * kWowDepthSemitones;
    float second   = wf.Process(rateHz, 0.0f);
    CHECK(second == doctest::Approx(expected));
}

TEST_CASE("WowFlutter - Value reflects the last Process result") {
    WowFlutter wf;
    wf.Init(48000.0f);
    float result = wf.Process(1.0f, 0.1f);
    CHECK(wf.Value() == doctest::Approx(result));
}

TEST_CASE("WowFlutter - output stays bounded across many samples") {
    WowFlutter wf;
    wf.Init(48000.0f);
    for (int i = 0; i < 10000; i++)
    {
        float value = wf.Process(1.0f, kFlutterDepthMaxSemitones);
        CHECK(fabsf(value) <= (kWowDepthSemitones + kFlutterDepthMaxSemitones) + 0.01f);
    }
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd flux-capacitor/tests && make test_wow_flutter
```

Expected: FAIL — compile error, `../wow_flutter.h: No such file or directory`.

- [ ] **Step 3: Create `wow_flutter.h`**

Create `flux-capacitor/wow_flutter.h`:

```cpp
#pragma once
#include <cmath>
#include "Utility/dsp.h"
#include "Synthesis/oscillator.h"
#include "Noise/whitenoise.h"
#include "dsp_util.h"

namespace fluxcap
{
constexpr float kWowRateMinHz      = 0.1f;
constexpr float kWowRateMaxHz      = 2.0f;
constexpr float kWowDepthSemitones = 0.15f; // fixed; REFLECT controls rate only

constexpr float kFlutterDepthMaxSemitones = 0.3f; // BLUR's full-scale depth
constexpr float kFlutterLpfTauSeconds     = 0.015f; // ~10.6 Hz -3dB point; audio-rate coeff, not control-rate

constexpr float kWowFlutterLedMaxSemitones       = kWowDepthSemitones + kFlutterDepthMaxSemitones; // 0.45f
constexpr float kWowFlutterLedMinBrightnessScale = 0.5f;

/** Maps KNOB_REFLECT (0..1) + CV_REFLECT (additive, same pattern as
 *  WARP/MIX) to a wow LFO rate in Hz, log-curved so the knob feels
 *  musically even across its range rather than front-loaded.
 */
inline float ComputeWowRateHz(float reflect_knob, float reflect_cv)
{
    float combined = daisysp::fclamp(reflect_knob + reflect_cv, 0.0f, 1.0f);
    return daisysp::fmap(combined, kWowRateMinHz, kWowRateMaxHz, daisysp::Mapping::LOG);
}

/** Maps KNOB_BLUR (0..1) + CV_BLUR (additive) to a flutter modulation
 *  depth in semitones, linearly from 0 to kFlutterDepthMaxSemitones.
 */
inline float ComputeFlutterDepthSemitones(float blur_knob, float blur_cv)
{
    float combined = daisysp::fclamp(blur_knob + blur_cv, 0.0f, 1.0f);
    return combined * kFlutterDepthMaxSemitones;
}

/** Maps a combined wow+flutter semitone offset to a WARP bar-graph
 *  brightness multiplier in [kWowFlutterLedMinBrightnessScale, 1.0].
 *  The LEDs breathe symmetrically around the range's midpoint (0.75)
 *  as the wobble signal swings positive/negative -- they are not
 *  pinned to full brightness at rest (combined == 0).
 */
inline float ComputeWowFlutterLedScale(float combined_semitones)
{
    float normalized = daisysp::fclamp(
        combined_semitones / kWowFlutterLedMaxSemitones, -1.0f, 1.0f); // -1..1
    return daisysp::fmap(
        (normalized + 1.0f) * 0.5f, kWowFlutterLedMinBrightnessScale, 1.0f);
}

/** Combined tape wow (slow sine) + flutter (fast filtered noise)
 *  pitch modulation source, in semitones. Wow depth is fixed
 *  (kWowDepthSemitones); flutter depth is caller-controlled per call
 *  (from BLUR). Both rate_hz and flutter_depth_semitones are expected
 *  to already be control-rate smoothed by the caller.
 */
class WowFlutter
{
  public:
    void Init(float sample_rate)
    {
        wow_osc_.Init(sample_rate);
        wow_osc_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        wow_osc_.SetAmp(1.0f); // Oscillator defaults to amp_ = 0.5; full range needed
                                // so kWowDepthSemitones is the true peak deviation.
        flutter_noise_.Init();
        flutter_lpf_.Init(0.0f);
        flutter_lpf_coeff_ = 1.0f / (kFlutterLpfTauSeconds * sample_rate);
        last_value_        = 0.0f;
    }

    // Must be called once per audio sample -- it advances both the wow
    // oscillator's phase and the flutter noise/filter state.
    float Process(float rate_hz, float flutter_depth_semitones)
    {
        wow_osc_.SetFreq(rate_hz);
        float wow = wow_osc_.Process() * kWowDepthSemitones;

        float noise    = flutter_noise_.Process();
        float filtered = flutter_lpf_.Process(noise, flutter_lpf_coeff_);
        float flutter  = filtered * flutter_depth_semitones;

        last_value_ = wow + flutter;
        return last_value_;
    }

    // Last combined semitone offset, read from the main loop for LED display.
    float Value() const { return last_value_; }

  private:
    daisysp::Oscillator wow_osc_;
    daisysp::WhiteNoise  flutter_noise_;
    OnePoleSmoother       flutter_lpf_; // reused as an audio-rate LPF, not a control smoother
    float                 flutter_lpf_coeff_ = 0.0f;
    float                 last_value_        = 0.0f;
};
} // namespace fluxcap
```

- [ ] **Step 4: Run it to verify it passes**

```bash
cd flux-capacitor/tests && make test_wow_flutter && ./test_wow_flutter
```

Expected: PASS — all assertions pass.

- [ ] **Step 5: Update `flux-capacitor/tests/Makefile`**

Add an `OSCILLATOR_SRC` variable (parallel to the existing `DAISYSP_SRC` for `phasor.cpp`) and wire `test_wow_flutter` into `all`/`clean`:

```makefile
CXX      = g++
CXXFLAGS = -std=c++14 -Wall -I.. -I../../lib/Aurora-SDK/libs/DaisySP/Source -I../../lib/Aurora-SDK/libs/DaisySP/Source/Utility -I../../lib/Aurora-SDK/libs/DaisySP/Source/Control

DOCTEST_URL = https://raw.githubusercontent.com/doctest/doctest/master/doctest/doctest.h
DOCTEST_H   = doctest.h

DAISYSP_SRC    = ../../lib/Aurora-SDK/libs/DaisySP/Source/Control/phasor.cpp
OSCILLATOR_SRC = ../../lib/Aurora-SDK/libs/DaisySP/Source/Synthesis/oscillator.cpp

all: $(DOCTEST_H) test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter
	./test_warp_control
	./test_tape_voice
	./test_dsp_util
	./test_mix_control
	./test_tape_transport
	./test_wow_flutter

$(DOCTEST_H):
	curl -sSL $(DOCTEST_URL) -o $@

test_warp_control: test_warp_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_tape_voice: test_tape_voice.cpp $(DAISYSP_SRC) $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $< $(DAISYSP_SRC)

test_dsp_util: test_dsp_util.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_mix_control: test_mix_control.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_tape_transport: test_tape_transport.cpp $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_wow_flutter: test_wow_flutter.cpp $(OSCILLATOR_SRC) $(DOCTEST_H)
	$(CXX) $(CXXFLAGS) -o $@ $< $(OSCILLATOR_SRC)

clean:
	rm -f test_warp_control test_tape_voice test_dsp_util test_mix_control test_tape_transport test_wow_flutter $(DOCTEST_H)
```

- [ ] **Step 6: Run the full host test suite**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all six test binaries build and pass.

- [ ] **Step 7: Commit**

```bash
git add flux-capacitor/wow_flutter.h flux-capacitor/tests/test_wow_flutter.cpp \
        flux-capacitor/tests/Makefile
git commit -m "flux-capacitor: add WowFlutter wow/flutter modulation and REFLECT/BLUR mapping"
```

---

### Task 2: Wire wow/flutter into `main.cpp`

**Files:**
- Modify: `flux-capacitor/main.cpp`

**Interfaces:**
- Consumes: `fluxcap::WowFlutter`, `fluxcap::ComputeWowRateHz`, `fluxcap::ComputeFlutterDepthSemitones`, `fluxcap::ComputeWowFlutterLedScale` (Task 1); `fluxcap::OnePoleSmoother` (existing, `dsp_util.h`); existing `fluxcap::ComputeWarpSemitones`, `fluxcap::TapeVoice`, `fluxcap::TapeTransport`, `fluxcap::StopSemitones`, `fluxcap::StopAmplitude`, `fluxcap::ComputeMix`, `fluxcap::ComputeMixGains`, `fluxcap::ComputeWarpLedLevels` (all unchanged); `hw.GetKnobValue(KNOB_REFLECT)`, `hw.GetCvValue(CV_REFLECT)`, `hw.GetKnobValue(KNOB_BLUR)`, `hw.GetCvValue(CV_BLUR)` (Aurora SDK, `aurora.h`).

This task has no new pure logic to unit-test — it's hardware glue, verified by the firmware compiling and (separately, per the spec's hardware acceptance criteria) on the module itself.

- [ ] **Step 1: Replace `main.cpp`'s contents**

Replace the full contents of `flux-capacitor/main.cpp` with:

```cpp
/** flux-capacitor
 *
 *  Phase 3: REFLECT-controlled wow (slow sine pitch drift) and
 *  BLUR-controlled flutter (fast filtered-noise pitch jitter), summed
 *  into the WARP + tape-stop pitch pipeline. True stereo. LED_1-6 show
 *  the WARP pitch shift as a bar-graph, its brightness breathing with
 *  the wow/flutter amount; LED_FREEZE tracks the tape-stop fade only.
 *  See docs/superpowers/specs/2026-08-15-flux-capacitor-phase1-pitch-bend-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md,
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md, and
 *  docs/superpowers/specs/2026-08-16-flux-capacitor-phase3-wow-flutter-design.md
 */
#include "aurora.h"
#include "tape_voice.h"
#include "warp_control.h"
#include "tape_transport.h"
#include "mix_control.h"
#include "wow_flutter.h"
#include "dsp_util.h"

using namespace daisy;
using namespace aurora;
using namespace fluxcap;

Hardware        hw;
TapeVoice       voiceL, voiceR;
TapeTransport   transport;
WowFlutter      wowFlutter;
OnePoleSmoother warpSmoother;
OnePoleSmoother mixSmoother;
OnePoleSmoother reflectSmoother;
OnePoleSmoother blurSmoother;
float           warpSmoothCoeff    = 0.0f;
float           mixSmoothCoeff     = 0.0f;
float           stopRampCoeff      = 0.0f;
float           reflectSmoothCoeff = 0.0f;
float           blurSmoothCoeff    = 0.0f;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();

    float rawWarpSemis = ComputeWarpSemitones(hw.GetKnobValue(KNOB_WARP), hw.GetWarpVoct());
    float warpSemis     = warpSmoother.Process(rawWarpSemis, warpSmoothCoeff);

    bool freezeEdge = hw.GetButton(SW_FREEZE).RisingEdge();
    bool gateHigh   = hw.GetGateState(GATE_FREEZE);
    transport.Update(freezeEdge, gateHigh, stopRampCoeff);
    float speed = transport.Speed();
    float wetAmp = StopAmplitude(speed);

    float rawMix = ComputeMix(hw.GetKnobValue(KNOB_MIX), hw.GetCvValue(CV_MIX));
    float mix     = mixSmoother.Process(rawMix, mixSmoothCoeff);
    float dryGain, wetGain;
    ComputeMixGains(mix, &dryGain, &wetGain);

    float rawWowRateHz = ComputeWowRateHz(hw.GetKnobValue(KNOB_REFLECT), hw.GetCvValue(CV_REFLECT));
    float wowRateHz     = reflectSmoother.Process(rawWowRateHz, reflectSmoothCoeff);

    float rawFlutterDepth = ComputeFlutterDepthSemitones(hw.GetKnobValue(KNOB_BLUR), hw.GetCvValue(CV_BLUR));
    float flutterDepth     = blurSmoother.Process(rawFlutterDepth, blurSmoothCoeff);

    for (size_t i = 0; i < size; i++)
    {
        float wobble     = wowFlutter.Process(wowRateHz, flutterDepth);
        float totalSemis = warpSemis + StopSemitones(speed) + wobble;

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
    // (not pitch/amplitude glide -- TapeVoice's crossfade, TapeTransport's
    // own ramp, and WowFlutter's per-sample oscillator/filter handle those).
    constexpr float kWarpSmoothTimeSeconds    = 0.02f;
    constexpr float kMixSmoothTimeSeconds     = 0.02f;
    constexpr float kReflectSmoothTimeSeconds = 0.02f;
    constexpr float kBlurSmoothTimeSeconds    = 0.02f;
    // fonepole's "time" parameter is an exponential time constant (tau), not a
    // fixed ramp duration -- see dsp.h's fonepole doc comment. TapeTransport
    // snaps to its target once within kSnapEpsilon (0.001), which an
    // exponential decay reaches at t = tau * ln(1/kSnapEpsilon) = tau * ln(1000).
    // Solving for a ~1.5s settle time: tau = 1.5 / ln(1000) ~= 0.217s.
    constexpr float kStopRampTimeSeconds = 0.217f;
    warpSmoothCoeff    = 1.0f / (kWarpSmoothTimeSeconds * hw.AudioCallbackRate());
    mixSmoothCoeff     = 1.0f / (kMixSmoothTimeSeconds * hw.AudioCallbackRate());
    stopRampCoeff      = 1.0f / (kStopRampTimeSeconds * hw.AudioCallbackRate());
    reflectSmoothCoeff = 1.0f / (kReflectSmoothTimeSeconds * hw.AudioCallbackRate());
    blurSmoothCoeff    = 1.0f / (kBlurSmoothTimeSeconds * hw.AudioCallbackRate());

    voiceL.Init(hw.AudioSampleRate());
    voiceR.Init(hw.AudioSampleRate());
    transport.Init();
    wowFlutter.Init(hw.AudioSampleRate());
    warpSmoother.Init(0.0f);
    mixSmoother.Init(0.0f);
    reflectSmoother.Init(0.0f);
    blurSmoother.Init(0.0f);

    hw.StartAudio(AudioCallback);

    // WARP pitch-shift bar-graph: LED_4/5/6 light amber outward for an
    // upward shift, LED_3/2/1 light cyan outward for a downward shift,
    // brightness breathing with the wow/flutter modulation amount.
    // LED_FREEZE lights red, brightness tracking (1 - tape speed) only --
    // unaffected by wow/flutter, since it represents transport state, not
    // the pitch-shift display.
    // See docs/superpowers/specs/2026-08-16-flux-capacitor-warp-led-feedback-design.md,
    // docs/superpowers/specs/2026-08-16-flux-capacitor-phase2-tape-stop-design.md, and
    // docs/superpowers/specs/2026-08-16-flux-capacitor-phase3-wow-flutter-design.md
    constexpr float kWarpUpColor[3]   = {1.0f, 0.4f, 0.0f}; // amber
    constexpr float kWarpDownColor[3] = {0.0f, 0.6f, 1.0f}; // cyan
    const Leds      upLeds[3]         = {LED_4, LED_5, LED_6};
    const Leds      downLeds[3]       = {LED_3, LED_2, LED_1};

    while (1)
    {
        WarpLedLevels levels   = ComputeWarpLedLevels(warpSmoother.Value());
        float          ledScale = ComputeWowFlutterLedScale(wowFlutter.Value());
        hw.ClearLeds();
        for (int i = 0; i < 3; i++)
        {
            hw.SetLed(upLeds[i],
                      kWarpUpColor[0] * levels.up[i] * ledScale,
                      kWarpUpColor[1] * levels.up[i] * ledScale,
                      kWarpUpColor[2] * levels.up[i] * ledScale);
            hw.SetLed(downLeds[i],
                      kWarpDownColor[0] * levels.down[i] * ledScale,
                      kWarpDownColor[1] * levels.down[i] * ledScale,
                      kWarpDownColor[2] * levels.down[i] * ledScale);
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

Expected: builds cleanly, producing `flux-capacitor/build/flux-capacitor-<version>.bin` with no errors — confirms `main.cpp`'s new includes and `KNOB_REFLECT`/`CV_REFLECT`/`KNOB_BLUR`/`CV_BLUR` usage compile against the real Aurora SDK and that `libdaisysp.a` already contains `Oscillator`/`WhiteNoise` (not just the host-side doctest stubs, which link `oscillator.cpp` directly per Task 1).

- [ ] **Step 4: Run the full host test suite one more time**

```bash
cd flux-capacitor/tests && make clean && make
```

Expected: PASS — all six test binaries build and pass.

- [ ] **Step 5: Commit**

```bash
git add flux-capacitor/main.cpp
git commit -m "flux-capacitor: wire WowFlutter wow/flutter modulation into main.cpp"
```

- [ ] **Step 6: Hardware verification (manual, per the spec's acceptance criteria)**

Flash to the module (see `run-aurora` skill for mount/flash steps) and check:
- With REFLECT at minimum: a slow, smooth pitch drift (roughly 0.1 Hz) is audible even with BLUR at 0.
- Sweeping REFLECT from minimum to maximum audibly speeds up the wow cycle, log-curved (feels like even musical steps, not front-loaded).
- With BLUR at 0: no audible fast jitter, only wow. Raising BLUR adds an increasingly audible fast, irregular (not periodic-sounding) flutter on top.
- WARP bend and tape-stop still work normally and audibly compose with wow/flutter (e.g. a WARP offset plus wow/flutter still center-wobbles around the bent pitch, not the unbent one).
- LED_1-6 visibly breathe/flicker in brightness in sync with the audible wobble whenever a bar-graph LED is lit (WARP off-center); `LED_FREEZE`'s brightness continues to track `1 - speed` only, with no visible wow/flutter flicker.
- No audible pop, glitch, or CPU-starvation artifact (dropouts, crackle) — this phase makes `TapeVoice`'s internal same-value short-circuit stop firing every sample, which is expected to be within the H750's budget but is worth confirming by ear.

This step has no automated pass/fail — record the outcome in conversation with the user rather than checking the box unattended.

---
