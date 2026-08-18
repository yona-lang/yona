# Refinement Types

**Status (2026-08-18):** **partial.** `{ n : Int | pred }` parses. **E0500**
covers `head`/`tail` nonempty and `/` nonzero in `RefinementChecker`
(non-blocking; skipped on modules). Signature aliases (`NonEmpty`, `Port`,
`NonZero`) are **syntax only** — not enforced. No `.yonai` predicates.
Evidence: [type-system-status.md](type-system-status.md) §5.

## Overview

Refinement types extend a base type with a compile-time predicate that constrains its values. A value of type `{ n : Int | n > 0 }` is an `Int` that is guaranteed to be positive. The compiler verifies the predicate at call sites — no runtime check is needed.

```yona
type NonEmpty a = { xs : [a] | length xs > 0 }
type Port      = { n : Int  | n > 0 && n < 65536 }
type NonZero   = { n : Int  | n != 0 }
```

## Built-in Refinement Checks

The compiler automatically checks these common safety properties without any type annotations:

### Non-empty sequences

`head` and `tail` require a non-empty sequence. The compiler emits `[E0500]` if it can't prove the argument is non-empty:

```yona
head someList           -- Error: 'someList' may be empty [E0500]

let xs = [1, 2, 3] in
head xs                 -- OK: literal sequence is non-empty

let xs = 1 :: rest in
head xs                 -- OK: cons always produces non-empty

case someList of
    [h|t] -> head someList  -- OK: [h|t] pattern proves non-empty
    []    -> default
end
```

### Division by zero

Division (`/`) requires a non-zero divisor:

```yona
let d = 0 in
100 / d                 -- Error: 'd' may be zero [E0500]

let d = 5 in
100 / d                 -- OK: d == 5, which is != 0

case n of
    0 -> 0
    x -> 100 / x        -- OK: wildcard after 0 excludes zero
end
```

## How Facts Are Established

The compiler uses **flow-sensitive analysis** to track facts at each program point.

### Integer Literals

```yona
let port = 8080 in
-- Fact: port in [8080, 8080]
-- Satisfies: port > 0, port < 65536, port != 0
```

### Pattern Matching

```yona
case xs of
    [h|t] -> ...  -- Fact: xs is non-empty
    []    -> ...  -- Fact: xs is empty
end

case n of
    0 -> ...      -- Fact: n == 0
    1 -> ...      -- Fact: n == 1
    x -> ...      -- Fact: n != 0, n != 1 (wildcard complement)
end
```

### Wildcard Complement Narrowing

After matching specific integer literals, a wildcard `_` or identifier pattern in a later clause knows the scrutinee excludes those values:

```yona
case n of
    0 -> "zero"
    _ -> 100 / n   -- OK: n != 0 (0 was handled above)
end
```

### Guard Expressions

Guard conditions narrow facts in the guarded body:

```yona
case x of
    n | n > 0 -> 100 / n   -- OK: guard proves n > 0, so n != 0
end
```

### If Conditions

All six comparison operators narrow facts in then/else branches:

```yona
if x > 0 then
    -- Fact: x > 0 (i.e., x >= 1)
    100 / x    -- OK
else
    -- Fact: x <= 0
    0
```

```yona
if x != 0 then
    -- Fact: x != 0 (via excluded values)
    100 / x    -- OK
else
    -- Fact: x == 0
    0
```

Supported operators: `>`, `<`, `>=`, `<=`, `==`, `!=`. Compound conditions with `&&` narrow both sides.

### Arithmetic Propagation

The compiler tracks intervals through addition and subtraction:

```yona
let x = 5 in         -- Fact: x in [5, 5]
let y = x + 1 in     -- Fact: y in [6, 6]
100 / y               -- OK: y != 0

let a = 10 in         -- Fact: a in [10, 10]
let b = a - 3 in      -- Fact: b in [7, 7]
```

### Cons and Sequence Literals

```yona
let xs = [1, 2, 3] in  -- non-empty (3 elements)
let ys = 1 :: rest in   -- non-empty (cons always produces non-empty)
```

### Variable Aliasing

Facts transfer through simple aliases:

```yona
let x = 42 in
let y = x in           -- y inherits x's interval [42, 42]
```

## Syntax for Refined Types

### Declaring

```yona
type Name = { var : BaseType | predicate }
```

The predicate language supports:

| Operator | Example |
|----------|---------|
| `>`, `<`, `>=`, `<=`, `==`, `!=` | `n > 0`, `n != 0` |
| `length var > N` | `length xs > 0` |
| `&&`, `\|\|` | `n > 0 && n < 65536` |

### Using in Function Signatures

```yona
head : NonEmpty a -> a
safeDivide : Int -> NonZero -> Int
connect : String -> Port -> Socket
```

## Architecture

### Zero-Cost Erasure

Refinement types are erased at codegen. A `NonEmpty a` compiles to identical LLVM IR as `[a]`. No boxing, no tagging, no runtime overhead.

### Fact Environment

At each program point, the checker maintains:

- **Integer intervals**: `x in [lo, hi]` — bounds for integer variables
- **Non-empty sets**: which sequence variables are known non-empty
- **Excluded values**: which specific integer values are known to be impossible

### Decision Procedures

The checker uses simple, always-decidable procedures:
- **Interval arithmetic** for integer bounds (+, -)
- **Set membership** for non-emptiness and excluded values
- **Propositional logic** for `&&`/`||` combinators

No SMT solver needed — compilation is always fast.

## Error Messages

```
file.yona:10:5: error: 'head' requires a non-empty sequence, but 'xs' may be empty;
  use a [h|t] pattern match to prove non-emptiness [E0500]
   10 | head xs
      | ^~~~

file.yona:15:10: error: division requires a non-zero divisor, but 'd' may be zero [E0500]
   15 | 100 / d
      |       ^
```

Use `yonac --explain E0500` for detailed help with examples.

## Limitations

- No function calls in predicates (keeps it decidable)
- No dependent types (predicates can't reference other variables like `x < y`)
- No quantifiers (`forall`, `exists`)
- No recursive predicates (`sorted xs`, `balanced tree`)
- Length tracking limited to non-emptiness (`length > 0`), not exact counts
- Multiplication not tracked (only `+` and `-`)
- Interprocedural: facts don't flow across function boundaries (by design)
