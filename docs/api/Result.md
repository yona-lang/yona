# Std.Result

Error handling — represents either success (`Ok value`) or failure (`Err error`).

Chain operations with `flatMap`/`andThen`, transform errors with `mapErr`,
or extract values with `unwrapOr`. Convert to Option with `toOption`.

## Functions

### `isOk : Result (a, e) -> Bool`

Returns `true` if the result is `Ok`.

```
isOk (Ok 42)       # => true
isOk (Err "fail")  # => false
```

### `isErr : Result (a, e) -> Bool`

Returns `true` if the result is `Err`.

```
isErr (Err "fail")  # => true
isErr (Ok 42)       # => false
```

### `unwrapOr : a -> Result (a, e) -> a`

Extracts the value from `Ok`, or returns `default` if `Err`.

```
unwrapOr 0 (Ok 42)       # => 42
unwrapOr 0 (Err "fail")  # => 0
```

### `map : (a -> b) -> Result (a, e) -> Result (b, e)`

Transforms the success value, leaving errors unchanged.

```
map (\x -> x * 2) (Ok 21)       # => Ok 42
map (\x -> x * 2) (Err "fail")  # => Err "fail"
```

### `mapErr : (e -> f) -> Result (a, e) -> Result (a, f)`

Transforms the error value, leaving successes unchanged.

```
mapErr (\e -> e + "!") (Err "fail")  # => Err "fail!"
mapErr (\e -> e + "!") (Ok 42)       # => Ok 42
```

### `flatMap : (a -> Result (b, e)) -> Result (a, e) -> Result (b, e)`

Applies `fn` which returns a Result, flattening the nested result.

```
flatMap (\x -> if x > 0 then Ok (x * 2) else Err "negative") (Ok 21)  # => Ok 42
flatMap (\x -> Ok (x * 2)) (Err "fail")                                # => Err "fail"
```

### `flatten : Result (Result (a, e), e) -> Result (a, e)`

Flattens a nested `Result (Result a e) e` into `Result a e`.

```
flatten (Ok (Ok 42))       # => Ok 42
flatten (Ok (Err "inner")) # => Err "inner"
flatten (Err "outer")      # => Err "outer"
```

### `toOption : Result (a, e) -> Option a`

Converts to an Option: `Ok value` becomes `Some value`; `Err` becomes
`None`.

```
toOption (Ok 42)       # => Some 42
toOption (Err "fail")  # => None
```

### `andThen : (a -> Result (b, e)) -> Result (a, e) -> Result (b, e)`

Alias for `flatMap` — chains a computation that may fail.

```
andThen (\x -> Ok (x + 1)) (Ok 41)  # => Ok 42
```

### `orElse : (e -> Result (a, f)) -> Result (a, e) -> Result (a, f)`

Recovers from an error by applying `fn` to the error value.

```
orElse (\e -> Ok 0) (Err "fail")  # => Ok 0
orElse (\e -> Ok 0) (Ok 42)       # => Ok 42
```

### `fold : (e -> b) -> (a -> b) -> Result (a, e) -> b`

Eliminates a result: applies `onErr` to errors, `onOk` to successes.

```
fold (\e -> 0) (\v -> v * 2) (Ok 21)       # => 42
fold (\e -> 0) (\v -> v * 2) (Err "fail")  # => 0
```
