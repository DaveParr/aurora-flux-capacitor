# GitHub CI Firmware Build and Release

**Date:** 2026-06-26

## Goal

Add a GitHub Actions CI workflow to this template repo that builds all firmware projects, runs host-side unit tests, and publishes a versioned GitHub Release when a semantic version tag is pushed.

## Architecture

One workflow file: `.github/workflows/ci.yml`, with three jobs:

```
push / PR         tag v*
    │                │
    ▼                ▼
  test ──────────► test
    │                │
    ▼                ▼
 build ──────────► build
                    │
                    ▼
                 release
```

- **`test`**: Compiles and runs `hello-aurora/tests/` with plain `g++` on the runner. No ARM toolchain required. Runs on every push and PR.
- **`build`**: Installs `gcc-arm-none-eabi` from apt, checks out submodules, builds libDaisy, auto-discovers all firmware projects, and builds each with `FIRMWARE_VERSION` injected. Runs on every push and PR.
- **`release`**: Runs only on `v*` tag pushes. Downloads the `.bin` artifacts from `build` and creates a GitHub Release.

## Triggers

| Event | Jobs run |
|-------|----------|
| Push to any branch | `test`, `build` |
| Pull request | `test`, `build` |
| Push of `v*` tag | `test`, `build`, `release` |

## Version Injection

The git tag is the single source of truth for the version. No `VERSION` file or separate version commit is needed.

`config.mk` derives the version from git at build time:

```makefile
FIRMWARE_VERSION ?= $(shell git describe --tags --exact-match 2>/dev/null | sed 's/^v//' || echo dev)
```

- On a tagged commit (`v1.2.0`): `FIRMWARE_VERSION = 1.2.0`
- On any untagged commit: `FIRMWARE_VERSION = dev`

The `?=` means CI can override it by passing `FIRMWARE_VERSION=1.2.0` on the command line, ensuring the exact tag value is used in release builds.

### Binary naming

The top-level Makefile passes `TARGET` to the sub-make, naming the binary after both the project and the version:

```makefile
build:
    $(MAKE) -C $(PROJECT) TARGET=$(PROJECT)-$(FIRMWARE_VERSION)
```

This produces `<project>/build/<project>-<version>.bin` — e.g. `hello-aurora/build/hello-aurora-1.2.0.bin` on a tagged build, or `hello-aurora/build/hello-aurora-dev.bin` locally.

The `flash` target is updated to match:

```makefile
flash:
    cp "$(PROJECT)/build/$(PROJECT)-$(FIRMWARE_VERSION).bin" "$(MOUNT)/"
```

### Firmware version at runtime

The version is also injected as a preprocessor define via `C_DEFS`:

```makefile
build:
    $(MAKE) -C $(PROJECT) TARGET=$(PROJECT)-$(FIRMWARE_VERSION) \
        'C_DEFS=-DFIRMWARE_VERSION=\"$(FIRMWARE_VERSION)\"'
```

Accessible in firmware source:

```cpp
const char* version = FIRMWARE_VERSION; // "1.2.0" or "dev"
```

`C_DEFS` is the right hook: the libDaisy core Makefile declares it with `?=` (so our value takes effect) and then appends all required STM32 defines via `+=` (so they are not lost).

## Project Discovery

CI scans the repo root for subdirectories that contain a `Makefile`, excluding `lib/`, `docs/`, and dotdirectories:

```bash
find . -maxdepth 1 -mindepth 1 -type d \
  | while read dir; do
      [ -f "$dir/Makefile" ] && echo "${dir#./}"
    done \
  | grep -v -E '^(lib|docs|\.)'
```

Each discovered project is built in sequence. If a project fails, CI reports its name and continues building remaining projects so all failures are visible in a single run.

Built artifacts are collected from `<project>/build/<project>-<version>.bin` and uploaded as a single `firmware` workflow artifact. No renaming step is needed — the build already produces correctly versioned filenames.

## GitHub Release

The `release` job uses the GitHub CLI (`gh release create`) to:

- Set the release title to the tag name (e.g. `v1.2.0`)
- Auto-generate release notes listing commits since the previous tag (`--generate-notes`)
- Attach all `.bin` files from the `firmware` artifact (e.g. `hello-aurora-1.2.0.bin`)
- Publish immediately (no draft)

Required workflow permissions:

```yaml
permissions:
  contents: write
```

## Workflow Skeleton

```yaml
name: CI

on:
  push:
    branches: ["**"]
    tags: ["v*"]
  pull_request:

permissions:
  contents: write

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build and run host tests
        run: |
          cd hello-aurora/tests
          make
          ./test_colour
          ./test_audio

  build:
    needs: test
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install ARM toolchain
        run: sudo apt-get install -y gcc-arm-none-eabi
      - name: Derive version
        id: version
        run: |
          if [[ "${GITHUB_REF}" == refs/tags/v* ]]; then
            echo "version=${GITHUB_REF_NAME#v}" >> "$GITHUB_OUTPUT"
          else
            echo "version=dev" >> "$GITHUB_OUTPUT"
          fi
      - name: Build libDaisy
        run: make libdaisy
      - name: Discover and build projects
        run: |
          PROJECTS=$(find . -maxdepth 1 -mindepth 1 -type d \
            | while read dir; do [ -f "$dir/Makefile" ] && echo "${dir#./}"; done \
            | grep -v -E '^(lib|docs|\.)' )
          FAILED=0
          for PROJECT in $PROJECTS; do
            make build PROJECT="$PROJECT" \
              FIRMWARE_VERSION="${{ steps.version.outputs.version }}" \
              || { echo "BUILD FAILED: $PROJECT"; FAILED=1; }
          done
          [ $FAILED -eq 0 ]
      - name: Collect artifacts
        run: |
          mkdir -p dist
          find . -path '*/build/*.bin' -exec cp {} dist/ \;
      - uses: actions/upload-artifact@v4
        with:
          name: firmware
          path: dist/*.bin

  release:
    if: startsWith(github.ref, 'refs/tags/v')
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4
        with:
          name: firmware
          path: dist
      - name: Create GitHub Release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release create "${{ github.ref_name }}" \
            --repo "${{ github.repository }}" \
            --generate-notes \
            dist/*.bin
```

## Submodule Handling

The `test` job does not need submodules (tests build with `g++`, no SDK headers). The `build` job checks out with `submodules: recursive` to pull libDaisy and DaisySP through the Aurora SDK submodule tree.

## What Users Inherit When Forking

A user who forks or templates this repo and adds a new firmware project (e.g. `my-patch/`) gets CI for free: the discovery step will find and build it automatically. To cut a release they push a `v*` tag:

```bash
git tag v1.0.0
git push origin v1.0.0
```

GitHub Actions builds all projects, injects the version, and publishes a release with all `.bin` files attached.

Local builds also produce versioned filenames automatically — `git describe` in `config.mk` picks up the tag, so `make build PROJECT=hello-aurora` on a tagged commit produces `hello-aurora/build/hello-aurora-1.0.0.bin`. On untagged commits it produces `hello-aurora-dev.bin`.
