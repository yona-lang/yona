# Std.Tuple

Operations on 2-tuples (pairs).

Tuples are the built-in product type `(a, b)`. This module provides
accessors, transformers, and conversion functions.

## Functions

### `fst : (a, b) -> Int`

Returns the first element of a pair.

```
fst (1, 2)   # => 1
```

### `snd : (a, b) -> Int`

Returns the second element of a pair.

```
snd (1, 2)   # => 2
```

### `swap : (a, b) -> (c, d)`

Swaps the elements of a pair.

```
swap (1, 2)   # => (2, 1)
```

### `mapBoth : (a -> b) -> (c -> d) -> (e, f) -> (g, h)`

Applies two functions to the respective elements.

```
mapBoth (\x -> x + 1) (\x -> x * 2) (3, 5)   # => (4, 10)
```

### `mapFst : (a -> b) -> (c, d) -> (e, f)`

Transforms the first element, keeping the second unchanged.

```
mapFst (\x -> x * 10) (3, 5)   # => (30, 5)
```

### `mapSnd : (a -> b) -> (c, d) -> (e, f)`

Transforms the second element, keeping the first unchanged.

```
mapSnd (\x -> x * 10) (3, 5)   # => (3, 50)
```

### `toList : (a, b) -> [c]`

Converts a pair to a two-element sequence.

```
toList (1, 2)   # => [1, 2]
```

### `curry : (a -> b) -> Int -> Int -> (c, d)`

Converts a function taking a pair into one taking two arguments.

```
let add = \(a, b) -> a + b in curry add 3 4   # => 7
```

### `uncurry : (a -> b) -> (c, d) -> Int`

Converts a function taking two arguments into one taking a pair.

```
let add = \a b -> a + b in uncurry add (3, 4)   # => 7
```
