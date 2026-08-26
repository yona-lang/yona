# Pattern-Matrix Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace shallow unreachable-pattern warnings with sound typed
pattern-matrix usefulness analysis.

**Architecture:** Add a compiler-owned, LLVM-independent pattern-analysis
module that normalizes AST patterns and determines whether each unguarded case
arm has any value not covered by prior unguarded arms. `Codegen` remains the
compatibility API and diagnostic emitter; code generation is unchanged.

**Tech Stack:** C++23, existing AST/type metadata, doctest, CMake/Ninja.

## Global Constraints

- Work on `master`; do not change `VERSION`.
- Warn only for provably unreachable unguarded arms.
- Keep guarded and unsupported patterns useful/conservative.
- Preserve finite-ADT/`Bool` exhaustiveness and `--require-effect-free`.
- Use test-driven development; run complete CTest and `git diff --check`.
- Update internal docs, public site, TODO, and `CHANGELOG.md` in the same change.
- Verify that `yls` publishes the improved overlap diagnostic with the same
  warning code/range; keep VS Code and Zed transport-only and update their
  syntax/query assets only when the grammar exposes a useful new pattern node.

---

### Task 1: Establish usefulness-analysis regressions

**Files:**
- Modify: `test/codegen_test.cpp`

**Produces:** Direct analysis and diagnostic regressions for nested patterns,
aliases/or-patterns, partial overlap, guards, and `--Werror`.

- [ ] Add tests where `Some _` makes later `Some 1` unreachable, `(true, _)`
  makes later `(true, 0)` unreachable, and `[x | _]` makes later `[1 | xs]`
  unreachable.
- [ ] Add tests where `Some 1` followed by `Some _`, distinct literals, and
  guarded arms remain reachable.
- [ ] Add tests where `Some _ | None` makes a later `None` arm unreachable,
  and an alias around `Some _` retains that result.
- [ ] Run `./out/build/x64-debug-linux/tests -tc='Case analysis*'` and confirm
  the nested cases are not yet reported unreachable.
- [ ] Commit: `test: specify pattern-matrix overlap behavior`.

### Task 2: Introduce a compiler-independent pattern matrix

**Files:**
- Create: `include/PatternAnalysis.h`
- Create: `src/PatternAnalysis.cpp`
- Modify: `CMakeLists.txt`
- Modify: `include/Codegen.h`

**Consumes:** `ast::PatternNode`, ADT constructor metadata, and case clauses.

**Produces:** `pattern_analysis::Result analyze_case(const ast::CaseExpr&,
const ConstructorLookup&)`, with `unreachable_clauses` and existing finite
coverage facts.

- [ ] Define a normalized IR with wildcard, constructor, literal, product,
  sequence-shape, opaque, and alternative nodes. Strip aliases and expand
  or-patterns into separate rows.
- [ ] Implement matrix specialization and defaulting: a candidate is useful
  when at least one normalized row is useful against the matrix of prior rows.
  Closed families enumerate known constructors; open families only prove
  identical-literal or wildcard coverage.
- [ ] Treat any guarded clause as absent from the coverage matrix.
- [ ] Register `src/PatternAnalysis.cpp` in the core CMake source list.
- [ ] Build and run the new focused tests; expect all to pass.
- [ ] Commit: `feat: add typed pattern-matrix analysis`.

### Task 3: Route Codegen diagnostics through the shared analysis

**Files:**
- Modify: `src/codegen/CodegenCase.cpp`
- Modify: `include/Codegen.h`
- Modify: `test/codegen_test.cpp`
- Modify: `test/yona_script_test.cpp`

**Produces:** Existing `Codegen::analyze_case_patterns` delegates to the new
module; `-Woverlapping-patterns` has the precise unreachable-only contract.

- [ ] Replace the local `collect`/set-based unreachable logic in
  `Codegen::analyze_case_patterns` with a constructor-lookup adapter and
  shared analysis result.
- [ ] Change the warning text to `unreachable pattern: earlier unguarded arms
  already cover every value it can match`.
- [ ] Add CLI checks for explicit warning enablement, `--Wall`, and `--Werror`.
- [ ] Re-run prior finite ADT/Bool strict-gate tests to prove no behavioral
  regression.
- [ ] Commit: `feat: diagnose nested unreachable patterns`.

### Task 4: Document the precise guarantee and complete verification

**Files:**
- Modify: `docs/pattern-matching.md`
- Modify: `docs/error-codes.md`
- Modify: `docs/type-system-status.md`
- Modify: `docs/todo-list.md`
- Modify: `site/src/content/docs/`
- Modify: `CHANGELOG.md`

- [ ] Document supported structural patterns, conservative guards/open
  domains, and that partial intersections are not warnings.
- [ ] Move complete-overlap analysis from #5's remaining work to completed;
  retain general termination and arbitrary open-domain exhaustiveness.
- [ ] Run `cmake --build --preset build-debug-linux -j2`,
  `ctest --preset unit-tests-linux --output-on-failure`, and `git diff --check`.
- [ ] Commit: `docs: complete pattern overlap analysis`.

### Task 5: Verify LSP and editor integration

**Files:**
- Modify: `test/lsp_test.cpp` only if the protocol regression exposes a gap
- Modify: `editors/zed/` only if a new grammar node needs a query

- [ ] Add an `yls` diagnostic regression for a nested unreachable arm; assert
  `overlapping-patterns` remains the published warning code and its range
  selects the later arm.
- [ ] Run the VS Code extension test suite and the Zed manifest/package smoke
  checks. Do not add editor-local pattern analysis.
- [ ] Commit any required editor-facing regression or query/documentation
  update with the compiler feature.
