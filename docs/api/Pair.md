# Std.Pair

ADT-based pairs with named fields — an alternative to tuples.

Unlike tuples, `Pair` is a proper ADT with named fields (`fst`, `snd`),
enabling dot access and named pattern matching.

## Types

### Pair

`type Pair a b = Pair { fst : a, snd : b }`

A pair with named fields.

## Functions

### `pair : a -> b -> Pair c d`

Creates a pair from two values.

```
pair 1 2   # => Pair { fst = 1, snd = 2 }
```

### `first : Pair a b -> c`

Extracts the first element.

```
first (pair 1 2)   # => 1
```

### `second : Pair a b -> c`

Extracts the second element.

```
second (pair 1 2)   # => 2
```

### `mapFirst : (a -> b) -> Pair c d -> Pair e f`

Transforms the first element.

```
mapFirst (\x -> x * 10) (pair 3 5)   # => Pair { fst = 30, snd = 5 }
```

### `mapSecond : (a -> b) -> Pair c d -> Pair e f`

Transforms the second element.

```
mapSecond (\x -> x * 10) (pair 3 5)   # => Pair { fst = 3, snd = 50 }
```

### `mapPair : (a -> b) -> (c -> d) -> Pair e f -> Pair g h`

Transforms both elements with two functions.

```
mapPair (\x -> x + 1) (\x -> x * 2) (pair 3 5)   # => Pair { fst = 4, snd = 10 }
```

### `swap : Pair a b -> Pair c d`

Swaps the two elements.

```
swap (pair 1 2)   # => Pair { fst = 2, snd = 1 }
```

### `toTuple : Pair a b -> (c, d)`

Converts to a tuple `(a, b)`.

```
toTuple (pair 1 2)   # => (1, 2)
```

### `fromTuple : (a, b) -> Pair c d`

Creates a pair from a tuple.

```
fromTuple (1, 2)   # => Pair { fst = 1, snd = 2 }
```
