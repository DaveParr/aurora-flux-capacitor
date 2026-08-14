# Aurora SDK Development Environment

**Date:** 2026-06-17

## Goal

Configure this repo as a development workspace for Qu-Bit Aurora firmware using the Daisy Seed toolchain on Linux. The Aurora SDK is a managed dependency. SDK examples are buildable locally and flashable to the hardware via USB drive. Own firmware projects live flat at the repo root alongside the SDK, and can be spun out into their own repos using the same structure.

## Directory Structure

```
aurora/
├── lib/
│   └── Aurora-SDK/        ← git submodule (Qu-Bit-Electronix/Aurora-SDK)
├── docs/
│   └── superpowers/specs/
├── config.mk              ← GCC_PATH, SDK path variables (included by all project Makefiles)
├── .gitignore
├── Makefile               ← top-level build and flash convenience targets
└── hello-aurora/          ← starter project / template for future repos
    ├── main.cpp
    └── Makefile
```

## Toolchain

- **ARM toolchain:** `gcc-arm-none-eabi-10-2020-q4-major` (already downloaded)
- **Install location:** `~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/`
- The toolchain tarball and extracted folder currently in the repo root are moved there and removed from the project directory.
- No changes to `.bashrc` or system PATH. The path is set via `GCC_PATH` in `config.mk` and can be overridden on the command line.

## Dependencies

The Aurora SDK is added as a git submodule:

```
git submodule add https://github.com/Qu-Bit-Electronix/Aurora-SDK.git lib/Aurora-SDK
git submodule update --init --recursive
```

The recursive update pulls libDaisy (and its STM32 HAL dependencies) through the Aurora SDK's own submodule tree. No separate libDaisy management is required.

## config.mk

Shared variables included by all project Makefiles:

```makefile
GCC_PATH     ?= $(HOME)/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin
AURORA_SDK_DIR ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))lib/Aurora-SDK
LIBDAISY_DIR   ?= $(AURORA_SDK_DIR)/libDaisy
```

All variables use `?=` so they can be overridden from the command line without modifying any file.

## Top-level Makefile

Convenience targets that delegate to individual project directories:

```makefile
build:
	$(MAKE) -C $(PROJECT)

flash:
	cp $(PROJECT)/build/$(PROJECT).bin $(MOUNT)/
```

Usage:
- `make build PROJECT=hello-aurora`
- `make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA`

`MOUNT` can also be defaulted in `config.mk` if a consistent USB volume label is used.

## Per-project Makefile

Each project (including `hello-aurora/`) follows the standard libDaisy Makefile pattern:

```makefile
include ../config.mk

TARGET = hello-aurora
CPP_SOURCES = main.cpp

C_INCLUDES = \
  -I$(AURORA_SDK_DIR)/src \
  -I$(LIBDAISY_DIR)/src

include $(LIBDAISY_DIR)/core/Makefile
```

`build/` output directories are gitignored globally.

## Flashing Workflow

1. `make build PROJECT=<name>` — compiles to `<name>/build/<name>.bin`
2. `make flash PROJECT=<name> MOUNT=<usb-mount-path>` — copies `.bin` to the USB drive
3. Insert USB drive into the Aurora module and trigger firmware update from the hardware

## Building SDK Examples

SDK examples live inside the submodule at `lib/Aurora-SDK/examples/`. Each has its own Makefile. To build one:

```bash
cd lib/Aurora-SDK/examples/<example-name>
make GCC_PATH=$(HOME)/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin
```

Or set `GCC_PATH` in the shell for the duration of the session when iterating on examples.

## .gitignore

```
# Toolchain (large binaries, installed to ~/.local)
gcc-arm-none-eabi*/

# Build artifacts
build/
*.bin
*.elf
*.hex
*.map
```

## Template for Future Repos

When starting a new standalone project repo, copy the `hello-aurora/` directory, rename it, and add the Aurora SDK submodule at the same relative path (`../lib/Aurora-SDK` becomes `lib/Aurora-SDK` at the repo root). The Makefile include path changes from `../config.mk` to `config.mk`.
