---
title: Collections
description: Persistent sequences, dictionaries, and sets — structural sharing, operators, generators, and the core stdlib functions.
---

Yona's built-in collections — sequences, dictionaries, and sets — are
**persistent**: every operation returns a new value and never modifies the
original. Versions share structure, so "copying" is cheap, and any value can
be handed to another thread without defensive copies or locks.

## Literals

```yona
[1, 2, 3]                    # sequence
["a", "b", "c"]
(1, "hello", true)           # tuple (fixed arity, not a collection)
{name: "Alice", age: 30}     # dictionary — key: value
{1, 2, 3}                    # set
{}                           # empty — usable as an empty dict or set
[1..10]                      # integer range sequence
```

Empty braces `{}` denote the empty hash trie, which serves as both the
empty dictionary and the empty set — the first insertion determines which
you have.

## Structural sharing

An "update" allocates only the path from the root to the change; everything
else is shared with the original:

```yona
import put from Std\Dict in
let original = {1: "one", 2: "two", 3: "three"} in
let updated = put original 4 "four" in
# original still has 3 entries; updated has 4.
# They share the subtrees holding keys 1–3.
```

Because values never change in place, equality of versions is structural
and old versions remain valid — undo stacks and snapshots are free.

## Sequences

Sequence literals are written `[1, 2, 3]`. Prepending, head, and tail are
O(1); indexing is O(1) for small sequences and O(log₃₂ n) for large ones.

Implementation note. Sequences use a hybrid representation: up to 32
elements live in a flat array with an offset-based O(1) tail; larger
sequences become a radix-balanced trie with a head chain that absorbs
prepends. When a sequence's reference count is 1, cons and tail mutate in
place, making recursive list processing nearly allocation-free.

### Sequence operators

```yona
0 :: [1, 2, 3]       # => [0, 1, 2, 3] — cons (prepend), O(1)
[1, 2] ++ [3, 4]     # => [1, 2, 3, 4] — concatenation, O(n)
```

`::` is right-associative, so `1 :: 2 :: [3]` is `[1, 2, 3]`.

### Append, remove, membership <span class="yona-status yona-status--partial">Partial</span>

The grammar also defines `seq :> elem` (append at the end), `a -- b`
(remove elements of `b` from `a`), and `x in coll` (membership test), but
compiler support for these three is currently limited. Use `xs ++ [x]` to
append, `Std\List::filter` to remove, and `Std\List::contains` /
`Std\Dict::contains` / `Std\Set::contains` for membership.

## Dictionaries

Dictionary literals pair keys and values with `:`. Lookup, insert, and
membership are O(1) amortized.

```yona
import put, get, contains, size, keys from Std\Dict in
let d = {10: 100, 20: 200} in
let d2 = put d 30 300 in
get d2 30 0        # => 300     (third argument is the default)
get d2 99 0        # => 0
contains d2 20     # => true
size d2            # => 3
keys d2            # => [10, 20, 30]  (order not specified)
```

`get` never throws — it takes a default to return for missing keys.
For streaming access, `entries`, `keysIter`, and `values` return
`Iterator`s that walk the trie with O(1) memory per element; `forEach`
applies a two-argument callback to every entry. Full API:
[Std\Dict](/stdlib/dict/).

Implementation note. Dictionaries are Hash Array Mapped Tries (HAMT) with
splitmix64 hashing — at most 7 levels deep. Inserts into a node with
reference count 1 mutate in place, so building a large dict in a loop
allocates only for trie growth, not per insert.

## Sets

Set literals list elements in braces. Sets share the HAMT machinery with
dictionaries, with the same complexities.

```yona
import insert, contains, union, intersection, difference, elements from Std\Set in
let a = {1, 2, 3, 4, 5},
    b = {3, 4, 5, 6, 7} in
contains a 3                       # => true
Std\Set::size (union a b)          # => 7
Std\Set::size (intersection a b)   # => 3
elements (difference a b)          # => [1, 2]  (order not specified)
insert a 6                         # => {1, 2, 3, 4, 5, 6} — a unchanged
```

Inserting an element that is already present is a no-op. Full API:
[Std\Set](/stdlib/set/).

## Generators (comprehensions)

A generator builds a collection from a source sequence or iterator. The
general form is `[expr for pattern = source]`, with an optional guard
introduced by `, if`:

```yona
[x * 2 for x = [1, 2, 3]]                  # => [2, 4, 6]
[x for x = [1, 2, 3, 4, 5, 6], if x > 3]   # => [4, 5, 6]
```

Set and dictionary generators use braces; the dict form gives `key : value`:

```yona
{x * 2 for x = [1, 2, 3]}        # => {2, 4, 6}
{x : x * 10 for x = [1, 2, 3]}   # => {1: 10, 2: 20, 3: 30}
```

Guards work in all three:

```yona
{x : x * x for x = [1, 2, 3, 4], if x % 2 == 0}   # => {2: 4, 4: 16}
```

Implementation note. Generators compile to counted loops, not chains of
closures. A guarded generator uses two passes — count matches, then fill —
so the result is allocated exactly once.

### Parallel generators

`[| … ]` evaluates the body for each element **concurrently** on the
runtime's thread pool, collecting results in order. If any task fails, the
remaining tasks are cancelled:

```yona
[| x * 2 for x = [1, 2, 3, 4, 5] ]     # => [2, 4, 6, 8, 10]
[| httpGet url for url = urls ]        # all requests in flight at once
```

Use it for I/O-bound or CPU-heavy per-element work; for trivial bodies the
sequential form is faster. See the
[concurrency guide](/learn/concurrency/).

## Stream fusion

When a generator is bound in a `let` and consumed exactly once by another
generator, the compiler **fuses** the two into a single loop — the
intermediate sequence is never materialized:

```yona
let nums = [1, 2, 3, 4, 5] in
let doubled = [x * 2 for x = nums] in     # fused into the next line
[x for x = doubled, if x > 4]             # => [6, 8, 10] — one loop, no temp list
```

Write map/filter pipelines naturally; the staging costs nothing as long as
each intermediate binding is used exactly once. A binding referenced more
than once is materialized as a real sequence.

## Core Std\List functions

`Std\List` operates on sequences. The essentials:

```yona
import map, filter, foldl, foldr, length, reverse, take, drop,
       zip, zipWith, sum, product, sortBy, partition, find, flatten
from Std\List in

map (\x -> x * 2) [1, 2, 3]              # => [2, 4, 6]
filter (\x -> x > 2) [1, 2, 3, 4]        # => [3, 4]
foldl (\acc x -> acc + x) 0 [1, 2, 3]    # => 6
foldr (\x acc -> x :: acc) [] [1, 2, 3]  # => [1, 2, 3]
length [1, 2, 3]                          # => 3
reverse [1, 2, 3]                         # => [3, 2, 1]
take 2 [1, 2, 3, 4]                       # => [1, 2]
drop 2 [1, 2, 3, 4]                       # => [3, 4]
zip [1, 2, 3] [10, 20, 30]                # => [(1, 10), (2, 20), (3, 30)]
zipWith (\a b -> a + b) [1, 2] [10, 20]   # => [11, 22]
sum [1, 2, 3, 4, 5]                       # => 15
product [1, 2, 3, 4]                      # => 24
sortBy (\a b -> a - b) [3, 1, 4, 1, 5]    # => [1, 1, 3, 4, 5]
partition (\x -> x > 2) [1, 2, 3, 4]      # => ([3, 4], [1, 2])
find (\x -> x > 3) [1, 2, 5, 4]           # => (:some, 5)
flatten [[1, 2], [3], [4, 5]]             # => [1, 2, 3, 4, 5]
```

`foldl` is tail-recursive — use it for aggregation over long sequences.
`head` and `tail` crash on empty input; prefer pattern matching with a
`[]` case (see [Pattern matching](/learn/pattern-matching/)). The full
list — `any`, `all`, `flatMap`, `enumerate`, `intersperse`, `scanl`,
`groupBy`, and more — is in [Std\List](/stdlib/list/).

## Choosing a collection

- **Sequence** — ordered data, head-tail recursion, pipelines. O(1)
  cons/head/tail.
- **Dictionary** — keyed lookup. O(1) amortized get/put.
- **Set** — membership and set algebra. O(1) amortized insert/contains.
- **Tuple** — a fixed number of possibly differently-typed values; not
  iterable.

## Where to next

- [Persistent data structures guide](/guides/persistent-data-structures/) —
  representations, complexity tables, and benchmarks.
- [Std\List](/stdlib/list/), [Std\Dict](/stdlib/dict/),
  [Std\Set](/stdlib/set/) — complete APIs, and the rest of the
  [standard library](/stdlib/).
- [Pattern matching](/learn/pattern-matching/) — destructuring sequences
  and tuples.
