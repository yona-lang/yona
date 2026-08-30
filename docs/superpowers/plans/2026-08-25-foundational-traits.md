# Foundational Traits Implementation Plan

> **For Codex:** Execute on `master` with the executing-plans and
> test-driven-development skills. The user explicitly authorized direct work
> on `master`. Keep every task red/green/refactor and preserve unrelated edits.

**Goal:** Replace representation-based operators with coherent static trait
dispatch, complete Yona's foundational trait surface, provide lawful standard
instances and reusable law tests, and keep compiler, stdlib, generated API
docs, and the public site synchronized.

**Architecture:** A shared typed instance-selection model serves the type
checker and code generator. `.yonai` retains complete trait contracts.
Operators elaborate through `Eq` and `Ord`; a successfully selected primitive
instance may carry an intrinsic lowering. The derive engine supplies structural
instances, Yona supplies pure defaults and law suites, and marker traits are
checked at concurrency boundaries then erased.

**Technology:** C++23, LLVM 22, Yona, doctest 2.5.3, CMake/Ninja, Astro.

## Implementation status

- [x] Phases 0–3: coherent typed contracts, `.yonai` round-tripping,
  trait-directed operators, lawful structural derivation, authoritative
  Prelude generation, and lifted Option/Result/Seq/tuple instances.
- [x] Phase 4: finish collection/algebra instances and witness-directed,
  structured conversion/parsing APIs.
- [x] Phase 5: enforce `Send` and `Shareable` at every concurrency boundary.
- [x] Phase 6: extend executable laws to every standard instance, synchronize
  generated/internal/public documentation, and complete all verification.

Completed 2026-08-26. The native foundational fixture exercises 163 cases,
the standard-library manifest covers all 40 public modules, law and ownership
regressions pass at `-O0` through `-O3`, and the generated API plus public site
describe the same contracts.

The Phase 4 conversion API is witness-directed until explicit type
applications exist. Instance selection must use the complete trait head; it
must never guess a result type from whichever instance happens to be visible.
Native `Iterator` adapters additionally require RC-managed cursor state so an
iterator owns both its state and source for exactly its lifetime.

---

## Phase 0 — Contract and red regressions

### Task 1: Record the program

**Files:** `docs/todo-list.md`,
`docs/superpowers/specs/2026-08-25-type-directed-equality-design.md`, this plan.

1. Replace direct aggregate comparison with the trait contract.
2. Add individually checkable compiler, Prelude, library, marker, docs, and test
   milestones to the TODO.
3. Keep the Option and Collection bugs open through final verification.
4. Run `git diff --check`.

### Task 2: Specify operator behavior in tests

**Files:** `test/Semantics/TraitTest.cpp`, `test/Semantics/TypeCheckerTest.cpp`,
`test/Codegen/CodegenTest.cpp`, `test/stdlib/pure/Option_test.yona`.

1. Add failing checker tests that `==` creates `Eq` and ordering creates `Ord`
   obligations, including one actionable missing-instance diagnostic.
2. Add failing E2E tests for Option, Result, nested ADTs, tuples, distinct
   strings, `!=`, and a `Std\Test` callback.
3. Add an ownership-sensitive nested heap-value case.
4. Run focused filters and retain the red evidence.

## Phase 1 — Coherent typed resolution

### Task 3: Model complete trait and instance contracts

**Files:** `include/yona/Codegen/Codegen.h`, `include/yona/Semantics/TypeChecker.h`,
`include/yona/Model/InferType.h`, `src/Codegen/Codegen.cpp`,
`src/Semantics/TypeChecker.cpp`, `src/Codegen/CodegenModule.cpp`,
`test/Semantics/TraitTest.cpp`, and `test/Codegen/CodegenTest.cpp`.

1. Test method signatures, superclasses, parameterized heads, constraints, and
   deterministic duplicate-instance rejection.
2. Introduce shared method/instance contracts and typed `InstanceSelection`.
3. Replace unordered first-match dispatch with exact deterministic lookup.
4. Emit one coherence diagnostic per duplicate visible key.
5. Run trait/module tests and refactor type-name normalization.

### Task 4: Round-trip contracts through `.yonai`

**Files:** `src/Codegen/CodegenModule.cpp`, `docs/module-system.md`,
`site/src/content/docs/guides/modules-interfaces.md`, module/trait tests.

1. Add red cross-module tests for `Eq a => Eq (Option a)` and a two-parameter
   conversion instance.
2. Serialize method type descriptors, superclasses, instance applications, and
   constraints; reject stale incomplete interfaces with a rebuild hint.
3. Load exact schemes into `TypeChecker`, removing synthetic unary method types.
4. Verify interface text and cross-module dispatch.

## Phase 2 — Lawful operators

### Task 5: Elaborate equality through `Eq`

**Files:** `src/Model/TypeEnv.cpp`, `src/Semantics/TypeChecker.cpp`,
`src/Codegen/CodegenExpr.cpp`, `src/Codegen/CodegenApply.cpp`, diagnostics and
error-code docs.

1. Remove the temporary recursive LLVM-struct comparison.
2. Unify operands, require `Eq`, and select `eq` before codegen.
3. Borrow both operands; keep intrinsic lowering only behind a resolved
   primitive instance; make `!=` negate `eq`.
4. Emit one actionable missing-instance diagnostic with deriving, explicit
   instance, and comparator examples.
5. Run checker, trait, codegen, Option, and ownership filters.

### Task 6: Elaborate ordering through `Ord`

**Files:** `lib/Prelude.yona`, checker/comparison code, derive engine, tests.

1. Add `Ordering = Less | Equal | Greater`.
2. Change `Ord.compare` and derived `Ord` to return `Ordering`.
3. Lower all relational operators through one selected `compare` call.
4. Cover primitive, enum, nested, cross-module, and missing-instance cases.

### Task 7: Constrain structural derives

**Files:** derive engine headers/source, `src/Codegen/Codegen.cpp`, trait tests,
auto-derive docs and site type guide.

1. Test preservation of field constraints and rejection of function/linear
   fields once at the deriving clause.
2. Generate constrained instance heads from field type references.
3. Enforce `Ord`/`Hash` superclass obligations on `Eq`.
4. Normalize Float signed zero hashing and document NaN semantics.

## Phase 3 — Authoritative Prelude and lifted instances

### Task 8: Make `Prelude.yona` the source of truth

**Files:** `lib/Prelude.yona`, generated `lib/Prelude.yonai`,
the core runtime component, `CMakeLists.txt`, new `test/Interface/PreludeInterfaceTest.cpp`,
`docs/prelude.md`.

1. Test that regenerating Prelude produces no semantic interface diff.
2. Declare/export complete core traits, defaults, primitive externs/instances,
   and structural Prelude derives in source.
3. Add bootstrap-safe primitive symbols; remove hand-maintained interface-only
   trait metadata.
4. Add an explicit regeneration target and CI test.

### Task 9: Add lifted immutable instances

**Files:** Prelude, runtime only where representation access is unavoidable,
trait/codegen/RC tests.

1. Cover tuples, Seq, Set, and Dict with nested heap values, empty values,
   different shapes, Unicode, and hash consistency.
2. Implement constrained lifted instances.
3. Reject Function, Promise, Channel, Linear, and native mutable arrays absent
   an explicit lawful instance.

## Phase 4 — Collection, algebra, and conversion foundations

### Task 10: Add `Sized`, `Iterable`, and `Foldable`

**Files:** Prelude, `lib/Std/List.yona`, affected collection/array modules, new
`lib/Std/TraitLaws.yona`, and `test/stdlib/foundation/Traits_test.*`.

1. Test multi-parameter dispatch.
2. Implement lawful instances for Seq, String, arrays, Set, Dict, Option,
   Result, and finite Iterators where applicable.
3. Remove specialized APIs when the foundational trait supplies the operation.
4. Test empty, singleton, nested, Unicode, and large inputs plus consistency
   between `size`, iteration, and folds.

### Task 11: Add `Semigroup` and `Monoid`

**Files:** Prelude, trait laws, foundation fixture, trait docs/site.

1. Test associativity and both identity directions.
2. Add String, Seq, Set, and documented Dict instances; use explicit additive
   and multiplicative numeric wrappers rather than ambiguous bare numbers.
3. Implement `concat`/`foldMap` helpers in Yona.

### Task 12: Add conversion and parsing traits

**Files:** Prelude; new `lib/Std/Convert.yona`, generated interface, and
`test/stdlib/foundation/Convert_test.*`.

1. Define structured `ConvertError` and `ParseError` ADTs.
2. Implement `From`, `TryFrom`, and `Parse` for supported primitive, textual,
   byte, and numeric conversions without silent truncation.
3. Test boundaries, whitespace/sign policy, Unicode, overflow, malformed input,
   round trips, and cross-module dispatch.

## Phase 5 — Concurrency markers

### Task 13: Implement `Send` and `Shareable`

**Files:** Prelude, trait parser/AST if zero-method traits require it, checker,
linearity checker, concurrency validation, checker/codegen tests.

1. Add red positive/negative tests for spawn, channel send, parallel
   comprehensions, and parallel `let`.
2. Support marker traits and transitive structural derivation.
3. Mark immutable primitives/ADTs/collections; reject linear resources, mutable
   native buffers, and nonconforming closure captures.
4. Erase markers before codegen and prove no runtime dictionary overhead.

## Phase 6 — Conformance, documentation, and cleanup

### Task 14: Add reusable law suites to `Std\Test`

**Files:** `lib/Std/Test.yona`, `lib/Std/TraitLaws.yona`, framework and all
relevant stdlib fixtures, `test/stdlib/manifest.md`.

1. Add functional law-suite constructors with explicit samples/generators and
   counterexample rendering.
2. Apply them to every standard instance.
3. Fix the existing `Std\Collection` crash, then satisfy recursive fixture
   discovery and the complete public-module manifest.

### Task 15: Synchronize documentation

**Files:** trait/language/array/error docs, generated `docs/api/`, matching site
guides/reference/learn pages, `CHANGELOG.md`, `docs/todo-list.md`.

1. Document laws, instances, operator desugaring, diagnostics, conversions,
   markers, and the higher-kinded-type deferral.
2. Run `python3 scripts/gendocs.py` and update the public site in the same change.
3. Check completed plan tasks, remove resolved bugs, and leave no stale boxes.

### Task 16: Final verification

1. Run `./scripts/format.sh` and `git diff --check`.
2. Run focused checker, trait, module, codegen, ownership, and law filters.
3. Run `cmake --build --preset build-debug-linux`.
4. Run `ctest --preset unit-tests-linux --output-on-failure`.
5. Run stdlib manifest/conformance and confirm no disabled/skipped tests except
   documented capability probes.
6. Run API generation and the site build/check.
7. Review for dead adapters, duplicate registries, stale generated
   interfaces, and undocumented behavior.
