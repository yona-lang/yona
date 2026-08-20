# Std.List

Sequence (list) operations — map, filter, fold, sort, and more.

All functions operate on sequences (`[1, 2, 3]`). Most are recursive
and work with pattern matching on head|tail (`[h|t]`).

## Functions

### `map : (a -> b) -> [a] -> [b]`

Applies `fn` to every element, returning a new sequence.

```
map (\x -> x * 2) [1, 2, 3]   # => [2, 4, 6]
map (\x -> x + 1) []           # => []
```

### `filter : (a -> Bool) -> [a] -> [a]`

Keeps only elements where `fn` returns true.

```
filter (\x -> x > 2) [1, 2, 3, 4]   # => [3, 4]
```

### `fold : (b -> a -> b) -> b -> [a] -> b`

Left fold — reduces a sequence to a single value, left to right.

```
fold (\acc x -> acc + x) 0 [1, 2, 3]   # => 6
```

### `foldl : (b -> a -> b) -> b -> [a] -> b`

Alias for `fold`.

### `foldr : (a -> b -> b) -> b -> [a] -> b`

Right fold — reduces a sequence right to left.

```
foldr (\x acc -> x :: acc) [] [1, 2, 3]   # => [1, 2, 3]
```

### `length : [a] -> Int`

Returns the number of elements.

```
length [1, 2, 3]   # => 3
length []           # => 0
```

### `head : [a] -> Int`

Returns the first element. Crashes on empty sequence.

```
head [1, 2, 3]   # => 1
```

### `tail : [a] -> [b]`

Returns all elements except the first. Crashes on empty sequence.

```
tail [1, 2, 3]   # => [2, 3]
```

### `reverse : [a] -> Int`

Reverses the sequence.

```
reverse [1, 2, 3]   # => [3, 2, 1]
```

### `take : Int -> [a] -> [b]`

Returns the first `n` elements.

```
take 2 [1, 2, 3, 4]   # => [1, 2]
```

### `drop : Int -> [a] -> [b]`

Drops the first `n` elements.

```
drop 2 [1, 2, 3, 4]   # => [3, 4]
```

### `flatten : [a] -> [b]`

Flattens a sequence of sequences into a single sequence.

```
flatten [[1, 2], [3], [4, 5]]   # => [1, 2, 3, 4, 5]
```

### `any : (a -> Bool) -> [a] -> Bool`

Returns `true` if any element satisfies `fn`.

```
any (\x -> x > 3) [1, 2, 3, 4]   # => true
any (\x -> x > 5) [1, 2, 3]      # => false
```

### `all : (a -> Bool) -> [a] -> Bool`

Returns `true` if all elements satisfy `fn`.

```
all (\x -> x > 0) [1, 2, 3]   # => true
all (\x -> x > 2) [1, 2, 3]   # => false
```

### `contains : Int -> [a] -> Bool`

Returns `true` if `elem` is in the sequence.

```
contains 3 [1, 2, 3]   # => true
contains 5 [1, 2, 3]   # => false
```

### `isEmpty : [a] -> Bool`

Returns `true` if the sequence is empty.

```
isEmpty []        # => true
isEmpty [1, 2]    # => false
```

### `nth : Int -> [a] -> Int`

Returns the element at index `idx` (0-based). Crashes if out of bounds.

```
nth 0 [10, 20, 30]   # => 10
nth 2 [10, 20, 30]   # => 30
```

### `zip : [a] -> [b] -> [c]`

Pairs elements from two sequences. Stops at the shorter one.

```
zip [1, 2, 3] [10, 20, 30]   # => [(1, 10), (2, 20), (3, 30)]
zip [1, 2] [10]              # => [(1, 10)]
```

### `zipWith : (a -> b) -> [a] -> [c] -> [b]`

Combines elements from two sequences using `fn`.

```
zipWith (\a b -> a + b) [1, 2, 3] [10, 20, 30]   # => [11, 22, 33]
```

### `enumerate : [a] -> [b]`

Pairs each element with its 0-based index.

```
enumerate [10, 20, 30]   # => [(0, 10), (1, 20), (2, 30)]
```

### `partition : (a -> b) -> [c] -> Int`

Splits into two sequences: elements satisfying `pred` and those that don't.

```
partition (\x -> x > 2) [1, 2, 3, 4]   # => ([3, 4], [1, 2])
```

### `intersperse : Int -> [a] -> [b]`

Inserts `sep` between every pair of elements.

```
intersperse 0 [1, 2, 3]   # => [1, 0, 2, 0, 3]
intersperse 0 [1]          # => [1]
```

### `scanl : (b -> a -> b) -> b -> [a] -> [b]`

Like `foldl` but returns all intermediate accumulator values.

```
scanl (\a b -> a + b) 0 [1, 2, 3]   # => [0, 1, 3, 6]
```

### `flatMap : (a -> b) -> [c] -> [d]`

Maps then flattens — applies `fn` which returns a sequence, then concatenates all results.

```
flatMap (\x -> [x, x * 10]) [1, 2, 3]   # => [1, 10, 2, 20, 3, 30]
```

### `find : (a -> Bool) -> [a] -> Symbol`

Returns `(:some, value)` for the first element satisfying `pred`, or `:none`.

```
find (\x -> x > 3) [1, 2, 5, 4]   # => (:some, 5)
find (\x -> x > 9) [1, 2, 3]      # => :none
```

### `sortBy : (a -> b) -> [a] -> [b]`

Sorts using a comparison function. `cmp a b` should return negative if a < b,
zero if equal, positive if a > b. Uses quicksort.

```
sortBy (\a b -> a - b) [3, 1, 4, 1, 5]   # => [1, 1, 3, 4, 5]
```

### `groupBy : (a -> b) -> [c] -> Int`

Groups elements by a key function. Returns a sequence of `(key, [values])` pairs.

```
groupBy (\x -> x % 2) [1, 2, 3, 4]   # => [(1, [1, 3]), (0, [2, 4])]
```

### `sum : [a] -> Int`

Sums all elements (integers).

```
sum [1, 2, 3, 4, 5]   # => 15
```

### `product : [a] -> Int`

Multiplies all elements (integers).

```
product [1, 2, 3, 4, 5]   # => 120
```
