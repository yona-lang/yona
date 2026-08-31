# Std.Function

Function combinators — identity, composition, application, flipping.

These are the fundamental higher-order function building blocks.

## Functions

### `identity : a -> a`

Returns its argument unchanged.

```
identity 42   # => 42
```

### `const : a -> b -> a`

Creates a function that always returns `value`, ignoring its argument.

```
let always5 = const 5 in always5 99   # => 5
```

### `compose : (a -> b) -> (c -> a) -> c -> b`

Composes two functions: `(compose f g) x = f (g x)`.

```
let double = \x -> x * 2 in
let inc = \x -> x + 1 in
compose double inc 3   # => 8  (double(inc(3)) = double(4) = 8)
```

### `flip : (a -> (b -> c)) -> b -> a -> c`

Swaps the arguments of a two-argument function.

```
let sub = \a b -> a - b in
flip sub 3 10   # => 7  (sub 10 3)
```

### `on : (a -> (a -> b)) -> (c -> a) -> c -> c -> b`

Applies a function to both arguments before combining.
`(on cmp f) a b = cmp (f a) (f b)`

```
let compareLength = on (\a b -> a - b) (\s -> length s) in
compareLength [1,2,3] [1,2]   # => 1
```

### `apply : a -> (a -> b) -> b`

Applies a function to a value (flip of function application).

```
apply 42 (\x -> x + 1)   # => 43
```

### `pipe : a -> [(a -> a)] -> a`

Pipes a value through a chain of functions (left to right).
`pipe x [f, g, h] = h (g (f x))`

```
let fns = [\x -> x + 1, \x -> x * 2, \x -> x - 3] in
pipe 5 fns   # => 9  ((5+1)*2-3 = 9)
```

### `fix : ((a -> b) -> (a -> b)) -> a -> b`

Fixed-point combinator for anonymous unary recursion. The callback receives
the recursive function as its first argument.

```
let factorial = fix (\self n -> if n <= 1 then 1 else n * (self (n - 1))) in
factorial 5   # => 120
```
