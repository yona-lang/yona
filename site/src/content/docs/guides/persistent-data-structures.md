---
title: Persistent data structures
description: How Yona's immutable sequences, dicts, and sets work — structural sharing, complexities, and performance guidance.
---

All of Yona's built-in collections are **persistent**: operations return a
new value and never modify the original. Persistence is what makes Yona's
concurrency model safe (any task can read any value without locks), makes
equational reasoning valid (a value never changes under you), and gives
versioning and undo for free. This page explains what persistence means
operationally, how each structure is represented, what the operations cost,
and how to write fast code with them.

## What persistence means operationally

An "update" produces a new version; the old version remains valid and
unchanged. The two versions are not copies of each other — they **share
structure**, and only the changed path is newly allocated:

```yona
import put from Std\Dict in
let original = {1: "one", 2: "two", 3: "three"} in
let updated  = put original 4 "four" in
(original, updated)
# => original still has 3 entries; updated has 4.
#    They share the subtree holding keys 1, 2, 3.
```

For a 10,000-entry dict, `put` allocates O(log n) new nodes — a handful —
while everything else is shared. "Copy-on-write" semantics at a fraction of
the copying.

## Sequences

Sequences (`[1, 2, 3]`) are Yona's list type. The representation is hybrid:

- **Small sequences (≤ 32 elements)** are a flat array with an offset
  field, so removing the head is a pointer bump, not a copy.
- **Large sequences** are a 32-way **radix-balanced trie** with a head
  chain that absorbs prepends and a tail buffer that absorbs appends.

### Complexity

| Operation | Cost | Notes |
|-----------|------|-------|
| `cons` (prepend, `::`) | O(1) amortized | head chain absorbs prepends |
| `head` | O(1) | direct access |
| `tail` | O(1) amortized | offset bump / chain pull |
| index (`nth`) | O(log32 n) | trie descent; O(1) when small |
| `length` | O(1) | stored in the root |
| `++` (concat) | O(n) | flatten and rebuild |

```yona
let xs = [1, 2, 3, 4, 5] in
let ys = 0 :: xs in                  # => [0, 1, 2, 3, 4, 5] — O(1)
case xs of [h|t] -> h end            # => 1 — O(1), xs unchanged
```

Note the branching factor: log32 of a million is about 4, so indexed access
into large sequences is a handful of pointer hops, not a linked-list walk.

## Dictionaries and sets

Dicts (`{"a": 1}`) and sets (`{1, 2, 3}`) share one engine: a **hash array
mapped trie (HAMT)** — a 32-way bitmap-compressed persistent hash trie. A
set is a HAMT whose entries carry no payload.

| Operation | Cost |
|-----------|------|
| `put` / `insert` | O(1) amortized |
| `get` / `contains` | O(1) amortized |
| `size` | O(1) |
| `keys`, `entries`, iteration | O(n) |

"O(1) amortized" is precise here: a 64-bit hash consumed 5 bits per level
bounds the trie at 7 levels, so every lookup or insert touches at most 7
compact nodes. An insert path-copies those nodes and shares the rest of the
trie with the previous version.

```yona
import put, get, contains from Std\Dict in
let d = put (put {} "name" "Alice") "age" 30 in
(get d "name" "unknown", contains d "email")
# => ("Alice", false)
```

```yona
import union, intersection from Std\Set in
let a = {1, 2, 3, 4, 5},
    b = {3, 4, 5, 6, 7} in
(Std\Set.size (union a b), Std\Set.size (intersection a b))
# => (7, 3)
```

## Functional update idioms

There is no assignment; "updating" a collection means computing a new one
and threading it through the program. The standard idioms:

**Thread the new version through recursion:**

```yona
import put from Std\Dict in
let index n d =
    if n <= 0 then d
    else index (n - 1) (put d n (n * n)) in
Std\Dict.size (index 100 {})         # => 100
```

**Build with folds, not repeated concatenation:**

```yona
import foldl from Std\List in
let evens = foldl (\acc x -> if x % 2 == 0 then x :: acc else acc)
                  [] [1, 2, 3, 4, 5, 6] in
evens                                # => [6, 4, 2]
```

Each `::` is O(1); building a list of n elements by folding is O(n).
Building it with repeated `xs ++ [x]` is O(n²), because every `++` rebuilds
the left operand. If output order matters, cons and `reverse` once at the
end (O(n)) — still linear overall.

**Use collection combinators before manual recursion:** `map`, `filter`,
`foldl`, `take`, `zip` and friends from `Std\List` cover most shapes and
are written against the fast paths described below.

## In-place optimization: transparent uniqueness

Persistence sounds expensive — a new version per operation — but Yona's
runtime checks the reference count before copying. If a collection's
refcount is exactly 1, no other reference can observe it, so the operation
**mutates in place**:

- unique `cons`/`tail` on a sequence: O(1), zero allocation
- unique HAMT `put`: edits the node directly instead of path-copying

```yona
# Runs with O(1) allocation per step: each intermediate list
# is uniquely owned, so tail reuses storage instead of copying.
let sum acc xs = case xs of
    []    -> acc
    [h|t] -> sum (acc + h) t
end in
sum 0 [1, 2, 3, 4, 5]                # => 15
```

This is **transparent**: semantics are unchanged, and the fast path fires
exactly when immutability cannot be observed. A dict built in a tight loop
performs like a mutable hash table while remaining a persistent value the
moment you share it. The ownership analysis that keeps refcounts at 1
through call chains is described in [Memory and linearity](/guides/memory/).

*Implementation note.* Uniqueness is checked with one atomic load of the
refcount header. The compiler's callee-owns convention and last-use
analysis avoid spurious refcount increments precisely so that hot-loop
accumulators stay at rc==1 and hit these paths.

## Pattern matching over collections

Sequences destructure with head-tail and literal patterns; dicts match on
keys; both nest freely with ADT and tuple patterns:

```yona
let describe xs = case xs of
    []        -> "empty"
    [x]       -> "one"
    [h|t]     -> "head " ++ show h
end in
describe [10, 20, 30]                # => "head 10"
```

```yona
case {"status": 200, "body": "ok"} of
    {"status": 200} -> "success"
    _               -> "failure"
end                                   # => "success"
```

Matching never copies: `[h|t]` binds `h` by access and `t` by structural
sharing (or an in-place offset bump when unique). A dict key pattern is a
lookup, not a traversal.

## Performance guidance

**Choosing a structure:**

- **Sequence** — ordered data, front-heavy access, recursion over elements,
  building results. The default collection.
- **Dict** — keyed lookup. Use when you would reach for a hash map; don't
  simulate one with a sequence of pairs and linear search.
- **Set** — membership tests and set algebra (`union`, `intersection`,
  `difference`). A set beats `contains` on a sequence from a few dozen
  elements up.

**Building:**

- Fold with `::` (O(n)); never grow with `xs ++ [x]` in a loop (O(n²)).
- Build dicts/sets by threading through a fold — the uniqueness fast path
  makes it competitive with mutable tables.
- Let bindings keep intermediate collections uniquely owned; sharing a
  value across tasks or storing it in a long-lived structure ends the
  in-place regime for that value (correctly — from then on versions
  genuinely share).

**Traversal:** prefer `foldl` (loop-based, no stack growth) over hand-rolled
non-tail recursion for aggregation; use `Iterator`-returning functions for
O(1)-memory streaming over large inputs — see
[Iterators and streams](/guides/iterators/).

**Concat:** `++` is O(n); concatenating many pieces is best done once at
the end (`flatten`) rather than pairwise in a loop.

## Why this design

The alternative — mutable collections with defensive copying — pushes the
cost onto every boundary where data is shared: across tasks, into caches,
between versions. Persistent structures invert this: sharing is free and
*updating* pays a small logarithmic cost, which the uniqueness optimization
then erases in the common single-owner case. Combined with reference
counting (deterministic reclamation, no GC pauses), the result is
predictable performance with immutability as the default. Measured numbers
against C and other languages are in [Performance](/guides/performance/).
