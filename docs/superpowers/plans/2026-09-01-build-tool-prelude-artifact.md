# Build Tool Prelude Artifact Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CMake-built Yona tools and their sibling compiler link the
active build's generated Prelude object instead of an ignored `lib/Prelude.o`.

**Architecture:** Promote the deterministic Prelude custom command from a
test-only artifact to a build-wide dependency. Have `yona_add_executable`
search that artifact directory before source `lib/`, and have `yonac` prefer
the matching `--sysroot/artifacts` object before arbitrary module paths. The
remaining standard-library interfaces still come from source `lib/`.

**Tech Stack:** CMake/Ninja custom commands, C++23 `yonac`, CTest.

## Global Constraints

- Do not require, modify, or delete ignored `lib/Prelude.o` files.
- Keep `yona_add_executable(SOURCE ... [OUTPUT_NAME ...])` unchanged.
- Preserve and validate installed-consumer behavior.
- Add the stale-object regression before altering the build graph.

---

### Task 1: Define the build-wide Prelude artifact

**Files:**

- Modify: `CMakeLists.txt:618-644`
- Modify: `test/Support/YonaTestConfig.h.in` only if the existing test macro is
  renamed

**Produces:** `YONA_BUILD_ARTIFACT_DIR`, `YONA_BUILD_PRELUDE_OBJECT`,
`YONA_BUILD_PRELUDE_INTERFACE`, and target `yona_build_prelude_object`. Retain
the existing `YONA_TEST_PRELUDE_*` variables and `yona_test_prelude_object` as
aliases for test compatibility.

- [x] **Step 1: Reproduce the stale-object failure**

With an existing ignored `lib/Prelude.o` that references the retired
`yona_rt_*` ABI, the in-tree `yona` target failed to link against the current
`YonaRuntime*` archive. This directly exercises the actual helper and is the
regression scenario for the dependency change below.

- [x] **Step 2: Promote the current custom command**

Replace `YONA_TEST_ARTIFACT_DIR` as the command's owner with:

```cmake
set(YONA_BUILD_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set(YONA_BUILD_PRELUDE_OBJECT
  "${YONA_BUILD_ARTIFACT_DIR}/Prelude${CMAKE_C_OUTPUT_EXTENSION}")
set(YONA_BUILD_PRELUDE_INTERFACE
  "${YONA_BUILD_ARTIFACT_DIR}/Prelude.yonai")
add_custom_target(yona_build_prelude_object DEPENDS
  "${YONA_BUILD_PRELUDE_OBJECT}")
```

Alias the existing test variables to these paths and make
`yona_test_prelude_object` depend on `yona_build_prelude_object`.

- [x] **Step 3: Verify producer output**

Ran `cmake --build --preset build-debug-linux --target
yona_build_prelude_object`; `nm -u out/build/x64-debug-linux/artifacts/Prelude.o`
contains only the current `YonaRuntime*` ABI, not `yona_rt_*` symbols.

### Task 2: Wire tool targets to the current Prelude

**Files:**

- Modify: `cmake/YonaTools.cmake:34-46`
- Test: `test/CMake/BuildToolPrelude/RunBuildToolPrelude.cmake`

**Consumes:** Task 1's artifact variables and target. **Preserves:** the public
`yona_add_executable` signature.

- [x] **Step 1: Change the helper command and dependency graph**

Use this ordering in its custom command:

```cmake
COMMAND ${CMAKE_COMMAND} -E env "YONAC_CC=${CCompiler}"
  $<TARGET_FILE:yonac> --sysroot "${CMAKE_BINARY_DIR}"
  -I "${YONA_BUILD_ARTIFACT_DIR}"
  -I "${CMAKE_SOURCE_DIR}/lib"
  -o "${OutputPath}" "${SourcePath}"
DEPENDS yonac yona_runtime yona_build_prelude_object
  "${SourcePath}" ${YonaStdlibInputs}
```

The artifact path must remain first because `cli/Main.cpp` must link the
Prelude generated for the selected runtime.

- [x] **Step 2: Run the failure regression and actual tools**

Built targets `yona yls-yona` and invoked
`out/build/x64-debug-linux/yona --version` successfully while the stale
legacy-ABI `lib/Prelude.o` remained in place.

### Task 3: Keep direct `yonac` and `yona` invocations ABI-matched

**Files:**

- Modify: `cli/Main.cpp`
- Modify: `test/Toolchain/YonaScriptTest.cpp`
- Modify: `docs/todo-list.md`

- [x] **Step 1: Add a deterministic stale-object regression**

The test creates a non-object local `Prelude.o`, invokes `yonac` with the
active build as `--sysroot`, and verifies it compiles and runs `1 + 2`.
Before the precedence rule it selects the local object and fails to link.

- [x] **Step 2: Prefer the active sysroot artifact**

`yonac` now finds `artifacts/Prelude.o` (`.obj` on Windows) under its selected
runtime sysroot before the legacy module-search fallback. This makes an
in-tree `yona` runner invocation use the Prelude generated alongside its
runtime, even if an earlier partial sysroot has a stale artifact. A direct
runner regression keeps a stale object beside the script source.

### Task 4: Package the matching artifact

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `cmake/YonaConfig.cmake.in`
- Modify: `test/CMake/InstalledConsumer/RunInstalledConsumer.cmake`
- Modify: `test/CMake/InstalledConsumer/hello.yona`

- [x] **Step 1: Make artifact invalidation and installation explicit**

The Prelude custom command now directly depends on `yona_runtime`, so a runtime
archive rebuild recompiles the artifact. Installation copies that artifact to
`${libdir}/yona/artifacts`, and `YonaConfig.cmake` rejects a package without it.

- [x] **Step 2: Exercise an installed Prelude value**

The installed CMake-consumer fixture compiles and runs `Some 1`, which verifies
that the packaged compiler can use the installed artifact rather than a
caller-local object.

### Task 5: Close and verify

**Files:**

- Modify: `test/Codegen/CodegenTest.cpp:750-765`
- Modify: `docs/todo-list.md`
- Modify: `CHANGELOG.md`
- Modify: this plan

- [x] **Step 1: Clarify the existing artifact test**

Rename it to `CMake provides the active-build Prelude object` while retaining
the assertions that its path is absolute, exists, and differs from
`lib/Prelude.o`.

- [x] **Step 2: Run the completion gate**

Run the Linux configure preset, debug build preset, unit-test CTest preset, and
`git diff --check`. Expect every target and test to pass.

- [x] **Step 3: Record the fix**

Checked off the stale-Prelude bugs, added the Unreleased changelog entry, and
completed the Linux configure/build/CTest gate with all 10 CTest entries
passing.

## Self-Review

The plan fixes the actual dependency boundary: it builds the Prelude once with
the runtime as a direct dependency, selects the artifact belonging to the
runtime actually chosen for linking, installs that artifact with the package,
and tests CMake tools, direct `yonac`, the `yona` runner, and installed CMake
consumers. It does not alter runtime symbols or hide the linker error by
removing source artifacts.

## Execution Handoff

Plan complete and saved to
`docs/superpowers/plans/2026-09-01-build-tool-prelude-artifact.md`.

1. Subagent-Driven (recommended) — fresh reviewer per task.
2. Inline Execution — execute the tasks in this session with checkpoints.
