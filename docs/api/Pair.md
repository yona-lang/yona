# Std.Pair

ADT-based pairs with named fields — an alternative to tuples.

Unlike tuples, `Pair` is a proper ADT with named fields (`fst`, `snd`),
enabling dot access and named pattern matching.

## Types

### Pair

`type Pair a b = Pair { fst : a, snd : b }`

A pair with named fields.

## Functions

### pair

`pair a b = Pair { fst = a, snd = b }`

Creates a pair from two values.

```
pair 1 2   # => Pair { fst = 1, snd = 2 }
```

### first

`first p`

Extracts the first element.

```
first (pair 1 2)   # => 1
```

### second

`second p`

Extracts the second element.

```
second (pair 1 2)   # => 2
```

### mapFirst

`mapFirst fn p`

Transforms the first element.

```
mapFirst (\x -> x * 10) (pair 3 5)   # => Pair { fst = 30, snd = 5 }
```

### mapSecond

`mapSecond fn p`

Transforms the second element.

```
mapSecond (\x -> x * 10) (pair 3 5)   # => Pair { fst = 3, snd = 50 }
```

### mapPair

`mapPair fn gn p`

Transforms both elements with two functions.

```
mapPair (\x -> x + 1) (\x -> x * 2) (pair 3 5)   # => Pair { fst = 4, snd = 10 }
```

### swap

`swap p`

Swaps the two elements.

```
swap (pair 1 2)   # => Pair { fst = 2, snd = 1 }
```

### toTuple

`toTuple p`

Converts to a tuple `(a, b)`.

```
toTuple (pair 1 2)   # => (1, 2)
```

### fromTuple

`fromTuple (a, b) = Pair { fst = a, snd = b }`

Creates a pair from a tuple.

```
fromTuple (1, 2)   # => Pair { fst = 1, snd = 2 }
```
