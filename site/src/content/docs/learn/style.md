---
title: Style
description: Idiomatic Yona — flat lets, do for effects, with for resources, parallel comprehensions, folds, iterators, and naming conventions.
---

Idiomatic Yona is not just aesthetics: several of these rules change what
the compiler can do for you. Flat `let` bindings parallelize; nested ones
serialize. Each rule below shows the bad form, the good form, and why.

## Never nest `let`

`let` takes multiple comma-separated bindings; nesting buries that and
hurts readability.

```yona
# Bad — unnecessary nesting
let x = 42 in
let y = x + 1 in
x + y
```

```yona
# Good — flat multi-binding
let x = 42, y = x + 1 in x + y
# => 85
```

The payoff is bigger than style: **independent bindings in one `let` run in
parallel**. Nested `let`s force sequential execution even when the bindings
don't depend on each other.

```yona
# Bad — each read waits for the previous one
let a = readFile "foo.txt" in
let b = readFile "bar.txt" in
a ++ b
```

```yona
# Good — both reads in flight at once; elapsed ≈ max, not sum
let a = readFile "foo.txt",
    b = readFile "bar.txt"
in a ++ b
```

See [Concurrency](/learn/concurrency/) for the full model.

## `let` and `do` have different semantics

`let` binds values. Independent right-hand sides may run in parallel and
are awaited at first use. `do` sequences effects: every step runs strictly
top to bottom, even when the steps look independent. Combining them is
valid — and often the right shape — when you want both:

```yona
# Good — two reads in flight, then ordered writes
let
    a = readFile "foo.txt",
    b = readFile "bar.txt"
in do
    writeFile "out-a.txt" (process a)
    writeFile "out-b.txt" (process b)
end
```

Putting those reads in a `do` would serialize them. Putting those writes in
a multi-binding `let` would allow them to overlap. Use `let` when the
bindings are values (and may run together); use `do` when *order itself*
is the point. See [Concurrency](/learn/concurrency/).

The anti-pattern is using `let` *as* a sequencer for an unused effect:

```yona
# Bad — discard-binding to force an effect
let _ = writeFile "out.txt" data in data
```

```yona
# Good — do block for side effects; last expression is the value
do
    writeFile "out.txt" data
    data
end
```

`do` also takes intermediate bindings (`name = expr`), executed strictly in
order — the idiomatic shape for a protocol or any sequential I/O where the
steps depend on each other:

```yona
do
    content = readFile "input.txt"
    result = process content
    writeFile "output.txt" result
    result
end
```

Do not wrap a single expression in `do`. Do not pad a function or program
with a dummy last value such as `0` — the last real expression *is* the
result (`println` already yields `()`).

## Comma-separate imports

Nested `import` expressions add a level of indentation per module for no
benefit.

```yona
# Bad — one import wrapping another
import length from Std\String in
import println from Std\IO in
println (length "hello")
```

```yona
# Good — one import expression, comma-separated clauses
import length from Std\String, println from Std\IO in
println (length "hello")
# 5
```

## Use `with` for resources

Manual close calls are lost on every early exit and exception; `with`
releases the resource deterministically when the scope exits.

```yona
# Bad — close is skipped if send raises
do
    fd = tcpConnect "localhost" 8080
    send fd "hello"
    close fd
end
```

```yona
# Good — released on success or exception, checked by the Closeable trait
with fd = tcpConnect "localhost" 8080 in
    send fd "hello"
```

The resource type must implement `Closeable` — this is verified at compile
time, so a `with` over a non-resource is an error, not a surprise at
runtime.

## Parallel comprehensions for concurrent work

Mapping an I/O-bound or CPU-heavy function sequentially wastes the runtime's
thread pool; `[| … ]` runs one task per element and keeps result order.

```yona
# Bad — one fetch at a time
[ httpGet url for url = urls ]
```

```yona
# Good — all fetches concurrent, results in source order
[| httpGet url for url = urls ]
```

```yona
[| x * 2 for x = [1, 2, 3, 4, 5] ]
# => [2, 4, 6, 8, 10]
```

Keep the plain form `[ … ]` for cheap pure bodies, where task overhead
would exceed the work.

## `foldl` for aggregation

Hand-rolled non-tail recursion over a sequence grows the call stack;
`Std\List.foldl` is tail-recursive, which the compiler turns into a loop,
so it cannot overflow.

```yona
# Bad — deep recursion, stack depth proportional to length
let sum xs = case xs of
    []    -> 0
    [h|t] -> h + sum t
end in sum bigList
```

```yona
# Good — foldl, constant stack
import foldl from Std\List in
foldl (\acc x -> acc + x) 0 bigList
```

```yona
import foldl from Std\List in
foldl (\acc x -> acc + x) 0 [1, 2, 3, 4]
# => 10
```

`foldr` exists for the cases that genuinely need right association;
default to `foldl`.

## Iterators for streaming

Reading a whole file into a sequence costs O(file) memory; iterator-based
functions like `readLines`, `chars`, and `split` stream in O(1).

```yona
# Bad — reads the whole file into one string before counting
import readFile from Std\File, chars from Std\String, foldl from Std\List in
foldl (\n c -> if c == '\n' then n + 1 else n) 0 [c for c = chars (readFile "big.log")]
```

```yona
# Good — streams line by line, constant memory
import readLines from Std\File, foldl from Std\List in
foldl (\n _ -> n + 1) 0 [line for line = readLines "big.log"]
```

Iterator values feed comprehensions as generator sources; nothing is read
from the file until the generator pulls it.

## Naming conventions

Consistent casing carries information: you can tell a constructor from a
function from a symbol at a glance.

```yona
# Bad
process_item x = x          # snake_case function
module std\my_utils          # lowercase module
case status of :OK -> 1 end # uppercase symbol
```

```yona
# Good
processItem x = x            # camelCase functions and variables
module Std\MyUtils           # PascalCase modules, backslash-separated
case status of :ok -> 1 end  # :snake_case symbols
```

- **Functions, variables:** `camelCase` — `readFile`, `processItem`
- **Modules, types, constructors:** `PascalCase` — `Std\String`, `Option`, `Some`
- **Symbols:** `:snake_case` — `:ok`, `:not_found`
- **Type variables:** single lowercase letters — `a`, `b`, `e`

## Indentation

Two spaces per level, lines within 80–100 characters.

```yona
# Bad — four spaces and tab mixes drift into misalignment
case xs of
        []    -> 0
        [h|t] -> h
end
```

```yona
# Good — two spaces
case xs of
  []    -> 0
  [h|t] -> h
end
```

Newlines end expressions in case arms, `do` blocks, and module bodies, so
consistent shallow indentation keeps expression boundaries obvious. Inside
brackets, and after binary operators and `->`, newlines are suppressed — use
that for natural line continuation instead of escape characters.

## Use the prelude

`Some`, `None`, `Ok`, `Err`, `Linear`, `Iterator`, `identity`, `const`,
`flip`, and `compose` are always in scope; importing or re-defining them is
noise. (Collection functions like `foldl` are not prelude — import them
from [Std\List](/stdlib/list/).)

```yona
# Bad — shadowing a prelude type with a homemade one
type Maybe a = Just a | Nothing
case lookup k m of Just v -> v; Nothing -> 0 end
```

```yona
# Good — prelude Option, no import, no declaration
case lookup k m of
  Some v -> v
  None   -> 0
end
```

Prefer `Result a e` (`Ok`/`Err`) for fallible operations and `Option a`
(`Some`/`None`) for absence; both pattern-match everywhere without setup.

## Quick checklist

- Flat `let`, one binding list — independent bindings parallelize.
- `let` for values, `do` for ordered effects; combining them is fine.
  Never `let _ = effect`, never a one-line `do`, never a dummy trailing `0`.
- One `import`, comma-separated clauses.
- `with` for anything `Closeable`.
- `[| … ]` when the body is worth a task; `[ … ]` otherwise.
- `Std\List.foldl` over hand-rolled recursion for aggregation.
- Iterators for large inputs.
- `camelCase` / `PascalCase` / `:snake_case`; two-space indent.
- Reach for the prelude before writing it yourself.

For the semantics behind these rules, see [Concurrency](/learn/concurrency/),
[Modules](/learn/modules/), and the
[specification](/reference/specification/).
