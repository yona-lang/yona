# Std.Result

Error handling — represents either success (`Ok value`) or failure (`Err error`).

Chain operations with `flatMap`/`andThen`, transform errors with `mapErr`,
or extract values with `unwrapOr`. Convert to Option with `toOption`.

## Types

### Result

`type Result a e = Ok a | Err e`

A result type: either `Ok value` (success) or `Err error` (failure).

## Functions

### isOk

`isOk : Result a b -> Bool`

Returns `true` if the result is `Ok`.

```
isOk (Ok 42)       # => true
isOk (Err "fail")  # => false
```

### isErr

`isErr : Result a b -> Bool`

Returns `true` if the result is `Err`.

```
isErr (Err "fail")  # => true
isErr (Ok 42)       # => false
```

### unwrapOr

`unwrapOr : a -> Result b c -> d`

Extracts the value from `Ok`, or returns `default` if `Err`.

```
unwrapOr 0 (Ok 42)       # => 42
unwrapOr 0 (Err "fail")  # => 0
```

### map

`map : (a -> b) -> Result c d -> Result e f`

Transforms the success value, leaving errors unchanged.

```
map (\x -> x * 2) (Ok 21)       # => Ok 42
map (\x -> x * 2) (Err "fail")  # => Err "fail"
```

### mapErr

`mapErr : (a -> b) -> Result c d -> Result e f`

Transforms the error value, leaving successes unchanged.

```
mapErr (\e -> e + "!") (Err "fail")  # => Err "fail!"
mapErr (\e -> e + "!") (Ok 42)       # => Ok 42
```

### flatMap

`flatMap : (a -> b) -> Result c d -> e`

Applies `fn` which returns a Result, flattening the nested result.

```
flatMap (\x -> if x > 0 then Ok (x * 2) else Err "negative") (Ok 21)  # => Ok 42
flatMap (\x -> Ok (x * 2)) (Err "fail")                                # => Err "fail"
```

### flatten

`flatten : Result a b -> Result c d`

Flattens a nested `Result (Result a e) e` into `Result a e`.

```
flatten (Ok (Ok 42))       # => Ok 42
flatten (Ok (Err "inner")) # => Err "inner"
flatten (Err "outer")      # => Err "outer"
```

### toOption

`toOption : Result a b -> (c, d)`

Converts to a symbol-tagged option: `Ok v` → `(:some, v)`, `Err _` → `:none`.

```
toOption (Ok 42)       # => (:some, 42)
toOption (Err "fail")  # => :none
```

### andThen

`andThen : (a -> b) -> Result c d -> e`

Alias for `flatMap` — chains a computation that may fail.

```
andThen (\x -> Ok (x + 1)) (Ok 41)  # => Ok 42
```

### orElse

`orElse : (a -> b) -> Result c d -> Result e f`

Recovers from an error by applying `fn` to the error value.

```
orElse (\e -> Ok 0) (Err "fail")  # => Ok 0
orElse (\e -> Ok 0) (Ok 42)       # => Ok 42
```

### fold

`fold : (a -> b) -> (c -> d) -> Result e f -> g`

Eliminates a result: applies `onErr` to errors, `onOk` to successes.

```
fold (\e -> 0) (\v -> v * 2) (Ok 21)       # => 42
fold (\e -> 0) (\v -> v * 2) (Err "fail")  # => 0
```
