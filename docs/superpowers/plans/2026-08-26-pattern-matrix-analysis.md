# Pattern-Matrix Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace shallow unreachable-pattern warnings with sound structural
pattern usefulness analysis for the supported pattern domains.

**Architecture:** Add a compiler-owned, LLVM-independent pattern-analysis
module that compares structural AST patterns and determines whether each
unguarded case arm has any value not covered by prior unguarded arms. `Codegen`
only adapts closed-ADT metadata and emits diagnostics; code generation is
unchanged.

**Tech Stack:** C++23, existing AST/type metadata, doctest, CMake/Ninja.

## Global Constraints

- Work on `master`; do not change `VERSION`.
- Warn only for provably unreachable unguarded arms.
- Keep guarded and unsupported patterns useful/conservative.
- Preserve finite-ADT/`Bool` exhaustiveness and `--require-effect-free`.
- Use test-driven development; run complete CTest and `git diff --check`.
- Update internal docs, public site, TODO, and `CHANGELOG.md` in the same change.
- Verify that `yls` publishes the improved overlap diagnostic with the same
  warning message/range; keep VS Code and Zed transport-only and update their
  syntax/query assets only when the grammar exposes a useful new pattern node.

---

### Task 1: Establish usefulness-analysis regressions

**Files:**
- Modify: `test/Codegen/CodegenTest.cpp`

**Produces:** Direct analysis and diagnostic regressions for nested patterns,
aliases/or-patterns, partial overlap, guards, and `--Werror`.

- [x] Add tests where `Some _` makes later `Some 1` unreachable, `(true, _)`
  makes later `(true, 0)` unreachable, and `[x | _]` makes later `[1 | xs]`
  unreachable.
- [x] Add tests where `Some 1` followed by `Some _`, distinct literals, and
  guarded arms remain reachable.
- [x] Add tests where `Some _ | None` makes a later `None` arm unreachable,
  and an alias around `Some _` retains that result.
- [x] Run `./out/build/x64-debug-linux/tests -tc='Case analysis*'` and confirm
  the nested cases are not yet reported unreachable.
- [x] Commit: `test: specify pattern-matrix overlap behavior`.

### Task 2: Introduce a compiler-independent pattern matrix

**Files:**
- Create: `include/yona/Semantics/PatternAnalysis.h`
- Create: `src/Semantics/PatternAnalysis.cpp`
- Modify: `CMakeLists.txt`
- Modify: `include/yona/Codegen/Codegen.h`

**Consumes:** `ast::PatternNode`, ADT constructor metadata, and case clauses.

**Produces:** `pattern_analysis::Result analyze_case(const ast::CaseExpr&,
const ConstructorLookup&)`, with `unreachable_clauses` and existing finite
coverage facts.

- [x] Define an LLVM-independent analysis interface with wildcard, constructor,
  literal, product, sequence-shape, alias, and alternative support; unsupported
  forms remain conservative.
- [x] Implement structural row coverage and finite-family defaulting: a
  candidate is useful unless a prior row or complete closed family proves it
  covered. Open families only prove identical literal/structural coverage.
- [x] Treat any guarded clause as absent from the coverage matrix.
- [x] Register `src/Semantics/PatternAnalysis.cpp` in the core CMake source list.
- [x] Build and run the new focused tests; expect all to pass.
- [x] Commit: `feat: add typed pattern-matrix analysis`.

### Task 3: Route Codegen diagnostics through the shared analysis

**Files:**
- Modify: `src/Codegen/CodegenCase.cpp`
- Modify: `include/yona/Codegen/Codegen.h`
- Modify: `test/Codegen/CodegenTest.cpp`
- Modify: `test/Toolchain/YonaScriptTest.cpp`

**Produces:** Existing `Codegen::analyze_case_patterns` delegates to the new
module; `-Woverlapping-patterns` has the precise unreachable-only contract.

- [x] Replace the local `collect`/set-based unreachable logic in
  `Codegen::analyze_case_patterns` with a constructor-lookup adapter and
  shared analysis result.
- [x] Change the warning text to `unreachable pattern: earlier unguarded arms
  already cover every value it can match`.
- [x] Add CLI checks for explicit warning enablement, `--Wall`, and `--Werror`.
- [x] Re-run prior finite ADT/Bool strict-gate tests to prove no behavioral
  regression.
- [x] Commit: `feat: diagnose nested unreachable patterns`.

### Task 4: Document the precise guarantee and complete verification

**Files:**
- Modify: `docs/pattern-matching.md`
- Modify: `docs/error-codes.md`
- Modify: `docs/type-system-status.md`
- Modify: `docs/todo-list.md`
- Modify: `site/src/content/docs/`
- Modify: `CHANGELOG.md`

- [x] Document supported structural patterns, conservative guards/open
  domains, and that partial intersections are not warnings.
- [x] Move sound structural overlap analysis from #5's remaining work to
  completed; retain general termination and arbitrary open-domain coverage.
- [x] Run `cmake --build --preset build-debug-linux -j2`,
  `ctest --preset unit-tests-linux --output-on-failure`, and `git diff --check`.
- [x] Commit: `docs: complete pattern overlap analysis`.

### Task 5: Verify LSP and editor integration

**Files:**
- Modify: `test/Lsp/LspTest.cpp` only if the protocol regression exposes a gap
- Modify: `editors/zed/` only if a new grammar node needs a query

- [x] Add an `yls` diagnostic regression for a nested unreachable arm; assert
  its warning message and range
  selects the later arm.
- [x] Run the VS Code extension test suite and the Zed manifest/package smoke
  checks. Do not add editor-local pattern analysis.
- [x] Commit any required editor-facing regression or query/documentation
  update with the compiler feature.
