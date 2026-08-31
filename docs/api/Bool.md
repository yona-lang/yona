# Std.Bool

Boolean combinators and conditional helpers.

Provides logical operations beyond the built-in `&&` and `||` operators,
plus conditional execution helpers.

## Functions

### `not : Bool -> Bool`

Logical negation.

```
not true    # => false
not false   # => true
```

### `and : Bool -> Bool -> Bool`

Logical AND (short-circuiting).

```
and true true    # => true
and true false   # => false
```

### `or : Bool -> Bool -> Bool`

Logical OR (short-circuiting).

```
or false true    # => true
or false false   # => false
```

### `xor : Bool -> Bool -> Bool`

Exclusive OR — true when exactly one argument is true.

```
xor true false   # => true
xor true true    # => false
```

### `implies : Bool -> Bool -> Bool`

Logical implication: `a → b`. False only when `a` is true and `b` is false.

```
implies true true    # => true
implies true false   # => false
implies false true   # => true
```

### `when : Bool -> (() -> Symbol) -> Symbol`

Executes `fn ()` if `cond` is true, otherwise returns `:ok`.

```
when true (\-> :done)    # => :done
when false (\-> :done)   # => :ok
```

### `unless : Bool -> (() -> Symbol) -> Symbol`

Executes `fn ()` if `cond` is false, otherwise returns `:ok`.

```
unless false (\-> :done)   # => :done
unless true (\-> :done)    # => :ok
```
