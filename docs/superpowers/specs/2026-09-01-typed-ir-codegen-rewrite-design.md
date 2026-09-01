# Typed IR Codegen Rewrite Design

## Status

Approved on 2026-09-01. This design authorizes breaking compiler, runtime,
object, and interface compatibility where doing so produces a simpler and
more correct architecture. Yona is a new project; no legacy backend or ABI
compatibility layer is required at the end of the migration.

## Goal

Replace the stateful direct AST-to-LLVM backend with one complete Typed
IR-first compiler pipeline. The finished pipeline must make types, callable
ABIs, control effects, ownership, cleanup, and cross-module specialization
explicit before LLVM emission. It must close every confirmed Codegen review
finding, remove the legacy backend, and provide regression coverage across
optimization levels and supported architectures.

## Evidence and root causes

Independent review and source-level validation confirmed twenty-three problem
areas. One reported partial-application issue is not currently reachable, but
the unsafe path is a latent invariant hazard and will be deleted or made
unrepresentable.

The confirmed failures are not independent defects. They come from five
architectural problems:

1. `Codegen` holds lexical bindings, LLVM insertion state, ownership state,
   transfer state, handler state, TCO state, and module caches in one mutable
   object. Nested LLVM functions can therefore observe values and state owned
   by another function.
2. Direct native signatures and type-erased `i64` conventions are mixed
   without one adapter or descriptor contract. Closures, async callbacks,
   effects, and exceptions consequently disagree about register classes,
   arity, value representation, and ownership.
3. Ownership is inferred while LLVM is emitted through global maps, stacks,
   delayed drops, textual use counts, and booleans. It is neither part of the
   input IR nor verifiable as a whole-function property.
4. Parser and Codegen paths consume non-canonical syntax. Function clauses,
   guards, pattern decisions, cleanup, and free-variable traversal are
   repeatedly reimplemented and sometimes silently omitted.
5. The current minimal Typed IR stores type strings and scalar values only. It
   cannot yet serve as the semantic boundary described by its architecture.

The rewrite addresses these roots instead of preserving the affected
implementation paths.

## Chosen strategy

Use a big-bang backend cutover with continuously tested internal milestones.
The complete Typed IR pipeline is built in parallel through a test-only entry
point. The legacy backend remains frozen as an oracle for behavior already
known to be correct, but no generated function may mix legacy and new
ownership, callable, control-outcome, or runtime ABI conventions. Once the new
backend passes the complete parity and platform matrix, the compiler switches
atomically and the legacy implementation is deleted.

The implementation is still divided into vertical milestones so failures are
localized and every layer is exercised early. Those milestones do not become
partial production cutovers. Repairing the legacy backend first was rejected
because it would duplicate work and encode its implicit contracts into the
replacement.

## Pipeline

The canonical pipeline is:

```text
parsed AST
  -> semantic model
  -> canonical Typed IR
  -> clause/pattern canonicalization
  -> closure and effect conversion
  -> explicit control-flow IR
  -> ownership and cleanup lowering
  -> optimization passes
  -> LLVM lowering
  -> runtime ABI
```

Every arrow is a named pass with a declared input phase, output phase, and
verifier. A pass cannot leave operations from an earlier phase unless its
output contract explicitly permits them.

## Typed IR model

### Structural types

Replace string-valued types with an algebraic `Type` model. It covers Unit,
Bool, Int, Float, String, symbols, functions, tuples, sequences, sets,
dictionaries, records, nominal ADTs, arrays, channels, promises, resources,
type parameters, and effect rows. Nominal identity and recursive type
arguments are preserved; physical representation is separate from semantic
identity.

The immutable structural type vocabulary and its intern table live in
`yona_model`, below both `yona_interface` and `yona_typed_ir` in the component
graph. Typed IR modules own a table instance and refer to it with strong
`model::TypeId` values. Display strings are derived data and are never used as
semantic or ABI identity.

Function types include:

- ordered parameter types and ownership contracts;
- result type and ownership contract;
- latent effect row;
- calling convention;
- generic parameters and constraints;
- source and exported symbol identity.

Calling conventions are explicit: direct Yona, closure entry, continuation,
effect operation, async adapter, native extern, and exported C ABI.

### Functions and control flow

A Typed IR module owns nominal type declarations, imports, interface metadata,
globals, and functions. A function owns SSA blocks. Blocks own typed
`ValueId`s, instructions, block arguments, and one terminator. LLVM types,
values, blocks, and metadata do not appear in Typed IR.

The instruction set includes constants, calls, aggregate construction and
projection, field updates, closure creation and capture access, collection
operations, ownership operations, and explicit control outcomes. Terminators
include branch, conditional branch, decision-tree switch, return, raise,
perform, resume, and unreachable.

Every value records:

- structural semantic type;
- physical representation selected for the current phase;
- source range;
- ownership state (`Trivial`, `Borrowed`, or `Owned`);
- nominal identity where relevant.

`Transferred` is an ownership operation and data-flow fact, not a stable value
kind. A verifier rejects a use after transfer or an owned value that reaches an
exit without transfer or release.

### Canonical clauses and patterns

The frontend preserves each source equation as a complete function clause
containing patterns, guards, body, and source range. It no longer appends later
bodies while discarding their patterns.

A canonicalization pass converts all equations, guards, and destructuring
parameters into one typed decision tree over synthetic function parameters.
Guards must typecheck as Bool and all successful bodies must unify to the
declared result type. Later passes consume only this representation.

Non-exhaustive runtime flow enters a `MatchError` block that releases the
scrutinee, constructs a source-linked exception value, and raises it. It never
supplies a fabricated zero or null to a result block.

### Pass isolation and verification

Passes operate on a module or one function through explicit context objects.
They do not read mutable state belonging to another active function. Analyses
return immutable results keyed by `ValueId` and block identity.

Required verifiers include:

- structural type and nominal-identity consistency;
- SSA definition, dominance, and block-argument consistency;
- exhaustive visitor/canonicalization coverage;
- no lexical free variables after closure conversion;
- callable descriptor/signature agreement;
- effect operation and continuation agreement;
- ownership conservation on every CFG edge;
- cleanup coverage for every nonlocal exit;
- no high-level operations entering LLVM lowering.

## Callable and runtime ABI

### Direct calls

Monomorphic direct calls retain native typed LLVM signatures. This preserves
normal register allocation and avoids paying a universal representation cost
where the full signature is statically known.

### First-class callable boundary

Closures, partial applications, continuations, effect handlers, async work,
and dynamically selected functions use one universal entry contract. Each
callable owns an immutable descriptor containing:

- arity;
- complete argument and result types;
- argument and result ownership masks;
- effect row;
- entry convention;
- environment layout and destructor;
- native adapter entry point.

The universal entry receives an environment, an encoded word array, an
argument count, and explicit result/control-outcome storage. Generated adapters
alone encode and decode Unit, Bool, Int, Float, pointers, aggregates, and
nominal values. Runtime code never infers a semantic type from pointer bits or
casts an arbitrary native function to a universal callback.

Under-application creates a new closure containing the original callable and
the supplied encoded arguments. Over-application invokes one callable at a
time and verifies that each intermediate result is callable before applying
the remainder. Dynamic arity is runtime control flow, not a C++ decision made
while IR is emitted.

All async externs use adapters, including zero- and one-argument functions.
Directly passing a typed native function to a universal worker callback is
forbidden by the verifier and C++ API.

### Cross-module ABI

Version the interface format and serialize structural signatures, calling
conventions, ownership contracts, effect rows, nominal declarations, and
generic Typed IR bodies required for specialization. Cross-module generic
calls no longer reparse source embedded in `.yonai` files.

The interface component stores generic Typed IR fragments as canonical,
length-prefixed opaque bytes because it cannot depend upward on Typed IR. The
Typed IR component owns fragment encoding, decoding, and verification. This
keeps the component graph acyclic while allowing the interface reader to
validate framing and reject incompatible versions deterministically.

Specialization keys use complete structural types, effects, ownership, and
calling convention. A generated wrapper is exported only when crossing a
declared ABI boundary.

## Ownership and memory

### Core rules

- Trivial values require no cleanup.
- Borrowed values may be observed but must be retained before an owning
  boundary.
- Owned values must be transferred or released exactly once on every path.
- Aggregate constructors consume owned children by default. Sharing requires
  an explicit retain.
- Projection states whether it returns a borrow or transfers a child from an
  consumed owner.
- Functional update uses the uniform heap representation and a typed
  path-copy/unique-owner operation. LLVM aggregate insertion is never applied
  to runtime ADT pointers.

Collection descriptors are installed before the first insertion. Insertions
have one documented consuming contract for the collection, key, and value,
including duplicate and same-pointer replacement. Generators retain an
independent cursor owner and release it on exhaustion, early return, raise,
cancellation, and failed match.

### Escape analysis

Correct reference-counted lowering is the baseline. The new backend does not
use arena allocation until ownership parity is complete.

Escape analysis builds alias and containment dependencies and propagates
escape to a fixed point. Arena allocation is enabled later as an optional,
verified optimization. An aggregate cannot outlive any arena child it owns.
The arena pass must prove this property or leave the allocation reference
counted.

### Cleanup regions

Structured cleanup regions record owned values and resource finalizers in
Typed IR. Canonical control-flow lowering routes return, raise, perform,
resume, cancellation, and failed-match edges through the applicable cleanup
blocks. Cleanup storage is represented in IR and is not capped at a fixed
number of values.

Self-tail recursion is lowered to an explicit loop with block arguments. The
ownership pass inserts edge-specific transfers and releases before the
backedge. No function-wide `tco_cleanup_done_` flag exists.

## Exceptions, resources, and effects

### Explicit control outcomes

Generated programs stop using SJLJ. Potentially nonlocal operations produce
an explicit control outcome with a tag and owned payload. Outcomes cover at
least success, raised exception, performed effect, and cancellation. The
first-class callable ABI returns this outcome through stable out parameters so
platform C struct-return differences do not become part of the ABI.

Functions whose inferred effect row excludes nonlocal control keep the simpler
direct return ABI. Adapters bridge typed direct results and universal outcomes.

### Exceptions

`raise` transfers the complete nominal exception ADT to an error edge. Catch
clauses use the same normal pattern-decision machinery as `case`; nullary,
scalar, heap, and multi-field exceptions require no special field-zero ABI.
Propagation retains nominal identity and owner information. Unhandled
reporting formats the value through a typed exception formatter rather than
assuming a string pointer.

### Resources

`with` creates a cleanup region immediately after successful acquisition. Its
finalizer executes on normal completion, raised exceptions, effect exits,
cancellation, and early control flow. Re-raising happens only after applicable
finalizers complete.

### Effects and continuations

Effect conversion turns `perform` into an explicit performed outcome carrying
the operation identity, encoded typed arguments, and a continuation closure.
Handler clauses are closure-converted normally and capture lexical values in
an explicit environment. A handler loop dispatches the operation, invokes the
typed clause adapter, and resumes through the continuation descriptor.

No handler LLVM function inherits outer lexical maps, task-group pointers,
arenas, debug scopes, or ownership state. Effect arguments, results, and
resume values use the same callable adapters as other first-class boundaries.

## LLVM backend

The LLVM backend is a consumer of verified, ownership-lowered Typed IR. It has
three bounded objects:

- `LlvmModuleLowerer`: target data layout, runtime declarations, nominal type
  layout, symbol emission, and module finalization;
- `LlvmFunctionLowerer`: one source function, its LLVM function, block map,
  value map, debug scope, and local ABI adapters;
- `LlvmBlockLowerer`: one insertion point and its incoming block arguments.

An LLVM value map is private to one `LlvmFunctionLowerer`. APIs accept
`ValueId`, not arbitrary `llvm::Value *`, across lowering boundaries. Debug
locations and source scopes derive from Typed IR ranges. Module finalization
has one path that flushes declarations, verifies, optimizes, and verifies
again.

Accelerator lowering consumes typed operations before LLVM lowering and emits
either a verified accelerator operation or an explicit CPU operation. It does
not inspect partially emitted LLVM CFGs.

## Migration slices

1. **IR foundation:** structural types, modules, functions, SSA blocks,
   instructions, phase verifiers, printer/parser for test snapshots.
2. **Scalars and control flow:** literals, arithmetic, bindings, if, direct
   calls, returns, diagnostics, debug ranges.
3. **Canonical functions and patterns:** complete clauses, guards, decision
   trees, tuples, records, ADTs, exhaustive/no-match behavior.
4. **Callables:** free-variable analysis, closure conversion, descriptors,
   adapters, partial/over-application, higher-order calls.
5. **Ownership and collections:** explicit ownership pass, sequences, sets,
   dictionaries, generators, field updates, allocation statistics.
6. **Control outcomes:** complete exception values, cleanup regions, `with`,
   cancellation, removal of generated-program SJLJ.
7. **Async and effects:** universal async adapters, continuations, handler
   environments, typed effect values.
8. **Modules and generics:** imports, exports, traits, structural interface
   format, typed generic specialization, installed sysroot behavior.
9. **Accelerators and tooling:** GPU lowering, debug info, object emission,
   REPL/LSP/compiler consumers.
10. **Cutover:** complete parity matrix, remove legacy Codegen and obsolete
    runtime entry points, enable verified arena optimizations separately.

Each milestone extends only the parallel backend and its tests. Temporary
coexistence is internal and cannot silently mix ownership or ABI conventions
in one generated function. Legacy removal happens once, during the final
cutover, after the complete backend passes all gates.

## Testing strategy

### IR and pass tests

- builder and malformed-IR verifier tests;
- textual IR snapshots for each canonical form;
- decision-tree tests for clauses, guards, and patterns;
- closure/effect conversion tests proving no free lexical values remain;
- ownership-conservation tests over every CFG edge;
- cleanup-region tests for all exit kinds;
- pass determinism and idempotence where applicable.

Property-based generators cover structural types, carrier round trips,
ownership graphs, CFG dominance, pattern matrices, and nested cleanup regions.

### ABI matrix

Exercise Unit, Bool, Int, Float, String, every managed aggregate, ADTs,
closures, and resources across:

- direct calls;
- capturing and non-capturing closures;
- under- and over-application;
- async externs of zero, one, and multiple arguments;
- effect operations and resumptions;
- exception propagation;
- exported and cross-module calls.

Every case runs at `-O0`, `-O1`, `-O2`, and `-O3` and verifies LLVM before
and after optimization.

### Ownership and execution

End-to-end fixtures assert output and per-tag allocation balance on normal,
failed-match, exceptional, cancellation, handler, and resource-finalization
paths. Required regressions include transitive arena containment, temporary
pattern payload escape, duplicate collection replacement, named generator
source reuse, raised heap locals, more than sixteen live owners, mixed TCO
branches, and module-compiled asymmetric ownership.

ASan/LSan/UBSan builds remain mandatory. Linux x64 is the fast local gate;
Fedora 44 ARM64 under QEMU is the architecture-diverse gate. Native macOS and
Windows CI validate their object formats and platform runtime adapters.

Differential testing uses the legacy backend only for cases whose behavior is
already established as correct. New semantics and known legacy defects use
the language specification and explicit expected fixtures as the oracle.

## Finding-to-regression requirements

The rewrite is not complete until named tests cover:

- runtime dynamic under- and over-application;
- captured Float, Bool, pointer, and aggregate arguments;
- lexical captures in handlers;
- typed effect arguments, results, and resumptions;
- all async arities and register classes;
- complete nullary, scalar, heap, multi-field, and nominal exception values;
- transitive aggregate escape;
- exact-sequence and typed-pattern heap payload escape;
- collection ownership before insertion and duplicate replacement;
- nonmutating named generator sources;
- uniform heap ADT field updates;
- guarded and multi-equation function dispatch;
- exhaustive free-variable discovery for every expression family;
- `with` cleanup on every control outcome;
- arbitrary unwind-owner counts and recursive loops;
- non-exhaustive match failure;
- identical module and expression finalization;
- path-local TCO cleanup;
- AArch64 SJLJ restore under register pressure until generated-program SJLJ is
  removed;
- Bool enforcement for case guards and generator conditions;
- identical name/type environments across every or-pattern alternative.

The unsafe but currently unreachable partial-wrapper capture path is removed or
guarded by a verifier assertion and a structural unit test.

## Documentation and compatibility

The interface version, compiler architecture, runtime ABI, memory-management
design, exception/effect design, contributor guide, changelog, implementation
plan, TODO list, and public language/reference documentation are updated in
the same slices that change behavior. Generated API documentation is refreshed
when stdlib contracts move.

There is no compatibility shim for the old Typed IR, legacy Codegen internal
APIs, SJLJ generated-program ABI, runtime closure layout, or old `.yonai`
generic representation. Cached objects and interfaces with the old version
fail with a clear rebuild diagnostic.

## Acceptance criteria

The rewrite is complete only when:

1. every supported language construct lowers through Typed IR;
2. every pass verifier is enabled in debug and test builds;
3. the complete ABI and ownership matrices pass at all optimization levels;
4. sanitizer, x64, ARM64 QEMU, macOS, and Windows gates pass;
5. generated programs contain no SJLJ dependency;
6. the legacy AST-to-LLVM Codegen implementation and obsolete runtime ABI are
   deleted;
7. all confirmed review bugs and their TODO entries are closed with named
   regression tests;
8. documentation describes only the new pipeline.

## Non-goals

- Preserve legacy internal APIs, object files, interface files, or runtime
  layouts.
- Keep silent fallback paths while migration is in progress.
- Re-enable arena allocation before reference-counted ownership is verified.
- Add unrelated language features that are not required to preserve current
  specified behavior.
