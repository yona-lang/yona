# Std.Range

Integer ranges with optional step — lazy representation, materialized on demand.

Ranges are represented as `(:range, start, stop, step)` tuples.
They don't allocate a sequence until `toList` is called.

## Functions

### `range : Int -> Int -> (a, b)`

Creates a range from `start` to `stop` (inclusive) with step 1.

```
toList (range 1 5)   # => [1, 2, 3, 4, 5]
```

### `rangeStep : Int -> Int -> Int -> (a, b)`

Creates a range from `start` to `stop` with a custom `step`.

```
toList (rangeStep 0 10 3)   # => [0, 3, 6, 9]
toList (rangeStep 10 0 (0 - 2))   # => [10, 8, 6, 4, 2, 0]
```

### `toList : (a, b) -> [c]`

Materializes the range into a sequence.

```
toList (range 1 3)   # => [1, 2, 3]
```

### `contains : Int -> (a, b) -> Bool`

Returns `true` if `value` falls within the range and aligns with the step.

```
contains 3 (range 1 5)         # => true
contains 3 (rangeStep 0 10 2)  # => false  (0, 2, 4, 6, 8, 10)
```

### `length : (a, b) -> Int`

Returns the number of elements in the range.

```
length (range 1 10)   # => 10
```

### `take : Int -> (a, b) -> (c, d)`

Returns a range containing only the first `n` elements.

```
toList (take 3 (range 1 10))   # => [1, 2, 3]
```

### `drop : Int -> (a, b) -> (c, d)`

Returns a range with the first `n` elements removed.

```
toList (drop 3 (range 1 5))   # => [4, 5]
```

### `map : (a -> b) -> (c, d) -> [b]`

Applies `fn` to each element, returning a sequence (not a range).

```
map (\x -> x * x) (range 1 4)   # => [1, 4, 9, 16]
```

### `filter : (a -> Bool) -> (b, c) -> [a]`

Keeps only elements satisfying `pred`, returning a sequence.

```
filter (\x -> x % 2 == 0) (range 1 6)   # => [2, 4, 6]
```

### `fold : (a -> b) -> Int -> (c, d) -> Int`

Left fold over the range.

```
fold (\acc x -> acc + x) 0 (range 1 5)   # => 15
```

### `forEach : (a -> b) -> (c, d) -> Symbol`

Applies `fn` to each element for side effects.

```
forEach (\x -> print x) (range 1 3)   # prints 1, 2, 3
```
