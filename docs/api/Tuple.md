# Std.Tuple

Operations on 2-tuples (pairs).

Tuples are the built-in product type `(a, b)`. This module provides
accessors, transformers, and conversion functions.

## Functions

### fst

`fst (a, _) = a`

Returns the first element of a pair.

```
fst (1, 2)   # => 1
```

### snd

`snd (_, b) = b`

Returns the second element of a pair.

```
snd (1, 2)   # => 2
```

### swap

`swap (a, b) = (b, a)`

Swaps the elements of a pair.

```
swap (1, 2)   # => (2, 1)
```

### mapBoth

`mapBoth f g (a, b) = (f a, g b)`

Applies two functions to the respective elements.

```
mapBoth (\x -> x + 1) (\x -> x * 2) (3, 5)   # => (4, 10)
```

### mapFst

`mapFst f (a, b) = (f a, b)`

Transforms the first element, keeping the second unchanged.

```
mapFst (\x -> x * 10) (3, 5)   # => (30, 5)
```

### mapSnd

`mapSnd f (a, b) = (a, f b)`

Transforms the second element, keeping the first unchanged.

```
mapSnd (\x -> x * 10) (3, 5)   # => (3, 50)
```

### toList

`toList (a, b) = [a, b]`

Converts a pair to a two-element sequence.

```
toList (1, 2)   # => [1, 2]
```

### curry

`curry f a b = f (a, b)`

Converts a function taking a pair into one taking two arguments.

```
let add = \(a, b) -> a + b in curry add 3 4   # => 7
```

### uncurry

`uncurry f (a, b) = f a b`

Converts a function taking two arguments into one taking a pair.

```
let add = \a b -> a + b in uncurry add (3, 4)   # => 7
```
