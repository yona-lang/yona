# Std.Function

Function combinators — identity, composition, application, flipping.

These are the fundamental higher-order function building blocks.

## Functions

### identity

`identity : Int -> Int`

Returns its argument unchanged.

```
identity 42   # => 42
```

### const

`const : Int -> Int -> Int`

Creates a function that always returns `value`, ignoring its argument.

```
let always5 = const 5 in always5 99   # => 5
```

### compose

`compose : (a -> b) -> (c -> d) -> Int -> Int`

Composes two functions: `(compose f g) x = f (g x)`.

```
let double = \x -> x * 2 in
let inc = \x -> x + 1 in
compose double inc 3   # => 8  (double(inc(3)) = double(4) = 8)
```

### flip

`flip : (a -> b) -> Int -> Int -> Int`

Swaps the arguments of a two-argument function.

```
let sub = \a b -> a - b in
flip sub 3 10   # => 7  (sub 10 3)
```

### on

`on : (a -> b) -> (c -> d) -> Int -> Int -> Int`

Applies a function to both arguments before combining.
`(on cmp f) a b = cmp (f a) (f b)`

```
let compareLength = on (\a b -> a - b) (\s -> length s) in
compareLength [1,2,3] [1,2]   # => 1
```

### apply

`apply : Int -> (a -> b) -> Int`

Applies a function to a value (flip of function application).

```
apply 42 (\x -> x + 1)   # => 43
```

### pipe

`pipe : Int -> [a] -> Int`

Pipes a value through a chain of functions (left to right).
`pipe x [f, g, h] = h (g (f x))`

```
let fns = [\x -> x + 1, \x -> x * 2, \x -> x - 3] in
pipe 5 fns   # => 9  ((5+1)*2-3 = 9)
```

### fix

`fix : (a -> b) -> Int`

Fixed-point combinator for anonymous recursion.
`fix f = f (fix f)` — enables recursion without naming.

```
let factorial = fix (\self n -> if n <= 1 then 1 else n * (self (n - 1))) in
factorial 5   # => 120
```
