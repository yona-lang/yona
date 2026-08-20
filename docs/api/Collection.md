# Std.Collection

Higher-order collection operations — functional helpers for sequences, sets, dicts.

Provides iterate, unfold, repeat, cycle, and windowing operations
that complement the core List module.

## Functions

### `iterate : Int -> (a -> b) -> Int -> [b]`

Generates a sequence by repeatedly applying `fn` to a seed value.
Returns the first `n` values: `[seed, fn(seed), fn(fn(seed)), ...]`.

```
iterate 5 (\x -> x * 2) 1   # => [1, 2, 4, 8, 16]
```

### `unfold : (a -> b) -> Int -> [b]`

Generates a sequence from a seed using a producer function.
`fn seed` returns `(:some, (value, next_seed))` to continue, `:none` to stop.

```
unfold (\n -> if n > 0 then (:some, (n, n - 1)) else :none) 5
# => [5, 4, 3, 2, 1]
```

### `replicate : Int -> Int -> [a]`

Creates a sequence of `n` copies of `value`.

```
replicate 3 42   # => [42, 42, 42]
```

### `tabulate : Int -> (a -> b) -> [b]`

Creates a sequence by applying `fn` to indices `0..n-1`.

```
tabulate 4 (\i -> i * i)   # => [0, 1, 4, 9]
```

### `window : Int -> [a] -> [b]`

Sliding window of size `size` over a sequence.
Returns a sequence of sub-sequences (represented as sequences).

```
window 2 [1, 2, 3, 4]   # => [[1, 2], [2, 3], [3, 4]]
```

### `chunks : Int -> [a] -> [b]`

Splits a sequence into chunks of size `size`.

```
chunks 2 [1, 2, 3, 4, 5]   # => [[1, 2], [3, 4], [5]]
```

### `pairwise : [a] -> [b]`

Returns consecutive pairs from a sequence.

```
pairwise [1, 2, 3, 4]   # => [(1, 2), (2, 3), (3, 4)]
```

### `dedup : [a] -> [b]`

Removes consecutive duplicates.

```
dedup [1, 1, 2, 2, 3, 1, 1]   # => [1, 2, 3, 1]
```

### `frequencies : [a] -> [b]`

Counts occurrences of each element. Returns sequence of `(element, count)` pairs.

```
frequencies [1, 2, 1, 3, 2, 1]   # => [(1, 3), (2, 2), (3, 1)]
```
