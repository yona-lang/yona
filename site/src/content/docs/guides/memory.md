---
title: Memory and linearity
description: Reference counting, ownership transfer, in-place optimization, and compile-time linear resource tracking.
---

Yona has no garbage collector and no GC pauses. Memory is managed by
**atomic reference counting** with a set of compile-time analyses — Perceus
ownership transfer, borrow inference, uniqueness detection, escape analysis
— that eliminate most counting in practice. On top of the memory story,
**linear types** track external resources (file handles, sockets, channel
endpoints) so that leaking or double-closing them is caught at compile
time. This page covers both layers and states the trade-offs plainly.

## Reference counting

Every heap-allocated value — sequences, dicts, sets, strings, tuples,
closures, recursive ADTs — carries a two-word header before its payload:

```
[refcount: i64] [type_tag: i64] [ ... payload ... ]
```

The refcount starts at 1 on allocation. Increments and decrements are C11
atomics (relaxed increment; acquire-release decrement), so values can be
shared freely across Yona's thread-pool tasks without extra
synchronization. When a decrement brings the count to zero, a **recursive
destructor** runs: guided by the type tag and a per-object bitmask of
heap-typed children, it decrements each child in turn — a dict frees its
subtrie nodes, a closure frees its captures, a tuple frees its heap
elements. Deallocation is deterministic and immediate; there is no
collector thread and no pause.

*Implementation note.* Common allocation sizes come from a slab-based pool
allocator with thread-local free lists rather than raw `malloc`, and the
pool class is encoded in the header's tag word so the destructor knows how
to return the block.

## Ownership transfer (Perceus, callee-owns)

Naive reference counting would bracket every call with an
increment/decrement pair. Yona instead uses a **callee-owns** calling
convention in the style of Perceus: the caller passes one reference, the
callee is responsible for it — either consuming it, returning it, or
dropping it at exit.

The key optimization is at the call site. If the compiler can prove an
argument is the **last use** of a binding, it skips the increment entirely
and transfers ownership:

```yona
import foldl from Std\List in
let sum xs = foldl (\a b -> a + b) 0 xs in
let data = [1, 2, 3, 4] in
sum data                             # => 10 — data moved, no RC traffic
```

`data` is used exactly once, so it is moved into `sum` without touching the
refcount; `sum`'s exit logic accounts for the reference instead. Recursive
list processing — fold, map, filter chains — runs with almost no counter
updates as a result. Where a binding is used more than once, only the
non-final uses pay an increment.

Branching is handled per-branch: if a value is transferred in one arm of an
`if` or `case` but not another, the compiler inserts a compensating
decrement only in the arms that did not transfer, keeping counts exact on
every path.

### Borrow inference and `@borrow`

Many functions only *read* a heap parameter — they do not return it, store
it, or capture it. The compiler infers this and drops the RC bracketing for
such parameters entirely; inferred borrow contracts are recorded in
`.yonai` interfaces so the optimization holds across module boundaries.

You can also state the contract explicitly:

```yona
let count @borrow xs = length xs in
count [1, 2, 3]                      # => 3
```

`@borrow` produces the same code as inference; its value is that the
contract is now *checked* — if a later edit makes the body return or
capture `xs`, the compiler rejects it with **E0603** instead of silently
reintroducing refcount traffic.

## Uniqueness: in-place updates behind a persistent interface

Before mutating-free structures are copied, the runtime checks the
refcount. If it is exactly 1, no one else can observe the value, so the
operation mutates **in place**:

- **Sequence `cons` and `tail`**: with a unique owner, prepend writes into
  reserved space and tail bumps an offset — both O(1) with no allocation.
  This is why an accumulator threaded through a recursive loop is nearly
  allocation-free.
- **Dict/set `put`/`insert` (HAMT)**: with a unique root, the trie node is
  edited directly instead of path-copied. Building a 10,000-entry dict
  costs a few hundred allocations (trie growth) instead of one per insert.

The optimization is invisible: values are semantically immutable, and the
fast path fires only when immutability cannot be observed. See
[Persistent data structures](/guides/persistent-data-structures/) for the
data-structure side of this story.

## Escape analysis and arenas

Values bound in a `let` that provably do not escape the scope — not
returned, not captured by a closure, not stored into an escaping structure
— are **bump-allocated** from a per-scope arena instead of the pool
allocator. Arena values carry a sentinel refcount that makes decrements
no-ops; the whole arena is freed in one step at scope exit. Bump allocation
is roughly 3× faster than malloc and needs no per-object free.

Multi-binding `let` blocks (which form task groups — see
[Concurrency in depth](/guides/concurrency/)) attach an arena to the group;
it is reclaimed on normal exit and also when an exception unwinds past the
scope.

## Weak self-references

A recursive closure captures itself, which would form a reference cycle
that counting alone could never free. The compiler detects self-capture and
makes it **weak**: the self-slot is not counted and not decremented by the
destructor. Recursive functions therefore cost nothing extra:

```yona
let fact n = if n <= 1 then 1 else n * fact (n - 1) in
fact 10                              # => 3628800 — no cycle, no leak
```

## Async safety: buffer pinning

Async writes hand buffers to the kernel (io_uring on Linux). The runtime
copies outgoing payloads into a pinned RC-managed buffer at submission and
releases it only after completion, so a value freed by ordinary RC can
never be read by an in-flight kernel operation.

## Trade-offs, honestly

- **Cycles.** Reference counting cannot reclaim cycles. The one cycle the
  language itself creates — recursive closures — is broken by weak
  self-references, and immutable data cannot otherwise form cycles by
  construction. But this is a property to know, not a solved-in-general
  problem.
- **Contention.** Atomic counters on values shared hot across many threads
  can bounce cache lines. The mitigations (transfer, borrowing, arenas)
  remove most counting, but a heavily shared structure updated from many
  tasks still pays for atomicity.
- **Throughput vs. latency.** A tracing GC can beat RC on raw allocation
  throughput; Yona trades that for deterministic reclamation, no pauses,
  and a small fixed memory baseline.

## Linear types

Memory is reference-counted, but *external resources* — file descriptors,
sockets, spawned processes, channel endpoints — need a different guarantee:
each must be released **exactly once**. Yona expresses this with the
prelude type `Linear a`, an ordinary ADT with special compile-time
tracking:

```yona
type Linear a = Linear a
```

Wrapping a value in `Linear` creates an obligation. The only way to reach
the payload is pattern matching, which is also the **consumption point**:

```yona
let conn = Linear (tcpConnect "localhost" 8080) in
case conn of
    Linear fd ->
        let reply = recv fd 1024 in  # borrowing use — no consume
        do; close fd; reply; end     # fd released exactly once
end
```

### Which stdlib values are linear

Resource constructors return `Linear`-wrapped handles, recorded in their
module interfaces: `openFile` (file handles), `tcpConnect` / `tcpListen` /
`tcpAccept` / `udpBind` (sockets), `Std\Process.spawn` (process handles),
and `Std\Channel.channel`, which returns a tuple of two linear endpoints —
`(Linear (Sender a), Linear (Receiver a))`.

### The rules

1. **Consume exactly once.** A linear binding must be pattern-matched
   exactly once on every execution path.
2. **Transfer, don't alias.** `let y = x` moves the obligation to `y`; `x`
   is dead afterwards.
3. **Branch consistency.** All arms of an `if`/`case` must consume the same
   set of linear values.
4. **No silent drop.** A linear value still live at scope exit is a leak
   and is reported.

### What the checker rejects

Use after consume is error **E0600**:

```yona
let conn = Linear (tcpConnect "host" 8080) in
let conn2 = conn in                  # conn consumed by transfer
send conn "hello"
# error[E0600]: linear value 'conn' was already consumed
```

Branch inconsistency is error **E0601**:

```yona
if ready then
    case conn of Linear fd -> close fd end   # consumed here
else
    0                                        # not consumed here
# error[E0601]: 'conn' consumed in then-branch but not else-branch
```

Dropping silently draws a leak warning:

```yona
let conn = Linear (tcpConnect "host" 8080) in
42
# warning: linear value 'conn' not consumed — possible resource leak
```

### `with`: the idiomatic consumer

For the common open-use-close pattern, `with` scopes the resource, closes
it automatically at exit (on success or exception, via the `Closeable`
trait), and discharges the linear obligation in one step:

```yona
with conn = tcpConnect "host" 8080 in
    recv conn 4096                   # conn closed automatically at exit
```

Prefer `with` whenever the resource's lifetime matches a lexical scope;
reach for explicit `Linear` pattern matching only when the handle must
cross scopes or travel through data structures.

### Scope and limitations

The linearity checker is flow-sensitive and compile-time only — codegen and
RC are unchanged by it. It runs on expression programs but is currently
skipped inside module top-level compilation, its diagnostics do not yet
fail the build, and closures interact with linear captures only in limited
ways (use `with` for resource-scoped work). It tracks resource lifecycle,
not memory: it is not a borrow checker, and Yona does not need one — RC
plus the uniqueness fast path already provide memory safety and in-place
performance.

## Further reading

- [Persistent data structures](/guides/persistent-data-structures/) —
  structural sharing and the rc==1 fast paths from the data-structure side
- [Concurrency in depth](/guides/concurrency/) — task groups, arenas, and
  linear channel endpoints in practice
- [The type system](/guides/type-system/) — where linearity fits among the
  other static checks
