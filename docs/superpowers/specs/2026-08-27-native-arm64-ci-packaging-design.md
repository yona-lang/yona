# Native Windows and macOS ARM64 CI/Packaging Design

## Goal

Ship and continuously test native Windows ARM64 and macOS ARM64 binaries with
architecture-correct CI labels, build directories, release asset names, and
installer metadata. Intel macOS is deliberately out of scope.

## Constraints

- Do not use vcpkg. Continue ordinary CMake package discovery first and the
  repository's pinned `FetchContent` fallback only where it already applies.
- Build on native hosted runners; do not claim a cross-compiled binary is
  tested as native.
- Keep the existing Linux x64/ARM64 and Windows x64 support intact.
- A release must name every binary and installer by its actual architecture.
- Windows ARM64 must use an ARM64 LLVM package and an ARM64 MSI, not an x64
  toolchain or installer with a renamed file.

## Architecture

`CMakePresets.json` owns concrete target architecture. macOS presets set
`CMAKE_OSX_ARCHITECTURES` explicitly because Ninja's external architecture
metadata does not select an architecture. `macos-26` is the ARM64 job; no
Intel macOS job is added.

The shared LLVM setup action accepts (or derives) a Windows LLVM target triple
and downloads only the matching official complete LLVM archive. It fails
clearly if the requested release has no matching archive rather than silently
falling back to x64.

The CI and release workflows use explicit architecture matrix fields to select
presets, runner labels, artifact paths, and package names. The Windows packer
receives an architecture parameter that drives its isolated staging directory,
MSI output name, and `wix build -arch` value.

## Verification

- Static workflow/preset tests assert ARM64 runner, preset, LLVM triple, and
  artifact wiring.
- Existing CMake prerequisite test remains vcpkg-free and covers the ARM64 DIA
  target branch structurally; native GitHub ARM64 jobs are the final execution
  proof.
- Linux CI verifies YAML syntax/commands indirectly through the existing build
  tests; a tagged/PR GitHub run provides native Windows/macOS execution.
- Release packaging runs CTest on macOS as well as Linux and Windows before
  uploading architecture-labelled artifacts.

## User-facing scope

Installation documentation lists native macOS ARM64 and Windows ARM64 archive
names. It states that Intel macOS release artifacts are not part of this
matrix, while Homebrew remains source-built and architecture-native.
