# Std.Test

Simple test assertions — returns `(:pass, name)` or `(:fail, message)`.

Each assertion returns a symbol-tagged tuple so test runners can
collect results programmatically.

## Functions

### assertEqual

`assertEqual expected actual`

Asserts that `expected` equals `actual`.

```
assertEqual 42 42   # => (:pass, "assertEqual")
assertEqual 1 2     # => (:fail, "assertEqual: expected equal values")
```

### assertNotEqual

`assertNotEqual expected actual`

Asserts that `expected` does not equal `actual`.

```
assertNotEqual 1 2   # => (:pass, "assertNotEqual")
```

### assertTrue

`assertTrue value`

Asserts that `value` is true.

```
assertTrue (1 > 0)   # => (:pass, "assertTrue")
```

### assertFalse

`assertFalse value`

Asserts that `value` is false.

```
assertFalse (1 > 2)   # => (:pass, "assertFalse")
```

### assertGreater

`assertGreater a b`

Asserts that `a > b`.

```
assertGreater 5 3   # => (:pass, "assertGreater")
```

### assertLess

`assertLess a b`

Asserts that `a < b`.

```
assertLess 3 5   # => (:pass, "assertLess")
```
