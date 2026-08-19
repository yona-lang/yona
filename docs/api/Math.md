# Std.Math

Math — polymorphic numeric operations and float math.

The `Num` trait provides polymorphic `abs`, `max`, `min` for both
Int and Float. Float-specific functions (sqrt, sin, cos, etc.) are
bound from the C math library via extern declarations.

## Traits

### Num

```yona
trait Num a
    abs : a -> a
    max : a -> a -> a
    min : a -> a -> a
    negate : a -> a
end
```

Numeric trait — polymorphic over Int and Float.

## Functions

### abs

`abs : a -> a`

### max

`max : a -> a -> a`

### min

`min : a -> a -> a`

### negate

`negate : a -> a`

### clamp

`clamp : Int -> Int -> Int -> Int`

Restricts a value to the range `[lo, hi]`.

```
clamp 0 10 15   # => 10
```

### sign

`sign : Int -> Int`

Returns 1 for positive, -1 for negative, 0 for zero.

```
sign 42   # => 1
```

### isEven

`isEven : Int -> Bool`

Returns `true` if the integer is even.

### isOdd

`isOdd : Int -> Bool`

Returns `true` if the integer is odd.

### gcd

`gcd : Int -> Int -> Int`

Greatest common divisor (Euclidean algorithm).

```
gcd 12 8   # => 4
```

### pow

`pow : Int -> Int -> Int`

Integer exponentiation. Uses fast squaring.

```
pow 2 10   # => 1024
```

### factorial

`factorial : Int -> Int`

Factorial: `n! = 1 * 2 * ... * n`.

```
factorial 5   # => 120
```

### sqrt

`sqrt : Float -> Float`

Square root (Float -> Float).

### sin

`sin : Float -> Float`

Sine (Float -> Float, radians).

### cos

`cos : Float -> Float`

Cosine (Float -> Float, radians).

### tan

`tan : Float -> Float`

Tangent (Float -> Float, radians).

### log

`log : Float -> Float`

Natural logarithm (Float -> Float).

### exp

`exp : Float -> Float`

Exponential e^x (Float -> Float).

### floor

`floor : Float -> Float`

Floor (Float -> Float).

### ceil

`ceil : Float -> Float`

Ceiling (Float -> Float).

### round

`round : Float -> Float`

Round to nearest integer (Float -> Float).

### pi

`pi : Float = 3.141592653589793`

Pi constant.

```
pi   # => 3.14159265358979
```
