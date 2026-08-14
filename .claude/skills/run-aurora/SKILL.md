---
name: run-aurora
description: Build and flash Aurora eurorack firmware. Use when asked to build, compile, flash, or load firmware onto the Aurora USB drive.
---

This project builds ARM Cortex-M7 firmware for the Qu-Bit Aurora eurorack module (Daisy/STM32H750 platform). "Running" means compiling a project subdirectory and copying the resulting `.bin` to a USB drive that the Aurora bootloader reads on next power-cycle.

The toolchain path is defined in `config.mk` (`GCC_PATH`). The default is `~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin` but this will vary by machine — check `config.mk` for the current value rather than assuming.

## Prerequisites

ARM GCC toolchain must be present at the path set in `config.mk` (`GCC_PATH`). Read the current value, then verify the binary exists:

```bash
grep GCC_PATH config.mk
ls <GCC_PATH>/arm-none-eabi-gcc
```

The two SDK libraries (libDaisy and DaisySP) must be built before the first project build, and after any SDK update. Check whether they exist:

```bash
ls lib/Aurora-SDK/libs/libDaisy/build/libdaisy.a
ls lib/Aurora-SDK/libs/DaisySP/build/libdaisysp.a
```

If either is missing, build them (one-time, takes ~2 minutes):

```bash
make libdaisy
make -C lib/Aurora-SDK/libs/DaisySP GCC_PATH=<GCC_PATH>
```

Note: `make libdaisy` only builds libDaisy. DaisySP has no root-level target and must be built directly as shown above.

## Build

```bash
make build PROJECT=hello-aurora
```

Replace `hello-aurora` with the target subdirectory name. The output is `<PROJECT>/build/<PROJECT>.bin`.

## Flash

### Step 1 — Find the mount point

The Aurora USB drive auto-mounts when inserted. Find it:

```bash
lsblk -o NAME,MOUNTPOINT,LABEL,SIZE | grep -v loop
```

If not mounted, identify the device from `lsblk` output (look for a ~1 GB FAT volume) and mount it:

```bash
udisksctl mount -b /dev/<DEVICE>
```

The resulting mount point (shown in the `udisksctl` output) will be something like `/media/<USER>/<LABEL>` — use that path as `<MOUNT>` in the steps below. Do not assume a fixed label or username.

### Step 2 — Check for existing firmware (REQUIRED before flashing)

The Aurora bootloader flashes only the **first** `.bin` file it finds in the root of the USB drive. If multiple `.bin` files are present, which one gets flashed depends on filesystem ordering — not which was copied most recently.

Before copying, check what's already there:

```bash
ls <MOUNT>/*.bin 2>/dev/null
```

**If existing `.bin` files are found, stop and ask the user:**
- Abort the flash (user will manually remove old firmware first), or
- Delete the conflicting file(s) and proceed

Do not proceed with two `.bin` files on the drive without explicit instruction.

### Step 3 — Flash

```bash
make flash PROJECT=<PROJECT> MOUNT=<MOUNT>
```

This copies `hello-aurora/build/hello-aurora.bin` to the USB root and calls `sync`. Eject the drive, insert it into the Aurora, and power-cycle the module. The bootloader runs on every boot before the application.

## Boot log (troubleshooting)

After a boot cycle, the Aurora writes to `<MOUNT>/daisy_boot_log.txt` (where `<MOUNT>` is the USB mount point). Each successful flash appends a numbered line:

```
1. Successfully flashed file "hello-aurora.bin" to address 0x90040000
```

If the line for your `.bin` doesn't appear after power-cycling, the bootloader did not see the file. Common causes:
- Drive was not ejected cleanly before power-cycling (`sync` may not have flushed)
- The `.bin` was placed in a subdirectory instead of the root
- Multiple `.bin` files on the drive; check which one the log shows

`Aurora_Version.txt` on the drive is written by official Aurora firmware; it will be empty or absent when running custom firmware.

## Gotchas

- **`make libdaisy` does not build DaisySP.** The linker needs both `libdaisy.a` and `libdaisysp.a`. Build DaisySP separately on first setup or after SDK updates.
- **The USB label and mount path vary.** Do not assume a fixed label (e.g. `AURORA`) or username in the mount path. Always use `lsblk` to find the current mount point and treat it as `<MOUNT>`.
- **Trash counts.** Files in `.Trash-1000/` on the USB are not in the root and do not affect flashing, but they do consume space on the 953 MB drive.
- **`BOOT_SRAM` app type.** Projects use `APP_TYPE = BOOT_SRAM`, meaning firmware runs from SRAM after the bootloader copies it. The linker script is `STM32H750IB_sram.lds`. Do not use the QSPI flash linker script for these projects.
