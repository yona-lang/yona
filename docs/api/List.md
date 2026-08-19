# Std.List

Sequence (list) operations — map, filter, fold, sort, and more.

All functions operate on sequences (`[1, 2, 3]`). Most are recursive
and work with pattern matching on head|tail (`[h|t]`).

## Functions

### map

`map fn seq`

Applies `fn` to every element, returning a new sequence.

```
map (\x -> x * 2) [1, 2, 3]   # => [2, 4, 6]
map (\x -> x + 1) []           # => []
```

### filter

`filter fn seq`

Keeps only elements where `fn` returns true.

```
filter (\x -> x > 2) [1, 2, 3, 4]   # => [3, 4]
```

### fold

`fold fn acc seq`

Left fold — reduces a sequence to a single value, left to right.

```
fold (\acc x -> acc + x) 0 [1, 2, 3]   # => 6
```

### foldl

`foldl fn acc seq`

Alias for `fold`.

### foldr

`foldr fn acc seq`

Right fold — reduces a sequence right to left.

```
foldr (\x acc -> x :: acc) [] [1, 2, 3]   # => [1, 2, 3]
```

### length

`length seq`

Returns the number of elements.

```
length [1, 2, 3]   # => 3
length []           # => 0
```

### head

`head seq`

Returns the first element. Crashes on empty sequence.

```
head [1, 2, 3]   # => 1
```

### tail

`tail seq`

Returns all elements except the first. Crashes on empty sequence.

```
tail [1, 2, 3]   # => [2, 3]
```

### reverse

`reverse seq = foldl (\acc x -> x :: acc) [] seq`

Reverses the sequence.

```
reverse [1, 2, 3]   # => [3, 2, 1]
```

### take

`take n seq`

Returns the first `n` elements.

```
take 2 [1, 2, 3, 4]   # => [1, 2]
```

### drop

`drop n seq`

Drops the first `n` elements.

```
drop 2 [1, 2, 3, 4]   # => [3, 4]
```

### flatten

`flatten seq`

Flattens a sequence of sequences into a single sequence.

```
flatten [[1, 2], [3], [4, 5]]   # => [1, 2, 3, 4, 5]
```

### any

`any fn seq`

Returns `true` if any element satisfies `fn`.

```
any (\x -> x > 3) [1, 2, 3, 4]   # => true
any (\x -> x > 5) [1, 2, 3]      # => false
```

### all

`all fn seq`

Returns `true` if all elements satisfy `fn`.

```
all (\x -> x > 0) [1, 2, 3]   # => true
all (\x -> x > 2) [1, 2, 3]   # => false
```

### contains

`contains elem seq`

Returns `true` if `elem` is in the sequence.

```
contains 3 [1, 2, 3]   # => true
contains 5 [1, 2, 3]   # => false
```

### isEmpty

`isEmpty seq`

Returns `true` if the sequence is empty.

```
isEmpty []        # => true
isEmpty [1, 2]    # => false
```

### nth

`nth idx seq`

Returns the element at index `idx` (0-based). Crashes if out of bounds.

```
nth 0 [10, 20, 30]   # => 10
nth 2 [10, 20, 30]   # => 30
```

### zip

`zip seqA seqB`

Pairs elements from two sequences. Stops at the shorter one.

```
zip [1, 2, 3] [10, 20, 30]   # => [(1, 10), (2, 20), (3, 30)]
zip [1, 2] [10]              # => [(1, 10)]
```

### zipWith

`zipWith fn seqA seqB`

Combines elements from two sequences using `fn`.

```
zipWith (\a b -> a + b) [1, 2, 3] [10, 20, 30]   # => [11, 22, 33]
```

### enumerate

`enumerate seq`

Pairs each element with its 0-based index.

```
enumerate [10, 20, 30]   # => [(0, 10), (1, 20), (2, 30)]
```

### partition

`partition pred seq`

Splits into two sequences: elements satisfying `pred` and those that don't.

```
partition (\x -> x > 2) [1, 2, 3, 4]   # => ([3, 4], [1, 2])
```

### intersperse

`intersperse sep seq`

Inserts `sep` between every pair of elements.

```
intersperse 0 [1, 2, 3]   # => [1, 0, 2, 0, 3]
intersperse 0 [1]          # => [1]
```

### scanl

`scanl fn acc seq`

Like `foldl` but returns all intermediate accumulator values.

```
scanl (\a b -> a + b) 0 [1, 2, 3]   # => [0, 1, 3, 6]
```

### flatMap

`flatMap fn seq = flatten (map fn seq)`

Maps then flattens — applies `fn` which returns a sequence, then concatenates all results.

```
flatMap (\x -> [x, x * 10]) [1, 2, 3]   # => [1, 10, 2, 20, 3, 30]
```

### find

`find pred seq`

Returns `(:some, value)` for the first element satisfying `pred`, or `:none`.

```
find (\x -> x > 3) [1, 2, 5, 4]   # => (:some, 5)
find (\x -> x > 9) [1, 2, 3]      # => :none
```

### sortBy

`sortBy cmp seq`

Sorts using a comparison function. `cmp a b` should return negative if a < b,
zero if equal, positive if a > b. Uses quicksort.

```
sortBy (\a b -> a - b) [3, 1, 4, 1, 5]   # => [1, 1, 3, 4, 5]
```

### groupBy

`groupBy fn seq`

Groups elements by a key function. Returns a sequence of `(key, [values])` pairs.

```
groupBy (\x -> x % 2) [1, 2, 3, 4]   # => [(1, [1, 3]), (0, [2, 4])]
```

### sum

`sum seq = foldl (\a b -> a + b) 0 seq`

Sums all elements (integers).

```
sum [1, 2, 3, 4, 5]   # => 15
```

### product

`product seq = foldl (\a b -> a * b) 1 seq`

Multiplies all elements (integers).

```
product [1, 2, 3, 4, 5]   # => 120
```
