---
title: Prelude
description: Types and functions available in every Yona program without an import.
---

The Prelude is a Yona module that is automatically loaded for all programs. Its types and functions are available everywhere without an explicit import. This page is the complete reference for the core Prelude surface; utility functions over these types (such as `map` and `unwrapOr` for options) live in the standard library — see [Std\Option](/stdlib/option/) and [Std\Result](/stdlib/result/).

## Types

| Type | Constructors | Purpose |
|------|--------------|---------|
| `Linear a` | `Linear a` | Resource wrapper; must be consumed exactly once |
| `Option a` | `Some a`, `None` | Optional value |
| `Result a e` | `Ok a`, `Err e` | Success or failure |
| `Iterator a` | `Iterator (() -> Option a)` | Pull-based streaming iterator |

All constructors are first-class functions and can be used in pattern matching.

### Linear a

```yona
type Linear a = Linear a
```

Wraps a resource (file handle, socket, process handle) whose lifecycle is tracked by the linearity checker. A `Linear` value must be consumed **exactly once**, by pattern matching. Using it after consumption, consuming it in only one branch of a conditional, or letting it go out of scope unconsumed are compile-time errors (see [error codes](/reference/error-codes/) E0600–E0602).

```yona
let conn = Linear (tcpConnect host port) in
case conn of
    Linear fd -> do
        send fd "hello"
        close fd
    end
end
```

### Option a

```yona
type Option a = Some a | None
```

An optional value: `Some x` when a value is present, `None` when it is absent. Functions that may not produce a result return `Option` instead of a sentinel value.

```yona
let safeDiv = (\a b -> if b == 0 then None else Some (a / b)) in
case safeDiv 10 2 of
    Some v -> v
    None   -> 0
end  # => 5
```

### Result a e

```yona
type Result a e = Ok a | Err e
```

The outcome of a computation that can fail: `Ok value` on success, `Err error` on failure. The error type `e` is often a symbol or a string.

```yona
let toPort = (\n -> if n > 0 && n < 65536 then Ok n else Err "out of range") in
case toPort 8080 of
    Ok p  -> p
    Err _ -> 0
end  # => 8080
```

### Iterator a

```yona
type Iterator a = Iterator (() -> Option a)
```

A pull-based iterator: it wraps a function that returns `Some element` on each call and `None` once exhausted. Streaming producers in the standard library (`readLines`, `chars`, `split`) return `Iterator` so large inputs are processed in O(1) memory. Generators consume iterators as sources, which is the idiomatic way to feed one into a fold:

```yona
import readLines from Std\File, foldl from Std\List in
foldl (\acc _ -> acc + 1) 0 [line for line = readLines "data.txt"]
# => number of lines in the file
```

## Functions

| Function | Signature | Semantics |
|----------|-----------|-----------|
| `identity` | `a -> a` | Returns its argument unchanged |
| `const` | `a -> b -> a` | Ignores its second argument |
| `flip` | `(a -> b -> c) -> b -> a -> c` | Swaps a function's two arguments |
| `compose` | `(b -> c) -> (a -> b) -> a -> c` | Applies `g`, then `f` |

These four combinators are the complete prelude function surface. Collection
folds and transformations (`foldl`, `foldr`, `map`, `filter`, …) are **not**
prelude functions — import them from [Std\List](/stdlib/list/):

```yona
import foldl from Std\List in
foldl (\acc x -> acc + x) 0 [1, 2, 3, 4]  # => 10
```

`Std\List.foldl` is tail-recursive, which the compiler turns into a loop —
it never grows the stack. `foldr` recurses to the right and is not
tail-recursive; prefer `foldl` for aggregation over long sequences.

### identity

```yona
identity x = x
```

Returns its argument unchanged. Useful as a default transformation for higher-order functions.

```yona
identity 42       # => 42
identity "yona"   # => "yona"
```

### const

```yona
const x _ = x
```

Returns its first argument and ignores the second. Partially applied, `const x` is a function that returns `x` for any input.

```yona
const 1 99   # => 1

let always0 = const 0 in always0 5   # => 0
```

### flip

```yona
flip f a b = f b a
```

Reverses the argument order of a two-argument function.

```yona
flip (\a b -> a - b) 2 10  # => 8
```

### compose

```yona
compose f g x = f (g x)
```

Function composition: applies `g` first, then `f` to the result.

```yona
compose (\x -> x * 2) (\x -> x + 1) 5  # => 12
```

## Other always-available definitions

The Prelude also defines file-I/O and reflection support types — `FileHandle`,
`FileMode` (`Read | Write | ReadWrite | Append`), `Whence`
(`SeekSet | SeekCur | SeekEnd`), and `Type` (returned by `typeOf`).

Its foundational traits are `Eq`, `Ord`, `Hash`, `Show`, `Array`, `Closeable`,
`Sized`, `Iterable`, `Foldable`, `Semigroup`, `Monoid`, `From`, `TryFrom`,
`Parse`, `Send`, and `Shareable`. Operators use `Eq` and `Ord` statically;
collection traits lift over their element types; conversion traits select the
complete source/target instance; and the method-free concurrency markers are
proved at compile time and erased. See [Traits](/guides/traits/) for laws,
standard instances, conversion witnesses, and marker restrictions, plus
[Std\File](/stdlib/file/) and [Std\Types](/stdlib/types/) for the functions
that use the support types.
