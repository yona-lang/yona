---
title: Traits
description: Yona's type classes — foundational contracts, declarations, instances, laws, static dispatch, auto-derive, and cross-module use.
---

Traits define shared behavior across types — the same idea as Haskell's
type classes or Rust's traits. A trait declares a set of method signatures;
an **instance** implements them for a concrete type; the compiler resolves
every call to the right instance **statically**, so trait polymorphism
costs nothing at runtime. This page is the full treatment: syntax,
superclass constraints, constrained instances, dispatch mechanics, module
export, auto-derive, and a worked example.

## Declaring a trait

A trait names a type parameter and lists method signatures:

```yona
trait Num a
    abs    : a -> a
    max    : a -> a -> a
    min    : a -> a -> a
    negate : a -> a
end
```

Multi-method traits are the norm — group the operations that belong to one
concept. Signatures use ordinary type syntax with the trait's parameter `a`
standing for "the implementing type".

## Implementing instances

`instance Trait Type` provides the method bodies:

```yona
instance Num Int
    abs x    = if x < 0 then 0 - x else x
    max a b  = if a > b then a else b
    min a b  = if a < b then a else b
    negate x = 0 - x
end

instance Num Float
    abs x    = if x < 0.0 then 0.0 - x else x
    max a b  = if a > b then a else b
    min a b  = if a < b then a else b
    negate x = 0.0 - x
end
```

Calling a trait method dispatches on the argument's type, resolved at
compile time:

```yona
abs (0 - 42)                         # => 42   (Num Int)
max 3.14 2.71                        # => 3.14 (Num Float)
```

## Default methods

A trait may supply a body for a method, used by any instance that does not
override it:

```yona
trait Eq a
    eq  : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = if eq x y then false else true    # default, in terms of eq
end
```

An `instance Eq T` then needs only `eq`; `neq` comes for free. Defaults
keep the *minimal complete definition* small while offering a rich call
surface.

## Superclass constraints

A trait can require another trait as a prerequisite:

```yona
trait Eq a => Ord a
    compare : a -> a -> Ordering
end
```

`Eq a => Ord a` reads: "to have an `Ord` instance, `a` must also have an
`Eq` instance." Methods of the superclass are then available wherever the
subclass constraint holds — an `Ord` context brings `eq` into scope. The
compiler checks that every `instance Ord T` is accompanied by an
`instance Eq T`.

## Constrained instances

Instances themselves can be conditional on other instances — this is how
traits lift over type constructors:

```yona
instance Show a => Show (Option a)
    show opt = case opt of
        Some x -> "Some(" ++ show x ++ ")"
        None   -> "None"
    end
end
```

"`Option a` is showable whenever `a` is." The recursive `show x` call
dispatches to whichever `Show a` instance the concrete element type
supplies — resolved, as always, at compile time once `a` is known.

## Multi-parameter traits

Traits may relate two types:

```yona
trait Iterable a b
    toIterator : a -> Iterator b
end

instance Iterable String Int
    toIterator str = chars str       # a String iterates as character codes
end
```

The instance is keyed on the complete ordered type head, not only its first
type. Multi-parameter traits express relationships such as "collection `a`
yields elements `b`" and "source `b` converts to target `a`". This lets
`From String Int` and `From String Float` coexist without ambiguous or
colliding implementations. Single-parameter traits are unaffected.

## Foundational traits

The Prelude makes the following contracts available without imports:

| Trait | Essential operation | Standard use |
|---|---|---|
| `Eq a` | `eq : a -> a -> Bool` | `==` and `!=` |
| `Ord a` | `compare : a -> a -> Ordering` | `<`, `<=`, `>`, `>=` |
| `Hash a` | `hash : a -> Int` | hash-compatible immutable values |
| `Show a` | `show : a -> String` | deterministic structural rendering |
| `Array array element` | `length`, `get` | indexed finite values |
| `Sized a` | `size : a -> Int` | collection cardinality |
| `Iterable collection element` | `toIterator` | streaming traversal |
| `Foldable collection element` | `foldLeft`, `foldRight` | aggregation |
| `Semigroup a` | `combine : a -> a -> a` | associative combination |
| `Monoid a` | `emptyLike : a -> a` | a witness-directed identity |
| `From target source` | `convert : target -> source -> target` | total conversion |
| `TryFrom target source` | `tryConvert` | checked conversion with `ConvertError` |
| `Parse target` | `parse : target -> String -> Result target ParseError` | strict text parsing |
| `Closeable a` | `close : a -> Unit` | `with` resource cleanup |
| `Send a` | marker | safe ownership transfer across concurrency |
| `Shareable a` | marker, requires `Send` | safe shared concurrent access |

`Ordering` is the Prelude ADT `Less | Equal | Greater`. Operators are ordinary
trait-directed syntax: `a == b` selects `Eq`, while ordering operators select
`Ord` and inspect its `Ordering` result. A missing instance is a compile-time
error with the concrete type and suggested remedies.

`emptyLike`, `convert`, `tryConvert`, and `parse` take a witness because Yona
does not yet have explicit type applications. For example, `emptyLike {0: ""}`
constructs an empty `Dict Int String`, and `convert 0 "42"` selects
`From Int String`'s complete two-type instance head.

## Laws and executable conformance

Instances are expected to obey their algebraic contracts:

- `Eq` is reflexive, symmetric, and transitive.
- `Ord` agrees with `Eq`, reverses consistently, and is transitive.
- equal values have equal `Hash` values.
- `Show` is deterministic.
- `Semigroup.combine` is associative.
- `Monoid.emptyLike` is both a left and right identity.

`Std\TraitLaws` turns these rules into ordinary `Std\Test` cases. Each law
suite accepts explicit samples and rendering/operation callbacks, checks all
relevant pairs or triples, and reports the first concrete counterexample.
This is useful for user-defined instances as well as the standard library.

```yona
import run, render from Std\Test,
       eqLaws from Std\TraitLaws
in render (run (eqLaws "Int" (\value -> show value)
    (\left right -> left == right) [0, 1, 2]))
```

## Concurrency marker traits

`Send` and `Shareable` have no methods or runtime dictionaries. The compiler
proves them at task, channel, parallel-comprehension, and parallel-`let`
boundaries, then erases the evidence. Immutable primitive and structurally
safe immutable aggregate types lift these markers transitively. Native arrays
are `Send` because unique ownership can move, but are not `Shareable` because
their buffers are mutable. Synchronized `Sender` and `Receiver` endpoints are
both `Send` and `Shareable`; the value passed to `send` must independently be
`Send`. Linear resources, promises, and closures with unsafe captures remain
rejected.

## Static resolution: what monomorphization implies

Yona compiles polymorphic functions by **monomorphization**: each call site
is compiled with concrete types (see
[The type system](/guides/type-system/)). Trait dispatch rides on this —
when a generic function using `compare` is instantiated at `Int`, the call
compiles to a direct call to the `Ord Int` implementation. There is no
vtable, no dictionary passing at runtime, no indirect branch: a trait
method call costs exactly what a hand-written direct call costs.

The flip side, stated plainly:

- **No runtime polymorphism.** There are no trait objects and no
  `dyn Trait`-style values. You cannot build a list whose elements are
  "anything Showable" and dispatch per element at runtime — the element
  type must be known at compile time.
- **Heterogeneous cases use ADTs.** When you genuinely need one value that
  is "one of several types", define an ADT with a constructor per case and
  pattern match; each arm then has a concrete type and traits apply
  normally.
- **Code size over indirection.** Each distinct instantiation produces
  specialized code — the classic monomorphization trade.

## Traits and modules

Traits and instances are exported like other declarations:

```yona
module Geo\Core

export trait Area
export area

trait Area a
    area : a -> Float
end
```

Importers get the trait declaration and every instance from the module's
`.yonai` interface. Instance methods are compiled with external linkage
under predictable mangled names (`TraitName_TypeName__method`), so a call
in an importing module resolves directly to the defining module's compiled
code — cross-module dispatch is still static and still direct. When a
generic function or derived method must be re-instantiated at a type the
defining module never saw, its source travels in the interface and is
monomorphized at the call site (see
[Modules and interfaces](/guides/modules-interfaces/)).

## The built-in `Closeable` trait

The `with` expression is trait-powered: at scope exit it calls the
resource's `close` through the `Closeable` trait, on success or exception:

```yona
with f = openFile "data.txt" Read in
    readAll f                        # f closed automatically at exit
```

Implementing `Closeable` for your own handle type makes it usable with
`with` — and discharges its linear obligation, if it has one (see
[Memory and linearity](/guides/memory/)).

## Auto-derive

For the four structural traits — `Show`, `Eq`, `Ord`, `Hash` — the compiler
can generate instances from an ADT's shape with a `deriving` clause:

```yona
type Color = Red | Green | Blue
    deriving Show, Eq, Ord, Hash

type Pair a b = Pair a b deriving (Show, Eq)    # inline form
```

What each derivation produces:

- **Show** — nullary constructors print their name; constructors with
  fields print `Name(f1, f2, …)`, fields shown via their own `Show`.

```yona
show (Pair 1 2)                      # => "Pair(1, 2)"
```

- **Eq** — structural: same constructor and all fields `eq`.

```yona
eq (Pair 1 2) (Pair 1 2)             # => true
```

- **Ord** — `compare` returns `Less`, `Equal`, or `Greater`. Declaration order of
  constructors defines the ordering (first declared is smallest); same
  constructor compares fields lexicographically left to right.

```yona
type Priority = Low | Medium | High deriving Ord

compare Low High                     # => Less
compare (Version 2 0) (Version 1 9)  # => Greater with type Version = Version Int Int
```

- **Hash** — mixes the constructor tag with field hashes.

Derivation is recursive: polymorphic fields resolve through trait dispatch
at the use site, so `type Option a = Some a | None deriving Show, Eq` works
for any showable/comparable `a`. Derived instances are exported like
hand-written ones and are usable across modules. Restrictions: types with
function-typed fields can derive `Show` (functions print as `<function>`)
but not meaningfully `Eq`, `Ord`, or `Hash`; deriving on recursive types
works, with the usual caveat that printing extremely deep structures
recurses. `Int`, `Float`, `String`, `Bool`, and `Symbol` have built-in
instances of all four traits (`Symbol` lacks `Ord`), which the derived code
builds on.

## Worked example: Eq and Ord for a user ADT

A card-game rank, with equality and ordering — first derived, then by hand
to show what the clauses mean:

```yona
type Rank = Jack | Queen | King | Ace
    deriving Show, Eq, Ord

compare Jack Ace                     # => Less (declaration order)
eq Queen Queen                       # => true
show King                            # => "King"
```

The hand-written equivalent, using a superclass-constrained `Ord`:

```yona
trait Eq a
    eq : a -> a -> Bool
end

trait Eq a => Ord a
    compare : a -> a -> Ordering
end

type Rank = Jack | Queen | King | Ace

instance Eq Rank
    eq a b = case (a, b) of
        (Jack, Jack)   -> true
        (Queen, Queen) -> true
        (King, King)   -> true
        (Ace, Ace)     -> true
        _              -> false
    end
end

instance Ord Rank
    compare a b =
        let value r = case r of
            Jack  -> 1
            Queen -> 2
            King  -> 3
            Ace   -> 4
        end,
            left = value a,
            right = value b
        in if left < right then Less else if left > right then Greater else Equal
end

compare Ace Jack                     # => Greater
```

Generic code written against the constraint now works for `Rank` and every
other `Ord` type, and each use compiles to direct calls on the concrete
instance:

```yona
let maxBy a b = if compare a b == Less then b else a in
maxBy King Queen                     # => King
```

Prefer `deriving` when structural behavior is what you want; write the
instance by hand when the semantics differ from structure — for example, a
case-insensitive `Eq` or a domain-specific ordering.
