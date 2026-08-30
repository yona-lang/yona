# Runtime Type Introspection

Yona provides `typeOf` as a built-in compile-time intrinsic that returns
a value of the `Type` ADT representing the type of any expression. This
enables type-based pattern matching, generic dispatch, and reflection-like
patterns — with **zero runtime cost** for most variants.

## The Type ADT

Defined in the Prelude:

```yona
type Type = TypeInt | TypeFloat | TypeBool | TypeString | TypeSymbol | TypeUnit
          | TypeSeq | TypeSet | TypeDict | TypeTuple | TypeFunction | TypePromise
          | TypeByteArray | TypeIntArray | TypeFloatArray
          | TypeAdt String | TypeSum | TypeRecord
```

`TypeAdt` carries the ADT type name as a `String` field — `typeOf (Some 42)`
returns `TypeAdt "Option"`.

## Usage

```yona
typeOf 42         -- TypeInt
typeOf 3.14       -- TypeFloat
typeOf "hello"    -- TypeString
typeOf true       -- TypeBool
typeOf [1, 2, 3]  -- TypeSeq
typeOf {1, 2}     -- TypeSet
typeOf {1: 2}     -- TypeDict
typeOf (1, 2)     -- TypeTuple
typeOf (Some 42)  -- TypeAdt "Option"
typeOf None       -- TypeAdt "Option"
typeOf (Ok 1)     -- TypeAdt "Result"
```

## Pattern Matching

```yona
case typeOf x of
    TypeInt        -> "an integer"
    TypeString     -> "a string"
    TypeSeq        -> "a sequence"
    TypeDict       -> "a dictionary"
    TypeAdt name   -> "an ADT: " ++ name
    _           -> "something else"
end
```

The compiler can warn on non-exhaustive matches since `Type` is a closed
ADT (unlike symbols which are open).

## Implementation: Zero Runtime Cost

`typeOf` is a **compile-time intrinsic**. The compiler intercepts calls
to `typeOf x` and replaces them with a constant `Type` ADT value based
on the argument's compile-time CType. There is no runtime type tag lookup
and no overhead for primitive type variants.

```yona
typeOf 42
-- compiles to:
TypeInt   -- a constant ADT value with tag=0
```

Only `TypeAdt` carries dynamic data (the type name string), and even that
is determined at compile time from the ADT's known type name — the string
is a global constant.

## Limitation: Polymorphic Functions

Inside a polymorphic function, the type parameter is monomorphized at
the call site. So `typeOf x` inside a generic function gets the *first*
type the function is compiled with:

```yona
let describe x = typeOf x in
describe 42       -- TypeInt  (compiles describe with x: Int)
describe "hello"  -- TypeInt  (reuses compiled version, NOT TypeString)
```

This is a consequence of Yona's monomorphization-based generics. To get
correct type dispatch on each call, write the `typeOf` check at the
call site rather than inside a shared function:

```yona
let int_match = case typeOf 42 of TypeInt -> 1; _ -> 0 end in
let str_match = case typeOf "hi" of TypeString -> 2; _ -> 0 end in
int_match + str_match
```

## Why ADT, not Symbol?

Earlier designs considered symbols (`:int`, `:string`, ...) instead of
an ADT. The ADT approach was chosen for:

- **Exhaustive pattern matching** — compiler can check all variants are handled
- **Type safety** — `TStrung` is a compile error; `:strung` would silently fail
- **Structured data** — `TypeAdt name` carries the type name as a String field
- **Idiomatic Yona** — closed sets of values belong in ADTs, symbols are for
  user-defined dynamic tags
- **Future extensions** — could carry element types: `TypeSeq Type`, `TypeDict Type Type`

Symbols remain available for user code that needs lightweight dynamic tags.

## Use Cases

- **Generic serialization**: dispatch on type to choose encoding
- **Debugging/logging**: include type info in error messages
- **Type-based error handling**: match on the type of an exception value
- **Schema introspection**: build runtime descriptions from compile-time types
- **REPL display**: show different formats for different types
