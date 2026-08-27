# Native ARM64 CI and Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add verified native Windows ARM64 CI/release artifacts and make the
existing macOS ARM64 build explicit, without adding Intel macOS jobs.

**Architecture:** CMake presets state the target architecture, the LLVM setup
action selects a matching complete Windows LLVM archive, and workflow matrices
carry architecture values all the way to artifact and MSI names.

**Tech Stack:** CMake/Ninja, GitHub Actions, PowerShell, WiX v5, official LLVM
release archives.

## Global Constraints

- No vcpkg: use normal CMake discovery and existing project-pinned fallback
  dependencies only.
- Native runner validation is mandatory for Windows ARM64 and macOS ARM64.
- Do not add Intel macOS CI or release packaging.
- Do not bump `VERSION`; include docs, changelog, TODO status, and tests in the
  final single commit.

---

### Task 1: Make architecture explicit in presets and LLVM provisioning

**Files:**
- Modify: `CMakePresets.json`
- Modify: `.github/actions/setup-llvm/action.yml`
- Modify: `test/cmake/windows_llvm_prerequisites/CMakeLists.txt`

- [x] Add `arm64-debug` / `arm64-release` Windows configure presets plus
  matching build and CTest presets, with distinct `out/build/arm64-*` paths.
- [x] Add `arm64-debug-macos` / `arm64-release-macos` presets with
  `CMAKE_OSX_ARCHITECTURES=arm64`; retain no macOS x64 execution target.
- [x] Give the LLVM composite action a Windows architecture input or matrix
  environment, select exactly `x86_64-pc-windows-msvc` or
  `aarch64-pc-windows-msvc`, and include that triple in its cache key.
- [x] Assert the resolved archive and compiler target match the requested
  architecture; failure to find an ARM64 archive is a configuration error.
- [x] Add a structural CMake/action regression for the ARM64 target contract,
  run it red before implementation and green afterward.

### Task 2: Run native ARM64 CI and package matching release artifacts

**Files:**
- Modify: `.github/workflows/cmake-multi-platform.yml`
- Modify: `.github/workflows/release.yml`
- Modify: `packaging/windows/build-msi.ps1`
- Modify: `packaging/windows/README.md`

- [x] Add Debug and Release Windows ARM64 jobs on `windows-11-vs2026-arm` and
  use the ARM64 presets/LLVM target.
- [x] Rename the current macOS jobs to ARM64 and use explicit ARM64 presets on
  `macos-26`; do not add an Intel matrix entry.
- [x] Make release build matrices explicit for macOS ARM64 and Windows x64 /
  ARM64. Run CTest for macOS release builds before packaging.
- [x] Parameterize `build-msi.ps1` with validated `x64|arm64`, distinct staging
  directories, file names, and `wix build -arch` values.
- [x] Produce `yona-<version>-windows-arm64.zip` and `.msi`, and
  `yona-<version>-macos-arm64.tar.gz`; preserve x64 Windows assets.
- [x] Add a static PowerShell/workflow contract test or validation that fails
  before the parameterization and passes after it.

### Task 3: Document actual support and close the tracking issue

**Files:**
- Modify: `INSTALL.md`
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/todo-list.md`
- Modify: `site/src/content/docs/install.md`
- Modify: this plan

- [x] Document native Windows ARM64 and macOS ARM64 release file names and the
  no-vcpkg toolchain policy.
- [ ] Mark the misleading macOS architecture bug fixed only after matrix and
  packaging validation passes.
- [ ] Run `pnpm exec astro build` and `git diff --check`.

### Task 4: Final verification

- [ ] Run Linux configure/build, focused CMake/packaging tests, full doctest,
  `ctest --preset unit-tests-linux --output-on-failure`, editor checks, and
  static workflow validation.
- [ ] Use the next GitHub Actions run as the native Windows ARM64/macOS ARM64
  execution verification; report any runner-only issue with its exact log.
- [ ] Make the one final commit only when every local check is green.
