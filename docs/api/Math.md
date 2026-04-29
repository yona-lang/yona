# Std.Math

Math — polymorphic numeric operations and float math.

The `Num` trait provides polymorphic `abs`, `max`, `min` for both
Int and Float. Float-specific functions (sqrt, sin, cos, etc.) are
bound from the C math library via extern declarations.

## Functions

### `trait`

```yona
trait Num a
```

Numeric trait — polymorphic over Int and Float.

### `instance`

```yona
instance Num Int
```

Num instance for Int.

### `instance`

```yona
instance Num Float
```

Num instance for Float.

### `clamp`

```yona
clamp lo hi x = if x < lo then lo else if x > hi then hi else x
```

Restricts a value to the range `[lo, hi]`.

```
clamp 0 10 15   # => 10
```

### `sign`

```yona
sign x = if x > 0 then 1 else if x < 0 then 0 - 1 else 0
```

Returns 1 for positive, -1 for negative, 0 for zero.

```
sign 42   # => 1
```

### `isEven`

```yona
isEven x = x % 2 == 0
```

Returns `true` if the integer is even.

### `isOdd`

```yona
isOdd x = x % 2 != 0
```

Returns `true` if the integer is odd.

### `gcd`

```yona
gcd a b =
```

Greatest common divisor (Euclidean algorithm).

```
gcd 12 8   # => 4
```

### `pow`

```yona
pow base exp =
```

Integer exponentiation. Uses fast squaring.

```
pow 2 10   # => 1024
```

### `factorial`

```yona
factorial n =
```

Factorial: `n! = 1 * 2 * ... * n`.

```
factorial 5   # => 120
```

### `extern`

```yona
extern sqrt : Float -> Float
```

Square root (Float -> Float).

### `extern`

```yona
extern sin : Float -> Float
```

Sine (Float -> Float, radians).

### `extern`

```yona
extern cos : Float -> Float
```

Cosine (Float -> Float, radians).

### `extern`

```yona
extern tan : Float -> Float
```

Tangent (Float -> Float, radians).

### `extern`

```yona
extern log : Float -> Float
```

Natural logarithm (Float -> Float).

### `extern`

```yona
extern exp : Float -> Float
```

Exponential e^x (Float -> Float).

### `extern`

```yona
extern floor : Float -> Float
```

Floor (Float -> Float).

### `extern`

```yona
extern ceil : Float -> Float
```

Ceiling (Float -> Float).

### `extern`

```yona
extern round : Float -> Float
```

Round to nearest integer (Float -> Float).

### `pi`

```yona
pi = 3.141592653589793
```

Pi constant.

```
pi   # => 3.14159265358979
```

