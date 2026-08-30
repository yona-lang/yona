# Effect-Freedom Exhaustiveness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `yonac --require-effect-free` reject incomplete matches over finite ADTs while preserving `--Wincomplete-patterns` as the non-strict warning mode.

**Architecture:** Move finite-ADT coverage discovery out of `Codegen::codegen_case` into a shared `Codegen` helper returning the case node’s ADT name and missing constructor names. Code generation uses this helper for warnings; the CLI strict gate walks expression/module ASTs after constructor metadata is loaded and emits E0203 for each result.

**Tech Stack:** C++23, LLVM codegen, doctest, CLI11, Markdown/Astro static documentation.

## Global Constraints

- Preserve default compilation behavior: only `--require-effect-free` makes incomplete finite-ADT matches errors.
- Keep `--Wincomplete-patterns` and `--Wall` warning semantics intact, including wildcard and guarded-arm behavior.
- Analyze only finite registered ADTs; do not add termination, overlap, primitive, sequence, record, or open-ADT coverage claims.
- Update `docs/`, `site/src/content/docs/`, `CHANGELOG.md`, and `docs/todo-list.md` in the same change.

---

### Task 1: Share finite-ADT coverage analysis

**Files:**
- Modify: `include/yona/Codegen/Codegen.h:155-170, 640-675`
- Modify: `src/Codegen/CodegenCase.cpp:530-625`
- Test: `test/Codegen/CodegenTest.cpp:1303-1360`

**Interfaces:**
- Produces `Codegen::FiniteCaseCoverage { std::string adt_name; std::vector<std::string> missing; }`.
- Produces `std::optional<FiniteCaseCoverage> Codegen::finite_case_coverage(ast::CaseExpr*) const`.
- Consumes registered `types_.adt_constructors` and case clause patterns.

- [x] **Step 1: Write failing direct-analysis tests**

Add doctest assertions that call `finite_case_coverage` after prelude loading:

```cpp
auto coverage = codegen.finite_case_coverage(
    static_cast<CaseExpr*>(parsed.value().get()));
REQUIRE(coverage.has_value());
CHECK(coverage->adt_name == "Option");
CHECK(coverage->missing == vector<string>{"None"});
```

Add a wildcard case asserting `!coverage.has_value()` and a guarded `Some`
case asserting missing `{ "None", "Some" }`.

- [x] **Step 2: Run the coverage tests and verify they fail**

Run: `./out/build/x64-debug-linux/tests -tc='finite ADT coverage analysis*'`

Expected: compile failure because `finite_case_coverage` is not declared.

- [x] **Step 3: Implement the helper**

Declare the result structure and helper in `Codegen`. In `CodegenCase.cpp`, perform the existing constructor collection once:

```cpp
for (auto *clause : node->clauses) {
    if (clause->guard) continue;
    collect_constructor_pattern(clause->pattern, covered, wildcard);
}
if (wildcard || covered.size() == constructors.size()) return std::nullopt;
return FiniteCaseCoverage{adt_name, sorted_missing(constructors, covered)};
```

The collector recognizes `ConstructorPattern`, `RecordPattern`, every
alternative in `OrPattern`, and identifier/wildcard `PatternValue` as full
coverage. Return no result when the scrutinee cannot be tied to a registered
finite ADT.

- [x] **Step 4: Make codegen consume the helper**

Replace the inline coverage block in `codegen_case` with:

```cpp
if (diag_ && diag_->warning_enabled(WarningFlag::IncompletePatterns)) {
    if (auto coverage = finite_case_coverage(node))
        diag_->warning(node->Range, WarningFlag::IncompletePatterns,
                       "non-exhaustive pattern match on " + coverage->adt_name +
                       " — missing constructor" +
                       (coverage->missing.size() == 1 ? " " : "s ") +
                       join(coverage->missing, ", "));
}
```

- [x] **Step 5: Run direct diagnostics tests**

Run: `cmake --build --preset build-debug-linux --target tests && ./out/build/x64-debug-linux/tests -ts='Diagnostics'`

Expected: all Diagnostics tests pass, including the new helper assertions and
the existing warning/wildcard/guarded behavior.

- [x] **Step 6: Commit Task 1**

```bash
git add include/yona/Codegen/Codegen.h src/Codegen/CodegenCase.cpp test/Codegen/CodegenTest.cpp
git commit -m "refactor: share finite ADT case coverage"
```

### Task 2: Enforce coverage in strict effect-freedom mode

**Files:**
- Modify: `cli/Main.cpp:240-265, 560-630`
- Test: `test/Toolchain/YonaScriptTest.cpp:302-340`

**Interfaces:**
- Consumes `Codegen::finite_case_coverage(ast::CaseExpr*)` from Task 1.
- Produces E0203 diagnostics for every incomplete finite-ADT case beneath the input AST when `--require-effect-free` is set.

- [x] **Step 1: Write failing CLI tests**

Add these tests using `run_yonac_ir`:

```cpp
TEST_CASE("yonac --require-effect-free rejects incomplete finite ADT cases") {
    auto src = write_temp_yona("effect_free_incomplete_case",
        "case Some 1 of Some x -> x end\n");
    auto r = run_yonac_ir(src, {"--require-effect-free"});
    CHECK(r.status != 0);
    CHECK(r.out.find("E0203") != string::npos);
    CHECK(r.out.find("None") != string::npos);
}
```

Add one passing wildcard case and one failing guarded-`Some` case. Add a
module fixture with an exported function containing the incomplete case to
prove module and expression paths both enforce the gate.

- [x] **Step 2: Run strict CLI tests and verify they fail**

Run: `./out/build/x64-debug-linux/tests -tc='yonac --require-effect-free *finite ADT*'`

Expected: incomplete-case tests exit 0 before strict coverage enforcement is
added.

- [x] **Step 3: Walk ASTs in the strict gate**

Add a local `collect_incomplete_cases(ast::AstNode*, Codegen&, DiagnosticEngine&)`
visitor in `cli/Main.cpp`. It recursively visits expression bodies, case
scrutinees, clauses, guards, lets, functions, modules, instance methods, and
trait defaults. For each `CaseExpr`, call `finite_case_coverage` and emit:

```cpp
diag.error(case_expr->Range, ErrorCode::E0203,
           "`--require-effect-free` requires an exhaustive match on " +
           coverage.adt_name + "; missing constructor" + suffix + names);
```

Return false from `require_effect_free` when any strict coverage error was
emitted. Execute this after `Codegen::loadPrelude` and after module imports
are registered so constructor metadata is available.

- [x] **Step 4: Run strict CLI tests**

Run: `cmake --build --preset build-debug-linux --target yonac tests && ./out/build/x64-debug-linux/tests -tc='yonac --require-effect-free *'`

Expected: pure and wildcard cases pass; unknown effects, incomplete cases,
guarded cases, and module cases fail with E0203.

- [x] **Step 5: Commit Task 2**

```bash
git add cli/Main.cpp test/Toolchain/YonaScriptTest.cpp
git commit -m "feat: enforce finite ADT coverage in strict totality mode"
```

### Task 3: Documentation cleanup and full verification

**Files:**
- Modify: `CHANGELOG.md:3-25`
- Modify: `docs/error-codes.md:E0203`
- Modify: `docs/type-system-status.md:case exhaustiveness and limitations`
- Modify: `docs/todo-list.md:Active Priorities and Suggested next steps`
- Modify: `docs/superpowers/plans/2026-08-17-next-plan-of-action.md:Phase 3B`
- Modify: `site/src/content/docs/reference/cli.md`
- Modify: `site/src/content/docs/reference/error-codes.md`
- Modify: `site/src/content/docs/guides/type-system.md`
- Modify: `site/src/content/docs/learn/pattern-matching.md`

**Interfaces:**
- Documents that strict effect-freedom now requires closed empty rows plus exhaustive finite-ADT matches.
- Leaves termination, overlap, and non-ADT totality obligations explicitly open.

- [x] **Step 1: Update user-facing behavior and limitations**

Change the `--require-effect-free` description and E0203 text to state the
two enforced facts. Document that ordinary compilation still emits only the
opt-in `--Wincomplete-patterns` warning. Keep the listed non-goals visible.

- [x] **Step 2: Consolidate roadmap completion records**

Move completed #8, #10, and #11 details from the Active Priorities list into
Completed Milestones; retain short references in the #5 dependency narrative.
Mark the new finite-ADT strict obligation complete while keeping the
termination/overlap/non-ADT row open.

- [x] **Step 3: Verify static-site wording is aligned**

Run: `rg -n 'require-effect-free|Wincomplete-patterns|E0203' docs site/src/content/docs`

Expected: every strict-gate description says finite-ADT coverage is enforced
only by `--require-effect-free`; every general warning description remains
non-fatal unless `--Werror` is set.

- [x] **Step 4: Run full verification**

Run: `ctest --preset unit-tests-linux --output-on-failure && git diff --check`

Expected: 2/2 CTest targets pass and the diff has no whitespace errors.

- [x] **Step 5: Commit Task 3**

```bash
git add CHANGELOG.md docs site/src/content/docs
git commit -m "docs: document strict finite ADT totality gate"
```
