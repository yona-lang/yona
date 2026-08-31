# Interface and Generic Stabilization Implementation Plan

> Execute this plan with the subagent-driven development workflow. Every
> production change starts from a focused failing regression, receives a
> separate code review, and lands as an independently reviewable commit.

**Goal:** Restore the canonical interface boundary used by imports, generic
source reconstruction, effect schemes, whole-module dependencies, and
cross-module generic dispatch on Linux.

**Architecture:** Semantic types are authoritative for `.yonai` structural
descriptors; LLVM `CType` metadata remains an ABI concern. Imported generic
functions retain their exact owning export identity so private native
dependencies and lexical recursion cannot be confused between siblings with
the same local name. Legacy bare ABI descriptors remain compatible wildcards,
while canonical scalar descriptors such as `INT` stay exact.

**Scope:** This plan addresses the generic-source, canonical `LINEAR`/`TUPLE`,
zero-arity `FN`, exported effect-row, whole-module import, generic native
dependency, stream-import recursion, and related cross-module trait failures.
Independent runner, formatting, resource/effect fixture, annotated ADT, and
dictionary-ownership bugs remain for later focused plans.

---

## Task 1: Repair semantic generic-source regression coverage

**Files:**

- Modify: `test/Semantics/InterfaceCatalogTest.cpp`

- [x] **Step 1: Witness the current failure**

Run:

```bash
./out/build/x64-debug-linux/tests \
  -tc="Semantics generic source service retains GENFN source ownership"
```

Expected RED: the parser reports that the test fixture's `case` expression is
missing its mandatory `end` token.

- [x] **Step 2: Correct only the malformed test source**

Add `end` to the embedded generic source. Do not change
`GenericFunctionSourceService`; the service already retains the source manager
and parsed module correctly for valid canonical source.

- [x] **Step 3: Run the focused semantic interface suite**

Run the focused test above and the real semantic interface/catalog filter:
`./out/build/x64-debug-linux/tests -tc="Semantics*"`.

- [x] **Step 4: Commit**

Commit as `test: repair generic source ownership fixture`.

## Task 2: Restore canonical import type compatibility and zero-arity calls

**Files:**

- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Modify: `test/Codegen/CodegenTest.cpp`

- [x] **Step 1: Strengthen focused RED coverage**

Add direct import tests proving:

1. bare legacy `LINEAR` and `TUPLE` descriptors import as fresh wildcard
   structural types;
2. `LINEAR(...)` and `TUPLE(...)` remain recursively structural;
3. `INT` remains exact;
4. `FN thunk 0 -> INT effects Fs.read` imports as `Unit -> Int` and produces
   the expected unhandled-effect diagnostic when applied.

Run those tests and the existing sibling-wrapped effect-row case. Confirm bare
legacy descriptors throw and zero-arity `wrap ()` is currently typed as an
application of a value.

- [x] **Step 2: Decode legacy ABI-only atoms deliberately**

In `TypeChecker::mono_from_import_sig`, map bare `LINEAR` and `TUPLE` to fresh
wildcards. Keep recursive descriptors structural and do not make scalar atoms
permissive.

- [x] **Step 3: Reconstruct zero-arity rows as callable functions**

When a parameterless canonical `FN` row contains semantic evidence of a
source-level thunk (a nested return-arrow effect scheme, or a legacy non-empty
known effect row), synthesize a `Unit -> Return` arrow and normalize away the
hidden module-binding layer. Keep zero-arity CAF/native rows as values, and do
not alter constructor rows.

- [x] **Step 4: Verify focused and stdlib consumers**

Run the new tests, the five interface effect-row Codegen cases, and
`./out/build/x64-debug-linux/tests -tc="Stdlib conformance fixtures"`.

- [x] **Step 5: Commit**

Commit as `fix: restore canonical interface call shapes`.

## Task 3: Emit semantic structural signatures with effect schemes

**Files:**

- Modify: `include/yona/Semantics/TypeChecker.h`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `src/Codegen/CodegenModule.cpp`
- Modify: `test/Codegen/CodegenTest.cpp`
- Modify: `test/Semantics/TraitTest.cpp`
- Modify only if required by the public contract: `test/Semantics/TypeCheckerTest.cpp`
- Regenerate affected canonical interfaces under: `lib/`

- [ ] **Step 1: Add descriptor RED assertions**

In existing exported effect-row tests, assert that generated unannotated
functions serialize their inferred semantic parameter and return descriptors,
including `VAR(...)`, `FUNCTION(...)`, and `TUPLE(...)`, rather than guessed
ABI `INT` shapes. Confirm the producer currently emits incompatible guesses.

- [ ] **Step 2: Add one canonical semantic type serializer**

Expose a TypeChecker operation that serializes a zonked `MonoType` into the
canonical descriptor grammar, assigning stable variable names within one
signature. Cover arrows, tuples, linear/named types, collections, ADTs, and
scalars. Fail explicitly on an unrepresentable type. Parameterless module
definitions have one checker-internal `Unit` binding arrow: strip exactly that
outer layer before serializing the exported value. Thus `x = 42` remains a
zero-arity `INT` value, while `wrap = \() -> ...` returns
`FUNCTION(UNIT,...)` with its visible effect scheme rooted at `$`.

- [ ] **Step 3: Overlay inferred structure and effects together**

Extend `Codegen::populate_interface_effect_rows` so the checked semantic type
replaces interface parameter/return descriptors in `imports_.meta` and the
matching compiled-function metadata at the same point that it installs the
effect scheme. Preserve all LLVM `CType` fields and linkage metadata. Update
the Trait module-emission test helper to call this canonical post-typecheck
pass before it emits `.yonai`, matching the CLI path.

- [ ] **Step 4: Regenerate affected packaged interfaces**

Recompile the source-owned `.yona` modules whose checked-in `.yonai` rows
currently encode generic exports as exact `INT` placeholders, including
`Prelude` and `Std\\Function`. Inspect the diff and retain only deterministic
canonical interface changes produced by the repaired compiler.

- [ ] **Step 5: Verify effect-row and import round trips**

Run all interface effect-row Codegen cases and the semantic effect-row suite.
Expected GREEN: exported, higher-order open-rest, sibling-wrapper, and
independent-callback rows compile and enforce `E0202` correctly.
Also run `yonac module dependencies respect whole-module import bindings`;
the resolver and dependency SCCs are already correct, so the heterogeneous
String call must pass once imported `apply`/`identity` retain their HM schemes.

- [ ] **Step 6: Commit**

Commit as `fix: serialize inferred interface signatures`.

## Task 4: Preserve exact generic-function dependency ownership

**Files:**

- Modify: `include/yona/Codegen/Codegen.h`
- Modify: `src/Codegen/CodegenModule.cpp`
- Modify: `src/Codegen/CodegenFunction.cpp`
- Modify only if call routing requires it: `src/Codegen/CodegenApply.cpp`
- Modify: `test/Codegen/CodegenTest.cpp`
- Modify: `test/Semantics/TraitTest.cpp`

- [ ] **Step 1: Add owner-conflation RED tests**

Add a focused imported `Std\\Convert.intToFloat` regression and a combined
`intToFloat`/`floatToInt` case. Confirm the current deferred body loses
`intToFloatNative` and that same-named sibling generic functions cannot safely
share a union of private dependencies. Retain the focused multi-head,
constrained `Some`/`None`, cross-module generic, and derived Show-field trait
failures: trait implementation sources currently overwrite ordinary deferred
keys such as `show`, `into`, and `stringify`.

- [ ] **Step 2: Carry imported owner identity on deferred functions**

Extend `DeferredFunction` with the optional mangled GENFN owner. When
`register_sibling_genfns` reparses and registers an imported sibling, retain
the exact `dep_mangled` identity on the resulting deferred definition.

- [ ] **Step 3: Overlay only the owner's private dependencies**

At `compile_function`, install an RAII-scoped overlay for the deferred
function's exact owner from `private_genfn_dependencies`. Restore prior local,
compiled, deferred, named-value, and external bindings on every exit. Do not
activate dependencies from unrelated exports that happen to share a source
name.

- [ ] **Step 4: Keep trait implementation overloads out of sibling locals**

In `register_sibling_genfns`, exclude every mangled target owned by
`types_.trait_instances[*].method_mangled_names`. Those implementations form an
overload set selected by complete trait heads; registering them as ordinary
same-named siblings makes direct compiled/deferred lookup bypass trait
selection. Preserve registration of ordinary private generic helpers.

- [ ] **Step 5: Verify generic and trait consumers**

Run the new Convert tests, relevant generic interface tests, and the Trait
suite. Record any still-independent trait failure before continuing.

- [ ] **Step 6: Commit**

Commit as `fix: retain imported generic dependency owners`.

## Task 5: Bind lexical recursion for imported specializations and guard error IR

**Files:**

- Modify: `src/Codegen/CodegenFunction.cpp`
- Modify: `src/Codegen/Codegen.cpp`
- Modify: `test/Codegen/AdtTest.cpp`
- Modify: `test/Codegen/CodegenTest.cpp`

- [ ] **Step 1: Witness both failure modes**

Run `Lazy stream takeStream` and add an expression-compilation regression that
produces a diagnostic during case lowering. Confirm imported specializations
cannot resolve their lexical self name and that expression compilation proceeds
to verify incomplete recovery IR. Retain `Annotated ADT case functions
heap-box non-recursive results`: its imported recursive `run` body exposes the
same missing lexical-self alias and subsequent cross-function case blocks.

- [ ] **Step 2: Bind the source-level self alias narrowly**

When a deferred AST's lexical name differs from the specialization name, bind
that lexical name to the function being compiled for the function body and ABI
rebuild scope, even when no local deferred-map alias exists. Restrict the alias
to the same AST and restore prior state afterward.

- [ ] **Step 3: Stop expression compilation after diagnostics**

Make expression `Codegen::compile(AstNode *)` mirror module compilation's error
guard immediately after `codegen_main`, before transfer-drop flushing and LLVM
verification. Do not weaken verification or add artificial terminators to all
case early-return paths.

- [ ] **Step 4: Verify stream and case suites**

Run the focused stream case, annotated ADT heap-box case, all ADT/case Codegen
tests, and the new diagnostic regression.

- [ ] **Step 5: Commit**

Commit as `fix: preserve imported recursive specializations`.

## Task 6: Prove whole-module import shadowing after signature repair

**Files:**

- Modify: `test/Toolchain/YonaScriptTest.cpp`

- [ ] **Step 1: Keep the exact focused RED**

Run `yonac module dependencies respect whole-module import bindings`. Before
Task 3 this reports `expected Int, found String` because the imported generic
`Std\\Function.apply` and Prelude `identity` rows were serialized as exact
`INT`; after Task 3 it must pass.

- [ ] **Step 2: Add selective-import and local-shadow controls if absent**

Lock the diagnosis with a selective `Std\\Function.apply` heterogeneous call
and a same-named local/import shadowing control. Do not change dependency
collection: resolver tracing already proves it returns the right export and
the dependency components are correctly ordered and nonrecursive.

- [ ] **Step 3: Verify module dependencies**

Run all module dependency toolchain tests and the direct wildcard fixture.

- [ ] **Step 4: Commit**

Commit as `test: cover generic whole-module imports`.

## Task 7: Rebaseline the refinement ABI assertion

**Files:**

- Modify: `test/Codegen/CodegenTest.cpp`

- [ ] **Step 1: Confirm production IR is canonical**

Run `ABI refinement leaves one canonical function`; inspect IR and confirm it
contains one `define internal fastcc i1 @f(i64 %x)` and a direct
`call fastcc i1 @f(i64 1)`, with no legacy trampoline.

- [ ] **Step 2: Update only the stale assertion**

Replace the removed `%x` call-operand expectation with assertions for the
actual direct constant call and absence of duplicate/trampoline symbols.

- [ ] **Step 3: Run the focused ABI/refinement suite and commit**

Commit as `test: align refinement ABI regression`.

## Task 8: Close the interface batch and reassess failures

**Files:**

- Modify: `docs/todo-list.md`
- Modify: `docs/superpowers/specs/2026-08-31-open-bug-stabilization-design.md`
- Modify: this plan
- Modify: `CHANGELOG.md`
- Modify: `.superpowers/sdd/progress.md`

- [ ] **Step 1: Build and run focused suites**

Run the Semantics, Codegen Modules, Trait, Lazy stream, stdlib conformance, and
module-dependency suites.

- [ ] **Step 2: Run the full Linux gate**

```bash
cmake --build --preset build-debug-linux
ctest --preset unit-tests-linux
git diff --check
```

- [ ] **Step 3: Record combined results**

Check off only bugs proven fixed, summarize user-visible changes under
`Unreleased`, update the stabilization design's expanded Linux scope, and list
the exact remaining independent failures for the next plan. Keep the combined
documentation update in one commit, as requested.

- [ ] **Step 4: Commit**

Commit as `docs: record interface stabilization`.
