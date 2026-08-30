---
title: Functions
description:
  Defining and applying functions in Yona — clauses, guards, lambdas, currying,
  closures, and pipes.
---

Functions are Yona's basic building block. They are first-class values: you can
pass them, return them, store them in data structures, and apply them partially.

## Definitions

A function is a name, space-separated parameter patterns, `=`, and a body. This
is the form the standard library uses (`map fn seq = …`):

```yona
add x y = x + y

add 1 2   # => 3
```

There is no `name(x, y) -> body` definition syntax. Parentheses around
parameters are a _pattern_: `add (x, y) = x + y` is a one-argument function that
matches a tuple, not a two-argument function.

Parameters are patterns, so a definition can have several clauses. Clauses are
tried top to bottom; the first whose patterns match is used. Recursion is often
clearer as a `case` in one clause — the same shape as `Std\List`:

```yona
factorial n = case n of
    0 -> 1
    _ -> n * factorial (n - 1)
end

factorial 5   # => 120
```

### Guards

An optional `if` guard after the parameters restricts when a clause applies; if
the guard is `false`, matching falls through to the next clause:

```yona
abs x if x >= 0 = x
abs x if x < 0  = -x

abs (-3)   # => 3
```

Put more specific clauses first; matching is strictly top-to-bottom.

### Type annotations

Annotations are optional — the compiler infers every type (see
[Types and data](/learn/types/)). When you want one, write a Haskell-style
signature on the line before the definition:

```yona
scale : Float -> Float -> Float
scale factor x = factor * x

greet : String -> String
greet name = "Hello " ++ name

greet "Yona"   # => "Hello Yona"
```

Arrows in the signature are curried: `Float -> Float -> Float` is a function of
one `Float` returning a function of one `Float`.

## Lambdas and thunks

Anonymous functions use a backslash:

```yona
\x -> x * 2
\(x, y) -> x + y      # tuple-pattern parameter
```

A **thunk** is a zero-parameter lambda, written with no parameters at all:

```yona
\-> expensiveComputation
```

## Zero-arity functions auto-evaluate

Because evaluation is strict, referencing a zero-arity function by name _calls_
it. To pass a zero-arity function as a value without calling it, wrap it in a
thunk:

```yona
let getTime = \-> System.nanoTime in
let t = getTime in          # calls it — t is a number
let deferred = \-> getTime in
runLater deferred            # passes the function, does not call it
```

## Application

### Juxtaposition

The primary application syntax is juxtaposition — the function followed by
space-separated arguments, as in Haskell or ML:

```yona
add 1 2                       # => 3
map (\x -> x * 2) [1, 2, 3]   # => [2, 4, 6]
```

Application binds tighter than every binary operator, so `f x + g y` is
`(f x) + (g y)`. Parenthesize an argument when it is itself an application or
contains operators: `f (g x)`, `add (1 + 2) 3`.

`f(x)` is the same as `f x`. `f(x, y)` is **not** a two-argument call — it
applies `f` to the tuple `(x, y)`. For `add x y = x + y`, `add 1 2` is `3` and
`add(1, 2)` is a leftover function.

### Partial application and currying

Applying a function to fewer arguments than it takes returns a function of the
remaining arguments:

```yona
let add5 = add 5 in
add5 10   # => 15
```

Functions that return functions chain naturally:

```yona
let adder n = \x -> x + n in
adder 10 32   # => 42

let f a = \b -> \c -> a + b + c in
f 1 2 3   # => 6
```

## Closures

A function captures the free variables of its enclosing scope by value at the
point of definition. The captured environment travels with the function,
including through higher-order calls:

```yona
let n = 10,
    addN = \x -> x + n,          # addN captures n
    apply = \f x -> f x in
apply addN 5   # => 15
```

Implementation note. Closures compile to a heap record holding the function
pointer and the captured values; recursive closures use a weak self-reference so
a closure that mentions itself does not leak.

## Pipes

`|>` feeds a value into a function left to right; `<|` is the same, right to
left. Pipes have the lowest precedence, so the whole expression on each side is
evaluated first:

```yona
import map, filter, sum from Std\List in
[1, 2, 3, 4, 5]
  |> filter (\x -> x % 2 == 1)
  |> map (\x -> x * x)
  |> sum          # => 35

sum <| map (\x -> x * x) <| [1, 2, 3]   # => 14
```

Use `|>` for data-transformation pipelines — the value flows visibly through
each stage.

## Higher-order functions

Functions take and return functions freely. The stdlib and prelude are built on
this: `map`, `filter`, `fold` in [Std\List](/stdlib/list/), and prelude
combinators that need no import:

```yona
identity 42            # => 42
const 1 "ignored"      # => 1
flip (\a b -> a - b) 1 10   # => 9
compose (\x -> x + 1) (\x -> x * 2) 5   # => 11  (applies g, then f)
```

```yona
import foldl from Std\List in
foldl (\acc x -> acc + x) 0 [1, 2, 3, 4]   # => 10
```

`Std\List.foldl` is the idiomatic aggregation loop — it is tail-recursive and
never overflows the stack, unlike a hand-written right recursion over a long
sequence.

Writing your own higher-order function is nothing special:

```yona
twice f x = f (f x)

twice (\x -> x * 3) 2   # => 18
```

## Recursion

There is no loop syntax; iteration is recursion (or a
[generator](/learn/collections/) / stdlib function that encapsulates it).
Multiple clauses plus guards make recursive definitions read like their
mathematical specification:

```yona
fib n = case n of
    0 -> 0
    1 -> 1
    _ -> fib (n - 1) + fib (n - 2)
end

fib 10   # => 55
```

For sequence recursion, pattern-match on head and tail — see
[Pattern matching](/learn/pattern-matching/):

```yona
sum xs = case xs of
    []    -> 0
    [h|t] -> h + sum t
end

sum [1, 2, 3, 4, 5]   # => 15
```

## Where to next

- [Pattern matching](/learn/pattern-matching/) — the pattern forms usable in
  parameters and `case`.
- [Types and data](/learn/types/) — how the checker infers function types.
- [Collections](/learn/collections/) — the functions in `Std\List` and friends.
