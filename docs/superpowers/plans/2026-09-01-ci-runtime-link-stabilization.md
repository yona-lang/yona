# CI Runtime Link Stabilization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the generated-program linker and CI quality checks use the same native runtime dependencies as the CMake build on every supported platform.

**Architecture:** CMake resolves PCRE2 and instrumentation options once, serializes their concrete linker arguments into generated build configuration, and both `yonac` and in-tree fixture link helpers consume that configuration after `yona_runtime`. Runtime ownership and release-test liveness are investigated independently and receive minimal reproducer-led fixes only after the failing path is identified.

**Tech Stack:** CMake, C++23, Clang/LLVM, PCRE2, doctest, Python quality runner.

## Global Constraints

- Preserve the canonical `yona_runtime` archive as the only Yona runtime artifact.
- Link native dependencies after `yona_runtime` for static-link ordering.
- Do not rely on package-manager default linker search paths.
- Keep normal builds free of sanitizer options.
- Keep 80% critical-path coverage thresholds unchanged.
- Do not change public language semantics.

---

### Task 1: Reproduce and isolate current failures

**Files:**
- Modify: `docs/todo-list.md` only if a newly isolated bug is not already recorded.
- Test: `test/Codegen/PatternOwnershipTest.cpp:571`
- Test: `out/build/x64-release-linux/tests`

**Interfaces:**
- Consumes: the active Linux debug/release build presets.
- Produces: one exact failing doctest filter for the release timeout and a confirmed allocation-stat reproduction for the raw-channel leak.

- [ ] **Step 1: Run the raw-channel ownership regression**

Run: `./out/build/x64-debug-linux/tests -tc="*raw channel natives consume references on all return paths*"`

Expected: either a zero-leak pass, or output containing the precise leaked runtime tags.

- [ ] **Step 2: Bisect the release doctest timeout by test suite**

Run: `./out/build/x64-release-linux/tests --list-test-cases` followed by filtered runs using `-ts="<suite>"` until one suite reproduces the hang.

Expected: one doctest filter that can be run under `timeout 180` and exits non-zero due to timeout.

- [ ] **Step 3: Record newly discovered repros before production edits**

Append a one-line command and observed failure to `docs/todo-list.md` only for failures not already represented there; otherwise retain the existing item.

### Task 2: Define concrete external runtime link arguments

**Files:**
- Modify: `CMakeLists.txt:142-250,521-547,623-640`
- Modify: `test/Support/YonaTestConfig.h.in`
- Modify: `test/Toolchain/YonaLinkUtil.h:119-220`
- Test: `test/Toolchain/LinkerPlanTest.cpp`

**Interfaces:**
- Consumes: `${YONA_PCRE2_TARGET}`, `${CMAKE_EXE_LINKER_FLAGS}`, and CMake target imported locations.
- Produces: `YONA_RUNTIME_EXTERNAL_LINK_ARGS` and `YONA_TEST_RUNTIME_EXTERNAL_LINK_ARGS`, semicolon-separated CMake lists expanded as individual linker arguments.

- [ ] **Step 1: Write failing linker-plan assertions**

Add a doctest case that constructs the configured external-link argument list and requires a resolved PCRE2 path or archive when regex support is enabled; require no `-fsanitize` argument in an ordinary build.

- [ ] **Step 2: Run the focused test and verify the pre-fix failure**

Run: `./out/build/x64-debug-linux/tests -tc="*external runtime link arguments*"`

Expected: FAIL because the current configuration exposes only the optional bundled archive and falls back to `-lpcre2-8`.

- [ ] **Step 3: Generate the link manifest from CMake resolution**

In `CMakeLists.txt`, resolve the selected PCRE2 target's concrete archive or shared-library location. Build an ordered list containing its absolute path and, for shared non-Windows libraries, the matching runtime search-path argument. Append the CMake executable linker options only when they contain sanitizer instrumentation. Configure these lists into the compiler and test configuration headers.

- [ ] **Step 4: Consume the configured list in test links**

Replace `pcreLinkArguments()` fallback reconstruction in `YonaLinkUtil.h` with the configured external-link arguments. Keep the public helper API unchanged so existing codegen tests link their generated objects through the manifest.

- [ ] **Step 5: Run the focused test and verify it passes**

Run: `cmake --preset x64-debug-linux && cmake --build --preset build-debug-linux --target tests && ./out/build/x64-debug-linux/tests -tc="*external runtime link arguments*"`

Expected: exit 0.

### Task 3: Make `yonac` consume the runtime link manifest

**Files:**
- Modify: `CMakeLists.txt:521-547`
- Modify: `cli/Main.cpp:1134-1298`
- Test: `test/CMake/WindowsLlvmPrerequisites/CMakeLists.txt:73-91`
- Test: `test/Codegen/CodegenTest.cpp`

**Interfaces:**
- Consumes: generated compile definitions or header constants describing `YONA_RUNTIME_EXTERNAL_LINK_ARGS`.
- Produces: the same ordered argument sequence on both in-process LLD and external compiler-driver links.

- [ ] **Step 1: Extend the source-contract test first**

Require the compiler source contract to use the configured external-runtime argument collection after runtime and Prelude objects, and reject literal `-lpcre2-8` reconstruction as the production dependency source.

- [ ] **Step 2: Run the source-contract test and verify the pre-fix failure**

Run: `ctest --test-dir out/build/x64-debug-linux -R cmake_windows_llvm_prerequisites --output-on-failure`

Expected: FAIL because `cli/Main.cpp` still derives `pcre2_link_arg` from `packaged_pcre2` or `-lpcre2-8`.

- [ ] **Step 3: Replace PCRE2-specific linker branches with manifest application**

Add one helper in `cli/Main.cpp` that appends configured runtime external arguments. Invoke it after the runtime archive in both LLD and compiler-driver link sequences. Retain OS system libraries and Vulkan arguments as separate platform responsibilities.

- [ ] **Step 4: Add a regex generated-program regression**

Add a minimal codegen fixture that imports and invokes `Std\\Regex`; assert its native executable prints its expected match result. This proves the final generated-program link resolves PCRE2.

- [ ] **Step 5: Run the contract and fixture tests**

Run: `cmake --build --preset build-debug-linux --target tests && ./out/build/x64-debug-linux/tests -tc="*Regex*" && ctest --test-dir out/build/x64-debug-linux -R cmake_windows_llvm_prerequisites --output-on-failure`

Expected: all commands exit 0.

### Task 4: Preserve instrumentation across generated-program links

**Files:**
- Modify: `CMakeLists.txt:521-547`
- Modify: `cli/Main.cpp:1134-1298`
- Test: `scripts/quality.py:847-884`

**Interfaces:**
- Consumes: CMake executable linker options containing `-fsanitize=...`.
- Produces: generated Yona executable link commands with identical sanitizer options when, and only when, the compiler itself was instrumented.

- [ ] **Step 1: Write a source-level regression assertion**

Add a focused test or source contract that configures `-fsanitize=address,undefined`, builds `yonac`, and asserts an emitted executable links successfully against the instrumented runtime.

- [ ] **Step 2: Run it and verify the pre-fix undefined sanitizer symbols**

Run: `python3 scripts/quality.py sanitize --build-dir out/build/x64-debug-linux`

Expected: FAIL during a generated-program link with unresolved `__asan_*` or `__ubsan_*` symbols.

- [ ] **Step 3: Reuse the manifest sanitizer arguments in both linker paths**

Append the generated sanitizer arguments after all static archives in the in-process LLD and compiler-driver paths. Do not add them to a non-instrumented configuration.

- [ ] **Step 4: Verify sanitizer quality**

Run: `python3 scripts/quality.py sanitize --build-dir out/build/x64-debug-linux`

Expected: address and thread sanitizer configurations build and run their registered tests without linker failures.

### Task 5: Repair the isolated ownership and liveness failures

**Files:**
- Modify: source file identified by Task 1's raw-channel repro.
- Modify: source or test file identified by Task 1's release-timeout repro.
- Test: `test/Codegen/PatternOwnershipTest.cpp:571`

**Interfaces:**
- Consumes: the exact test-case filters isolated in Task 1.
- Produces: zero allocation leaks for raw channel operations and a release test that completes without timeout.

- [ ] **Step 1: Add the narrowest failing regression test for each isolated path**

Keep the raw-channel test's allocation-stat assertions and add one filterable regression for the timeout-producing operation. Do not change global CTest timeout settings.

- [ ] **Step 2: Run each regression and verify failure before edits**

Run: `./out/build/x64-debug-linux/tests -tc="*raw channel natives consume references on all return paths*"` and `timeout 180 ./out/build/x64-release-linux/tests -tc="<isolated timeout filter>"`.

Expected: the first reports leaked allocations on the affected configuration; the second times out before the fix.

- [ ] **Step 3: Correct the responsible ownership or blocking path**

Make the smallest change that gives every consuming raw-channel return path exactly one release and removes the isolated wait-cycle or unbounded operation. Do not suppress allocation reporting or lengthen the global timeout.

- [ ] **Step 4: Verify both regressions**

Run the two Task 5 commands again.

Expected: zero leaked allocations and exit 0 before 180 seconds.

### Task 6: Restore quality gates and document completion

**Files:**
- Modify: CMake files reported by `gersemi`.
- Modify: `docs/todo-list.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-09-01-ci-runtime-link-stabilization.md`

**Interfaces:**
- Consumes: completed regression evidence from Tasks 2-5.
- Produces: formatted CMake files, completed plan checkboxes, and Unreleased release notes.

- [ ] **Step 1: Run the canonical formatter**

Run: `gersemi --in-place CMakeLists.txt cmake test/CMake tools/CMakeLists.txt`.

- [ ] **Step 2: Update completion records**

Mark completed plan tasks, remove only resolved todo entries, and add an Unreleased changelog entry describing portable external runtime dependency linking and generated-program sanitizer support.

- [ ] **Step 3: Run final verification**

Run: `cmake --preset x64-debug-linux && cmake --build --preset build-debug-linux && ctest --preset unit-tests-linux && python3 scripts/quality.py static --build-dir out/build/x64-debug-linux && python3 scripts/quality.py coverage --build-dir out/build/x64-debug-linux`.

Expected: all commands exit 0, formatting reports no drift, and critical coverage remains at or above 80%.

- [ ] **Step 4: Commit the completed changes**

Run: `git add CMakeLists.txt cmake cli test scripts docs CHANGELOG.md && git commit -m "fix: stabilize generated runtime linking"`.
