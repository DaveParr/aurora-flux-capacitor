# Flux Capacitor — WARP Pitch-Shift LED Feedback

## Context

Phase 1 ([`2026-08-15-flux-capacitor-phase1-pitch-bend-design.md`](2026-08-15-flux-capacitor-phase1-pitch-bend-design.md))
shipped the WARP knob/CV pitch-bend engine (`ComputeWarpSemitones` +
`TapeVoice`) with no LED behavior at all — `main.cpp` clears the LEDs
once at boot and leaves them off for the rest of the program, purely so
the module doesn't look hung after a previous firmware left LEDs lit.

This spec adds LED feedback for the pitch shift that's actually
happening, scoped to the WARP path only. It does not touch FREEZE,
REVERSE, or any other control — those remain unused/off, as they were
in Phase 1.

## Approach: symmetric bar-graph across `LED_1`–`LED_6`

Per `context.md`, `LED_1`–`LED_6` form an arc:

```
  [2]   [3]   [4]
  [1]         [5]
              [6]
```

The bar-graph splits at the arc's natural break, center-out:

- **Upward shift** lights `LED_4 → LED_5 → LED_6` (amber).
- **Downward shift** lights `LED_3 → LED_2 → LED_1` (cyan).
- Near-zero shift (inside the existing `kWarpDeadzoneSemitones` = 0.05
  semitone dry deadzone in `ComputeWarpSemitones`) → all six LEDs off,
  consistent with the audio path already resolving to bit-exact dry
  passthrough there.

Full scale is ±12 semitones — matching `kWarpKnobRangeSemitones`, the
knob's own range — split into three 4-semitone bands per side, one per
LED, center-to-edge:

| Band | LED (up side) | LED (down side) |
|------|----------------|-------------------|
| 0–4 semitones | `LED_4` | `LED_3` |
| 4–8 semitones | `LED_5` | `LED_2` |
| 8–12 semitones | `LED_6` | `LED_1` |

Within a band, brightness fades in continuously (`0.0` at the band's
start, `1.0` at its end) rather than snapping on — e.g. a shift of 6
semitones renders as `LED_4` full-bright + `LED_5` half-bright. Once
the combined semitone value exceeds ±12 (CV pushing past what the knob
alone can reach), the corresponding side simply stays fully lit — no
separate overflow indicator.

## Implementation shape

Follows the existing `warp_control.h` pattern established in Phase 1:
a pure, host-testable mapping function plus thin hardware glue in
`main.cpp`.

```cpp
// warp_control.h
struct WarpLedLevels
{
    // Brightness 0..1, center-to-edge.
    // up[0]/down[0]   -> LED_4/LED_3
    // up[1]/down[1]   -> LED_5/LED_2
    // up[2]/down[2]   -> LED_6/LED_1
    float up[3];
    float down[3];
};

constexpr float kWarpLedFullScaleSemitones = kWarpKnobRangeSemitones; // 12.0f
constexpr float kWarpLedBandSemitones      = kWarpLedFullScaleSemitones / 3.0f; // 4.0f

/** Maps a combined semitone value (post-deadzone, post-smoothing) to
 *  per-LED brightness for the symmetric bar-graph. semitones > 0 lights
 *  `up`, semitones < 0 lights `down`; the other side is always all-zero.
 */
WarpLedLevels ComputeWarpLedLevels(float semitones);
```

`main.cpp`'s `while(1)` loop — currently just a single `ClearLeds`/
`WriteLeds` at boot — becomes an ongoing loop that reads
`warpSmoother.Value()` (the same smoothed semitone value the audio
callback already computes each block, no new shared state), calls
`ComputeWarpLedLevels`, and writes the six LEDs, mirroring
`hello-aurora`'s main-loop LED-update pattern:

```cpp
constexpr Rgb kWarpUpColor   = {1.0f, 0.4f, 0.0f}; // amber
constexpr Rgb kWarpDownColor = {0.0f, 0.6f, 1.0f}; // cyan

const Leds upLeds[3]   = {LED_4, LED_5, LED_6};
const Leds downLeds[3] = {LED_3, LED_2, LED_1};

while (1)
{
    WarpLedLevels levels = ComputeWarpLedLevels(warpSmoother.Value());
    hw.ClearLeds();
    for (int i = 0; i < 3; i++)
    {
        hw.SetLed(upLeds[i],
                  kWarpUpColor.r * levels.up[i],
                  kWarpUpColor.g * levels.up[i],
                  kWarpUpColor.b * levels.up[i]);
        hw.SetLed(downLeds[i],
                  kWarpDownColor.r * levels.down[i],
                  kWarpDownColor.g * levels.down[i],
                  kWarpDownColor.b * levels.down[i]);
    }
    hw.WriteLeds();
}
```

Reading `warpSmoother.Value()` from the main loop while the audio
callback writes it concurrently is the same cross-context float read
already used elsewhere in these firmwares (e.g. `hello-aurora` reading
`hw.GetKnobValue()` from its main loop) — acceptable for a display
value where a rare torn read is visually meaningless.

`LED_FREEZE`, `LED_REVERSE`, and `LED_BOT_1-3` are not touched by this
change; they stay off, as in Phase 1.

## Testing plan

Host-testable (doctest, same pattern as `test_warp_control.cpp`):

- `ComputeWarpLedLevels()`:
  - `semitones == 0` → all six levels zero.
  - Small semitone values inside a single band → correct single LED
    partially lit, all others zero.
  - Band boundaries (4, 8, 12 semitones exactly) → correct full/empty
    transitions.
  - Values beyond ±12 → clamped, all three LEDs on that side at `1.0`.
  - Negative semitones only populate `down`; positive only populate
    `up`; the unused side is always all-zero.

Hardware verification (acceptance criteria):

- Turning `KNOB_WARP` from center to fully clockwise sweeps `LED_4 →
  LED_5 → LED_6` on smoothly, ending with all three fully lit.
- Turning it fully counter-clockwise does the same for `LED_3 → LED_2
  → LED_1`.
- Patching a v/oct CV sweep into `CV_WARP` with the knob centered
  drives the same bar-graph behavior via CV alone.
- At dead-center with no CV patched, all six LEDs are off.

## Out of scope

FREEZE/tape-stop transport, wow/flutter, tape delay, saturation/
filtering (ATMOSPHERE), REVERSE mode, Shift+knob secondary pages —
unchanged from Phase 1's "out of scope" list. Any LED behavior for
those controls is deferred to their own future specs.
