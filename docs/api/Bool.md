# Std.Bool

Boolean combinators and conditional helpers.

Provides logical operations beyond the built-in `&&` and `||` operators,
plus conditional execution helpers.

## Functions

### not

`not : Int -> Bool`

Logical negation.

```
not true    # => false
not false   # => true
```

### and

`and : Int -> Int -> Int`

Logical AND (short-circuiting).

```
and true true    # => true
and true false   # => false
```

### or

`or : Int -> Int -> Bool`

Logical OR (short-circuiting).

```
or false true    # => true
or false false   # => false
```

### xor

`xor : Int -> Int -> Bool`

Exclusive OR — true when exactly one argument is true.

```
xor true false   # => true
xor true true    # => false
```

### implies

`implies : Int -> Int -> Int`

Logical implication: `a → b`. False only when `a` is true and `b` is false.

```
implies true true    # => true
implies true false   # => false
implies false true   # => true
```

### when

`when : Int -> (a -> b) -> Int`

Executes `fn ()` if `cond` is true, otherwise returns `:ok`.

```
when true (\-> 42)    # => 42
when false (\-> 42)   # => :ok
```

### unless

`unless : Int -> (a -> b) -> Symbol`

Executes `fn ()` if `cond` is false, otherwise returns `:ok`.

```
unless false (\-> 42)   # => 42
unless true (\-> 42)    # => :ok
```
