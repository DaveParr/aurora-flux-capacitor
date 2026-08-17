# Flux Capacitor

Continuous-pitch tape voice firmware for the [Qu-Bit Aurora](https://www.qubitelectronix.com/shop/aurora) Eurorack module.

## Features

Flux Capacitor turns the Aurora into a tape-style pitch and delay machine. WARP bends pitch continuously (no steps), FREEZE brakes the "tape" to a stop and back, REFLECT/BLUR add wow and flutter drift, and TIME layers a tape-style delay on top — all four coupled together the way a real tape machine's speed affects everything at once.

- **Continuous pitch bend** — WARP knob (±12 semitones) plus WARP CV (1V/oct) bend pitch smoothly, with no audible stepping; centering WARP snaps to a bit-exact dry passthrough
- **Tape stop/start** — FREEZE toggles a speed ramp from full speed to a full stop and back (~1.5s), dragging pitch and delay time down together as the tape "brakes"; GATE_FREEZE holds the stop for as long as the gate is high
- **Wow and flutter** — REFLECT sets a slow sine wow rate (0.1–2 Hz), BLUR sets fast filtered-noise flutter depth; both modulate pitch and, more subtly, the delay's tap position for authentic drift
- **Tape-style delay** — TIME (plus CV) sets delay time from 1 ms to 2 s, log-curved so short slapback and long ambient tails both feel like even steps; repeats darken via a fixed feedback lowpass and lengthen/slow as FREEZE brakes the tape
- **Dry/wet blend with auto-rescue** — MIX blends dry and wet with an equal-power crossfade; if MIX is left fully dry, FREEZE automatically goes full-wet while actively stopping/starting so the effect is still audible, and reverts once you're back at speed
- **WARP LED bar-graph** — LED_1–6 show pitch-shift direction and amount as an outward-growing bar (amber up, cyan down), breathing brightness with the wow/flutter amount; centered WARP shows a smooth cyan-to-amber gradient across all six instead
- **FREEZE LED tape-speed meter** — LED_FREEZE glows red in proportion to how far the tape has slowed, and flashes white briefly whenever MIX crosses in or out of the fully-dry auto-wet zone
- **Tape coloration** — ATMOSPHERE (plus CV) darkens the wet signal's tone, adds soft-knee saturation, and raises the delay's feedback amount, all from one knob; past about noon the delay starts to feed back on itself pleasantly, sustaining rather than decaying, without ever spiraling out of control

**Not yet implemented:** REVERSE and SHIFT are wired in hardware but have no effect on the sound yet.

## Download

Pre-built firmware is available on the [Releases page](https://github.com/DaveParr/aurora-flux-capacitor/releases).

1. Download `flux-capacitor-<version>.bin` from the latest release
2. Copy it to the root of a FAT32 USB drive (it must be the only `.bin` file there)
3. Insert the USB drive into the Aurora module
4. Power up the module with the drive inserted — the bootloader loads the firmware automatically
5. Power down and remove the drive; check `daisy_boot_log.txt` on the drive to confirm a successful flash

## Controls and Behaviour

### Knobs

| Knob | Function |
|------|----------|
| Warp | Pitch bend, ±12 semitones. Centered = dry passthrough (a small deadzone snaps near-center readings to exactly 0) |
| Time | Delay time, log-mapped from 1 ms to 2 s |
| Reflect | Wow rate — LFO frequency from 0.1 Hz to 2 Hz, log-curved. Wow depth itself is fixed |
| Blur | Flutter depth, linear from none to full jitter |
| Mix | Dry/wet balance, equal-power crossfade |
| Atmosphere | Tape coloration — darkens tone, adds saturation, and raises delay feedback together, from clean-ish to warm and self-sustaining |

### Buttons

| Button | Function |
|--------|----------|
| Freeze | Toggle tape stop/start — press once to brake to a stop, press again to spin back up to speed (~1.5s ramp either way) |
| Reverse | Unused |
| Shift | Unused |

### Gate/CV Inputs

| Input | Function |
|-------|----------|
| Freeze gate | While high, forces the tape to a stop regardless of the button's last state; releasing the gate returns control to the button |
| Reverse gate | Unused |
| Warp CV | 1V/oct pitch control, added to the Warp knob's semitone offset |
| Time CV | Added to the Time knob (delay time) |
| Reflect CV | Added to the Reflect knob (wow rate) |
| Blur CV | Added to the Blur knob (flutter depth) |
| Mix CV | Added to the Mix knob (dry/wet) |
| Atmosphere CV | Added to the Atmosphere knob (tape coloration) |

Each knob/CV pair sums and clamps to its parameter's valid range: the knob sets the center, CV swings the parameter around it.

### Audio

Signal path: pitch shift (Warp + tape speed + wow/flutter) → tape coloration (Atmosphere: saturation then tone lowpass) → tape delay (Time, speed- and wobble-coupled, with its own Atmosphere-driven feedback amount and feedback-loop saturation) → dry/wet blend (Mix) → output. True stereo throughout.

Pressing Freeze ramps the tape speed from 1.0 to 0.0 (or back) over about 1.5 seconds. As speed falls, pitch drops an octave for every halving of speed, the delay's repeats lengthen and slow in lockstep, and the wet signal's amplitude fades with speed — a full stop is silence, not a frozen loudness. If Mix is dialed fully dry, Freeze temporarily overrides it to fully wet for the duration of the stop/start so the effect is actually audible, then hands control back to your Mix setting once play resumes; any other Mix setting is left alone throughout.

Wow (slow, Reflect-controlled) and flutter (fast, Blur-controlled) sum into the same pitch signal Warp and tape-stop use, and separately nudge the delay's read position — so pitch and delay drift together, the way a real tape's speed instability would affect both.

Atmosphere is one knob driving three things at once: a lowpass that darkens the wet signal (more so as Atmosphere increases, and further still as Freeze brakes the tape, even with Atmosphere fully off), soft-knee saturation on the wet signal and inside the delay's feedback loop, and the delay's feedback amount itself. Past about noon it starts to feed back on itself pleasantly — repeats sustain rather than decay, without ever spiraling out of control.

### LEDs

| LED | Behaviour |
|-----|-----------|
| Arc (1–6) | WARP pitch-shift bar-graph: LED_4/5/6 light amber outward for an upward shift, LED_3/2/1 light cyan outward for a downward shift. Brightness breathes with the wow/flutter amount. When Warp is centered, all six instead show a smooth cyan-to-amber gradient across the arc, crossfading into the bar-graph as the knob leaves center. |
| Freeze LED | Glows red, brightness proportional to how far the tape has slowed (fully off at full speed, full red at a dead stop). Briefly flashes white whenever Mix crosses into or out of the fully-dry zone that triggers Freeze's auto-wet override. |
| Reverse LED | Unused. |
| Bottom LEDs | Unused. |

## License

MIT — see [LICENSE](LICENSE).
