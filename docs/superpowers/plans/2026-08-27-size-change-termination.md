# Size-Change Termination Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove sound structural termination for direct and mutual recursion with lexicographic multi-parameter decreases under `--require-effect-free`.

**Architecture:** Move termination proof construction out of `cli/Main.cpp` into an LLVM-independent analysis module. The module builds local recursive SCCs, derives `Strict`/`Weak`/`Unknown` parameter relations from unguarded structural patterns and aliases, and accepts only SCCs whose cycles have a valid lexicographic descent. The CLI adapts results into E0203 diagnostics; normal builds retain current behaviour.

**Tech Stack:** C++23, AST (`include/yona/Syntax/Ast.h`), doctest, CMake/Ninja, CLI integration, LSP diagnostics.

## Global Constraints

- Work on `master`; do not change `VERSION`.
- New proof facts are sound and conservative: never infer numeric, guarded, higher-order, or opaque-helper descent.
- Recursive SCC members must have identical arity; parameter position defines the shared lexicographic metric.
- Default compilation emits no termination diagnostics.
- Test first, observe each test fail, then implement the smallest passing change.
- Update `docs/todo-list.md`, `CHANGELOG.md`, internal docs, the public site, and LSP coverage in the final change.
- Make one final commit only after all verification passes.

---

## File structure

- `include/yona/Semantics/TerminationAnalysis.h`: compiler-independent public
  result and relation API.
- `src/Semantics/TerminationAnalysis.cpp`: AST walk, recursive call graph, SCC decomposition, relation composition, and proof result.
- `cli/Main.cpp`: replace local termination walk with analyser invocation and E0203 rendering.
- `test/Toolchain/YonaScriptTest.cpp`: end-to-end strict-mode acceptance/rejection regressions.
- `test/Lsp/LspTest.cpp`: strict-mode diagnostic publication regression if LSP exposes strict configuration; otherwise assert compiler diagnostics remain the shared source and document the CLI-only gate.
- Documentation: `docs/error-codes.md`, `docs/type-system-status.md`, `docs/todo-list.md`, `CHANGELOG.md`, and matching `site/src/content/docs/` pages.

## Task 1: Specify relation and SCC regressions

**Files:**
- Modify: `test/Toolchain/YonaScriptTest.cpp`

**Produces:** Failing end-to-end examples for accepted and rejected structural recursion.

- [x] **Step 1: Add the direct and mutual positive cases.**

```cpp
auto mutual = write_temp_yona("effect_free_mutual_structural",
    "module Test\\MutualStructural\n"
    "export even\nexport odd\n"
    "type Nat = Zero | Succ Nat\n"
    "even n = case n of Zero -> true; Succ rest -> odd rest end\n"
    "odd n = case n of Zero -> false; Succ rest -> even rest end\n");
CHECK(run_yonac_ir(mutual, {"--require-effect-free"}).status == 0);

auto lexical = write_temp_yona("effect_free_lexical_structural",
    "module Test\\LexicalStructural\n"
    "export walk\n"
    "type Nat = Zero | Succ Nat\n"
    "walk stable changing = case changing of Zero -> stable; Succ rest -> walk stable rest end\n");
CHECK(run_yonac_ir(lexical, {"--require-effect-free"}).status == 0);
```

- [x] **Step 2: Add the negative proof-boundary cases.**

```cpp
auto incompatible = write_temp_yona("effect_free_incompatible_cycle",
    "module Test\\IncompatibleCycle\n"
    "export left\nexport right\n"
    "type Nat = Zero | Succ Nat\n"
    "left a b = case a of Zero -> b; Succ rest -> right b rest end\n"
    "right a b = case a of Zero -> b; Succ rest -> left b rest end\n");
auto result = run_yonac_ir(incompatible, {"--require-effect-free"});
CHECK(result.status != 0);
CHECK(result.out.find("E0203") != std::string::npos);

auto numeric = write_temp_yona("effect_free_numeric_recursion",
    "module Test\\NumericRecursion\nexport loop\nloop n = loop (n - 1)\n");
CHECK(run_yonac_ir(numeric, {"--require-effect-free"}).status != 0);
```

- [x] **Step 3: Run the new test filter and confirm the mutual/lexical positives fail because mutual recursion is currently rejected.**

Run: `cmake --build --preset build-debug-linux -j2 && ./out/build/x64-debug-linux/tests -tc='yonac --require-effect-free *structural*'`

Expected: the existing direct-recursion case passes; each new positive mutual/lexicographic case fails with E0203.

## Task 2: Introduce compiler-owned termination analysis

**Files:**
- Create: `include/yona/Semantics/TerminationAnalysis.h`
- Create: `src/Semantics/TerminationAnalysis.cpp`
- Modify: `CMakeLists.txt`
- Modify: `test/Toolchain/YonaScriptTest.cpp`

**Interfaces:**

```cpp
namespace yona::compiler::termination_analysis {
enum class Relation { Strict, Weak, Unknown };
struct Failure {
    SourceRange call_location;
    std::string caller;
    std::string callee;
    std::string component;
    std::string reason;
};
struct Result { std::vector<Failure> failures; };
Result analyze(ast::AstNode& root);
}
```

- [x] **Step 1: Add direct unit-level expectations through the CLI tests, then run them to establish red.**

The tests from Task 1 are the behaviour contract. Do not add a parallel mock AST layer.

- [x] **Step 2: Define the header API exactly as above.**

`Result::failures.empty()` means all recursive SCCs were proved. A `Failure` is emitted once per unproved call edge, with the call span preserved for diagnostics.

- [x] **Step 3: Implement call collection and Tarjan SCC decomposition.**

Collect calls only where an `ApplyExpr` has a local `NameCall` target. Traverse call arguments, case guards/bodies, lets, functions, imports, do/with/handle expressions, and binary expressions. Do not treat closure captures or imported calls as recursive edges.

- [x] **Step 4: Implement scoped structural facts.**

Use a map from lexical identifier to a vector of relations against the enclosing function’s parameters. At an unguarded constructor pattern, each bound field becomes `Strict` relative to the scrutinee parameter; a head-tail tail binding is likewise `Strict`. A simple `let alias = known_name` copies relations. Guards neither introduce nor preserve new descent facts.

- [x] **Step 5: Implement per-call vectors and cycle proof.**

For an edge, map each callee argument to the relation vector of its argument expression. Reject an SCC with mismatched member arity. Enumerate simple paths within an SCC using a finite worklist over `(function, relation-prefix)` states; compose relations position-wise (`Strict` dominates `Weak`; `Unknown` invalidates that position). For every return to the start function, require a position with only `Weak` earlier relations and a `Strict` relation at that position. Emit a `Failure` for the call that leaves no provable lexicographic position.

- [x] **Step 6: Register the new source and run focused tests.**

Run: `cmake --preset x64-debug-linux && cmake --build --preset build-debug-linux -j2 && ./out/build/x64-debug-linux/tests -tc='yonac --require-effect-free *structural*'`

Expected: direct, mutual, and lexicographic cases pass; numeric and incompatible cases fail.

## Task 3: Replace CLI termination logic and improve diagnostics

**Files:**
- Modify: `cli/Main.cpp`
- Modify: `test/Toolchain/YonaScriptTest.cpp`
- Modify: `docs/error-codes.md`
- Modify: `site/src/content/docs/reference/error-codes.md`

**Consumes:** `termination_analysis::analyze` from Task 2.

- [x] **Step 1: Replace `require_structural_termination` with an adapter.**

```cpp
static bool require_structural_termination(ast::AstNode* root, DiagnosticEngine& diag) {
    const auto result = termination_analysis::analyze(*root);
    for (const auto& failure : result.failures) {
        diag.error(failure.call_location, ErrorCode::E0203,
            "`--require-effect-free` cannot prove structural termination of call '" +
            failure.caller + " -> " + failure.callee + "': " + failure.reason);
        diag.note(failure.call_location,
            "destructure an argument and pass its bound descendant; keep earlier lexicographic arguments unchanged");
    }
    return result.failures.empty();
}
```

- [x] **Step 2: Add exact diagnostic tests.**

Assert E0203, the caller/callee pair, and the repair phrase for a numeric decrease and incompatible SCC. Assert no diagnostic for accepted mutual recursion.

- [x] **Step 3: Run strict-mode regression coverage.**

Run: `./out/build/x64-debug-linux/tests -tc='yonac --require-effect-free *'`

Expected: all strict effect, coverage, direct recursion, mutual recursion, and imported-interface tests pass.

## Task 4: Document the proof boundary and verify editor integration

**Files:**
- Modify: `docs/todo-list.md`
- Modify: `docs/type-system-status.md`
- Modify: `docs/error-codes.md`
- Modify: `CHANGELOG.md`
- Modify: `site/src/content/docs/reference/cli.md`
- Modify: `site/src/content/docs/learn/pattern-matching.md`
- Modify: `docs/superpowers/plans/2026-08-27-size-change-termination.md`
- Modify: `test/Lsp/LspTest.cpp` only if a strict-mode configuration path exists

- [x] **Step 1: Update the TODO and status wording.**

Mark mutual structural recursion and lexicographic parameter decreases complete. Keep general termination, numeric measures, higher-order recursion, and arbitrary open-domain exhaustiveness open.

- [x] **Step 2: Update E0203 and public CLI documentation.**

State that strict mode accepts only structural size-change proofs; it does not prove numeric decrement, guards, or arbitrary helper calls.

- [x] **Step 3: Check LSP/editor boundary.**

If `yls` has no strict-mode configuration, document that the strict gate is CLI-only and retain compiler-owned ordinary diagnostics. Do not duplicate termination analysis in VS Code or Zed. Run `npm test` in `editors/vscode`, `python3 scripts/check_zed_extension.py`, and both native and `wasm32-wasip2` Zed cargo checks.

- [x] **Step 4: Run complete verification.**

Run:

```bash
cmake --build --preset build-debug-linux -j2
./out/build/x64-debug-linux/tests
ctest --preset unit-tests-linux --output-on-failure
git diff --check
```

Expected: all tests pass; no whitespace errors.

- [x] **Step 5: Make the single final commit.**

```bash
git add CHANGELOG.md docs site/src/content/docs \
  include/yona/Semantics/TerminationAnalysis.h \
  src/Semantics/TerminationAnalysis.cpp cli/Main.cpp test/ CMakeLists.txt
git commit -m "feat: prove structural size-change termination"
```
