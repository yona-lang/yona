# Std.Option

Optional values — represents a value that may or may not exist.

Use `Some value` to wrap a value, `None` for absence. Chain operations
with `flatMap`, filter with predicates, or provide defaults with `unwrapOr`.

## Types

### Option

`type Option a = Some a | None`

An optional value: either `Some value` or `None`.

## Functions

### isSome

`isSome : Option a -> Bool`

Returns `true` if the option contains a value.

```
isSome (Some 42)   # => true
isSome None        # => false
```

### isNone

`isNone : Option a -> Bool`

Returns `true` if the option is empty.

```
isNone None        # => true
isNone (Some 42)   # => false
```

### unwrapOr

`unwrapOr : a -> Option a -> a`

Extracts the value, or returns `default` if empty.

```
unwrapOr 0 (Some 42)   # => 42
unwrapOr 0 None         # => 0
```

### map

`map : (a -> b) -> Option a -> Option b`

Transforms the contained value with `fn`, leaving `None` unchanged.

```
map (\x -> x * 2) (Some 5)   # => Some 10
map (\x -> x * 2) None       # => None
```

### flatMap

`flatMap : (a -> b) -> Option a -> c`

Applies `fn` which itself returns an Option, flattening the result.
Useful for chaining operations that may fail.

```
flatMap (\x -> if x > 0 then Some (x * 10) else None) (Some 5)   # => Some 50
flatMap (\x -> if x > 0 then Some (x * 10) else None) (Some 0)   # => None
```

### filter

`filter : (a -> Bool) -> Option a -> Option a`

Keeps the value only if it satisfies `pred`, otherwise returns `None`.

```
filter (\x -> x > 3) (Some 5)   # => Some 5
filter (\x -> x > 3) (Some 1)   # => None
```

### orElse

`orElse : a -> Option a -> Option a`

Returns this option if it contains a value, otherwise returns `alternative`.

```
orElse (Some 99) None        # => Some 99
orElse (Some 99) (Some 42)   # => Some 42
```

### toResult

`toResult : a -> Option a -> (b, c)`

Converts to a Result: `Some v` becomes `(:ok, v)`, `None` becomes `(:err, err)`.

```
toResult "missing" (Some 42)   # => (:ok, 42)
toResult "missing" None        # => (:err, "missing")
```

### zip

`zip : Option a -> Option a -> Option a`

Combines two options into an option of a pair. Returns `None` if either is empty.

```
zip (Some 1) (Some 2)   # => Some (1, 2)
zip (Some 1) None       # => None
```

### fold

`fold : a -> (b -> c) -> Option b -> a`

Eliminates an option: returns `onNone` if empty, applies `onSome` if present.

```
fold 0 (\x -> x * 10) (Some 5)   # => 50
fold 0 (\x -> x * 10) None       # => 0
```
