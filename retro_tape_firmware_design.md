# Retro Tape Effects Firmware for Qu-Bit Aurora

## Overview

This document describes a high-level design for a "retro tape" effects firmware for the Qu-Bit Aurora Eurorack module. The firmware implements pitch bending, tape stop, tape wow and flutter, and tape-style delay using DaisySP modules and the Aurora SDK.

## Target Hardware

- **Platform**: Qu-Bit Aurora
- **DSP Library**: DaisySP
- **SDK**: Aurora SDK
- **Audio**: True stereo input/output
- **Sample Rate**: 48 kHz (typical)

## Features

1. **Pitch Bending via WARP CV**
   - WARP CV input provides 1 V/oct pitch control (typical range: ±12 semitones or more)
   - WARP knob provides coarse pitch offset / tape speed trim
   - Pitch is tied to tape speed model for authentic tape behaviour

2. **Tape Stop Effect**
   - FREEZE button/gate triggers tape stop (speed ramps from 1.0 to 0.0)
   - Stop rate is adjustable (optionally via WARP CV level or Shift+TIME)
   - Pitch and delay time fall together as tape slows
   - Optional amplitude envelope and lowpass filtering during stop

3. **Tape Wow and Flutter**
   - **Wow**: Slow periodic speed modulation (LFO-based, 0.1–2 Hz typical)
   - **Flutter**: Fast random/jittery speed modulation (noise-based)
   - REFLECT knob/CV controls wow rate
   - BLUR knob/CV controls flutter depth
   - Modulation affects both pitch and delay time for authentic tape character

4. **Tape-Style Delay**
   - Variable delay time (TIME knob/CV, typical range: 1 ms to 2+ seconds)
   - Feedback path with lowpass filtering and optional saturation
   - Delay time modulated by wow/flutter for unstable, vintage character
   - Delay write/read speed tied to tape speed model (delay tails slow during stop)

## Control Mapping

### Knobs (0–1 range via `GetKnobValue()`)

| Knob | Primary Function | Secondary Function (Shift+) |
|------|------------------|-----------------------------|
| KNOB_WARP | Coarse pitch offset / tape speed trim | Pitch bend range (e.g., ±1 / ±2 / ±3 octaves) |
| KNOB_TIME | Delay time (nominal) | Stop rate curve (linear vs exponential) |
| KNOB_BLUR | Flutter depth | Flutter type (noise vs multi-sine) |
| KNOB_REFLECT | Wow rate (LFO frequency) | Wow waveform (sine, triangle, complex) |
| KNOB_MIX | Dry/wet blend | Input gain / trim |
| KNOB_ATMOSPHERE | Tape coloration (brightness/saturation) | Saturation amount |

### CV Inputs (−5V to +5V via `GetCvValue()`)

| CV Input | Function |
|----------|----------|
| CV_WARP | Primary pitch bend (1 V/oct) |
| CV_TIME | Delay time modulation |
| CV_BLUR | Flutter depth modulation |
| CV_REFLECT | Wow rate modulation |
| CV_MIX | Dry/wet modulation |
| CV_ATMOSPHERE | Tape tone/saturation modulation |

### Buttons and Gates

| Control | Function |
|---------|----------|
| BUTTON_FREEZE / GATE_FREEZE | Tape stop/start trigger (toggle or gate-controlled) |
| BUTTON_REVERSE / GATE_REVERSE | Reverse tape playback mode |
| BUTTON_SHIFT | Secondary parameter page access (Shift+knob combinations) |

### Suggested Button Behaviour

- **FREEZE (default)**: Toggle PLAY ↔ STOPPING/STOPPED
- **FREEZE (gate)**: High = STOPPING/STOPPED, Low = PLAY
- **REVERSE**: Toggle reverse playback (affects delay read direction)
- **SHIFT + FREEZE**: Select stop mode (full stop to 0 vs slow-down without full stop)
- **SHIFT + knobs**: Access secondary parameter pages (see table above)

## DSP Architecture

### Signal Flow

```
Input → [Pitch Shifter] → [Tape Delay] → [Output Filter/Saturation] → Output
            ↑                    ↑
        (speed + bend)      (speed + wow/flutter)
```

### Core Modules (DaisySP)

- **`PitchShifter`**: Time-domain pitch shifting with semitone transposition and internal flutter modulation [cite:web:9][cite:web:20]
- **`DelayLine<float, N>`**: Fractional delay line with interpolation for tape delay [cite:web:6]
- **`Lfo`**: Wow modulation (slow sine/triangle LFO)
- **`Noise`** or multi-LFO: Flutter modulation (fast random jitter)
- **Utility DSP**: `fmap`, one-pole filters, smoothing functions from `dsp.h` [cite:web:17]

### Tape Speed Model

A global `speed` variable (nominal = 1.0) drives:

- Pitch shift amount (via `PitchShifter::SetTranspose()` or ratio conversion)
- Delay write/read rate (effective delay time scales with speed)
- Optional amplitude envelope and filter cutoff (tape loses HF as it slows)

**State Machine:**

```
PLAY → STOPPING → STOPPED → STARTING → PLAY
```

- `speed` ramps down during STOPPING (rate controlled by TIME knob or Shift+TIME)
- `speed` ramps up during STARTING
- WARP CV can optionally scale stop rate (higher CV = faster brake)

### Wow and Flutter Implementation

- **Wow**: `Lfo` at 0.1–2 Hz, depth controlled by REFLECT knob/CV
- **Flutter**: `Noise` or fast multi-sine, depth controlled by BLUR knob/CV
- Combined modulation applied to:
  - `PitchShifter` flutter parameter (if available)
  - Delay line read position (small fractional offsets)
  - Optional: effective `speed` variable for global pitch wobble

### Tape Delay Implementation

- Write head runs at nominal speed (or scaled by `speed`)
- Read head offset by `delayTime * sampleRate` samples
- Wow/flutter adds small, time-varying offsets to read position
- Feedback path includes:
  - Lowpass filter (cutoff tied to ATMOSPHERE knob and/or `speed`)
  - Soft clipping/saturation (ATMOSPHERE knob)
- During tape stop, delay tails slow and pitch-fall with the rest of the signal

## Software Structure

### Main Components

1. **`TapeTransport` class**
   - Manages `speed`, `transport` state, stop/start rates
   - Handles FREEZE button/gate logic
   - Provides smoothed `speed` output

2. **`WowFlutter` class**
   - Wraps `Lfo` and `Noise` modules
   - Reads REFLECT and BLUR knobs/CVs
   - Outputs wow and flutter modulation signals

3. **`TapeDelay` class**
   - Wraps `DelayLine`
   - Implements modulated read/write logic
   - Handles feedback, filtering, saturation
   - Tied to `speed` and wow/flutter signals

4. **`RetroTapeEffect` class (top-level)**
   - Initializes all modules
   - Reads all controls (knobs, CVs, buttons, gates)
   - Processes audio blocks in callback
   - Manages Shift+knob secondary pages

### Audio Callback Pseudocode

```cpp
void AudioCallback(float** in, float** out, size_t size) {
    hw.ProcessAllControls();

    // Read controls
    float warpKnob = hw.GetKnobValue(KNOB_WARP);
    float warpCV = hw.GetCvValue(CV_WARP);
    float timeKnob = hw.GetKnobValue(KNOB_TIME);
    float timeCV = hw.GetCvValue(CV_TIME);
    float blurKnob = hw.GetKnobValue(KNOB_BLUR);
    float blurCV = hw.GetCvValue(CV_BLUR);
    float reflectKnob = hw.GetKnobValue(KNOB_REFLECT);
    float reflectCV = hw.GetCvValue(CV_REFLECT);
    float mixKnob = hw.GetKnobValue(KNOB_MIX);
    float mixCV = hw.GetCvValue(CV_MIX);
    float atmKnob = hw.GetKnobValue(KNOB_ATMOSPHERE);
    float atmCV = hw.GetCvValue(CV_ATMOSPHERE);

    bool freezeBtn = hw.GetButtonState(BUTTON_FREEZE);
    bool reverseBtn = hw.GetButtonState(BUTTON_REVERSE);
    bool shiftBtn = hw.GetButtonState(BUTTON_SHIFT);

    // Update transport state
    transport.Update(freezeBtn, hw.GetGateValue(GATE_FREEZE));

    // Update wow/flutter
    wowFlutter.Update(reflectKnob, reflectCV, blurKnob, blurCV);

    // Update delay parameters
    tapeDelay.Update(timeKnob, timeCV, atmKnob, atmCV, transport.speed);

    for (size_t i = 0; i < size; ++i) {
        float L = in[0][i];
        float R = in[1][i];
        float mono = 0.5f * (L + R);

        // Pitch shift (speed + WARP bend)
        float bendSemitones = MapWarpToSemitones(warpKnob, warpCV);
        float effectiveSemitones = bendSemitones + SpeedToSemitones(transport.speed - 1.0f);
        pitchShifter.SetTranspose(effectiveSemitones);
        pitchShifter.SetFlutter(wowFlutter.flutter);

        float shifted = pitchShifter.Process(mono);

        // Tape delay
        float delayed = tapeDelay.Process(shifted, wowFlutter.wow, wowFlutter.flutter);

        // Mix
        float wet = delayed;
        float dry = shifted;
        float mix = mixKnob * (1.0f + mixCV); // example scaling
        float outSample = dry * (1.0f - mix) + wet * mix;

        out[0][i] = outSample;
        out[1][i] = outSample;
    }
}
```

## Development Phases

1. **Phase 1: Base Pitch Bend**
   - Implement WARP knob/CV → `PitchShifter` transpose
   - Verify smooth pitch tracking

2. **Phase 2: Tape Stop**
   - Implement `TapeTransport` state machine
   - Tie `speed` to pitch shift
   - Add FREEZE button/gate control

3. **Phase 3: Wow and Flutter**
   - Implement `WowFlutter` with LFO and noise
   - Modulate `PitchShifter` and/or `speed`
   - Add REFLECT/BLUR knob/CV control

4. **Phase 4: Tape Delay**
   - Implement `TapeDelay` with `DelayLine`
   - Add wow/flutter modulation to delay time
   - Couple delay to `speed` for slowdown effect

5. **Phase 5: Polish and Modes**
   - Add filtering and saturation (ATMOSPHERE)
   - Implement REVERSE mode
   - Add Shift+knob secondary pages
   - Tune control ranges and curves

## References

- Aurora SDK: https://github.com/Qu-Bit-Electronix/Aurora-SDK [cite:web:8][cite:web:21]
- DaisySP Documentation: https://electro-smith.github.io/DaisySP/ [cite:web:5][cite:web:9][cite:web:6]
- Aurora Hardware Manual: Qu-Bit Aurora getting started guide [cite:web:28][cite:web:31]
- DaisySP `PitchShifter`: https://electro-smith.github.io/DaisySP/classdaisysp_1_1_pitch_shifter.html [cite:web:9][cite:web:20]
- DaisySP `DelayLine`: https://electro-smith.github.io/DaisySP/classdaisysp_1_1_delay_line.html [cite:web:6]
- DaisySP `dsp.h` utilities: https://github.com/electro-smith/DaisySP/blob/master/Source/Utility/dsp.h [cite:web:17]
