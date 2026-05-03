# Memory Management in Yona-LLVM

## Overview

Yona is a compiled functional language with immutable data structures, structural
sharing, and transparent concurrency (thread pool + io_uring). Its memory system
uses **reference counting** with several optimizations:

- **Atomic RC** for thread safety
- **Recursive destructors** for all container types
- **Perceus-linear callee-owns ABI** for seqs, sets, and dicts —
  last-use args transferred without a DUP, callees consume on path-copy
- **Automatic borrow inference** — non-escaping heap params (not
  returned, not captured, not stored) skip rc_inc at the call site
  and rc_dec at function exit. Inferred borrow masks are emitted into
  `.yonai` metadata so imported functions keep the same ownership
  contract across module boundaries. Borrow inference is conservative:
  forwarded arguments only remain borrowed when the callee is known to
  borrow that parameter, functions that can directly raise keep owned
  unwind cleanup, and anonymous borrowed temporaries are released by the
  caller after the call. Eliminates refcount overhead for typical HOF
  patterns (foldl, map, filter) when params do not escape.
- **Optional explicit `@borrow`** — same refcount behavior as inference
  when the body satisfies the same non-escape rules; see
  [Explicit `@borrow`](#explicit-borrow) below
- **Per-branch transfer_scope** (if + case arms) — asymmetric
  transfers emit compensating rc_decs only on branches that didn't
  transfer, with SSA dominance preserved by snapshotting pre_blocks
  before branch-block creation
- **Unique-owner optimization** (in-place cons/tail for seqs, in-place
  HAMT node mutation for dicts/sets when rc==1)
- **Weak self-references** to break recursive closure cycles
- **io_uring buffer pinning** for async I/O safety
- **Scope-exit RC** for let-bound values
- **Slab-based pool allocator** for common allocation sizes
- **Arena allocation** for non-escaping let-bound values

### Explicit `@borrow` {#explicit-borrow}

You can write `@borrow` before a parameter (`let f @borrow x = …`, lambdas,
module functions). The typechecker (**E0603**) enforces the same rules as
borrow inference: the parameter must not escape (return, collection literal,
`::`, nested lambda capture, case scrutinee for head/tail consume, etc.), and
today only **simple identifier** parameters are supported.

**Why keep it if inference already skips refcount work?** For any body that
passes the `@borrow` check, inference would already mark that parameter borrowed,
so **codegen is usually unchanged** — explicit is redundant for performance.
The feature still makes sense because it:

1. **Documents intent** in the source (readers see a callee-reads-only contract).
2. **Fails at compile time** if a later edit makes the body escape the parameter
   (without `@borrow`, the same edit silently adds `rc_inc`/`rc_dec` again).
3. **Aligns with the roadmap** toward richer borrow / ownership annotations;
   the syntax and error path are the first slice.

**Benchmarks:** `bench/collections/list_sum.yona` vs
`list_sum_explicit_borrow.yona` are a **parity** pair (same output, expect
similar wall time); they detect regressions, not a speedup from the keyword.

**Roadmap:** type-level `&T` — see [design-borrow-types.md](./design-borrow-types.md).

## RC Header Layout

Every heap-allocated value has a 2-word header before the payload:

```
[refcount: i64] [type_tag_encoded: i64] [... payload ...]
                                         ^-- pointer returned to user
```

- `refcount`: starts at 1 from `rc_alloc`. Atomic increment/decrement.
- `type_tag_encoded`: lower 8 bits = type tag, upper bits = pool class
  index + 1 (0 = not pooled, 1-4 = pool class 0-3). Encoded via
  `ENCODE_TAG(tag, cls)`, decoded via `DECODE_TAG` / `DECODE_POOL_CLASS`.
- The user-visible pointer is `header + 2`, so `rc_inc`/`rc_dec` access
  the header at `ptr - 2`.

### Type Tags

| Tag | Name | Description |
|-----|------|-------------|
| 1 | `RC_TYPE_SEQ` | Flat sequence |
| 2 | `RC_TYPE_SET` | Set |
| 3 | `RC_TYPE_DICT` | Dictionary |
| 4 | `RC_TYPE_ADT` | Algebraic data type (recursive, heap-allocated) |
| 5 | `RC_TYPE_CLOSURE` | General closure (env-passing) |
| 6 | `RC_TYPE_STRING` | String |
| 7 | `RC_TYPE_BOX` | Boxed value (generic) |
| 8 | `RC_TYPE_BYTES` | Binary data |
| 9 | `RC_TYPE_TUPLE` | Tuple |
| 11 | `RC_TYPE_CHUNKED` | Legacy chunked sequence node (unused) |
| 12 | `RC_TYPE_RBT` | RBT sequence root (head chain + trie + tail buffer) |
| 13 | `RC_TYPE_RBT_NODE` | RBT internal trie node (32 child pointers) |
| 14 | `RC_TYPE_RBT_LEAF` | RBT trie leaf (heap_flag + 32 elements) |
| 15 | `RC_TYPE_RBT_CHUNK` | RBT head chain chunk (offset + count + 32 elems + next) |

## Container Layouts

### Flat Sequence (`RC_TYPE_SEQ`)
```
[count: i64] [heap_flag: i64] [elem0: i64] [elem1: i64] ...
```
- `SEQ_HDR_SIZE = 2` (count + heap_flag)
- Elements at `payload[SEQ_HDR_SIZE + index]`
- `heap_flag`: 1 if elements are heap-typed pointers (for recursive rc_dec)

### Chunked Sequence (`RC_TYPE_CHUNKED`)
```
[total_length: i64] [offset: i64] [count: i64] [elems: i64 * 32] [next: ptr]
```
- Linked list of 32-element chunks
- `offset`: first valid element index in `elems[]`
- `count`: number of valid elements from `offset`
- `next`: RC-managed pointer to the next chunk (or NULL)
- Structural sharing: `cons` creates a new head chunk pointing to the old

### Closure (`RC_TYPE_CLOSURE`)
```
[fn_ptr: i64] [ret_type: i64] [arity: i64] [num_captures: i64] [heap_mask: i64] [cap0: i64] ...
```
- `CLOSURE_HDR_SIZE = 5` (fn_ptr, ret_type, arity, num_captures, heap_mask)
- Captures at `payload[CLOSURE_HDR_SIZE + index]`
- `heap_mask`: bitmask — bit `i` set if capture `i` is heap-typed

### ADT (`RC_TYPE_ADT`, recursive only)
```
[tag: i64] [num_fields: i64] [heap_mask: i64] [field0: i64] [field1: i64] ...
```
- `ADT_HDR_SIZE = 3` (tag, num_fields, heap_mask)
- Fields at `payload[ADT_HDR_SIZE + index]`
- Non-recursive ADTs use LLVM struct values (no RC header)

### Tuple (`RC_TYPE_TUPLE`)
```
[num_elements: i64] [heap_mask: i64] [elem0: i64] [elem1: i64] ...
```
- Elements at `payload[2 + index]`
- `heap_mask`: bitmask of heap-typed elements

### Set (`RC_TYPE_SET`)
```
[count: i64] [heap_flag: i64] [elem0: i64] [elem1: i64] ...
```
- Elements at `payload[2 + index]`

### Dict — HAMT (`RC_TYPE_DICT`)
```
[datamap: i64] [nodemap: i64] [size: i64] [k0: i64] [v0: i64] ... [child0: ptr] ...
```
- Hash Array Mapped Trie: 32-way bitmap-compressed persistent hash map
- `datamap`: bitmap of slots with inline key-value entries
- `nodemap`: bitmap of slots with child sub-nodes
- `size`: total entries in this subtree
- Inline entries (popcount(datamap) pairs) followed by child pointers (popcount(nodemap))
- Hash function: splitmix64 for i64 keys
- O(1) amortized lookup/insert (max 7 levels)
- Persistent: insert creates new path-copy nodes, shares structure with old

## Atomic Reference Counting

All RC operations use C11 atomics for thread safety:

```c
// rc_inc: relaxed ordering (just a counter increment)
__atomic_fetch_add(&header[0], 1, __ATOMIC_RELAXED);

// rc_dec: acquire-release ordering (ensures writes visible before free)
int64_t old = __atomic_fetch_sub(&header[0], 1, __ATOMIC_ACQ_REL);
if (old <= 1) { /* free path */ }
```

### Arena Allocation

Non-escaping let-bound values are bump-allocated from a per-scope arena
instead of malloc. The escape analysis in `codegen_let` determines which
bindings don't escape (not returned, not captured by closures, not passed
to opaque functions). Non-escaping bindings use `yona_rt_arena_alloc`
which prepends an RC header with `refcount = INT64_MAX` (sentinel).
`rc_dec` checks for the sentinel and skips — arena values are freed
in bulk by `yona_rt_arena_destroy` at scope exit.

Benefits:
- Bump allocation is ~3x faster than malloc
- No per-object free (bulk deallocation)
- No RC overhead for arena values (sentinel skips rc_dec)

### Task-group arenas (structured concurrency)

Multi-binding `let` blocks (implicit **task group**, see `docs/structured-concurrency.md`)
always allocate a bump arena on the **parent thread**, attach it to the
`yona_task_group_t`, and route non-escaping bindings plus the let **body**
through `current_arena_` into that arena. On normal completion,
`yona_rt_group_end` destroys the arena wholesale after `cleanup_let_scope`
(so arena-backed seq/tuple/dict payloads are not individually `rc_dec`'d).

On **`raise`**, `yona_rt_raise` (`src/runtime/exceptions.c`) walks a small
TLS stack of active task groups (recorded at `group_arena_bind_push` time
with the current `yona_try_depth`) and calls `yona_rt_group_end` for each
scope being unwound past the target `catch` — so bump memory and the group
struct are reclaimed even when LLVM never reaches the normal `group_end`
call site.

Async work scheduled into the thread pool does **not** use this arena (v1);
only synchronous codegen in the enclosing function uses the bump pointer.

## Recursive Destructors

When `rc_dec` brings refcount to 0, the runtime recursively frees children
based on the type tag:

| Type | Children freed |
|------|----------------|
| Flat Seq | Each element if `heap_flag` set |
| Chunked Seq | `next` chunk pointer |
| Closure | Each capture per `heap_mask` |
| ADT | Each field per `heap_mask` |
| Tuple | Each element per `heap_mask` |
| Set | Each element if `heap_flag` set |
| Dict (HAMT) | All child sub-nodes (nodemap entries) |

The `heap_mask` is a 64-bit bitmask set by the codegen at allocation time.
It encodes which children are heap-typed (pointers that need `rc_dec`).

## Scope-Exit RC

Let-bound values are managed by the codegen at scope boundaries:

```
let x = create() in body
```

1. `create()` allocates with rc=1
2. Body is compiled
3. At scope exit: `rc_inc(result)`, then `rc_dec(x)`
4. If result IS x: net zero (rc_inc + rc_dec cancel)
5. If result is NOT x: x freed, result survives

This is implemented in `codegen_let` (`CodegenExpr.cpp`).

## Perceus DUP/DROP — unified (seqs + dicts/sets, 2026-04-15)

All persistent heap types — seqs, dicts, and sets — follow the
Perceus-linear callee-owns convention:

**At call sites** (`codegen_apply` → `emit_direct_call` / `codegen_extern_call`):
- For each heap-typed named arg: `rc_inc` (DUP) unless we can prove
  this is the last use of the binding, in which case we skip the DUP
  and mark the Value* as transferred. Single-use detection counts
  identifier references in the enclosing function body
  (`count_identifier_refs`) — count=1 means both the single and the
  last use.
- Temps (not in `named_values_`) keep their initial rc=1 and are
  marked transferred.
- Two disjoint transfer sets:
  - `transferred_seqs_` — SEQ Perceus; also participates in per-branch
    transfer-scope logic for compensating rc_decs across if/case arms.
  - `transferred_maps_` — SET/DICT callee-owns via extern ops (e.g.
    `Set.insert`, `Dict.put`). Kept separate so the per-branch
    transfer-scope logic doesn't drop map values as SEQs.

**At function exit** (`compile_function`):
- For each heap-typed param: `rc_dec` (DROP) unless the param is in
  `transferred_seqs_` / `transferred_maps_` (a consumer in the body
  already absorbed the ref) or a runtime-check `icmp eq` with the
  return value shows the param was returned to the caller.

**Pattern-match head-tail** (`codegen_pattern_headtail`):
- Single-use scrut → `yona_rt_seq_tail_consume`. In-place on rc==1,
  copy + rc_dec on rc>1.
- Multi-use scrut → `rc_inc` first, use plain `yona_rt_seq_tail`,
  drop both tail and the incremented scrut at arm exit.
- Empty-seq arm (`[] -> …`): explicit `rc_dec` of the scrutinee at
  the body_bb entry, AND insertion into `transferred_seqs_` so the
  case transfer_scope_exit doesn't emit a compensating second drop.

**Per-branch transfer-scope** (if-expressions + case arms):
- Before codegen'ing the first branch, snapshot `pre_blocks` (all
  currently-existing BBs in the enclosing function) and the current
  `transferred_seqs_`. Crucially, this snapshot happens *before* the
  branch BBs are created with `fn` — otherwise those BBs wrongly
  count as pre-existing, and values defined inside them pass the
  cross-branch droppability check, producing rc_dec instructions
  that don't dominate their operands.
- Each branch runs with a fresh copy of the entry snapshot; on exit
  we record what it transferred.
- At scope exit, for every value transferred by some (but not all)
  branches, emit a compensating `rc_dec` in the branches that didn't
  transfer it — as long as that value's SSA def is in `pre_blocks`
  (so the rc_dec dominates the def). Values created *inside* a
  branch are never dropped across branches.

**Runtime consume paths** (set/dict):
- `yona_rt_set_insert` / `yona_rt_dict_put`: after `hamt_put`, if the
  result pointer differs from the input we path-copied — rc_dec the
  old HAMT so the caller's one-ref-in is matched by one-ref-out.
- `set_ensure_hamt` consumes the flat set on flat→HAMT conversion.
- Without these, `let a = build N {} in let b = build N {} in …`
  would double-drop: each build level's `%s` could alias the same
  object after in-place mutations, and a deeper level's path-copy
  drop would free it while outer levels still held a stale ref.

**HAMT size-delta tracking**:
- `hamt_put_impl`'s child-recurse path propagates
  `new_child.size − old_child.size` to the parent, so same-key
  replaces through child nodes no longer inflate the parent's size.
  Fixed a `Set.union` wrong-result bug where re-inserting duplicates
  via child sub-nodes over-counted.

### Why this works now but didn't before

Earlier Perceus attempts on seqs hit "glibc tcache corruption" on
long seq chains because every allocation went through the tcache
free-list. The current fix flows `seq_cons` / `seq_tail` ownership
through `rc_alloc` / `pool_free` with a fixed slab allocator (see
pool section) that avoids tcache entirely. Combined with the
`transferred_seqs_` tracking and the fixed `count_identifier_refs`
traversal (previously dropping arguments inside curried `ExprCall`
chains, which made single-use detection miss `safe col placed 1`),
full Perceus for seqs is now correct and 2–3× faster on foldl-style
list benchmarks.

For dicts/sets, the analogous issue was extern calls that could
return a pointer aliased with their input (in-place `hamt_put`). The
current split — `transferred_maps_` + runtime consume — keeps the
call-site semantics clean: caller passes one ref, callee returns one
ref, no double-counting.

### Results

- list_sum, list_reverse, list_map_filter: 2–3× faster, 3× less
  memory compared to pre-Perceus (see `docs/benchmark-results.md`).
- queens: 43 MB → 2.2 MB, 2× faster.
- dict_build / set_build: 0 DICT/SET leaks with `let` binding
  (`YONA_ALLOC_STATS=1` verified).

## Seq Unique-Owner Optimization

When a seq's refcount is 1 (unique owner), operations modify it in place:

**`seq_cons` (prepend):**
- rc==1 + offset>0: write element in place, bump offset. O(1), no allocation.
- rc>1 or offset==0: allocate new chunk, copy elements. O(chunk_size).

**`seq_tail`:**
- rc==1 + count>1: bump offset in place. O(1).
- rc>1: allocate new chunk, copy elements.
- count==1: return next chunk (rc_inc next).

Unique-owner checks use `__atomic_load_n(&hdr[0], __ATOMIC_ACQUIRE)`.

## Weak Self-References

Recursive closures (e.g., `let f x = ... f (x-1) ...`) capture themselves.
Without special handling, the self-capture creates an RC cycle (closure
references itself, rc never reaches 0).

Fix: when a closure captures itself for recursion, the `rc_inc` for the
self-capture is skipped. The `heap_mask` bit for the self-capture is NOT
set, so the recursive destructor doesn't `rc_dec` it either.

Detection: `def.free_vars[ci] == fn_name` in `codegen_function_def`.

## io_uring Buffer Pinning

Async I/O operations (file write, network send) pass user buffers to the
kernel via io_uring. The kernel holds references to these buffers until
the I/O completes. If the user-side RC frees the buffer before completion,
the kernel reads freed memory.

Fix: write/send operations copy the content to an RC-managed buffer at
submit time. The buffer is `rc_dec`'d in the completer (`yona_rt_io_await`)
after the kernel signals completion.

Read/recv buffers are allocated by the runtime (not user-provided), so
they don't need pinning — they're not reachable until `io_await` returns.

## Temp Arg Cleanup

Heap-typed temporaries created as function arguments (e.g., `f (n :: xs)`)
are freed after the call returns, IF the arg's type differs from the
function's return type. This prevents leaking temps that the callee
doesn't own.

The type check (`all_args[ai].type != cf.return_type`) is conservative:
if the types match, the callee might return the arg, so we skip the DROP.

Borrowed heap temporaries are handled separately. If borrow inference says
the callee only reads a heap parameter, the callee skips its normal owned
DROP; therefore an anonymous temporary passed to that borrowed parameter is
released by the caller immediately after the call. Named borrowed arguments
are not dropped at the call site because their enclosing let/function scope
still owns them.

## Pool Allocator

A slab-based pool allocator reduces malloc/free overhead for common
allocation sizes:

- 4 size classes: 32B, 64B, 128B, 296B
- Thread-local free lists (`__thread pool_freelist[4]`)
- Slab allocation: `pool_grow` allocates 256 blocks at once via `malloc`
- Freed blocks go to the free list (never back to glibc `free`)
- Pool class encoded in upper bits of type_tag word (`ENCODE_TAG`/`DECODE_TAG`)
- Oversized allocations (>296B) fall through to `malloc`/`free`

**Critical constraint:** Pool blocks CANNOT be `realloc`'d because they
come from slabs (contiguous memory), not individual `malloc` calls.
The seq_cons flat realloc path checks `DECODE_POOL_CLASS(header[1])` and
skips realloc for pooled blocks, falling through to the copy path.

## Last-Use Analysis (Framework)

A backward AST walk (`LastUseAnalysis.h/cpp`) determines which reference
to each variable is the last one in a given scope. This enables:

- **DUP at non-last uses**: rc_inc to create extra references
- **No DUP at last use**: ownership transferred directly

The analysis handles:
- Binary expressions (right evaluated after left)
- If/case branching (conservative merge — both branches get DUP)
- Let bindings (body analyzed with bound names as owned set)
- Function applications (args analyzed, ExprCall chain recursed)
- Lambda captures (free variable uses tracked)

Currently used for the Perceus `perceus_tracked_` set but not fully
wired into codegen_identifier (only non-seq types get DUP/DROP).

## Concurrency Model

Yona uses transparent concurrency (async tasks on a thread pool, io_uring
for I/O). Tasks are short-lived thunks that share the caller's heap.

Memory safety is ensured by:
1. **Atomic RC** — all rc_inc/rc_dec are thread-safe
2. **Buffer pinning** — io_uring buffers survive until completion
3. **No per-task heaps** — unnecessary for short-lived tasks with shared heap

Per-task heaps (Erlang-style) were considered but rejected: Yona has no
processes or message passing, so deep-copy-on-spawn would add O(n) overhead
for every async call without significant benefit.

## Summary of RC Lifecycle

```
Value created:    rc_alloc → pool_alloc(total) → rc=1, type_tag encoded
Let binding:      no rc change (rc=1 from alloc)
DUP at call:      rc_inc at call site for named heap args EXCEPT on
                  single-use (SEQ/SET/DICT) or inferred borrowed params.
                  Single-use args skip the inc and mark the Value* as
                  transferred; borrowed args remain owned by the caller.
Function param:   callee-owns for all heap types (seqs, sets, dicts,
                  closures, strings, ADTs, tuples, ...) unless the
                  parameter has an inferred borrow contract.
Callee DROP:      rc_dec at function exit if param != return AND the
                  param was not transferred to a consumer in the body;
                  borrowed params skip this DROP and are excluded from
                  unwind frames.
Borrowed temp:    anonymous heap temporary passed to a borrowed parameter
                  is rc_dec'd by the caller after the call.
Scope exit:       rc_inc(result), rc_dec(binding) unless transferred
Closure capture:  rc_inc (except self-capture)
Seq cons/tail:    rc_inc for structural sharing (next chunk);
                  consume path rc_dec's old on path-copy
HAMT put:         in-place mutation when rc==1; path-copy otherwise,
                  with runtime rc_dec on the old input
Container free:   recursive rc_dec of children per heap_mask/heap_flag
Block freed:      pool_free (slab reuse) or free (oversized)
Arena values:     sentinel refcount, bulk free via arena_destroy
Non-escaping:     arena bump-alloc, no per-object RC, bulk free at scope exit
```

## Files

| File | Role |
|------|------|
| `src/compiled_runtime.c` | RC infrastructure, pool allocator, arena; `set_insert` / `dict_put` consume paths |
| `src/runtime/seq.c` | Persistent seq with chunked list, consume variants |
| `src/runtime/hamt.c` | HAMT put/get/destroy, size-delta tracking for same-key replace |
| `src/codegen/CodegenUtils.cpp` | `emit_rc_inc`, `emit_rc_dec`, `is_heap_type` |
| `src/codegen/CodegenExpr.cpp` | Scope-exit RC in `codegen_let`, transfer_scope helpers, Perceus analysis |
| `src/codegen/CodegenFunction.cpp` | DUP at call sites (single-use detection for SEQ/SET/DICT), DROP at function exit, `transferred_maps_` for extern map ops |
| `src/codegen/CodegenCase.cpp` | Per-branch transfer_scope for case arms, head-tail consume, empty-arm scrut drop |
| `src/codegen/CodegenCollections.cpp` | heap_mask for tuples/seqs |
| `src/codegen/LastUseAnalysis.cpp` | Backward AST walk for last-use detection |
| `include/LastUseAnalysis.h` | Last-use analysis API |
| `include/Codegen.h` | `CompiledFunction.closure_env`, `transferred_seqs_`, `transferred_maps_`, `TransferScope` helpers |
| `src/runtime/platform/file_linux.c` | io_uring buffer pinning (write) |
| `src/runtime/platform/net_linux.c` | io_uring buffer pinning (send) |
