# Aurora SDK Dev Environment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Configure the aurora repo as a Daisy Seed development workspace for Qu-Bit Aurora firmware on Linux, with the ARM toolchain installed user-locally, the Aurora SDK as a git submodule, and a `hello-aurora` starter project that builds to a flashable `.bin`.

**Architecture:** The ARM toolchain lives at `~/.local/share/gcc-arm-none-eabi/` (user-local, not in system PATH). All project Makefiles include a shared `config.mk` at the repo root that provides `GCC_PATH`, `AURORA_SDK_DIR`, and `LIBDAISY_DIR` as `?=` defaults (overridable from CLI). The Aurora SDK is a git submodule that vendors libDaisy recursively.

**Tech Stack:** GNU Make, gcc-arm-none-eabi-10-2020-q4-major, libDaisy (via Aurora SDK submodule), C++14, STM32H7

## Global Constraints

- Linux x86_64 host only
- ARM toolchain: `gcc-arm-none-eabi-10-2020-q4-major` (already extracted at `/home/dave/Development/aurora/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/gcc-arm-none-eabi-10-2020-q4-major/`)
- Toolchain install target: `~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/`
- Aurora SDK submodule path in repo: `lib/Aurora-SDK`
- No `.bashrc` changes, no system PATH changes
- All Makefile path variables use `?=` (overridable from CLI)
- `build/` directories and `*.bin *.elf *.hex *.map *.o *.d` are gitignored globally

---

### Task 1: Install toolchain to user-local path and create .gitignore

**Files:**
- Move: `gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/gcc-arm-none-eabi-10-2020-q4-major/` → `~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/`
- Delete: `gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/` (now-empty wrapper dir, from repo root)
- Delete: `gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux.tar.bz2` (from repo root)
- Create: `.gitignore`

**Interfaces:**
- Produces: `arm-none-eabi-gcc` available at `~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin/arm-none-eabi-gcc`

- [ ] **Step 1: Create the install directory and move the toolchain**

```bash
mkdir -p ~/.local/share/gcc-arm-none-eabi
mv /home/dave/Development/aurora/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/gcc-arm-none-eabi-10-2020-q4-major \
   ~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major
rmdir /home/dave/Development/aurora/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux
```

- [ ] **Step 2: Remove the tarball**

```bash
rm /home/dave/Development/aurora/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux.tar.bz2
```

- [ ] **Step 3: Verify the toolchain binary works**

```bash
~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin/arm-none-eabi-gcc --version
```

Expected output (first line):
```
arm-none-eabi-gcc (GNU Arm Embedded Toolchain 10-2020-q4-major) 10.2.1 20201103 (release)
```

- [ ] **Step 4: Create .gitignore**

Create `/home/dave/Development/aurora/.gitignore`:

```gitignore
# ARM toolchain — installed to ~/.local/share/gcc-arm-none-eabi/
gcc-arm-none-eabi*/

# Build artifacts
build/
*.bin
*.elf
*.hex
*.map
*.o
*.d

# macOS
.DS_Store
```

- [ ] **Step 5: Verify the repo is clean**

```bash
cd /home/dave/Development/aurora && git status
```

Expected: only `.gitignore` appears as untracked. No toolchain directory or tarball.

- [ ] **Step 6: Commit**

```bash
cd /home/dave/Development/aurora
git add .gitignore
git commit -m "chore: install ARM toolchain to ~/.local, add .gitignore"
```

---

### Task 2: Add Aurora SDK as a git submodule

**Files:**
- Create: `lib/Aurora-SDK/` (managed by git submodule)

**Interfaces:**
- Produces:
  - `lib/Aurora-SDK/` — Aurora BSP source tree
  - `lib/Aurora-SDK/libs/libDaisy/` — libDaisy pulled via recursive init
  - `lib/Aurora-SDK/libs/libDaisy/core/Makefile` — libDaisy build system entry point (verify in Step 3)
  - At least one example directory at `lib/Aurora-SDK/Examples/` (used in Task 5)

- [ ] **Step 1: Add the submodule**

```bash
cd /home/dave/Development/aurora
git submodule add https://github.com/Qu-Bit-Electronix/Aurora-SDK.git lib/Aurora-SDK
```

- [ ] **Step 2: Pull sub-dependencies recursively**

```bash
cd /home/dave/Development/aurora
git submodule update --init --recursive
```

This pulls libDaisy and its own STM32 HAL sub-dependencies through the Aurora SDK's submodule tree. May take a few minutes.

- [ ] **Step 3: Verify the SDK structure and note key paths for later tasks**

```bash
ls lib/Aurora-SDK/
ls lib/Aurora-SDK/libs/libDaisy/
ls lib/Aurora-SDK/libs/libDaisy/core/
```

Confirm `lib/Aurora-SDK/libs/libDaisy/core/Makefile` exists — Tasks 3 and 5 depend on it.

```bash
ls lib/Aurora-SDK/Examples/
```

Note the name of one example directory — you will read its Makefile in Task 5.

```bash
find lib/Aurora-SDK -maxdepth 2 -name "*.h" | grep -iv libdaisy | head -10
```

Note the Aurora BSP header name (e.g., `Aurora.h`) — you will use this in Task 5.

- [ ] **Step 4: Commit**

```bash
cd /home/dave/Development/aurora
git add .gitmodules lib/Aurora-SDK
git commit -m "chore: add Aurora-SDK as git submodule with libDaisy"
```

---

### Task 3: Create config.mk

**Files:**
- Create: `config.mk`

**Interfaces:**
- Produces (for any Makefile that does `include ../config.mk` or `include config.mk`):
  - `GCC_PATH` — absolute path to `arm-none-eabi-gcc` bin directory, default `~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin`
  - `AURORA_SDK_DIR` — absolute path to `lib/Aurora-SDK`, resolved relative to `config.mk`'s own location
  - `LIBDAISY_DIR` — `$(AURORA_SDK_DIR)/libs/libDaisy`
  - All three use `?=` — overridable from CLI

- [ ] **Step 1: Create config.mk**

Create `/home/dave/Development/aurora/config.mk`:

```makefile
# ARM toolchain installed to ~/.local — not in system PATH, no .bashrc changes needed.
# Override from CLI: make GCC_PATH=/other/path build PROJECT=foo
GCC_PATH ?= $(HOME)/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin

# Resolve SDK paths relative to this file's own location so they work correctly
# whether this file is included from a project subdirectory (../config.mk)
# or from the repo root (config.mk).
_CONFIG_MK_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

AURORA_SDK_DIR ?= $(_CONFIG_MK_DIR)lib/Aurora-SDK
LIBDAISY_DIR   ?= $(AURORA_SDK_DIR)/libs/libDaisy
```

- [ ] **Step 2: Verify the variables resolve to real paths**

```bash
cd /home/dave/Development/aurora
make -f <(printf 'include config.mk\nprint:\n\t@echo GCC_PATH=$$(GCC_PATH)\n\t@echo AURORA_SDK_DIR=$$(AURORA_SDK_DIR)\n\t@echo LIBDAISY_DIR=$$(LIBDAISY_DIR)\n') print
```

Expected (your home directory will differ):
```
GCC_PATH=/home/dave/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin
AURORA_SDK_DIR=/home/dave/Development/aurora/lib/Aurora-SDK
LIBDAISY_DIR=/home/dave/Development/aurora/lib/Aurora-SDK/libs/libDaisy
```

Then verify each path actually exists:
```bash
ls ~/.local/share/gcc-arm-none-eabi/gcc-arm-none-eabi-10-2020-q4-major/bin/arm-none-eabi-gcc
ls /home/dave/Development/aurora/lib/Aurora-SDK/libs/libDaisy/core/Makefile
```

Both should return the file path without error.

- [ ] **Step 3: Commit**

```bash
cd /home/dave/Development/aurora
git add config.mk
git commit -m "chore: add config.mk with local GCC_PATH and SDK path variables"
```

---

### Task 4: Create top-level Makefile

**Files:**
- Create: `Makefile`

**Interfaces:**
- Consumes: `PROJECT` variable (required for both targets), `MOUNT` variable (required for `flash`)
- Produces:
  - `make build PROJECT=<name>` — delegates to `<name>/Makefile`
  - `make flash PROJECT=<name> MOUNT=<path>` — copies `<name>/build/<name>.bin` to `<path>/`

- [ ] **Step 1: Create the top-level Makefile**

Create `/home/dave/Development/aurora/Makefile`:

```makefile
.PHONY: build flash

build:
ifndef PROJECT
	$(error PROJECT is not set. Usage: make build PROJECT=hello-aurora)
endif
	$(MAKE) -C $(PROJECT)

flash:
ifndef PROJECT
	$(error PROJECT is not set. Usage: make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA)
endif
ifndef MOUNT
	$(error MOUNT is not set. Usage: make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA)
endif
	cp $(PROJECT)/build/$(PROJECT).bin $(MOUNT)/
	sync
	@echo "Flashed $(PROJECT).bin to $(MOUNT)/"
```

Note: the indentation in Makefiles must be a **tab character**, not spaces.

- [ ] **Step 2: Verify error messages fire correctly**

```bash
cd /home/dave/Development/aurora
make build 2>&1 | grep "PROJECT is not set"
```

Expected: prints `PROJECT is not set. Usage: make build PROJECT=hello-aurora`

```bash
make flash PROJECT=hello-aurora 2>&1 | grep "MOUNT is not set"
```

Expected: prints `MOUNT is not set. Usage: make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA`

- [ ] **Step 3: Commit**

```bash
cd /home/dave/Development/aurora
git add Makefile
git commit -m "chore: add top-level Makefile with build and flash targets"
```

---

### Task 5: Create hello-aurora starter project

**Files:**
- Create: `hello-aurora/main.cpp`
- Create: `hello-aurora/Makefile`

**Interfaces:**
- Consumes:
  - `../config.mk` — `GCC_PATH`, `AURORA_SDK_DIR`, `LIBDAISY_DIR`
  - `$(LIBDAISY_DIR)/core/Makefile` — libDaisy build system
  - Aurora BSP header (name found in Task 2 Step 3)
- Produces: `hello-aurora/build/hello-aurora.bin` (flashable firmware)

- [ ] **Step 1: Read an existing SDK example to learn the correct include pattern**

```bash
# List available examples
ls /home/dave/Development/aurora/lib/Aurora-SDK/Examples/

# Pick the first one and read its Makefile
EXAMPLE=$(ls /home/dave/Development/aurora/lib/Aurora-SDK/Examples/ | head -1)
cat /home/dave/Development/aurora/lib/Aurora-SDK/Examples/$EXAMPLE/Makefile
```

Note:
- The final `include` line (the libDaisy build system entry point)
- Any `C_INCLUDES` paths it uses
- Any `AURORA_SDK_DIR` or equivalent variable it uses

```bash
# Also read the example's .cpp to find the correct Aurora header
cat /home/dave/Development/aurora/lib/Aurora-SDK/Examples/$EXAMPLE/*.cpp 2>/dev/null || \
cat /home/dave/Development/aurora/lib/Aurora-SDK/Examples/$EXAMPLE/*.cc 2>/dev/null
```

Note the `#include` at the top — this is the Aurora BSP header you will use in `main.cpp`.

- [ ] **Step 2: Create hello-aurora/Makefile**

Base this on what you found in Step 1. A typical libDaisy project looks like the following — **adjust `C_INCLUDES` and the final `include` path to match the example you read**:

Create `/home/dave/Development/aurora/hello-aurora/Makefile`:

```makefile
include ../config.mk

TARGET      = hello-aurora
CPP_SOURCES = main.cpp

C_INCLUDES = \
  -I$(AURORA_SDK_DIR)/src \
  -I$(LIBDAISY_DIR)/src \
  -I$(LIBDAISY_DIR)/src/sys \
  -I$(LIBDAISY_DIR)/src/usbd \
  -I$(LIBDAISY_DIR)/Drivers/CMSIS/Include \
  -I$(LIBDAISY_DIR)/Drivers/CMSIS/Device/ST/STM32H7xx/Include \
  -I$(LIBDAISY_DIR)/Drivers/STM32H7xx_HAL_Driver/Inc

include $(LIBDAISY_DIR)/core/Makefile
```

If the example Makefile's structure differs significantly, mirror it instead.

- [ ] **Step 3: Create hello-aurora/main.cpp**

Use the `#include` and `Hardware` init pattern from the example you read in Step 1. A minimal template:

```cpp
#include "Aurora.h"   // replace with the actual header name from Step 1

using namespace aurora;

Hardware hw;

int main(void)
{
    hw.Init();

    while(1)
    {
        hw.ProcessAllControls();
    }
}
```

Replace `"Aurora.h"`, `namespace aurora`, `Hardware`, and method names with what the SDK example actually uses if they differ.

- [ ] **Step 4: Build the project**

```bash
cd /home/dave/Development/aurora
make build PROJECT=hello-aurora
```

Expected: compilation succeeds and the final lines include something like:
```
arm-none-eabi-objcopy -O binary hello-aurora/build/hello-aurora.elf hello-aurora/build/hello-aurora.bin
```

If you see missing-header errors, add the missing `-I` paths (from the error output) to `C_INCLUDES` in `hello-aurora/Makefile` and re-run.

- [ ] **Step 5: Verify the binary exists and is non-zero**

```bash
ls -lh /home/dave/Development/aurora/hello-aurora/build/hello-aurora.bin
```

Expected: a file between ~20 kB and ~500 kB. A zero-byte file means the objcopy step failed.

- [ ] **Step 6: Verify build/ is gitignored**

```bash
cd /home/dave/Development/aurora && git status hello-aurora/
```

Expected: only `hello-aurora/main.cpp` and `hello-aurora/Makefile` appear as untracked. The `build/` directory must not appear.

- [ ] **Step 7: Commit**

```bash
cd /home/dave/Development/aurora
git add hello-aurora/
git commit -m "feat: add hello-aurora starter project"
```

---

## Self-Review

**Spec coverage:**

| Spec requirement | Task |
|---|---|
| Toolchain moved to `~/.local/share/gcc-arm-none-eabi/` | Task 1 |
| No `.bashrc` or PATH changes | Task 1, 3 (`?=` pattern) |
| `.gitignore` excludes toolchain, build artifacts | Task 1 |
| Aurora SDK as submodule at `lib/Aurora-SDK` | Task 2 |
| Recursive submodule init for libDaisy | Task 2 |
| `config.mk` with `GCC_PATH`, `AURORA_SDK_DIR`, `LIBDAISY_DIR` using `?=` | Task 3 |
| Top-level `make build PROJECT=x` | Task 4 |
| Top-level `make flash PROJECT=x MOUNT=y` copies `.bin` | Task 4 |
| `hello-aurora/` builds to `.bin` | Task 5 |
| SDK examples buildable (noted in spec; requires `GCC_PATH` on CLI) | Task 2 Step 3 verifies structure; Task 3 documents the CLI pattern |

**Placeholder scan:** Task 5 Steps 2 and 3 include "adjust based on what you find" notes — these are intentional guided discovery steps, not ambiguous placeholders. The implementer is directed to read the real SDK before writing those files.

**Type consistency:** Variable names `GCC_PATH`, `AURORA_SDK_DIR`, `LIBDAISY_DIR`, `PROJECT`, `MOUNT` are used consistently across all tasks.
