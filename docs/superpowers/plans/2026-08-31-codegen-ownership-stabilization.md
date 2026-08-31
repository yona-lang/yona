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

- [x] **Step 1: Add isolated RED allocation coverage**

Compile and run `let (x, y) = ([1], [2]) in 0`, a scalar temporary
constructor case, a heap-field temporary constructor case, and the existing
`channel_basic` fixture under `YONA_ALLOC_STATS=1`. Assert per-tag zero leaks,
not merely the expected printed value.

- [x] **Step 2: Transfer owned tuple fields explicitly**

When a pattern alias destructures an owned heap tuple, retain each extracted
heap binding, register it with the let scope, then release the tuple aggregate.
Use `is_heap_type` and the semantic field identity rather than an incomplete
hard-coded CType list. Primitive fields remain direct carriers.

- [x] **Step 3: Give constructor bindings an arm lifetime**

Every heap field retained by constructor matching must enter
`arm_drop_stack_`. Drop it on the selected arm after body evaluation unless it
escapes as the arm result or is transferred. Guard-failure paths must release
bindings before testing the next arm. A non-identifier heap case scrutinee is
case-owned and is released on the selected path; named scrutinees remain owned
by their enclosing scope.

- [x] **Step 3a: Preserve named tuple owners and clean failed prefixes**

Destructuring a named tuple must not release the enclosing scope's reference
before a later use; retain or defer based on the same last-use ownership model
used by ordinary calls. Route tuple literal/symbol mismatches through a cleanup
block that releases any heap prefix bindings retained before the mismatch.
Cover named reuse with allocator-slot reuse, mismatch stats, and canonical
`if false` guard syntax plus selected-output assertions.

- [x] **Step 3b: Give record-pattern fields the same arm lifetime**

Retain heap fields extracted from record patterns and register their arm drops,
including guard failure and escaping-result handling, before releasing a
temporary record scrutinee. Assert the semantic output as well as per-tag
allocation balance for a generic heap-valued record field.

- [x] **Step 4: Make raw channel natives honor callee-owns**

Keep the source-level raw extern contracts consuming. Balance the call-owned
channel reference in every POSIX and Win32 raw helper, including error/raise
paths; `rawSend` transfers the payload into the channel as before. Do not add
checked-in borrow metadata that regeneration would silently discard.

- [x] **Step 5: Verify and commit**

Run tuple/case/ADT ownership tests, direct runtime channel tests, every
non-GPU channel fixture, and allocation-stat probes. Commit as
`fix: balance pattern-owned aggregates`.

## Task 2: Transfer temporary heap values into ADT fields

**Files:**

- Modify: `src/Codegen/CodegenApply.cpp`
- Modify as needed: shared ownership helpers in `include/yona/Codegen/Codegen.h`
- Modify: focused ADT allocation tests under `test/Codegen/`

- [x] **Step 1: Isolate named and temporary field ownership**

Prove `Some [1]`, nested constructors, and multi-field ADTs retain named heap
arguments when required but do not leak anonymous heap expressions. Include a
returned-constructor control so eliminating a duplicate cannot create a
dangling field.

- [x] **Step 2: Transfer exactly one expression-owned reference**

Teach ADT construction whether a field carrier is an owned temporary or a
borrowed/named value. Transfer a temporary directly into the new ADT; duplicate
only a borrowed value. Reuse existing transfer tracking instead of adding a
constructor-specific leak exemption.

- [x] **Step 3: Verify and commit**

Run ADT, Option/Result, constructor-pattern, exception-frame, and allocation
suites. Commit as `fix: transfer temporary ADT fields`.

## Task 3: Preserve repeated and captured heap values across consuming calls

**Files:**

- Modify: `src/Codegen/CodegenApply.cpp`
- Modify as needed: `src/Codegen/CodegenFunction.cpp`
- Modify as needed: `src/Semantics/BorrowEscapeAnalysis.cpp`
- Modify: focused closure/dictionary ownership tests under `test/Codegen/`

- [x] **Step 1: Add a minimal reusable-capture RED regression**

Capture a heap tuple in a predicate/comparator closure, invoke that closure
more than once through a consuming higher-order call, and assert correct output
plus zero leaks. Retain `dict_lifted_trait_lifetime` as the integration case;
Valgrind currently reaches `compareEntry` with an invalid captured pivot. Add a
minimal `sortBy` control whose pattern-bound `rest` is consumed by two filter
calls; reference counting must be scoped to the defining case-arm body instead
of skipping that body because the pattern introduces the queried name.

- [x] **Step 2: Retain a per-call owned reference**

The environment keeps ownership of every capture until closure destruction.
When a captured heap carrier is passed to a non-borrowed parameter, retain a
new call reference before invocation. Do not mark the environment's capture as
transferred and do not weaken the callee-owns ABI.

- [x] **Step 3: Verify and commit**

Run closure capture, higher-order List, dictionary trait, recursive closure,
frame/raise, and allocation controls. Commit as
`fix: retain captured values for consuming calls`.

## Task 4: Make direct ownership tests install Prelude deterministically

**Files:**

- Modify: the shared semantic/codegen test setup under `test/`
- Modify: focused direct ownership tests only if required

- [x] **Step 1: Reproduce without ambient environment**

Unset `YONA_PATH` and run the direct allocation doctests. Assert the setup uses
the repository/configured test library path rather than an empty
`Codegen.ModulePaths` vector.

- [x] **Step 2: Centralize deterministic test module paths**

Route ownership tests through the same test support helper as other semantic
and codegen suites. Do not add an environment-variable fallback specific to a
single case.

- [x] **Step 3: Verify and commit**

Run the focused tests both with and without `YONA_PATH`, then the surrounding
runtime/codegen suites. Commit as `test: make ownership setup deterministic`.

## Task 5: Balance async file cancellation and repair macOS file lowering

**Files:**

- Modify: `src/Runtime/Platform/FileLinux.c`
- Modify: `src/Runtime/Platform/FileMacOs.c`
- Modify: `src/Runtime/Platform/NetLinux.c`
- Modify: `src/Runtime/Platform/NetMacOs.c`
- Modify: focused platform I/O tests under `test/Runtime/`

- [x] **Step 1: Add cancellation ownership coverage**

Cancel an in-flight managed ByteArray write and prove the retained submit pin is
released through `YonaRuntimeRelease`, while raw buffers for read/accept/connect
continue to use `free`. Assert zero allocation leaks and no invalid free.

Also force async submission failure for managed file/string/ByteArray reads and
network receives. Those paths must release the buffer allocated before submit
instead of freeing only the context; cover at least one String and one ByteArray
allocation counter.

- [x] **Step 2: Complete the macOS context/identifier migration**

Use the canonical `YonaIoContext` fields and declared parameter spelling in all
macOS file and network read, write, connect, accept, fallback, seek, truncate,
and iterator paths. Run a syntax/compile probe on Linux where possible in
addition to the Linux runtime tests.

- [x] **Step 3: Verify and commit**

Run platform I/O, exact-read, File contract, cancellation, and generated binary
fixtures. Commit as `fix: balance cancelled file buffers`.

## Task 6: Release generated native-call temporaries and root results

**Files:**

- Modify: `src/Codegen/CodegenApply.cpp`
- Modify: `src/Codegen/Codegen.cpp`
- Modify: focused generated-program allocation tests under `test/Codegen/`

- [x] **Step 1: Isolate async native-call ByteArray ownership**

Prove an anonymous ByteArray passed to `writeBytes` is released after the
borrowed async submission and the awaited ByteArray returned by `readBytes` is
released after its final scalar consumer. Keep File/Linear counts balanced.

- [x] **Step 2: Run borrowed-temporary cleanup on async calls**

Apply the same post-call cleanup used by direct extern calls after submission,
without releasing the runtime's independent retained pin. Release a heap-backed
entry-point result only after printing/await resolution is complete.

- [x] **Step 3: Verify and commit**

Run `binary_write_read`, direct/async native call controls, heap-root print
controls, File fixtures, and allocation tests. Commit as
`fix: release generated boundary values`.

## Task 7: Repair the generated GPU channel helper interface

**Files:**

- Modify source/type metadata responsible for `lib/Std/Gpu.yonai`
- Regenerate: `lib/Std/Gpu.yonai`
- Modify: focused interface and GPU fixture tests

- [x] **Step 1: Lock the typed helper contract**

Assert `drainMapFloatGpu` retains `FloatMapOp`, `Receiver FloatArray`, and
`Sender FloatArray` parameters and returns `Int`. Confirm the checked-in row
currently degrades to `ADT INT INT -> INT`.

- [x] **Step 2: Regenerate from the canonical source model**

Fix the serializer/type source if regeneration still loses the parameterized
endpoint types; otherwise regenerate the stale artifact and prove a second
generation is byte-stable. Do not hand-maintain a divergent interface row.

- [x] **Step 3: Verify and commit**

Run interface round-trips plus CPU fallback and available Linux GPU channel
fixtures. Commit as `fix: regenerate typed GPU channel interface`.

## Task 8: Close the ownership batch after full reassessment

**Files:**

- Modify: this plan
- Modify: `docs/todo-list.md`
- Modify: `docs/superpowers/specs/2026-08-31-open-bug-stabilization-design.md`
- Modify: `CHANGELOG.md`
- Modify: `.superpowers/sdd/progress.md`

- [x] **Step 1: Run focused and full Linux gates**

Build the debug preset; run ownership/allocation, channel, ADT, closure,
dictionary, interface, GPU fallback, and full fixture suites; then run the full
CTest preset and `git diff --check`. Run aggregate cleanup with
`MALLOC_PERTURB_=165` so arena sequence header initialization is checked
against poisoned allocator contents.

- [x] **Step 2: Record combined results later**

Close only bugs supported by fresh passing evidence. Fold these results into
the user-requested combined final stabilization update.
