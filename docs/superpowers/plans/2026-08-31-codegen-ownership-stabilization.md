# Codegen Ownership Stabilization Implementation Plan

> Execute with subagent-driven development. Start each production task from
> allocation-sensitive RED coverage, review every commit independently, and
> defer todo/changelog closure to the combined final stabilization update.

**Goal:** Make generated programs transfer and release aggregate, constructor,
captured, and native-call ownership exactly once, with zero Linux allocation
leaks in the channel and lifted-dictionary regressions.

**Architecture:** Pattern lowering owns explicit transfers. Destructuring an
owned aggregate retains each escaping heap child and releases the aggregate;
constructor fields retained for an arm are registered in that arm's drop scope;
temporary case scrutinees are released by the selected arm. Heap-valued
constructor arguments transfer their expression-owned reference into the ADT
instead of creating an unbalanced duplicate. A closure environment continues
to own its captures, so passing a captured heap value to a consuming callee
creates a per-call retained reference. Native channel helpers follow the
default callee-owns ABI on every normal and exceptional path. No compatibility
coercions or representation guesses are added.

## Task 1: Balance tuple and constructor-pattern ownership

**Files:**

- Modify: `src/Codegen/CodegenExpr.cpp`
- Modify: `src/Codegen/CodegenCase.cpp`
- Modify: `src/Runtime/Concurrency/ChannelPosix.c`
- Modify: `src/Runtime/Concurrency/ChannelWin32.c`
- Modify: focused allocation tests under `test/Codegen/`

- [ ] **Step 1: Add isolated RED allocation coverage**

Compile and run `let (x, y) = ([1], [2]) in 0`, a scalar temporary
constructor case, a heap-field temporary constructor case, and the existing
`channel_basic` fixture under `YONA_ALLOC_STATS=1`. Assert per-tag zero leaks,
not merely the expected printed value.

- [ ] **Step 2: Transfer owned tuple fields explicitly**

When a pattern alias destructures an owned heap tuple, retain each extracted
heap binding, register it with the let scope, then release the tuple aggregate.
Use `is_heap_type` and the semantic field identity rather than an incomplete
hard-coded CType list. Primitive fields remain direct carriers.

- [ ] **Step 3: Give constructor bindings an arm lifetime**

Every heap field retained by constructor matching must enter
`arm_drop_stack_`. Drop it on the selected arm after body evaluation unless it
escapes as the arm result or is transferred. Guard-failure paths must release
bindings before testing the next arm. A non-identifier heap case scrutinee is
case-owned and is released on the selected path; named scrutinees remain owned
by their enclosing scope.

- [ ] **Step 4: Make raw channel natives honor callee-owns**

Keep the source-level raw extern contracts consuming. Balance the call-owned
channel reference in every POSIX and Win32 raw helper, including error/raise
paths; `rawSend` transfers the payload into the channel as before. Do not add
checked-in borrow metadata that regeneration would silently discard.

- [ ] **Step 5: Verify and commit**

Run tuple/case/ADT ownership tests, direct runtime channel tests, every
non-GPU channel fixture, and allocation-stat probes. Commit as
`fix: balance pattern-owned aggregates`.

## Task 2: Transfer temporary heap values into ADT fields

**Files:**

- Modify: `src/Codegen/CodegenApply.cpp`
- Modify as needed: shared ownership helpers in `include/yona/Codegen/Codegen.h`
- Modify: focused ADT allocation tests under `test/Codegen/`

- [ ] **Step 1: Isolate named and temporary field ownership**

Prove `Some [1]`, nested constructors, and multi-field ADTs retain named heap
arguments when required but do not leak anonymous heap expressions. Include a
returned-constructor control so eliminating a duplicate cannot create a
dangling field.

- [ ] **Step 2: Transfer exactly one expression-owned reference**

Teach ADT construction whether a field carrier is an owned temporary or a
borrowed/named value. Transfer a temporary directly into the new ADT; duplicate
only a borrowed value. Reuse existing transfer tracking instead of adding a
constructor-specific leak exemption.

- [ ] **Step 3: Verify and commit**

Run ADT, Option/Result, constructor-pattern, exception-frame, and allocation
suites. Commit as `fix: transfer temporary ADT fields`.

## Task 3: Preserve captured heap values across consuming calls

**Files:**

- Modify: `src/Codegen/CodegenApply.cpp`
- Modify as needed: `src/Codegen/CodegenFunction.cpp`
- Modify: focused closure/dictionary ownership tests under `test/Codegen/`

- [ ] **Step 1: Add a minimal reusable-capture RED regression**

Capture a heap tuple in a predicate/comparator closure, invoke that closure
more than once through a consuming higher-order call, and assert correct output
plus zero leaks. Retain `dict_lifted_trait_lifetime` as the integration case;
Valgrind currently reaches `compareEntry` with an invalid captured pivot.

- [ ] **Step 2: Retain a per-call owned reference**

The environment keeps ownership of every capture until closure destruction.
When a captured heap carrier is passed to a non-borrowed parameter, retain a
new call reference before invocation. Do not mark the environment's capture as
transferred and do not weaken the callee-owns ABI.

- [ ] **Step 3: Verify and commit**

Run closure capture, higher-order List, dictionary trait, recursive closure,
frame/raise, and allocation controls. Commit as
`fix: retain captured values for consuming calls`.

## Task 4: Make direct ownership tests install Prelude deterministically

**Files:**

- Modify: the shared semantic/codegen test setup under `test/`
- Modify: focused direct ownership tests only if required

- [ ] **Step 1: Reproduce without ambient environment**

Unset `YONA_PATH` and run the direct allocation doctests. Assert the setup uses
the repository/configured test library path rather than an empty
`Codegen.ModulePaths` vector.

- [ ] **Step 2: Centralize deterministic test module paths**

Route ownership tests through the same test support helper as other semantic
and codegen suites. Do not add an environment-variable fallback specific to a
single case.

- [ ] **Step 3: Verify and commit**

Run the focused tests both with and without `YONA_PATH`, then the surrounding
runtime/codegen suites. Commit as `test: make ownership setup deterministic`.

## Task 5: Repair the generated GPU channel helper interface

**Files:**

- Modify source/type metadata responsible for `lib/Std/Gpu.yonai`
- Regenerate: `lib/Std/Gpu.yonai`
- Modify: focused interface and GPU fixture tests

- [ ] **Step 1: Lock the typed helper contract**

Assert `drainMapFloatGpu` retains `FloatMapOp`, `Receiver FloatArray`, and
`Sender FloatArray` parameters and returns `Int`. Confirm the checked-in row
currently degrades to `ADT INT INT -> INT`.

- [ ] **Step 2: Regenerate from the canonical source model**

Fix the serializer/type source if regeneration still loses the parameterized
endpoint types; otherwise regenerate the stale artifact and prove a second
generation is byte-stable. Do not hand-maintain a divergent interface row.

- [ ] **Step 3: Verify and commit**

Run interface round-trips plus CPU fallback and available Linux GPU channel
fixtures. Commit as `fix: regenerate typed GPU channel interface`.

## Task 6: Close the ownership batch after full reassessment

**Files:**

- Modify: this plan
- Modify: `docs/todo-list.md`
- Modify: `docs/superpowers/specs/2026-08-31-open-bug-stabilization-design.md`
- Modify: `CHANGELOG.md`
- Modify: `.superpowers/sdd/progress.md`

- [ ] **Step 1: Run focused and full Linux gates**

Build the debug preset; run ownership/allocation, channel, ADT, closure,
dictionary, interface, GPU fallback, and full fixture suites; then run the full
CTest preset and `git diff --check`.

- [ ] **Step 2: Record combined results later**

Close only bugs supported by fresh passing evidence. Fold these results into
the user-requested combined final stabilization update.
