# Std.Random

Random -- pseudo-random number generation.

Provides random integers, floats, element selection, and sequence shuffling.
Uses a fast PRNG seeded at program startup. For cryptographically secure
randomness, use `Std.Crypto`.

## Functions

### `int : Int -> Int -> Int`

Generate a random integer in the range `[lo, hi]` (inclusive).

```yona
import int from Std\Random in
int 1 100   # => 42 (random)
```

### `float : Float`

Generate a random float in the range `[0.0, 1.0)`.

```yona
import float from Std\Random in
float   # => 0.7312... (random)
```

### `choice : [a] -> Int`

Pick a random element from a sequence. Returns the element.

```yona
import choice from Std\Random in
choice [10, 20, 30]   # => 20 (random)
```

### `shuffle : [a] -> [b]`

Return a new sequence with elements in random order.

```yona
import shuffle from Std\Random in
shuffle [1, 2, 3, 4, 5]   # => [3, 1, 5, 2, 4] (random)
```
