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
point. After Task 1's isolated AArch64/SJLJ baseline repair, the legacy backend
remains frozen as an oracle for behavior already known to be correct, but no generated function may mix legacy and new
ownership, callable, control-outcome, or runtime ABI conventions. Once the new
backend passes the complete parity and platform matrix, the compiler switches
atomically and the legacy implementation is deleted. The production switch,
runtime replacement, interface regeneration, documentation cutover, and
legacy deletion form exactly one commit. Any native-only repair is amended
into that commit and the amended SHA is revalidated before a fast-forward to
master; only a later plan-only validation record may be a second commit.

The implementation is still divided into vertical milestones so failures are
localized and every layer is exercised early. Those milestones do not become
partial production cutovers. A broad legacy-backend repair was rejected
because it would duplicate work and encode its implicit contracts into the
replacement; Task 1 is the sole isolated oracle-safety repair.

## Pipeline

The canonical pipeline is:

```text
parsed AST
  -> semantic model
  -> canonical Typed IR
  -> local/imported generic extraction and specialization
  -> clause/pattern canonicalization
  -> structured-concurrency planning
  -> generator lowering
  -> decision-tree/control-flow lowering
  -> async preparation
  -> cleanup preparation
  -> effect/continuation preparation
  -> closure conversion
  -> closed effect-operation instantiation
  -> effect/continuation finalization
  -> tail-call lowering
  -> accelerator selection
  -> explicit control-outcome lowering
  -> representation selection
  -> ownership analysis/lowering
  -> cleanup lowering
  -> all phase/domain/SSA verifiers
  -> LLVM lowering
  -> runtime ABI
```

Structured-concurrency planning consumes the still-present independent-let and
Serial/Parallel generator facts, assigns deterministic dependency waves and
task groups, and preserves Parallel generator plans until generator lowering
owns their cursor/pattern expansion. Async preparation then fixes every
Promise result/ownership/effect contract, await mode, worker/native adapter,
and task/group ownership before cleanup and effects. Cleanup preparation
predeclares every armed-obligation descriptor and infallible drop entry.

Effect preparation creates every handler, try boundary, continuation,
handler-entry/dispatch, outcome router, and one-shot resume function while
signatures still carry closed semantic rows. The one generic
closure pass sees every one of those functions and creates all universal
adapter functions; that freezes the complete function set. Operation
instantiation then closes runtime rows for every function, callable, Promise,
and async result descriptor in one
SCC, and effect finalization replaces prepared operations with
descriptor-driven flow without creating functions. Control-outcome lowering precedes representation selection because
it creates explicit outcome routing plus typed request/payload carriers.
Whole `ControlOutcome` storage remains hidden ABI storage, never SSA.
That pass rewrites semantic/prepared effect and async operations to dedicated
checked ABI terminators with explicit produced-value and failure successors;
those closed forms remain visible through representation, ownership, and
cleanup verification and are lowered to exactly one C ABI call only by the
LLVM block lowerer. There is no generic runtime-call escape hatch.
Ownership analysis reasons about a
logical expansion of cleanup edges, cleanup lowering materializes that exact
plan, and ownership is reverified on the resulting CFG.

Every arrow is a named pass with a declared input phase, output phase, and
verifier. A pass cannot leave operations from an earlier phase unless its
output contract explicitly permits them.

Open local generic bodies are encoded into the same canonical fragment form
as imported generics and removed from the executable function set before the
runtime pipeline advances. Local and imported calls share one specialization
cache/transaction. Unused generic types may remain in the structural table,
but every runtime-reachable function, value, operation descriptor, and LLVM
input is closed.

## Typed IR model

### Structural types

Replace string-valued types with an algebraic structural type-and-effect
vocabulary. `StructuralType` covers Unit,
Bool, Byte, Char, Int, Float, String, symbols, anonymous structural sums,
functions, tuples, sequences, sets,
dictionaries, records, nominal ADTs, arrays, channels, promises, resources,
type parameters, internal collection cursors/cursor steps, outcome-task groups,
owned-slot boundary states, and a closed set of compiler-only ABI-opaque
carriers (exception, effect request, control outcome, execution
context, callable invocation environment, and continuation-boundary context);
a separate `EffectRowId` arena stores normalized effect rows. Nominal identity and recursive type
arguments are preserved; physical representation is separate from semantic
identity.

Records retain an optional structural row rest until specialization closes
it. Function and operation schemes carry declaration-ordered type and effect
binders, including phantom binders. Effect rows preserve the full normalized
ACI form: known qualified operation applications, any number of shared or
independent flexible/opaque tails, symbolic exclusions, and raise/cancel
facts. Exclusions use the same complete application identity—qualified key
plus every ordered type/effect argument—as row members, so handling
`E.op<Int>` cannot remove `E.op<String>` or an instance differing only in a
phantom argument.

The immutable interned records and their append-only, boundary-frozen table live in
`yona_model`, below both `yona_interface` and `yona_typed_ir` in the component
graph. A projected SemanticModel and its lowered Typed IR module share one
heap-allocated, stable-address, compilation-thread-confined table arena and
refer to it with strong domain-qualified `model::TypeId` values. Clone/decode
creates a fresh domain and exhaustively remaps IDs; display strings are derived
data and are never used as semantic or ABI identity.
All type/effect/binder growth uses an append transaction with provisional
domain-qualified IDs, a reserve-and-validate prepare barrier, and a
nonthrowing commit. Specialization coordinates that transaction with every
destination-module arena so no row can publish until every allocation, hash,
deduplication, reference check, and index reserve has succeeded; failure leaves
the shared table and module byte-for-byte unchanged.

The append protocol is exact: `reserveType` and `reserveEffect` create private
provisional IDs, every reserved slot is defined exactly once, and
`prepareCommit` validates one joint type/effect DAG before producing a total
provisional-to-final map. It rejects undefined or multiply defined slots,
foreign references, cycles, and a stale table generation. Sum normalization is
delayed until all referenced slots are defined: recursively flatten, order by
complete structural content rather than insertion ID, deduplicate, reject the
empty set, and collapse one alternative to that alternative. Consequently a
stored Sum has at least two alternatives and every non-nominal structural
graph is acyclic. Preparation rewrites every private child reference through
the resolved map and reserves all storage; only `commitPrepared`, after every
coordinated module arena is also prepared, may publish in a nonthrowing step.
No provisional ID is externally observable, and abandoning any stage restores
the table and all coordinated arenas byte-for-byte.

An unboxed Sum value is one `YonaAbiValue` carrying the immutable descriptor of
its actual alternative, never the Sum descriptor or a synthetic numeric tag.
`InjectSumInst` is the only construction operation. It copies a Trivial
alternative, moves an Owned alternative, and may accept a Shareable Borrow only
after ownership lowering inserts a checked retain; a linear Borrow is rejected.
Runtime type tests compare full descriptors, including collision cases, and a
payload borrow stays rooted in the Sum owner. Text/TIRF/v2 codecs, remappers,
ownership verification, LLVM lowering, and the ABI matrix cover every
alternative and reject a nonmember or Sum-marker dynamic descriptor.

Function types include:

- ordered parameter types with `Trivial`/`Borrow`/`Consume` contracts;
- result type with a distinct `Trivial`/`Owned` contract;
- latent effect row;
- calling convention;
- generic parameters and constraints;
- source and exported symbol identity.

Calling conventions are explicit: direct Yona, closure entry, continuation,
effect operation, async adapter, native extern, and exported C ABI.

A Promise stores its Success type, structurally derived result ownership, and
complete latent effect row. Async storage and all callable results can contain
only Trivial or Owned Success payloads; Borrowed is not a result-contract
variant and an unknown/third discriminant is malformed rather than normalized.
The
internal task-group type is managed but not source-nameable or a public
interface root.

Every submitted-task Promise uses the async lift of its work row: preserve all
operation applications and `MayRaise`, and force `MayCancel = true`. The
Promise, completion adapter, await/result record, and immutable runtime
descriptor must agree on that lifted row and exact Success
type/ownership; awaiting propagates the lifted cancellation fact.
Every demanded Promise use also persists a scoped semantic
`PromiseDemandProjection::ResumeEffects` fact for the exact caller suffix at
that syntax point. Projection and AST lowering carry that row into the await
pseudo-operation; async/effect preparation must never infer it from CFG
reachability. It may remain open only inside a Canonical generic fragment and
must be closed by specialization before executable async preparation.

Every async result descriptor also stores `RaisedConstraintType`. It is null
exactly when `MayRaise` is false; otherwise it is the general
`ExceptionValue` existential for ordinary work or the exact File, Net, GPU, or
other fixed nominal error for a dedicated manifest operation. Completion
validates a Raised payload against that descriptor. Missing, excess, or
mismatched constraints are internal contract failures, never source errors.

Generic binders form a declaration-scope forest spanning nominals, traits,
instances, operations, and nested functions. Every scheme records inherited
and declared type/effect binders—including phantoms—in declaration order.
Instance methods retain distinct instance and method scopes rather than a
synthetic owner. Projection stages the complete forest and declaration tables
atomically, then validates canonical identity and visibility before publishing
the semantic interface seed.
Nominal, trait, and instance declarations may declare type parameters only;
their effect-variable vectors are empty. Functions and operations may declare
effect variables, and operation-declared variables are flexible rather than
opaque. Projection and v2 decoding reject every forbidden owner/binder shape.

Handler routers, try boundaries, and suspended cleanup obligations use one
internal linear `OwnedSlotState` structural type family/form. Each distinct
layout is interned by state kind, ordered Trivial/Consume fields, and mandatory
generated drop identity. It is never Shareable or source-visible.

Resources are structural, parameterized types backed by explicit declaration
policy. `Sender a` and `Receiver a` are distinct source-visible resource types
with distinct channel ABI roles; the shared queue core is compiler-internal.
The public `Iterator a` similarly owns unique opaque mutable producer state and
is advanced by a runtime-only vtable under a scoped unique borrow. It is not a
retained zero-argument Yona callable. Compiler collection traversal instead
uses separate linear `Cursor`/`CursorStep` carriers, so public Iterator state
and compiler cursor state cannot be confused or retained through one another.
The factory state owns any moved source, including a FileHandle, without
copying or retaining it. Advance is transactional: false leaves state, logical
position, and Empty Option/output unchanged; success alone advances. Dropping
the Iterator invokes its authenticated state destructor exactly once, including
on early exit and without a later manual resource close.

### Functions and control flow

A Typed IR module owns nominal type declarations, imports, interface metadata,
globals, pattern/decision arenas, independent-let/task-group plans, structured
try/handler/generator records, cleanup descriptors, and functions. Functions explicitly distinguish definitions, imports, and
native extern declarations with visibility/symbol linkage. A function owns SSA blocks. Blocks own typed
`ValueId`s, instructions, block arguments, and one terminator. LLVM types,
values, blocks, and metadata do not appear in Typed IR.

The instruction set includes exact typed constants (including UTF-8 String
and Symbol spelling, Unicode-scalar Char, Byte, and bit-preserving Float), calls, aggregate construction and
projection, field updates, closure creation and capture access, collection
operations, ownership operations, and explicit control-outcome routing. Terminators
include branch, conditional branch, decision-tree switch, return, raise,
perform, resume, task/group create-submit-await-join-take, prepared/runtime
channel operations, and unreachable.

Integer arithmetic, negation, multiplication, and shifts have defined
two's-complement wrapping semantics. Signed divide and remainder are converted
to `CheckedSignedDivRem`: zero follows an internal arithmetic-contract
cleanup-and-trap edge, `INT64_MIN / -1` yields `INT64_MIN`,
`INT64_MIN % -1` yields zero, and every other signed result truncates toward
zero. LLVM may not emit an unchecked dynamic `sdiv`/`srem`. Float equality and
ordering use ordered predicates; `!=` is the logical inverse of ordered
equality, with NaN and signed-zero behavior frozen by tests.

Every value records:

- structural semantic type;
- physical representation selected for the current phase;
- source range;
- ownership state (`Trivial`, `Borrowed`, or `Owned`);
- exact borrow provenance for every Borrowed value;
- nominal identity where relevant.

`Transferred` is an ownership operation and data-flow fact, not a stable value
kind. A verifier rejects a use after transfer or an owned value that reaches an
exit without transfer or release.
Borrow provenance distinguishes an actual/derived parameter borrow,
static-lifetime storage, a view backed by a dominating owned value, and a
scoped unique loan. Derived views retain their exact backing root across CFG
edges. A block-argument borrow is legal only when every predecessor supplies
the same provenance and, for owner-backed views, the same live owner in the
paired successor slot; moved/released owners, forged rootless parameter
borrows, and loans escaping their operation are verifier errors.

### Canonical clauses and patterns

The frontend preserves each source equation as a complete function clause
containing patterns, guards, body, and source range. It no longer appends later
bodies while discarding their patterns.

A canonicalization pass converts all equations, guards, and destructuring
parameters into one typed decision tree over synthetic function parameters.
Guards must typecheck as Bool and all successful bodies must unify to the
declared result type. Later passes consume only this representation.

Each source match owns a persistent match plan containing its input places,
rows, alternatives, guard/body targets, and binding order. Decision leaves
refer to a plan-qualified alternative, so no transient row vector or ambient
index is needed to reach a body. CFG lowering emits explicit typed pattern
test/projection instructions for literals, symbols, constructors, tuple/
sequence shapes, dictionary literal keys, record fields, and runtime types.
Hashes may select collision buckets but full typed payload equality decides a
match. Dictionary lookup keys are literals or symbols; this deliberate
greenfield rule removes HAMT-order-dependent matching.

Decision-tree CFG lowering receives an explicit failure policy from its
caller. Non-exhaustive function equations, `case`, and generator bindings
enter a `MatchError` block that releases the scrutinee, constructs a
source-linked exception value, and raises it. An unmatched catch re-raises the
exact incoming nominal exception. A failed handler argument pattern branches
to the next matching clause. When none remains, dispatch preserves the same
owned operation and staged argument payload without taking or fabricating
either, and transactionally updates that request with the fresh deep-handler
boundary required for outward propagation. No failure path supplies a
fabricated zero, null, success value, or replacement exception.

### Pass isolation and verification

Passes operate on a module or one function through explicit context objects.
They do not read mutable state belonging to another active function. Analyses
return immutable results keyed by `ValueId` and block identity.
Each module also carries a nonserialized mutation-domain token;
specialization caches bind their destination-local FunctionIds to that token
and cannot reuse them in a parsed, cloned, or otherwise different module.

Required verifiers include:

- structural type and nominal-identity consistency;
- SSA definition, dominance, and block-argument consistency;
- exhaustive visitor/canonicalization coverage;
- no lexical free variables after closure conversion;
- callable descriptor/signature agreement;
- effect operation and continuation agreement;
- ownership conservation on every CFG edge;
- cleanup coverage for every nonlocal exit;
- no semantic or prepared high-level operations entering LLVM lowering; only
  closed checked ABI terminators already verified by ownership and cleanup may
  survive to the exhaustive LLVM block lowerer.

## Callable and runtime ABI

### Direct calls

Monomorphic direct calls retain native typed LLVM signatures. This preserves
normal register allocation and avoids paying a universal representation cost
where the full signature is statically known.
From effect preparation onward, every non-native Yona direct entry also has
one uniform trailing hidden continuation-boundary-context pointer, regardless
of whether its public effect row is empty. Direct callers pass their exact
dominating parameter; native externs and cleanup-drop callbacks do not gain
the argument. This unconditional internal rule avoids an implicit transitive
"needs context" ABI bit across modules and generated handler/try helpers.
Dynamic callable instructions also gain that exact dominating operand during
effect preparation. Operation-free dynamic calls preserve it into outcome
lowering and their universal adapters forward it; no later pass rediscovers an
ambient parameter. The sole exception is the generated CleanupDrop root: its
preverified effect-free finalizer apply passes a literal null and has no
boundary-context parameter.

Native linkage is an authenticated pair, not a name heuristic. Every native
declaration records both `NativeAsyncKind` and `NativeBoundaryRoute`:
`StableExternal/Synchronous`, `CheckedOutcomeV2/DedicatedOutcome`, or
`CheckedDirectV2` with `Synchronous` or `ThreadPool`. Other pairings and a
dedicated opcode on a non-Outcome route are invalid. Checked-direct leaves use
one descriptor-backed `YonaAbiCheckedDirectNativeEntryV2` signature and are
legal only as saturated direct calls; first-class and ThreadPool use goes
through generated exact-arity adapters whose bodies invoke that same checked
operation. A false result is strictly precommit—arguments and result stay
unchanged and cleanup traps—while true clears every Consume slot and publishes
the sole typed result. LLVM never declares the C symbol under the logical Yona
signature or casts it to another callback type.

### First-class callable boundary

Closures, partial applications, continuations, effect handlers, async work,
and dynamically selected functions use one universal entry contract. Each
callable references an immutable, generated static-lifetime descriptor containing:

- arity;
- complete argument and result types;
- parameter ownerships and the separate result ownership contract;
- effect row;
- entry convention;
- environment field descriptors;
- generated universal adapter entry point.

The public apply API consumes an owned callable plus heterogeneous typed
argument records: Trivial/Borrow sources remain immutable for an exact
synchronous call, while Consume records name mutable owner slots that are
always consumed on a structurally valid apply. Underapplication clones a
Borrow only when Shareable and otherwise returns a typed failure. A shared
transactional argument stage copies/clones before commit and lets partial,
effect, and async objects allocate and reserve publication before moving any
Consume owner. The callable runtime validates/references that immutable
descriptor; every universal adapter receives an immutable invocation view over a
mutable callable-owned typed environment array plus an optional borrowed
recursive-group pointer, a mutable argument carrier array owned by the
enclosing argument stage, an argument count, and
an explicit borrowed continuation-boundary context, versioned execution
context, and result/control-outcome storage. The boundary context is a
distinct hidden Borrow-only raw-address channel, never an invocation-
environment field, descriptor child, stored capture, or public carrier.
All Yona direct/universal calls forward it independently of ordinary closure
storage; root wrappers pass a null runtime value, resumed calls pass an
ephemeral Chain view whose lookup tail is the forwarded ambient context, and
immediate boundary-function/frame calls pass a Direct view whose parent is the
forwarded outer context.
Adapters read Trivial/Borrow carriers
without mutation and move/clear Consume carriers before the direct call.
Generated adapters alone encode and decode Unit, Bool, Byte, Char, Int, Float,
Symbol, String, pointer carriers, aggregates, and nominal values. Runtime code never infers a semantic type from
pointer bits or casts an arbitrary native function to a universal callback or
object pointer; generated adapters directly reference typed native entries.
The invocation view is an internal hidden Borrow-only raw address, never a
public value-descriptor child. Every recursive-member direct entry receives it
even with zero external captures; ordinary direct closure entries need it only
when they have environment storage. Only recursive members receive a non-null
borrowed group, and only verified direct intra-SCC calls propagate that view.
Trivial results copy and owned results move; Borrowed callable results are
unrepresentable. Null required storage, overlapping storage, or
invalid sentinel/storage state returns false before reading a carrier and
leaves every owner, output, and failure slot unchanged. By contrast, a valid
callable-owner slot whose carrier is null is a structurally valid call: it
returns true, consumes the callable slot and all Consume arguments, and writes
the reserved null-callable C diagnostic. A structurally valid `ApplyMove` is callee-owns: it
returns true and clears/releases the callable and every Consume slot on
Success, nonlocal control, descriptor/clone/OOM failure, and non-callable
intermediate results, writing one complete outcome. By contrast, standalone
Create, argument-stage, and publication APIs finish descriptor/clone/
allocation work before commit; their pre-commit failure leaves owners
unchanged and writes a complete nonallocating typed ABI-failure outcome.

Those ABI-failure outcomes are C-boundary diagnostics, not implicit language
effects. Generated descriptor/alias/null/infrastructure-OOM failure edges run
cleanup and trap; a source-visible fallible operation instead returns ABI
success and carries its exact declared Raised value through the normal typed
outcome. No diagnostic accompanying `false` enters source control flow. A
dynamic invoke keeps distinct checked failure edges for false-before-commit
(callable/Consume arguments unchanged) and a true reserved diagnostic after
the callee-owns commit (those owners cleared); ownership verification never
merges those post-states. A semantically direct-return function never acquires an outcome ABI
merely because an implementation detail allocates.

Environment access is explicit. Reusable lexical callable instances can only
borrow fields. A lexical callable containing any Consume capture is
non-Shareable; its unique ApplyMove may atomically take and clear those slots
before entering the typed callee. A synthetic RuntimePartial is different: its
root/prefix slots use Consume access in a private per-apply invocation copy,
while instance Shareability is derived from the actual root/prefix values and
the absence of a source Consume parameter. `TryRetain` is therefore an
instance-sensitive fallible operation, not a descriptor-wide unconditional
retain, and no adapter const-casts an immutable environment to consume a
capture.

That fallibility remains explicit in IR. Ownership lowering replaces every
`RetainInst` with a branching `TryRetainRuntime`; only Success produces an
Owned value. Failure produces no value or source diagnostic and leaves the
source—and a Borrow's backing owner—live for edge-specific cleanup before
trap. For an ABI value the runtime first verifies the static descriptor, then
retains through the actual dynamic descriptor, which is essential for Sum and
ExceptionValue carriers. No later pass may fold the failure edge away or turn
a failed retain into a null owner.

Self- and mutually recursive first-class functions are converted as maximal
binding SCCs. One group allocation owns the union of only Trivial/Shareable
outer captures and embeds non-owning member shells behind one group-wide
reference count; member bindings are never captures, so the layout contains no
strong cycle. Direct intra-SCC calls borrow the current invocation view and
perform no allocation or retain. A first-class self/sibling escape explicitly
retains its group-backed member; the borrowed member may feed only Borrow uses
or that retain, and every owning/escaping use consumes the retain result. A
partial owns the member as its root and
therefore keeps the group alive. Linear or unknown-shareability external
captures are rejected rather than hidden behind a cyclic one-shot object.
Callable/group/suffix descriptors use their own cycle-safe graph validator;
type and effect edges delegate to the structural validator, so legal recursive
runtime-row/operation SCCs are not mistaken for callable back-edges.

Symbols are Trivial pointers to immutable process-lifetime spelling
descriptors. Equality uses a fingerprint only to reject quickly and then
compares complete UTF-8 bytes, so separately compiled modules agree without a
fallible runtime interner or collision-prone per-module integer IDs.

Under-application creates a new closure containing the original callable and
the supplied encoded arguments. The public runtime apply ABI accepts no more
than the callable's remaining arity and rejects over-application before
commit. Source over-application is lowered into an explicit chain of exact
typed direct/dynamic calls, with each intermediate result and its complete
control outcome visible in Typed IR.

Reusing a Shareable callable retains a separate owner before apply. A callable
with a non-Shareable capture is linear and moves into its exact/partial call.
Argument/output/owner byte ranges are validated as nonoverlapping before any
carrier read or mutation.

### Async and structured concurrency

ThreadPool async externs use exact-arity universal adapters, including zero-
and one-argument functions. IoUring/platform submissions use their dedicated
typed Outcome entry FunctionIds, while Synchronous native calls remain direct.
Directly passing an arbitrary typed native function to a universal worker
callback is forbidden by the verifier and C++ API.
Runtime submission accepts only the callable's exact remaining arity. The
compiler generates an exact-arity wrapper for under/overapplied source
expressions; workers never curry or inspect an intermediate result descriptor.
Transparent Promise use inserts an await only where the promised Success value
is demanded. Returning or passing a Promise as a Promise never eagerly awaits
it.
Outcome task and group handles have explicit create/retain/release/cancel/
await contracts. A group owns its ordered child references; a child records
only atomic local membership state and its source ordinal and never stores or
dereferences a group pointer. Generated code never retains a group or releases
one from a worker, so there is no strong group/child cycle or self-join.
Each worker passes an explicit stack execution context backed by its retained
task's atomic cancellation flag. Generated cancellable functions forward that
context through nested direct/universal calls and query it at declared
cancellation points; synchronous roots use a process-lifetime never-cancelled
context. No TLS current-task state or boundary-only cancellation check is
permitted.

Cooperative CPU code uses the public
`Std\Task.checkCancellation : Unit -> Unit ! {Cancel}` wrapper. Its private
`CancellationCheck` declaration is an authenticated compiler-plan intrinsic,
not a native leaf: semantic projection records the exact direct application
and AST lowering emits `CancellationPointInst`, which queries the explicit
context and returns Cancelled through normal cleanup. Name matching, an
arbitrary user `intrinsic`, or an implicit loop-backedge poll is forbidden.

The semantic model records binding-identity dependency edges for each
multi-binding let and an explicit Serial/Parallel fact for every generator.
Async planning computes deterministic declaration-order topological waves
before generator lowering can erase syntax. Independent wave members and
parallel-comprehension elements execute in scoped task groups; extraction is
always source-index ordered, irrespective of completion order.

Typed IR represents group creation, task submission, task await, group join,
and joined-group extraction explicitly. One unique group owner coordinates
and owns the ordered child-result route, including a runtime-sized repeated shape for parallel
comprehensions. Join waits for every child and marks the group Joined but never
consumes a child outcome; its closed result row always has `MayCancel=true`
and it yields only Success(Unit) or wait-context
Cancelled(Unit) and leaves the group owner live. The compiler then moves each
next outcome in submission order. If extraction yields Performed, the caller
continuation captures prior results, accumulator/loop state, and the advanced
group owner so resume continues without rerunning or losing siblings.
Raised/Cancelled release that group and all unclaimed outcomes exactly once.
After grouped publication, the compiler immediately releases the uniform
temporary task owner and marks the child `Attached`; this membership state,
not the task's separate public `ExternalClaimCount`, gives group extraction
exclusive authority over the result. A C host may retain a child for lifetime
testing without turning that alias into an individual outcome claimant.
Individual Move/Keep await on an Attached, GroupClaimed, or GroupDrained task
is rejected with owner/output unchanged both before and after group join; only
ordered joined-group extraction claims those outcomes.

Control-outcome lowering replaces semantic async/channel forms with nine closed
runtime terminators: group create, task/native submission, task await, group
await, joined-result take, and channel create/send/receive/try-receive. Async
submission, task-await, and joined-result-take carry their interned result
descriptor; group create has the fixed owned-group contract, group await has
the fixed Trivial Unit contract, and channel forms carry exact structural
result types. Every blocking task/group/channel form
also carries the already-dominating execution-context SSA operand. Each has
explicit Success/Raised/Performed/Cancelled successors where its selected API
permits them and exact precommit/postcommit internal-failure edges. These forms deliberately survive
representation, ownership, and cleanup lowering so the LLVM block lowerer can
emit exactly one selected OutcomeTask/platform/channel/GPU call; LLVM never
rediscovers the async strategy or constructs an outcome edge.

Await mode is explicit. Keep is mandatory for every Keep-safe observation,
even a local last use; ownership lowering releases the retained task owner
afterward. Keep-safe means a Trivial or statically Shareable Owned Success and
a row that neither raises nor performs. Move is reserved for a non-Keep-safe
linear Promise whose sole external-owner provenance is proven across function
boundaries. It atomically reserves the claimant word only from
the exact final-owner state, blocks retains/Keep while reserved, waits, and
commits consumption only after a valid outcome; Poisoned/internal failure rolls
the reservation back with owner and output unchanged. Every task
stores an immutable closed result descriptor: exact Success type/ownership,
raise/cancel facts, and permitted closed operations. Completion validates all
four outcome kinds against it; an invalid first completion terminates with a
distinct internal Poisoned state and no language outcome rather than leaving a
task pending or inventing an undeclared effect. A subsequent await reports the
verified-unreachable ABI failure to its internal cleanup edge. IO,
native-handle, channel-task, and GPU creation receive this same
generated descriptor explicitly.

The checked IR keeps those poststates separate. A false preflight or Poisoned
result first rolls back any reservation and follows `Failure` with the task,
resume argument, continuation, and Empty output unchanged. If a structurally
valid runtime call has already committed Consume owners but reports a reserved
diagnostic, it returns true and follows a distinct `ConsumedFailure` cleanup
edge; it can never reuse the rollback state. Continuation resume obeys the same
preflight/commit split. Verifiers and LLVM lowering reject any merge of these
owner states, even when both edges ultimately trap.

AwaitMove has two separately linearized true commits under the task lock. A
published terminal outcome is moved to the caller before the reserved claimant
becomes zero. If wait-context cancellation wins while the task is still
Pending, the await instead returns a fresh Cancelled observation, clears the
owner, consumes the reserved claimant, and leaves future terminal storage
untouched. A runtime-private completion/reaper reference then owns the task
until that later outcome is released. The lock orders this choice: an already-
published terminal wins; otherwise the Pending cancellation commit wins.

Task/group and platform create/submit use a strict publication boundary.
Pre-commit validation/copy/allocation failure returns false with owned inputs
unchanged, a null handle, and the documented C diagnostic outcome. Once the
task/request is published, backend failure returns true and completes that
owned task with an exact typed terminal outcome; inputs are never restored and
no API returns a null Promise or status-only error.

Blocking task, group, and channel waits poll the explicit execution context at
entry and around waits bounded to at most 10ms. They register no ambient
cancellation waiter. Platform string operands are typed Yona values copied by
length; an embedded NUL in a path/host is source data and completes the task
with the exact declared file/network Raised value. Only descriptor/storage/OOM
failure returns false to the internal cleanup-and-trap edge; content preserves
embedded NUL unchanged.
No native handle crosses the language or public ABI as an Int. File and socket
resources store the full platform-width native handle privately, including a
high-bit Win64/ARM64 socket value, and operations accept only the typed
resource carrier. Pins keep that object alive across committed asynchronous
work without making the source resource Shareable.

Replacement channel Send first validates non-null, initially Empty,
byte-disjoint output storage before reading `OwnedValue`. Structural false
leaves value and output unchanged; every structurally valid true path
consumes/clears the value, whether it reports Success, cancellation, or a
declared channel error. Receive likewise requires initially Empty distinct
output and writes it exactly once.

All four channel forms carry one structurally interned
`ChannelDescriptorPlan` containing the payload, distinct Sender and Receiver
resource types, endpoint pair, `Option payload`, exact ChannelError nominal,
and both constructor tags. LLVM emits one immutable static descriptor from the
plan and passes it unchanged to create/send/receive/try-receive; it does not
reconstruct companion types from a module name or operand. Create returns the
direct Owned `(Sender a, Receiver a)` pair. Send accepts only Sender and
Receive/TryReceive only Receiver. Each operation has an unchanged false edge
and a separate true `InvalidOutcome` cleanup-and-trap edge; Send reaches the
latter only after consuming its value, while endpoint borrows remain live.
There is no ambient task-group parameter on channel calls.

GPU submission is functional unique-or-copy. A unique FloatArray owner may be
transferred for in-place mutation; an aliased input is copied into private
storage before publication so existing aliases remain immutable. Pins survive
until fence/device retirement even after cancellation. Terminal outcomes are
Success(FloatArray), Raised(GpuError), or Cancelled(Unit), never a successful
integer status.

### Cross-module ABI

Version the interface format and serialize structural signatures, calling
conventions, ownership contracts, effect rows, nominal declarations, and
generic Typed IR bodies required for specialization. Cross-module generic
calls no longer reparse source embedded in `.yonai` files.

`YONAI 2` has explicit Skeleton and Complete kinds. Skeletons contain only
the declaration graph needed to typecheck a genuinely cyclic SCC and never
contain generic Typed IR fragments. Complete interfaces contain a fragment for
every emitted `GenericFunction`; unrelated private locals are absent, while a
private dependency reachable from an emitted generic is embedded in that
fragment closure. Complete artifacts are the only interfaces accepted by normal compilation.
An acyclic module is produced directly as Complete. A Skeleton is built from a
separate `SkeletonDeclarationModule` and opaque
`PreparedSkeletonInterfaceRoots`; neither can contain a Typed IR FunctionId,
CFG, body, runtime descriptor, or runtime phase. There is no declaration-only
`typed_ir::Module` state. The Complete builder accepts exactly Canonical with
its original prepared roots during bootstrap extraction, or GenericPrepared
with the remapped/resealed roots during ordinary compilation, and rejects
cross-paired tokens and all other phases.

A cyclic SCC uses one all-member declaration/binder transaction. It first
predeclares every member and resolves annotated cross-edge signatures using
read-only predecessor-Complete catalog sessions plus a separate in-memory peer
environment; those sessions are destroyed before sealing. It then validates
and writes every Skeleton to the private provisional root before typechecking
any body. Each member is
completed against the entire staged Skeleton set, runs normal derivation and
ABI-root preparation, and uses only read-only generic extraction to attach
fragments—never runtime specialization, a specialization cache, or runtime
phase advancement. Only after every Complete matches every Skeleton outer
declaration is the whole Complete SCC published atomically; no consumer can
observe a partial or mixed set.

After the `YONAI 2\n` magic, the interface uses a normative fixed-width
little-endian, length-framed section grammar with canonical type/effect ID
reindexing, normalized relative paths/line endings, and one canonical golden
hash. The exact bytes and hashes are platform-independent; native jobs must
all reproduce the same fixture. The interface component stores generic Typed IR fragments as a
length-prefixed canonical UTF-8 `YONA-TIRF 1` envelope containing normalized
virtual-source metadata, an explicit root function, and canonical Typed IR
text. That Typed IR module owns the canonical
`FragmentGenericDefinitions` arena—declaration identity, FunctionId, effective
binders, constraints, and capture schema—which is the sole encoded index; TIRF
has no side table and readers never reconstruct it from ancestry or spelling.
The envelope remains opaque to the interface component because it cannot depend upward on
Typed IR. The Typed IR component owns fragment renumbering, printing, parsing,
and verification. This
keeps the component graph acyclic while allowing the interface reader to
validate framing and reject incompatible versions deterministically.

Specialization keys start with the generic declaration identity and then use
its complete effective inherited-plus-declared type/effect arguments
(including phantoms), ownership, and calling convention. Canonical fragment construction alpha-renumbers every ID
domain, including semantic binding IDs, and serializes its effective inherited
plus declared binder environment—including phantom type/effect binders. A
captured local generic declares an
ordinal capture schema in both its outer record and fragment root; producer
binding IDs map call-site values into that schema and never enter the cache
key. Exported/imported generics cannot carry lexical captures. Fragments carry
the transitive bodies of same-module Private callees. A same-module
Module/Public helper is declaration-only under its producer linkable symbol,
just like an imported or native dependency, so its producer object remains the
single definition. Fragments reject hidden free captures in that closure.
An imported generic is likewise declaration-only in the consumer TIRF with
its exact Symbol/signature/binders/constraints; specialization obtains its body
from the producer's Complete catalog record. This permits cyclic SCC members
to complete against Skeleton declarations without pretending those Skeletons
contain bodies.
Local and imported definitions enter one validation and specialization path.
A generated wrapper is exported only when crossing a declared ABI boundary.

Each embedded open Definition has one validated
`FragmentGenericDefinition` index entry containing its declaration identity,
effective binder environment, structural trait constraints, capture schema,
and fragment-local FunctionId.
Private nested-generic lookup and mutually recursive generic SCCs resolve only
through this index; a fragment reader never guesses a definition from name,
body occurrence, or ancestry.

Every `FunctionDeclarationIdentity`-bearing field in an outer `YONAI 2`
record—including FunctionType source identity, linkage, trait/default/
instance targets, and the outer GenericFunction identity—must contain the
linkable `SymbolIdentity` alternative. `LocalFunctionIdentity` is legal only
inside TIRF.
A reachable local trait/default target is promoted transactionally to a
Module-visible symbol before generic extraction when it is capture-free and
linkable; otherwise interface construction rejects it.

A public ABI slice is computed from every exported function/generic/type/
trait/instance/operation root and includes the transitive nominal, structural, trait-target,
effect, and generic-fragment closure needed to interpret it. Private records
outside that closure stay absent; a private generic dependency reachable from
an exported generic remains inside that fragment closure. Every
FunctionId-bearing field and module-owned arena is remapped, not just direct
calls. Imported declarations merge with local definitions by canonical
identity, and specialization results are scoped to the destination module's
mutation domain.

The atomic frontend order is: lower the Canonical bundle; run derivation so
generated functions, `LocalGenericSeed`s, and derived instances commit
together; prepare/promote interface ABI roots; extract generic definitions and
prepare the runtime module; then build the read-only interface slice. Skeleton
completion runs the same derivation and ABI-root preparation but uses only
interface extraction—never runtime specialization.
Catalog/import sessions are immutable snapshots of the local semantic seed.
The frontend destroys and rebuilds the destination-bound session after
derivation commits and again after ABI-root promotion commits, so generic/
trait resolution observes newly derived instances and promoted symbols rather
than a stale overlay.

Normal generic preparation rebuilds executable functions transactionally and
returns a total old-to-new FunctionId map. It uses that map to create a fresh
`PreparedInterfaceRoots` token bound to the rebuilt RuntimeModule, validates
identity, signature, visibility, linkage/symbol, binders, and body status for
every root, then reseals the type-table generation and rooted-prefix
fingerprint. Reusing the pre-rebuild token is invalid even when dense IDs happen
to match.

The caller's specialization cache is never mutated incrementally. A movable,
noncopyable `SpecializationCacheTransaction` holds all InProgress, Ready, and
Failed states in an overlay while the unpublished RuntimeModule, type table,
key adapters, interface roots, and accelerator candidates prepare. The cache
must be unbound and empty on entry. After all validation succeeds,
`prepareCommit` reserves publication, and one nonthrowing critical section
publishes the GenericPrepared module, binds the cache to that mutation domain,
and commits the overlay. Any earlier or late candidate failure discards both
module and overlay, leaving the caller cache unbound and byte-identical so the
same cache can be retried.

Canonical/generic Typed IR retains explicit fully qualified trait-method uses.
After type/effect substitution, generic preparation resolves local/imported
instances, defaults, and superclass evidence through the same catalog API as
type checking, then rewrites calls and first-class method values to concrete or
ordinary generic functions. No unresolved trait operation or cleanup
finalizer survives `GenericPrepared`.
Trait defaults and instance methods name a full target application—declaration
identity plus ordered type/effect arguments—not a bare symbol. Deferred
evidence stores the complete structural constraint and owning scheme. The
shared model/semantics resolver consumes this structural request and has no
Typed IR dependency; TypeChecker, local preparation, and imported
specialization therefore select the same instance or emit the same ranged
ambiguity/missing-instance diagnostic.

The catalog returns owning structural v2 records. One
`StructuralSchemeImporter` per imported module recreates fresh TypeChecker
schemes and effect-solver identities for each checking request; no
`MonoTypePtr`, solver object, or Typed IR value crosses that lifetime boundary
or originates in `yona_interface`.

Every native extern serializes its explicit ABI-distinct replacement symbol
and exact async strategy. No compiler phase derives or rewrites native linkage
from source spelling, and a Complete v2 artifact cannot reference a legacy
callable/task/platform signature.

Runtime-entry admission is generated from one typed semantics-layer registry,
not duplicated switches. Its closed 25 rows are the thirteen array intrinsics,
two key queries, five Iterator operations/producers, three String storage
intrinsics, and two File iterator producers; producer rows also authenticate
their runtime-only adapter and state-destroy symbols. Semantic projection,
descriptor planning, LLVM selection, manifest generation, and ABI conformance
consume that same table. `CancellationCheck` and `TaskSpawn` are compiler-plan
intrinsics and deliberately have no runtime-entry rows. Native v2 linkage serializes both the exact
`NativeBoundaryRoute` and `NativeAsyncKind`, rejecting unknown discriminants.

## Ownership and memory

### Core rules

- Trivial values require no cleanup.
- Borrowed values may be observed but must be retained before an owning
  boundary.
- Owned values must be transferred or released exactly once on every path.
- Aggregate constructors consume owned children by default. Sharing requires
  an explicit retain.
- Projection states whether it returns a borrow or an explicit retain. A
  separate single-result take operation consumes an aggregate, moves one
  selected field when unique. When shared, it copies a Trivial field or retains
  a Shareable field and releases only the caller's aggregate owner; a
  non-Shareable selected field is rejected unchanged. Multi-field patterns project then
  release the aggregate once.
- Functional update uses the uniform heap representation and a typed
  path-copy/unique-owner operation. LLVM aggregate insertion is never applied
  to runtime ADT pointers.

Structural tuple/record construction is distinct from nominal ADT
construction; a nominal constructor identity is never fabricated for a
structural aggregate. Sequence, set, and dictionary literals and generator
accumulators have explicit consuming Typed IR operations that lower through
descriptor-first runtime APIs. Handle outputs start empty and cannot alias
owning input slots; validation/allocation failure leaves every owner unchanged.
Whole aggregate/sequence/set/dictionary construction uses one bulk BuildMove
transaction, never a committing Create-plus-insert loop: all descriptors,
Hash/Eq calls, topology allocations, and result storage are prepared before an
infallible commit clears any child. Each published composite stores an atomic
instance Shareable bit computed from its actual staged children; functional
update/path-copy recomputes it at the same commit point.

Collection descriptors are installed before the first insertion. Insertions
have one documented consuming contract for the collection, key, and value,
including duplicate and same-pointer replacement. Every set/dictionary plan
also persists the semantically resolved Hash and Eq target applications. Its
static key-operations descriptor uses generated pure Borrow adapters, includes
the exact key type and both complete target applications in canonical bytes,
and treats hash only as a bucket prefilter before Eq; callback addresses and
fingerprints are never semantic identity. Generators acquire an
independent cursor owner—retain/clone a Shareable persistent source or move a
linear iterator—and release it on exhaustion, early return, raise,
cancellation, and failed match. Cursor and cursor-step carriers are internal,
linear, and non-Shareable. A step owns its unextracted element; extracting the
value consumes and clears the step, while releasing an unextracted step drops
that element. A shared persistent source may yield only a Trivial copy or a
Shareable retained owner; a unique persistent source and a linear iterator may
move destructively. Dictionary cursors yield the structural key/value tuple.
The semantic model persists the resolved iterable adapter identity; generator
lowering emits one static versioned cursor descriptor/vtable from that
evidence and never selects iteration by type spelling. Initialize/advance/
destroy adapters are closed, operation-free, nonraising, and noncancelling,
and advance is transactional: failure leaves the logical position unchanged.
Iteration errors must therefore be explicit element values such as `Result`,
not hidden cursor ABI diagnostics.
Named reuse of a moved linear source is rejected.

Key adapters are created before the function set freezes. Generic preparation
resolves both complete Hash/Eq target applications, canonicalizes plans by key
type and those targets, reserves a fixed-ABI `KeyHashAdapter` and
`KeyEqualsAdapter` FunctionId for each surviving plan, fills their pure Borrow
bodies, and transactionally rewrites every deduplicated plan reference. TIRF
import remaps the applications and regenerates destination-local adapters; it
does not preserve producer FunctionIds. Descriptor emission may reference only
the recorded IDs and may not synthesize an LLVM callback thunk.

Arrays expose exactly thirteen callback-free checked storage leaves: Alloc,
Length, Get, and PutMove for distinct `YonaByteArrayRef`, `YonaIntArrayRef`,
and `YonaFloatArrayRef` carriers, plus ByteArrayFromString. Every call receives
the exact closed array descriptor; PutMove is transactional and the three
incomplete reference types are non-interchangeable. Array map/filter/fold and
sequence conversions remain Yona code. Their semantic admission comes from
the generated runtime-entry registry, enum-based Typed IR lowering, and exact
prototype/ownership tests—never raw callback or element-pointer bridges.

### Escape analysis

Correct reference-counted lowering is the baseline. The new backend does not
use arena allocation until ownership parity is complete.

Escape analysis builds alias and containment dependencies and propagates
escape to a fixed point. Arena allocation is enabled later as an optional,
verified optimization. An aggregate cannot outlive any arena child it owns.
The arena pass must prove this property or leave the allocation reference
counted.

### Cleanup regions

Structured cleanup regions record owned values and cleanup actions in
Typed IR, and AST/canonical lowering assigns each block its innermost active
region. Ownership/cleanup analysis derives the applicable suffix on return,
raise, perform/resume, cancellation, and failed-match edges. Cleanup lowering
materializes blocks for true scope exits; a Performed suspension instead moves
an armed suffix instance into its continuation and resume reconstitutes it. Cleanup
storage is represented in IR and is not capped at a fixed
number of values.
Manifest resources use `ReleaseResource`, whose declaration supplies a static
infallible `void(YonaAbiWord)` release callback; no function-typed SSA value is
created. `InvokeFinalizer` is reserved for an actual source-selected,
non-resource Closeable finalizer. That action stores an ordinary
function-typed SSA value—not a bare callable descriptor—and closure conversion
captures its complete environment before cleanup lowering threads both the
resource and callable along exit edges. These two ABIs are never cast or
merged.

Before effect preparation, cleanup preparation interns one descriptor and
private `CleanupDrop` entry per structurally distinct live suffix. Each
suspension packs a fresh linear OwnedSlotState instance from that descriptor.
Packing and taking are transactional fallible
terminators: pack failure leaves source owners unchanged, take succeeds
all-or-nothing, and abandonment invokes the mandatory infallible DropMove
exactly once. Descriptor recipes contain only state-field ordinals and never a
foreign function's ValueId. Handler and try states use the same checked
Borrow/Take primitive and mandatory canonical drop identity.

Self-tail recursion is lowered to an explicit loop with block arguments. The
ownership pass inserts edge-specific transfers and releases before the
backedge. No function-wide `tco_cleanup_done_` flag exists.

## Exceptions, resources, and effects

### Explicit control outcomes

Generated programs stop using SJLJ. Potentially nonlocal operations produce
an explicit control outcome with a tag and owned payload. Language outcomes
have exactly four tags: Success, Raised, Performed, and Cancelled; Empty exists
only as a C storage sentinel and is never a language result. The
first-class callable ABI returns this outcome through stable out parameters so
platform C struct-return differences do not become part of the ABI.

Functions whose inferred effect row excludes nonlocal control keep the simpler
direct return ABI. Adapters bridge typed direct results and universal outcomes.
Effect rows carry explicit raise and cancellation facts; open or unknown rows
are conservative.

### Exceptions

`raise` transfers the complete nominal exception ADT to an error edge. Catch
clauses use the same normal pattern-decision machinery as `case`; nullary,
scalar, heap, and multi-field exceptions require no special field-zero ABI.
The static ExceptionValue type is an unboxed open-existential constraint. Its
runtime carrier is `YonaAbiValue` containing the actual flagged nominal
descriptor and word; clone/release dispatch through that actual descriptor,
and the marker is never boxed or stored as the carrier's dynamic type.
Propagation retains nominal identity and owner information. Unhandled
reporting formats the value through a typed exception formatter rather than
assuming a string pointer.

### Resources

`with` creates a cleanup region immediately after successful acquisition. Its
finalizer executes on normal completion, raised exceptions, cancellation,
early control flow, and abandonment of a suspended effect path. A live
Performed suspension transfers the obligation instead of finalizing it.
For a source-selected non-resource Closeable value, the implicit finalizer has
the exact `Consume value -> Unit` DirectReturn contract with an empty effect
row. A manifest resource instead uses only its declaration's infallible static
release callback. A fallible close is an explicit API, not an implicit
destructor. Both forms therefore
preserve the in-flight outcome, with nested finalizers running in reverse
acquisition order. A suspended continuation owns one armed cleanup obligation:
normal resume reconstitutes it, while abandoning the request/resume invokes a
precreated effect-free drop thunk exactly once. Cleanup is neither run eagerly
at `Performed` nor silently lost when a one-shot continuation is discarded.

### Effects and continuations

Effect conversion turns `perform` into an explicit performed outcome carrying
an immutable canonical closed-operation-instance descriptor, encoded typed
arguments, and a non-Shareable typed continuation chain. Every chain value has
an authoritative closed descriptor containing its one input, final result,
ownerships, continuation calling convention, and complete runtime effect row;
creation, append, take, and resume validate and transport that descriptor
rather than reconstructing it at a handler. Raw descriptors are normalized at
function/handled-body boundaries: operation result to boundary result with
the boundary's complete closed effect row. Exact suffix rows remain verifier
facts and must be subsets, but do not leak site-specific ABI types. Each
appended chain node carries a compiler-emitted canonical transition table,
derived solely from the closed callee FunctionType and caller boundary, that
maps the request's full operation/current descriptor pair to an already-
interned composite descriptor. This works for dynamic and separately compiled
callees without exposing internal suspension sites. Nested perform propagation
validates those descriptors without fabricating a runtime type. Appending a
new frame or installing a boundary remains a transactional allocation that can
fail before commit; resume-loop splice/relink of already-owned ordinary nodes
and detached try-boundary reinstall are infallible and allocation-free. A finite whole-CFG
normalization reserves one function per straight-line segment;
shared joins reuse one function and loop SCCs become finite mutually recursive
segments, so conversion never moves an arbitrary reachable suffix. The source
entry segment retains the original linked FunctionId. Every segment installed
as a continuation frame has exactly one dynamic result parameter and captures
all threaded edge state/live-ins in its closure environment. A
possibly-performing call carries its caller-suffix frame. Success invokes that
frame, while Performed appends it to the request chain before propagation.
Any non-Shareable Borrow live across suspension must have a dominating Owned
provenance value plus a deterministic projection recipe; the frame owns that
provenance and reprojects the borrow after resume. A borrow with no such owner
is a ranged suspension-linearity error. Handler-body and protected-try state
loans use the same specialized boundary-pinned recipe: the raw frame stores no
owner, and its invoking carrier must destroy the frame before consuming state
or atomically move that state into the corresponding boundary on Performed.
The frame reads the state only through its distinct hidden boundary-context
parameter. A pinned Success borrow is rooted at that Borrow parameter for the
duration of the invocation; boundary identity and state-descriptor equivalence
remain checked opcode facts rather than a fictitious SSA owner. On immediate
entry, a stack Direct context contains the current boundary/state loan and
points to the forwarded parent, so the handled/protected body sees its current
region even before a boundary is installed. On resume, the operation carries
the caller's exact hidden context into the runtime; its Chain view searches
owned continuation nodes first and then that borrowed parent. Contexts
are never stored in callable environments, frames, descriptors, or values.

`BoundaryContextAnalysis` solves two whole-module fixed points after every
generated function and edge is known. `Required(F)` is the least fixed point
of boundary-pinned captures plus undisclosed callee requirements.
`Guaranteed(F)` is the greatest sound fixed point formed by intersecting all
possible entry guarantees; source/import/export/unknown/worker roots and
functions with no proved incoming edge start empty, so a recursive SCC cannot
manufacture its own guarantee. Every reachable function must satisfy
`Required(F) ⊆ Guaranteed(F)`. For each success frame, the missing set
relative to its parent must have cardinality zero or one: zero uses the
ordinary invocation, one records the exact local Direct overlay with a
dominating scoped unique state loan, and more than one is a ranged preparation
error. Later passes consume the recorded decision and never infer an overlay
from LLVM or CFG shape.

Effectful handler guards obey the same
rule and never retain a borrowed request payload across suspension.
Resuming applies the chain in order and splices remaining frames into a nested
performed request. Module-local operation IDs never cross
the runtime or interface boundary.
Because the language currently has no effect-declaration syntax, semantic
analysis unifies every resolved perform/handler occurrence into one canonical
qualified operation declaration and rejects disagreements; imported
signatures come from v2 interfaces. A binder-aware declaration fingerprint is
used by semantics/interfaces/specialization. After substitution, a separate
closed runtime instance and fingerprint are produced for descriptor emission
and handler dispatch; v2 never stores that runtime fingerprint. Fingerprints
are only dispatch prefilters—handlers confirm complete same-domain canonical
bytes before selecting a clause.
Semantic effect rows contain qualified operation applications with ordered
type/effect arguments, not bare keys. Operation instantiation converts each
closed row into an explicit runtime row holding the exact ordered closed
operation instances used by callable and operation descriptors, including
imported/native signatures; recursive rows are published as one verified SCC.

Before preparation, every `HandleRegion` persists solver-owned
`BodyResultType`, whole `ResultType`, `BodyEffects`, `HandledEffects`, and
`ResidualEffects`; every `TryRegion` persists `ProtectedResultType`, whole
`ResultType`, `ProtectedEffects`, and `ResidualEffects`. Preparation and its
verifiers consume and cross-check these facts and never reconstruct masks,
rows, or router signatures from CFG reachability or operation spelling.

Handler clauses are closure-converted normally and capture lexical values in
an explicit environment. Yona uses deep, one-shot handlers. For a handled body
`A`, handle result `B`, and operation result `R`, the raw chain is `R -> A`,
while the source resume callable is a linear `R -> B`. One non-Shareable
owned router state contains the exact union of lexical captures needed by the
handled body, return clause, prepared dispatch, every handler clause, and
outstanding cleanup state. Those are fields of one owner, not separate
environment owners. The outside suffix remains a separate continuation-chain remainder and
is never owned by `RouteMove`. The initial handled-body call holds the state
owner while the body uses a verifier-scoped unique loan; ordinary generated CFG
routes its complete outcome because no detached boundary token exists yet.
Initial Performed transfers the request and state to prepared dispatch.
Installed boundary nodes alone invoke the generated OutcomeRouter. Unhandled forwarding installs a fresh HandlerBoundary; a
selected clause installs one only if its first owning resume use lazily
materializes the source-resume callable. A clause that never uses resume
allocates no boundary and drains/releases the raw chain and state normally.
The transactional source-resume factory validates the explicit operation and
transition row, allocates both boundary and callable, then commits the raw
chain/state moves together; there is no separately committing append-then-create
sequence.

The boundary router sends Success to the return clause, Performed to prepared
dispatch, and forwards Raised/Cancelled after region cleanup. An unhandled
dispatch reinstalls a fresh boundary around its outgoing request. Complete
outcomes produced lexically by a selected handler clause or return clause
bypass the consumed boundary, so a same-operation perform there is visible to
an outer handler. A prepared dispatch function tries
same-operation clauses in source order using the shared decision compiler,
and keeps the owned request intact while patterns and guards borrow from it.
An unmatched request keeps the same owned operation and staged arguments, but
dispatch transactionally installs a fresh deep boundary before forwarding it
outward. Only a selected leaf
transactionally takes the request-owned staged argument carriers and raw
continuation. Semantic arguments are passed with their declared
Trivial/Borrow/Consume contracts; a Borrow remains backed by the request-owned
Shareable clone/pin. Only the raw continuation and one router-state owner are
the two hidden Consume parameters. The predeclared `MakeHandlerResume` is
emitted only at the first owning use of the lexical resume binding; its one
transaction atomically installs the boundary and moves the raw chain/state
into the linear source-resume callable. A clause that never uses resume
constructs no callable and releases or consumes those hidden owners normally.
The clause adapter otherwise follows the operation's exact ownership contracts;
linear Consume arguments never require cloning.

Perform-capable `try` regions use the same boundary machinery with a distinct
TryBoundary router and owned state. Initial routing is ordinary CFG; initial
and resumed Raised outcomes enter the persisted catch dispatch and Success
enters the lexical success suffix. Initial Performed allocates/installs the
checked boundary, while resumed Performed moves state back into and relinks the
same detached node without allocation. Cancelled cleans up and propagates.
Catch/success bodies bypass the consumed boundary, so the same try cannot
catch its own catch-body raise. Operation-free try remains ordinary four-way
control routing and needs no runtime boundary.

Continuation resume is an explicit `(outcome, remaining chain)` loop. Ordinary
frames consume Success, Raised/Cancelled drop them, and Performed splices them
into the nested request through their transition tables. A boundary is
detached before its router is called: runtime moves out the state and supplies
the generated router with both that owner and a unique empty node token. Every
path consumes the token exactly once; try Performed reinstalls it, while other
try paths and handler routing release it. The router's returned
Success/Raised/Performed/Cancelled is then reprocessed against the saved outer
remainder. After detaching the current node, runtime passes the router a
separate ephemeral Chain view over that saved owned outer remainder followed
by the resume call's borrowed ambient parent. The router
forwards it through nested direct/universal calls and may use it as the parent
of an immediate Direct overlay, but may not retain or store it. The parent is
lookup-only: router results re-enter only the saved owned continuation chain,
while the caller outside resume still owns ambient routing. Thus a handler-produced Raised reaches an outer try, and a
router-produced Performed can acquire outer frames/boundaries without revisiting
the consumed inner boundary. Ordinary splices and try-node reinstall are
infallible allocation-free relinks; checked handler dispatch/resume may still
allocate a semantically fresh boundary before committing.

The resume ABI has a strict transactional boundary. False is reserved for
complete preflight rejection and leaves the continuation, Consume argument,
ambient context, and Empty outcome unchanged. Once either Consume slot clears,
the call is committed and returns true. If a later invariant produces a
reserved diagnostic, IR follows `ConsumedFailure` with both committed owner
poststates; it may not masquerade as false or rejoin the unchanged-input edge.
Preflight proves graph disjointness between the owned continuation and every
reachable borrowed boundary-context node, not only top-level slot
nonoverlap.

Regressions nest a synchronous inner handler resume inside an outer initial
try/handler state loan and require the resumed suffix to reborrow the outer
linear field exactly once through the ambient Direct parent, including
Performed and abandonment cleanup.

Handler grouping, effect masks, and runtime dispatch all use the complete
closed operation application, including phantom type/effect positions.
Preparation extracts the handled body, return clause, clause bodies, entry,
dispatch, and resume wrappers innermost-first before closure conversion and
persists every group entry, request-take result, and unhandled target;
finalization only wires descriptor checks to those stored dispatch-local
blocks and frozen closure layouts.

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

Candidate materialization is the final subpass of the same unpublished
GenericPrepared transaction, after generic/trait targets and key adapters are
fixed. Each candidate preserves the existing Yona `FrozenFallback`, original
operands and ownerships, resolved operation identity, exact result, semantic
effect row, and any static callable evidence. Effect preparation later fills
the explicit boundary-context operand, operation instantiation fills the
canonical closed runtime row, and every intervening pass preserves candidates
and deterministic ineligibility notes byte-for-byte through
`TailCallsLowered`; none may synthesize a helper.

Selection has only CPU and Accelerator dispositions and consumes one closed
eight-row mapping: Int map add/multiply/square, Int filter greater/less, Int
reduce sum, Float scale, and ordered Float reduce sum. Recognition uses typed
def-use graphs and exact Trivial captures, never names, AST spelling, or LLVM
shape. CPU calls the frozen Yona fallback. Accelerator lowering uses one of
the eight descriptor-backed v2 entries and converts its tri-state result:
Success publishes the exact result; Unavailable leaves inputs and output
unchanged and calls the frozen fallback; Error leaves inputs unchanged, owns a
reserved diagnostic, and cleans then traps. `CheckedAcceleratorOp` records all
three edges and their ownership poststates. Unknown status or inconsistent
storage is an invariant failure, and LLVM performs no capability or kernel
rediscovery.

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
- decision-tree tests for persistent rows/leaf targets, clauses, guards,
  exact String/Float literals, hash collisions, and every concrete projection;
- closure/effect conversion tests proving no free lexical values remain;
- ownership-conservation tests over every CFG edge;
- cleanup-region tests for all exit kinds;
- pass determinism and idempotence where applicable.
- alpha-renamed generic operation declarations, closed operation instances,
  and one shared local/imported specialization path.

Property-based generators cover structural types, carrier round trips,
ownership graphs, CFG dominance, pattern matrices, and nested cleanup regions.

### ABI matrix

Exercise Unit, Bool, Byte, Char, Int, Float, Symbol, String, every managed
aggregate, structural Sum alternatives, ADTs, closures, and resources across:

- direct calls;
- capturing and non-capturing closures;
- under- and over-application;
- async externs of zero, one, and multiple arguments;
- effect operations and resumptions;
- exception propagation;
- exported and cross-module calls.

Byte, Char, and Symbol have explicit scalar/export/cross-module rows; Symbol
also covers forced fingerprint collisions with unequal UTF-8 spellings. Array,
Channel, Promise, and resource cells are present only where their ownership
contract is legal, with illegal cells asserted as verifier rejections.
For every legal cell, exercise each permitted complete control outcome—Success,
Raised, Performed, and Cancelled—and assert that a forbidden outcome is
rejected by the structural row/descriptor verifier rather than silently
encoded.

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

- runtime dynamic exact/under-application, ABI over-application rejection, and
  compiler-explicit source over-application stages;
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

The checked-in stdlib manifest is executable architecture, not descriptive
prose. It freezes 45 interface artifacts, exactly 41 public API modules, nine
resource declarations, eight resource profiles, the closed native-leaf
routes, the sole generated 25-row RuntimeEntry registry, and the sole
generated 19-row Outcome-opcode table.
CancellationCheck and TaskSpawn are compiler-plan metadata, not runtime
leaves. A generated private bootstrap
sidecar may authenticate source/component details but is not serialized as a
public v2 field. The 38 runtime Outcome entry points map to exactly 19
source-visible Outcome operations; platform helper variants cannot become
extra language APIs.

Native File/Net/Process/Io reads are byte-oriented. Strict text decoding and
conversion errors live in Yona, and public `readLines` is a lazy Stream of
explicit Result values rather than a hidden native text iterator. The
compiler-backed documentation renderer consumes both public `types` and
`functions` from Complete v2 interfaces and produces exactly 41 module pages
plus the API README. The frozen module vector totals 484 public functions;
missing, extra, heuristic, or comment-derived exports fail generation.

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
8. all 45 v2 interfaces regenerate byte-identically, and the manifest's 41
   public modules, nine resources, eight profiles, 25 runtime-entry rows, 19
   Outcome opcodes, and 484 documented functions match exactly;
9. documentation describes only the new pipeline.

## Non-goals

- Preserve legacy internal APIs, object files, interface files, or runtime
  layouts.
- Keep silent fallback paths while migration is in progress.
- Re-enable arena allocation before reference-counted ownership is verified.
- Add unrelated language features that are not required to preserve current
  specified behavior.
