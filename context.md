# Aurora Project Context

## Hardware Layout Notes

- The Aurora has 6 knobs (TIME, REFLECT, MIX, ATMOSPHERE, BLUR, WARP) and 11 RGB LEDs.
- The numbered LEDs (LED_1 through LED_6) form an arc around the module — like an upside-down L:

```
  [2]   [3]   [4]
  [1]         [5]
              [6]
```

  - `LED_1` — left
  - `LED_2`, `LED_3`, `LED_4` — across the top (left to right)
  - `LED_5`, `LED_6` — down the right side (top to bottom)

- Physical adjacency to knobs (informational — do NOT assume LEDs are "paired" to these knobs):
  - `LED_2` is near `KNOB_WARP`
  - `LED_4` is near `KNOB_TIME`
  - `LED_5` is near `KNOB_REFLECT`
  - `LED_6` is near `KNOB_ATMOSPHERE`
  - `LED_1` and `LED_3` have no adjacent knob

- The code mapping (KNOB_TIME → LED_1, etc.) is **logical**, not spatial. Do not redesign
  around physical adjacency unless explicitly requested.

## Buttons

Three momentary switches, accessed via `hw.GetButton(SW_*)`:

| SDK name | Icon | LED |
|----------|------|-----|
| `SW_FREEZE` | Snowflake | `LED_FREEZE` (directly attached) |
| `SW_REVERSE` | Arrows (mode) | `LED_REVERSE` (directly attached) |
| `SW_SHIFT` | None | None |

`SW_FREEZE` and `SW_REVERSE` are the only controls with a physically attached LED.

## Bottom LEDs

- Hardware constraint: no red channel (`r = -1` in LedMap), green and blue only. The SDK silently discards the red value — no crash, G and B still set normally.
- The "BOT" name refers to the base of the module. The backside that is pointed into the rack. They are minimally visible in many mounted systems.

**Verified physical positions**

| SDK name | Physical position |
|----------|-------------------|
| `LED_BOT_1` | Middle of module |
| `LED_BOT_2` | Right of module |
| `LED_BOT_3` | Top of module |

## Knob Physical Layout

Two columns, top to bottom:

```
  WARP       TIME
  BLUR    REFLECT
   MIX  ATMOSPHERE
```
