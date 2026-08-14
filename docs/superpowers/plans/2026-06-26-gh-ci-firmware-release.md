# GitHub CI Firmware Build and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a single GitHub Actions workflow that runs host tests and builds versioned firmware on every push, and publishes a GitHub Release with attached `.bin` files when a `v*` tag is pushed.

**Architecture:** Version is derived from `git describe` in `config.mk` and passed as `TARGET` and `C_DEFS` overrides to the sub-make, so both local and CI builds produce identically named `<project>-<version>.bin` artifacts. CI uses three jobs (`test`, `build`, `release`) in a single workflow file; `release` is gated on a `v*` tag condition.

**Tech Stack:** GNU Make, GitHub Actions, `gcc-arm-none-eabi` (apt), `gh` CLI (pre-installed on `ubuntu-latest` runners), `actions/checkout@v4`, `actions/upload-artifact@v4`, `actions/download-artifact@v4`.

## Global Constraints

- Workflow file: `.github/workflows/ci.yml` — single file, no reusable workflows.
- Toolchain: `gcc-arm-none-eabi` installed via `apt-get` on `ubuntu-latest`.
- Versioned binary name format: `<project>-<version>.bin` (e.g. `hello-aurora-1.2.0.bin`).
- Version on untagged commits: `dev`.
- Version source: git tag only — no `VERSION` file.
- `C_DEFS` is the preprocessor hook in libDaisy core Makefile (declared with `?=`, extended with `+=`).
- Release: immediate publish (no draft), auto-generated notes, all `.bin` files attached.
- Workflow `permissions: contents: write` required for release creation.

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `config.mk` | Modify | Add `FIRMWARE_VERSION` derivation from `git describe` |
| `Makefile` | Modify | Pass `TARGET` and `C_DEFS` overrides in `build`; update `flash` binary path |
| `.github/workflows/ci.yml` | Create | Three-job CI workflow: `test`, `build`, `release` |

---

## Task 1: Version-aware build and flash

**Files:**
- Modify: `config.mk`
- Modify: `Makefile`

**Interfaces:**
- Produces: `<project>/build/<project>-<version>.bin` from `make build PROJECT=<project>`
- Produces: `FIRMWARE_VERSION` make variable available to all project Makefiles

- [ ] **Step 1: Add FIRMWARE_VERSION to `config.mk`**

Append these two lines to `config.mk` after the existing `LIBDAISY_DIR` line:

```makefile
# Firmware version: exact git tag (strip leading v), or "dev" on untagged commits.
# CI overrides this by passing FIRMWARE_VERSION=x.y.z on the command line.
FIRMWARE_VERSION ?= $(shell git describe --tags --exact-match 2>/dev/null | sed 's/^v//' || echo dev)
```

Full resulting `config.mk`:

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

# Firmware version: exact git tag (strip leading v), or "dev" on untagged commits.
# CI overrides this by passing FIRMWARE_VERSION=x.y.z on the command line.
FIRMWARE_VERSION ?= $(shell git describe --tags --exact-match 2>/dev/null | sed 's/^v//' || echo dev)
```

- [ ] **Step 2: Verify FIRMWARE_VERSION resolves correctly**

```bash
make -f config.mk -n 2>/dev/null || true
# Simpler check — evaluate just the variable:
make --no-print-directory -f /dev/stdin <<'EOF'
include config.mk
check:
	@echo "FIRMWARE_VERSION=$(FIRMWARE_VERSION)"
EOF
```

On an untagged commit you should see:
```
FIRMWARE_VERSION=dev
```

If you're on a tagged commit (e.g. `git tag v1.0.0 HEAD`) it should show:
```
FIRMWARE_VERSION=1.0.0
```

- [ ] **Step 3: Update `build` target in `Makefile`**

Replace the `build` and `flash` targets. Full resulting `Makefile`:

```makefile
include config.mk

.PHONY: build flash libdaisy

build:
ifndef PROJECT
	$(error PROJECT is not set. Usage: make build PROJECT=hello-aurora)
endif
	$(MAKE) -C $(PROJECT) TARGET=$(PROJECT)-$(FIRMWARE_VERSION) \
		'C_DEFS=-DFIRMWARE_VERSION=\"$(FIRMWARE_VERSION)\"'

flash:
ifndef PROJECT
	$(error PROJECT is not set. Usage: make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA)
endif
ifndef MOUNT
	$(error MOUNT is not set. Usage: make flash PROJECT=hello-aurora MOUNT=/media/dave/AURORA)
endif
	@test -d "$(MOUNT)" || (echo "Error: $(MOUNT) is not mounted or does not exist."; exit 1)
	cp "$(PROJECT)/build/$(PROJECT)-$(FIRMWARE_VERSION).bin" "$(MOUNT)/"
	sync
	@echo "Flashed $(PROJECT)-$(FIRMWARE_VERSION).bin to $(MOUNT)/"

libdaisy:
	$(MAKE) -C lib/Aurora-SDK/libs/libDaisy GCC_PATH=$(GCC_PATH)
```

Note: indentation in Makefiles must be tabs, not spaces.

- [ ] **Step 4: Build hello-aurora and verify the binary name**

```bash
make build PROJECT=hello-aurora
```

Expected: build succeeds and produces:
```
hello-aurora/build/hello-aurora-dev.bin
```

(On an untagged commit. On a tagged commit it would be `hello-aurora-1.0.0.bin`.)

Check:
```bash
ls hello-aurora/build/*.bin
```

Expected output:
```
hello-aurora/build/hello-aurora-dev.bin
```

- [ ] **Step 5: Verify FIRMWARE_VERSION reaches the compiler**

The single-quoted `'C_DEFS=-DFIRMWARE_VERSION=\"dev\"'` in the Makefile recipe survives two shell levels: make passes the literal backslash-quoted form to the sub-make, and the sub-make's recipe shell converts `\"` to `"` before passing to `gcc`. Confirm the define appears in the build output:

```bash
make build PROJECT=hello-aurora 2>&1 | grep DFIRMWARE_VERSION
```

Expected: a compiler invocation line containing `-DFIRMWARE_VERSION=\"dev\"` (with backslash-escaped quotes as printed by make before the shell processes them).

- [ ] **Step 6: Commit**

```bash
git add config.mk Makefile
git commit -m "build: derive firmware version from git tag, embed in binary name"
```

---

## Task 2: CI workflow — test and build jobs

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `hello-aurora/tests/Makefile` (runs `make` which builds and executes tests)
- Consumes: `Makefile` targets `libdaisy` and `build`
- Produces: `firmware` workflow artifact containing `<project>-<version>.bin` files

- [ ] **Step 1: Create the workflow directory and file**

```bash
mkdir -p .github/workflows
```

Create `.github/workflows/ci.yml`:

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
        run: make -C hello-aurora/tests

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
            | grep -v -E '^(lib|docs|\.)')
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
```

- [ ] **Step 2: Validate YAML syntax locally**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo "YAML valid"
```

Expected:
```
YAML valid
```

- [ ] **Step 3: Commit and push to GitHub**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add test and build jobs"
git push
```

- [ ] **Step 4: Verify CI passes on GitHub**

Go to the repository's **Actions** tab on GitHub. The most recent push should show a workflow run with two jobs: `test` and `build`, both green.

Check the `build` job's "Collect artifacts" step — it should show `hello-aurora-dev.bin` being found. The `firmware` artifact should be downloadable from the workflow summary page and contain `hello-aurora-dev.bin`.

If `test` fails: check the `make -C hello-aurora/tests` step output. The tests Makefile downloads `doctest.h` via `curl` — if that fails, it's a network issue on the runner (unlikely on GitHub-hosted runners).

If `build` fails at `make libdaisy`: check that `submodules: recursive` checked out the full submodule tree. The step output should show ARM toolchain commands.

---

## Task 3: CI workflow — release job

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `firmware` artifact from `build` job (contains `<project>-<version>.bin` files)
- Produces: GitHub Release at `https://github.com/<owner>/<repo>/releases/tag/<tag>`

- [ ] **Step 1: Add the release job to `.github/workflows/ci.yml`**

Append the following to the `jobs:` section of `.github/workflows/ci.yml` (after the `build` job):

```yaml
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

- [ ] **Step 2: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo "YAML valid"
```

Expected:
```
YAML valid
```

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add release job triggered by v* tags"
git push
```

- [ ] **Step 4: Tag and push to trigger a release**

```bash
git tag v0.1.0
git push origin v0.1.0
```

- [ ] **Step 5: Verify the release on GitHub**

Go to the repository's **Actions** tab. The tag push should trigger a workflow run showing all three jobs: `test`, `build`, `release` — all green.

Go to **Releases** on the repository page. You should see a release named `v0.1.0` with:
- Auto-generated release notes (list of commits since the previous tag, or all commits if this is the first tag)
- `hello-aurora-0.1.0.bin` attached as a release asset

Download the `.bin` file and verify the filename matches the format `<project>-<version>.bin`.

If the `release` job fails with a permissions error: confirm `permissions: contents: write` is present at the top level of the workflow (not inside a job). This is already in the workflow as written.

If `dist/*.bin` glob fails (no files found): check the `build` job's artifact upload step — the artifact must have uploaded at least one file. A glob that matches nothing causes `gh release create` to error.
