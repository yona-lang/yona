# Traits in Yona

Traits define shared behavior across types — similar to Haskell's type classes or Rust's traits.

## Declaring Traits

```yona
trait Num a
    abs : a -> a
    max : a -> a -> a
    min : a -> a -> a
    negate : a -> a
end
```

## Implementing Instances

```yona
instance Num Int
    abs x = if x < 0 then 0 - x else x
    max a b = if a > b then a else b
    min a b = if a < b then a else b
    negate x = 0 - x
end

instance Num Float
    abs x = if x < 0.0 then 0.0 - x else x
    max a b = if a > b then a else b
    min a b = if a < b then a else b
    negate x = 0.0 - x
end
```

## Using Trait Methods

```yona
abs (-42)        -- 42 (dispatches to Num Int instance)
max 3.14 2.71    -- 3.14 (dispatches to Num Float instance)
```

Dispatch is resolved at compile time based on argument types (monomorphization).

## Constrained Instances

Instances can require constraints on type parameters:

```yona
instance Show a => Show (Option a)
    show opt = case opt of
        Some x -> "Some(" ++ show x ++ ")"
        None -> "None"
    end
end
```

## Default Methods

Traits can provide default implementations:

```yona
trait Eq a
    eq : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = not (eq x y)    -- default: defined in terms of eq
end
```

## Cross-Module Traits

Traits and instances are exported via the module system:

```yona
module Std\Math
export trait Num
export abs, max, min, negate
```

Instance methods get `ExternalLinkage` in LLVM so importing modules can call them directly. Method resolution uses mangled names: `TraitName_TypeName__methodName`.

## Built-in Trait: Closeable

The `with` expression uses the `Closeable` trait for resource cleanup:

```yona
with open "file.txt" as f
    readAll f
end
-- f.close() called automatically at scope exit
```

## Multi-Parameter Traits

Traits support multiple type parameters for expressing relationships between types:

```yona
trait Iterable a b
    toIterator : a -> Iterator b
end

instance Iterable String Int
    toIterator str = chars str
end
```

The instance key is `"Trait:Type1:Type2"`. Multi-param traits are used for:
- `Iterable a b` — collection `a` produces elements of type `b`
- `Convertible a b` — type `a` can be converted to type `b`

Single-parameter traits use the same canonical parameter-vector representation
as multi-parameter traits.
