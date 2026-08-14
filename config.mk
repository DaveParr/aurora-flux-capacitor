# ARM toolchain installed to ~/.local — not in system PATH, no .bashrc changes needed.
# Override from CLI: make GCC_PATH=/other/path build PROJECT=foo
GCC_PATH ?= $(HOME)/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin

# Resolve SDK paths relative to this file's own location so they work correctly
# whether this file is included from a project subdirectory (../config.mk)
# or from the repo root (config.mk).
_CONFIG_MK_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

AURORA_SDK_DIR ?= $(_CONFIG_MK_DIR)lib/Aurora-SDK
LIBDAISY_DIR   ?= $(AURORA_SDK_DIR)/libs/libDaisy

# Firmware version: exact git tag (strip leading v), or "dev" on untagged commits.
# CI overrides this by passing FIRMWARE_VERSION=x.y.z on the command line.
FIRMWARE_VERSION ?= $(shell (git describe --tags --exact-match 2>/dev/null || echo dev) | sed 's/^v//')
