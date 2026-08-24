# Totality Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Status:** Completed 2026-08-24 in commit `1cbc1ce`.

**Goal:** Complete Yona's conservative totality checks with overlap diagnostics, Bool exhaustiveness, and structurally decreasing direct recursion under `--require-effect-free`.

**Architecture:** Keep pattern-domain reasoning in a single analysis API owned by `Codegen`, consumed by both the warning path and the strict CLI gate. Add a module-local call graph plus AST-based termination analyzer in the CLI: reject multi-function cycles and accept only direct calls whose recursive arguments are structurally bound descendants of a function parameter.

**Tech Stack:** C++23, LLVM codegen, doctest, CLI11, Markdown/Astro documentation.

## Global Constraints

- Preserve normal compilation: strict totality diagnostics occur only under `--require-effect-free`.
- `--Wall` enables `--Woverlapping-patterns` and `--Wincomplete-patterns`; `--Werror` promotes either warning.
- Claim finite non-ADT coverage only for Bool; symbols, numeric values, strings, collections, tuples, and records stay open.
- Reject every multi-function recursion cycle; accept only direct, structurally decreasing self-recursion; reject higher-order, numeric, and original-argument recursion in strict mode.
- Update `CHANGELOG.md`, `docs/`, `docs/todo-list.md`, and `site/src/content/docs/` with the same change.

---

### Task 1: Shared overlap and finite-domain analysis

**Files:**
- Modify: `include/Codegen.h`
- Modify: `src/codegen/CodegenCase.cpp`
- Test: `test/codegen_test.cpp`

**Interfaces:**
- Produces `Codegen::CasePatternAnalysis { std::vector<size_t> unreachable_clauses; std::optional<FiniteCaseCoverage> incomplete; }`.
- Produces `Codegen::analyze_case_patterns(ast::CaseExpr*) const`.
- Existing `finite_case_coverage` delegates to the new analysis so warnings and strict errors share the exact coverage result.

- [x] **Step 1: Write direct-analysis tests**

Add tests for a wildcard followed by `Some x`, duplicate `Some` arms, an
`or` arm that is shadowed, `True` without `False`, and a complete Bool case:

```cpp
auto analysis = codegen.analyze_case_patterns(case_expr);
CHECK(analysis.unreachable_clauses == vector<size_t>{1});
REQUIRE(analysis.incomplete.has_value());
CHECK(analysis.incomplete->adt_name == "Bool");
CHECK(analysis.incomplete->missing == vector<string>{"False"});
```

- [x] **Step 2: Verify focused analysis tests**

Run: `./out/build/x64-debug-linux/tests -ts='Diagnostics'`

Expected: the shared analysis identifies unreachable and missing Bool arms.

- [x] **Step 3: Implement normalized top-level coverage atoms**

In `CodegenCase.cpp`, classify each unguarded top-level pattern as `Any`, an
ADT constructor name, `True`, or `False`; flatten `OrPattern` into
atoms. Treat every guarded clause as contributing no atoms. Determine a clause
as unreachable only when every one of its atoms is already covered by `Any` or
the matching atom. Determine Bool incompleteness from its complete atom set;
retain existing registered-ADT constructor analysis. Unit remains outside this
slice until a type-directed Unit scrutinee check exists.

- [x] **Step 4: Route existing finite-ADT coverage through the shared result**

Implement:

```cpp
std::optional<Codegen::FiniteCaseCoverage>
Codegen::finite_case_coverage(ast::CaseExpr* node) const {
    return analyze_case_patterns(node).incomplete;
}
```

Keep the existing ADT diagnostic spelling and sorted missing constructors.

- [x] **Step 5: Verify direct diagnostics**

Run: `cmake --build --preset build-debug-linux --target tests && ./out/build/x64-debug-linux/tests -ts='Diagnostics'`

Expected: Diagnostics suite passes, including old ADT wildcard/guarded cases and the new Bool/overlap cases.

- [x] **Step 6: Commit Task 1**

```bash
git add include/Codegen.h src/codegen/CodegenCase.cpp test/codegen_test.cpp
git commit -m "feat: analyze finite case coverage and overlaps"
```

### Task 2: Emit overlap warnings and enforce Bool strict coverage

**Files:**
- Modify: `src/codegen/CodegenCase.cpp`
- Modify: `cli/main.cpp`
- Test: `test/yona_script_test.cpp`

**Interfaces:**
- Consumes `Codegen::analyze_case_patterns`.
- Emits `WarningFlag::OverlappingPatterns` for unreachable unguarded clauses.
- `collect_incomplete_cases` uses the shared incomplete result for ADT and Bool E0203 diagnostics.

- [x] **Step 1: Write CLI tests**

Add `run_yonac_ir` tests:

```cpp
auto warning = run_yonac_ir(src, {"--Woverlapping-patterns"});
CHECK(warning.status == 0);
CHECK(warning.out.find("unreachable pattern") != string::npos);

auto strict_bool = run_yonac_ir(bool_src, {"--require-effect-free"});
CHECK(strict_bool.status != 0);
CHECK(strict_bool.out.find("False") != string::npos);
```

Also check `--Werror --Woverlapping-patterns` fails and a complete `True` /
`False` case succeeds under `--require-effect-free`.

- [x] **Step 2: Verify focused CLI tests**

Run: `./out/build/x64-debug-linux/tests -tc='yonac *overlapping*'`

Expected: no overlap warning is emitted yet.

- [x] **Step 3: Emit warning diagnostics**

Immediately after shared case analysis in `Codegen::codegen_case`, emit one
warning for each unreachable index:

```cpp
diag_->warning(node->clauses[index]->source_context,
               "unreachable pattern: an earlier unguarded arm already covers it",
               WarningFlag::OverlappingPatterns);
```

Do not diagnose guarded clauses or partially overlapping alternatives.

- [x] **Step 4: Preserve strict diagnostic behavior**

Keep `collect_incomplete_cases` unchanged except for consuming
`finite_case_coverage`; its E0203 message must say `Bool` and list the missing
atom just as it lists a missing ADT constructor.

- [x] **Step 5: Verify warning and strict modes**

Run: `cmake --build --preset build-debug-linux --target yonac tests && ./out/build/x64-debug-linux/tests -tc='yonac *pattern*'`

Expected: opt-in warnings only in warning mode; E0203 only in strict mode; `--Werror` fails overlap warnings.

- [x] **Step 6: Commit Task 2**

```bash
git add src/codegen/CodegenCase.cpp cli/main.cpp test/yona_script_test.cpp
git commit -m "feat: diagnose overlapping finite patterns"
```

### Task 3: Structural direct-recursion termination gate

**Files:**
- Modify: `cli/main.cpp`
- Test: `test/yona_script_test.cpp`

**Interfaces:**
- Produces `bool require_structural_termination(ast::AstNode*, DiagnosticEngine&)`.
- Called only by `require_effect_free` after the effect and coverage checks.
- Emits E0203 at the recursive call when no structurally smaller argument is proven.

- [x] **Step 1: Write strict-mode tests**

Add module tests with functions such as:

```yona
sum xs = case xs of [] -> 0; [x | rest] -> x + sum rest end
loop x = loop x
down n = if n == 0 then 0 else down (n - 1)
```

Assert `sum` succeeds with `--require-effect-free`, while `loop` and `down`
fail with E0203. Add a mutual-recursion fixture and assert it fails.

- [x] **Step 2: Verify focused strict-mode tests**

Run: `./out/build/x64-debug-linux/tests -tc='yonac --require-effect-free *recursion*'`

Expected: recursive fixtures currently compile because the strict gate has no termination analysis.

- [x] **Step 3: Build the module-local recursion graph**

Walk each `ModuleDecl::functions` body and collect calls whose callee is a
module-local function name. Run a strongly connected component pass. For every
component with more than one function, emit E0203 at each participating
function declaration:

```cpp
diag.error(function->source_context, ErrorCode::E0203,
           "`--require-effect-free` cannot prove mutual recursion involving '" +
           function->name + "'");
```

Pass only singleton components with a self-edge to the structural checker.

- [x] **Step 4: Collect structural descendants per function body**

In `cli/main.cpp`, add a small lexical analyzer that receives a function's
parameter names, enters each unguarded `CaseExpr` clause, and records names
bound by a constructor subpattern or `[head | tail]` pattern as descendants of
the matched parameter. Recurse through `let`, `if`, `do`, nested case bodies,
and applications while preserving lexical scope.

- [x] **Step 5: Reject unproven recursive calls**

When visiting `ApplyExpr`, recognize a direct call whose callee identifier is
the current function name. Require at least one argument to be a recorded
descendant of the corresponding parameter. If not, emit:

```cpp
diag.error(apply->source_context, ErrorCode::E0203,
           "`--require-effect-free` cannot prove structural termination of '" +
           function->name + "'");
```

The call-graph pass has already rejected every cross-function recursive cycle.

- [x] **Step 6: Verify strict recursion behavior**

Run: `cmake --build --preset build-debug-linux --target yonac tests && ./out/build/x64-debug-linux/tests -tc='yonac --require-effect-free *recursion*'`

Expected: structural list recursion passes; original-argument, numeric, and mutual recursion fail with E0203.

- [x] **Step 7: Commit Task 3**

```bash
git add cli/main.cpp test/yona_script_test.cpp
git commit -m "feat: require structural recursion in totality mode"
```

### Task 4: Documentation, roadmap, and full verification

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `docs/error-codes.md`
- Modify: `docs/type-system-status.md`
- Modify: `docs/todo-list.md`
- Modify: `site/src/content/docs/reference/cli.md`
- Modify: `site/src/content/docs/reference/error-codes.md`
- Modify: `site/src/content/docs/guides/type-system.md`
- Modify: `site/src/content/docs/learn/pattern-matching.md`

- [x] **Step 1: Document the exact proof boundary**

State that strict mode requires closed effects, finite ADT/Bool coverage,
and direct structural recursion. State that guarded arms, symbols, scalar
domains, collections, product patterns, mutual recursion, higher-order
recursion, and numeric decreases are not proven.

- [x] **Step 2: Update the roadmap without stale checkboxes**

Move #5 overlap, Bool coverage, and structural-recursion completion into
Completed Milestones. Keep #5 open only for domains and termination models not
covered by this conservative checker.

- [x] **Step 3: Verify wording alignment**

Run: `rg -n 'require-effect-free|Wincomplete-patterns|Woverlapping-patterns|E0203|termination' docs site/src/content/docs CHANGELOG.md`

Expected: each strict-gate statement lists its finite-domain and recursion
limits; warning descriptions remain non-fatal absent `--Werror`.

- [x] **Step 4: Run the full test suite and diff check**

Run: `ctest --preset unit-tests-linux --output-on-failure && git diff --check`

Expected: both CTest targets pass and there are no whitespace errors.

- [x] **Step 5: Commit Task 4**

```bash
git add CHANGELOG.md docs site/src/content/docs
git commit -m "docs: define conservative totality guarantees"
```
