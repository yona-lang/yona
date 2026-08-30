---
title: Pattern matching
description:
  case expressions and every pattern form — structural decomposition with
  first-match semantics and exhaustiveness warnings.
---

Pattern matching is how Yona code inspects and decomposes values. It appears in
`case` expressions, function parameters, `let` bindings, and `catch` clauses. A
pattern either **matches** a value — binding any variables it contains — or
fails, in which case matching moves on to the next candidate.

## Case expressions

```yona
case value of
    pattern1 -> result1
    pattern2 -> result2
    _ -> fallback
end
```

Arms are tried strictly **top to bottom**; the first pattern that matches (and
whose guard, if any, passes) selects the arm, and its body becomes the value of
the whole expression. Later arms are not evaluated. If no arm matches at
runtime, the program aborts with a match error — so end with a `_` arm unless
the patterns provably cover every case.

```yona
case n of
    0 -> "zero"
    1 -> "one"
    _ -> "many"
end
```

## Pattern forms

### Literals

Integers, floats, strings, characters, booleans, and symbols match by equality:

```yona
case status of
    :ok      -> "success"
    :error   -> "failure"
    :pending -> "waiting"
end
```

Implementation note. Symbols are interned to integers at compile time, so a
`case` over symbols compiles to an integer switch — dispatch is a single
comparison per arm, or a jump table.

### Variables and wildcard

A lowercase name matches anything and binds it in the arm's body. `_` matches
anything and binds nothing:

```yona
case point of
    (x, _) -> x    # binds x, ignores the second component
end
```

### Tuples

Tuple patterns match tuples of exactly that arity, position by position:

```yona
case (1, "hello", :ok) of
    (n, msg, :ok)   -> msg          # => "hello"
    (_, _, :error)  -> "failed"
end
```

### Sequences — exact length

`[]`, `[x]`, `[a, b]` match sequences of exactly zero, one, two … elements:

```yona
case xs of
    []      -> "empty"
    [x]     -> "one element"
    [a, b]  -> "exactly two"
    _       -> "three or more"
end
```

### Sequences — head and tail

`[h|t]` matches any non-empty sequence, binding the first element and the
remaining sequence. Multiple heads may precede the tail: `[a, b | rest]`
requires at least two elements. This is the primary way to recurse over
sequences:

```yona
sum xs = case xs of
    []    -> 0
    [h|t] -> h + sum t
end

sum [1, 2, 3, 4, 5]   # => 15

case list of
    [x, y | rest] -> "starts with {x} then {y}"
    _ -> "fewer than two"
end
```

Implementation note. Taking head and tail of a persistent sequence is O(1), so
head-tail recursion has no hidden copying cost — see
[Collections](/learn/collections/).

### Constructors (ADTs)

Constructor patterns match a specific variant of an
[algebraic data type](/learn/types/) and bind its fields positionally. Patterns
nest arbitrarily. Prelude constructors such as `Some`/`None` work in an
expression program; your own `type` declarations belong in a `module` (see
[Modules](/learn/modules/)):

```yona
let maybeValue = Some 42 in
case maybeValue of
    Some x -> x * 2
    None   -> 0
end
```

```yona
module Demo\Tree

export depth

type Tree a = Node (Tree a) a (Tree a) | Leaf

depth t = case t of
    Leaf -> 0
    Node l _ r -> 1 + (if depth l > depth r then depth l else depth r)
end
```

### Named fields (records)

ADTs with named fields match with `Constructor { field = pattern, … }`. You only
name the fields you care about:

```yona
module Demo\People

export greet, ageOf

type Person = Person { name : String, age : Int }

greet person = case person of
    Person { name = n, age = a } -> "{n} is {a}"
end

ageOf person = case person of
    Person { age = a } -> a       # other fields ignored
end
```

### Or-patterns

`|` between patterns matches if any alternative matches. Alternatives share one
arm body:

```yona
case x of
    1 | 2 | 3 -> "small"
    _ -> "big"
end
```

### Guards

A pattern may carry an `if` guard; the arm is taken only when the pattern
matches _and_ the guard (which may use the pattern's bindings) is true. A failed
guard falls through to the next arm:

```yona
case x of
    0 -> "zero"
    n if n > 0 -> "positive"
    _ -> "negative"
end
```

### Typed patterns

`(name : Type)` matches on the _runtime type_ of a value from an anonymous sum
type like `Int | String`, binding it at the annotated type:

```yona
describe : Int | String -> String
describe v = case v of
    (n : Int)    -> "number {n}"
    (s : String) -> "text {s}"
end

describe 42        # => "number 42"
describe "hello"   # => "text hello"
```

### As-bindings <span class="yona-status yona-status--partial">Partial</span>

`name@pattern` matches the pattern and additionally binds the whole value to
`name`. Parser and type-checker support is in place, but code generation for
as-bindings in `case` arms is still limited — prefer rebinding explicitly when
it fails to compile.

```yona
case xs of
    all@[h|_] -> (h, all)   # first element and the whole sequence
    [] -> (0, [])
end
```

### Dictionary patterns <span class="yona-status yona-status--partial">Partial</span>

The grammar reserves `{ :key: pattern, … }` for matching dictionary entries by
key, but compiler support is currently limited. Use `Std\Dict::get`/`contains`
to inspect dictionaries instead — see [Collections](/learn/collections/).

## Patterns outside `case`

### In `let` bindings

A `let` binding's left-hand side may be a pattern; it destructures the value.
The pattern must match — a failed `let` pattern is a runtime error.

```yona
let (a, b) = (1, 2), [h|t] = [10, 20, 30] in
a + b + h   # => 13
```

### In function parameters

Every function parameter is a pattern, and multiple clauses give per-constructor
definitions (see [Functions](/learn/functions/)):

```yona
first pair = case pair of
    (a, _) -> a
end

first (1, 2)   # => 1

unwrap x = case x of
    Some v -> v
    None   -> 0
end
```

### In `catch` clauses

Exceptions are ADT values, and `catch` clauses are patterns over them. Unmatched
exceptions propagate to the next handler up the stack:

```yona
type Error = RuntimeError String | NotFound String

try
    riskyOperation
catch
    RuntimeError msg -> "runtime: " ++ msg
    NotFound path    -> "missing: " ++ path
    _ -> "unknown failure"
end
```

## Exhaustiveness

When the scrutinee is an ADT, the compiler checks that the arms cover every
constructor and emits a **warning** (not an error) for each missing one:

```yona
type Color = Red | Green | Blue

case color of
    Red   -> "red"
    Green -> "green"
end
# `--Wincomplete-patterns` warning: non-exhaustive pattern match on Color — missing constructor Blue
```

A `_` or variable arm makes any match exhaustive. Heed these warnings: a
non-exhaustive match that falls off the end aborts at runtime. For code checked
with `yonac --require-effect-free`, missing alternatives in registered finite
ADTs and `Bool` are instead E0203 errors; ordinary compilation keeps them as the
opt-in `--Wincomplete-patterns` warning. The strict gate also requires a sound
structural size-change proof for every local direct or mutual recursive SCC. It
accepts lexicographic multi-parameter descent when each cycle provably decreases
through fields or non-empty sequence tails bound by unguarded patterns. Numeric
decreases, guarded descent, opaque/helper or higher-order recursion,
incompatible-arity SCCs, and mixed incompatible cycles remain conservatively
rejected.

`--Woverlapping-patterns` identifies arms provably covered by earlier unguarded
arms, including aliases, alternatives, nested constructors, tuples, exact and
head–tail sequences, and scalar literals. It combines complete root `Bool` and
ADT families, while guards and unsupported/open domains stay conservative. It
does not prove general termination or coverage for arbitrary open-domain
patterns.

## Where to next

- [Types and data](/learn/types/) — defining the ADTs you match on.
- [Collections](/learn/collections/) — sequence, dict, and set operations.
- [Language specification](/reference/specification/) — the full pattern
  grammar.
