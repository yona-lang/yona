---
title: Types and data
description: Hindley–Milner inference, algebraic data types, records, first-class constructors, traits, and auto-derive.
---

Yona is statically typed with full type inference. You almost never write a
type; the compiler reconstructs the most general (Hindley–Milner) type of
every expression and rejects ill-typed programs at compile time.

## Type inference

No annotations are required — polymorphism is inferred:

```yona
let twice f x = f (f x) in     # inferred: (a -> a) -> a -> a
twice (\x -> x + 1) 40         # => 42
```

Annotations are optional documentation, written Haskell-style on the line
before a definition; the checker verifies the body against them:

```yona
scale : Float -> Float -> Float
scale factor x = factor * x
```

Type errors are compile-time errors — `1 + "two"` never reaches the
runtime. A full account of the checker lives in the
[type system guide](/guides/type-system/).

## Algebraic data types

`type` declares a sum type: a name, optional type parameters, and one or
more constructors separated by `|`. Constructor fields are types:

```yona
type Option a = Some a | None
type Result a e = Ok a | Err e
type Color = Red | Green | Blue
```

Construct values by applying the constructor; inspect them with
[pattern matching](/learn/pattern-matching/):

```yona
let found = Some 42 in
case found of
    Some x -> x
    None   -> 0
end   # => 42
```

### Recursive ADTs

A constructor field may mention the type being defined:

```yona
type List a = Cons a (List a) | Nil

len l = case l of
    Nil -> 0
    Cons _ t -> 1 + len t
end

len (Cons 1 (Cons 2 Nil))   # => 2
```

Implementation note. Non-recursive ADTs compile to flat structs
`{tag, payload}`; recursive ADTs (and ADTs with function-typed fields) are
heap-allocated and reference-counted.

### Function-typed fields

Fields can hold functions, written as an arrow type in parentheses. This is
how lazy structures like streams are built — the tail is a thunk:

```yona
type Lazy a = Cons a (() -> Lazy a) | Empty
type Reducer a b = MkReducer (a -> b -> a)

ones = Cons 1 (\-> ones)

case ones of
    Cons x _ -> x    # => 1
    Empty -> 0
end
```

## Records: named fields

A single-constructor ADT can name its fields. Construct with
`Name { field = value, … }`, read with dot access, and update functionally
— `p { age = 31 }` returns a *copy* with one field replaced, leaving `p`
unchanged:

```yona
type Person = Person { name : String, age : Int }

let p = Person { name = "Alice", age = 30 } in
let older = p { age = 31 } in
(p.age, older.age, older.name)   # => (30, 31, "Alice")
```

Named fields also work in patterns:

```yona
case p of
    Person { name = n } -> n   # => "Alice"
end
```

## Constructors are functions

Every constructor is a first-class function of its fields. Pass it to
higher-order functions or apply it partially like any other function:

```yona
type Pair a b = Pair a b

import map from Std\List in
map Some [1, 2, 3]   # => [Some 1, Some 2, Some 3]

let point = Pair 1 in    # partial application of a 2-field constructor
point 2                  # => Pair 1 2
```

## Traits

Traits are Yona's interfaces (type classes): a set of function signatures a
type can implement. This section is an introduction — the full story,
including superclass constraints and cross-module export, is in the
[traits guide](/guides/traits/).

### Declaring a trait

```yona
trait Show a
    show : a -> String
end
```

A trait may provide **default methods** — implementations in terms of the
other methods, inherited by instances that don't override them:

```yona
trait Eq a
    eq : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = if eq x y then false else true   # default
end
```

### Writing an instance

```yona
instance Show Int
    show x = Std\String::fromInt x
end

# Constrained instance: showing an Option a requires Show a
instance Show a => Show (Option a)
    show opt = case opt of
        Some x -> "Some(" ++ show x ++ ")"
        None -> "None"
    end
end

show (Some 42)   # => "Some(42)"
```

### Static resolution

Trait methods are resolved **at compile time** by monomorphization: each
call site compiles the concrete instance directly, so trait dispatch has
zero runtime overhead — there are no vtables or dictionaries at runtime.

## Auto-derive

The compiler can generate structural instances of `Show`, `Eq`, `Ord`, and
`Hash` from an ADT's shape via a `deriving` clause — postfix or inline:

```yona
type Color = Red | Green | Blue
    deriving Show, Eq, Ord, Hash

type Pair a b = Pair a b deriving (Show, Eq)

show Green                    # => "Green"
show (Pair 1 2)               # => "Pair(1, 2)"
eq Red Red                    # => true
compare Red Blue              # => -1 (declaration order defines Ord)
```

Semantics of the generated instances:

- **Show** — nullary constructors print their name; constructors with
  fields print `Name(field1, field2, …)`, fields shown recursively.
- **Eq** — same constructor and all fields equal.
- **Ord** — constructor declaration order first (first declared is
  smallest), then lexicographic left-to-right field comparison; returns
  `-1`, `0`, or `1`.
- **Hash** — the constructor tag mixed with field hashes.

Deriving works for polymorphic and recursive ADTs; the generated methods
recurse through fields. Types with function-typed fields can derive `Show`
(functions print as `<function>`) but not `Eq`, `Ord`, or `Hash`. Derived
instances are exported across modules like hand-written ones.

## Anonymous sum types

A value can be typed as one of several alternatives without declaring an
ADT, using `|` between types; match on the runtime type with typed patterns
`(name : Type)`:

```yona
parse : String -> Int | String

case result of
    (n : Int)    -> n
    (s : String) -> 0
end
```

## Prelude types

These types are available in every program with no import:

```yona
type Option a   = Some a | None            # optional value
type Result a e = Ok a | Err e             # success or error
type Linear a   = Linear a                 # must be consumed exactly once
type Iterator a = Iterator (() -> Option a)  # pull-based stream
```

- `Option` and `Result` are the standard ways to express absence and
  fallibility; see [Std\Option](/stdlib/option/) and
  [Std\Result](/stdlib/result/).
- `Linear` wraps resources (file handles, sockets) that the linearity
  checker requires you to consume exactly once.
- `Iterator` is the streaming protocol used by file and string iteration —
  O(1) memory per element.

Full signatures are in the [prelude reference](/reference/prelude/).

## Where to next

- [Pattern matching](/learn/pattern-matching/) — destructuring the data you define.
- [Traits guide](/guides/traits/) — superclasses, constrained instances, exports.
- [Type system guide](/guides/type-system/) — inference internals and status.
