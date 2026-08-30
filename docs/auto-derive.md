# Auto-Derive

Yona supports automatic derivation of trait instances for algebraic data types
via the `deriving` clause. The compiler generates structural implementations
based on the ADT's constructors and field types.

## Syntax

Two forms are supported:

```yona
-- Postfix (recommended for multi-line types)
type Color = Red | Green | Blue
    deriving Show, Eq, Ord, Hash

-- Inline (compact for simple types)
type Pair a b = Pair a b deriving (Show, Eq)
```

Both forms are equivalent. The `deriving` clause appears after the last
constructor variant and lists one or more trait names separated by commas.

## Derivable Traits

### Show

Converts a value to its string representation.

```yona
type Color = Red | Green | Blue
    deriving Show

show Red      -- "Red"
show Green    -- "Green"
```

For constructors with fields:

```yona
type Pair a b = Pair a b
    deriving Show

show (Pair 1 2)   -- "Pair(1, 2)"
```

Nullary constructors produce their name. Constructors with fields produce
`Name(field1, field2, ...)`. Field values are shown recursively via their
own `Show` instances.

### Eq

Structural equality. Two values are equal if they have the same constructor
and all corresponding fields are equal.

```yona
type Color = Red | Green | Blue
    deriving Eq

eq Red Red     -- true
eq Red Blue    -- false
```

For constructors with fields, each field is compared with `eq`:

```yona
type Point = Point Int Int
    deriving Eq

eq (Point 1 2) (Point 1 2)   -- true
eq (Point 1 2) (Point 3 4)   -- false
```

### Ord

Ordering comparison. Returns `-1` (less), `0` (equal), or `1` (greater).

Constructor declaration order defines the natural ordering — the first
declared constructor is the smallest:

```yona
type Priority = Low | Medium | High
    deriving Ord

compare Low High      -- -1 (Low < High)
compare High Medium   -- 1  (High > Medium)
compare Low Low       -- 0
```

For same-constructor values with fields, comparison is lexicographic
left-to-right:

```yona
type Version = Version Int Int
    deriving Ord

compare (Version 1 0) (Version 1 3)   -- -1 (same major, 0 < 3)
compare (Version 2 0) (Version 1 9)   -- 1  (2 > 1)
```

### Hash

Produces an integer hash code. Tag value is mixed with field hashes
using FNV-1a-style combining:

```yona
type Color = Red | Green | Blue
    deriving Hash

hash Red     -- 0
hash Green   -- 1
hash Blue    -- 2
```

For constructors with fields:

```yona
type Point = Point Int Int
    deriving Hash

hash (Point 3 4)   -- combined hash of tag + hash(3) + hash(4)
```

## Primitive Instances

The following types have built-in Show, Eq, Ord, and Hash instances
registered in the Prelude:

| Type | Show | Eq | Ord | Hash |
|------|------|----|-----|------|
| Int | yes | yes | yes | yes |
| Float | yes | yes | yes | yes |
| String | yes | yes | yes | yes |
| Bool | yes | yes | yes | yes |
| Symbol | yes | yes | — | yes |

These are used automatically when deriving traits for ADTs with
concrete-typed fields.

## Polymorphic Types

Deriving works with polymorphic ADTs. The generated instance calls
the trait method recursively on polymorphic fields, which resolves
via trait dispatch at the call site:

```yona
type Option a = Some a | None
    deriving Show, Eq

show (Some 42)       -- "Some(42)"
show None            -- "None"
eq (Some 1) (Some 1) -- true
eq (Some 1) None      -- false
```

## Restrictions

- **Function fields**: Types with function-typed fields (e.g., closures)
  can derive Show (functions display as `<function>`), but Eq, Ord, and
  Hash derivation is not meaningful for functions.
- **Recursive types**: Deriving works on recursive types. The generated
  methods call themselves recursively. Printing deeply nested or cyclic
  structures may stack-overflow.

## Architecture

The derive system uses **self-registering strategies**. Each derivable trait
is a strategy object that registers itself via a static initializer in
`DeriveEngine.cpp`. Adding a new built-in derivable trait requires:

1. Write a generator function that takes `DeriveAdtInfo` and returns Yona source
2. Add one `register_strategy()` call

The generated source is parsed through the existing `reparse_genfn` pipeline
(the same mechanism used for cross-module generic monomorphization), then
compiled as a deferred function. Derived instances are exported to `.yonai`
interface files like any hand-written instance, enabling cross-module use.

## Cross-Module Use

Derived instances are fully exported. A module that defines:

```yona
module Shapes

export type Shape

type Shape = Circle Float | Rect Float Float
    deriving Show, Eq
```

Makes `Show Shape` and `Eq Shape` available to any importing module:

```yona
import show from Shapes in show (Circle 3.14)
```

The derived method source is stored as GENFN in the `.yonai` file,
enabling re-compilation with concrete types at the call site when needed.

## Future: User-Defined Derives

The strategy registry is designed to support user-defined derivable traits
in the future. When a compile-time evaluator is added, traits will be able
to declare a `derive` block that templates over ADT structure. The existing
infrastructure already carries the required `AdtInfo` type parameters, field
type references, and `.yonai` metadata.
