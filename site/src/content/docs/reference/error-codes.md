---
title: Error codes
description: Catalog of Yona compiler error codes with examples, fixes, and warning flags.
---

Every compiler error includes a code like `[E0100]`. Run `yonac --explain E0100` to see a detailed explanation with examples for any code on this page. See the [Compiler CLI](/reference/cli/) reference for warning-control flags.

## Type errors (E01xx)

### E0100 — Type mismatch

Two types that should be compatible are not.

```yona
# Error: Int and String cannot be unified
1 + "hello"

# Fix: ensure both operands have the same type
1 + 2
```

Common causes:

- Operator applied to incompatible types (`Int + String`)
- If branches return different types (`if true then 1 else "no"`)
- Function called with the wrong argument type
- Sequence with mixed element types (`[1, "two", 3]`)

Constructor diagnostics include the constructor name, declared field shape,
and the specific field mismatch. A tuple annotation is one constructor field,
so preserve that nesting in the pattern:

```yona
type Box = Box (Int, Int)

# Error: two constructor fields were written for one tuple field
case Box (1, 2) of Box (first, second) -> first end

# Fix: match the single field with a tuple subpattern
case Box (1, 2) of Box ((first, second)) -> first end
```

### E0101 — Infinite type

A type variable would need to contain itself (occurs check failure). This happens when an expression's type depends on itself circularly.

```yona
# Error: f's type would contain itself
let f x = f in f
```

**Fix:** break the self-reference; a function cannot be its own return type.

### E0102 — Tuple size mismatch

A tuple pattern has a different number of elements than the tuple being matched.

```yona
# Error: 3-tuple matched against 2-tuple pattern
case (1, 2, 3) of (a, b) -> a end

# Fix: match all elements
case (1, 2, 3) of (a, b, c) -> a end
```

### E0103 — Undefined variable

A variable is used but not defined in the current scope. The compiler suggests similar names when a close match exists:

```
error: undefined variable 'lenght'; did you mean 'length'? [E0103]
```

```yona
# Variables are only visible within their defining scope
let x = 42 in x   # OK
x                 # Error: x is not in scope
```

**Fix:** correct the spelling or bind the variable before use.

### E0104 — Undefined function

A function is called but has not been defined or imported.

```
error: undefined function 'prnt'; did you mean 'print'? [E0104]
```

**Fix:** correct the typo or add the missing import.

### E0105 — No trait instance

A trait method is called on a type that doesn't implement the trait.

```yona
# Error: no instance for 'Num String'
abs "hello"

# Fix: use a type that has a Num instance
abs (-42)
```

### E0106 — Missing trait instances

A trait is used but no instances have been registered for it at all. This usually means the trait definition is missing or not imported.

**Fix:** define or import the trait and at least one instance.

## Effect errors (E02xx)

See [effects](/learn/effects/) for the effect system itself.

### E0200 — Unhandled effect operation

A `perform` calls an effect operation, but no `handle ... with` block in scope provides a handler.

```yona
# Error: no handler for State.get
perform State.get ()

# Fix: wrap in a handle block
handle
    perform State.get ()
with
    State.get () resume -> resume 42
    return val -> val
end
```

### E0201 — Effect argument count mismatch

A `perform` call passes the wrong number of arguments to an effect operation.

```yona
# Effect declares: put : s -> ()
# Error: put expects 1 argument, got 0
perform State.put

# Fix: pass the required argument
perform State.put 42
```

### E0202 — Unhandled effect at call site

A function whose type includes latent effects (`!{Effect.op}`) is applied where those operations are not covered by a surrounding `handle ... with`. The primary diagnostic points at the introducing `perform`; a note marks the call that lets the effect escape.

```yona
# f : a -> !{State.get} Int
let f = (\x -> perform State.get ()) in
f 0   # Error: points at `perform State.get`

# Fix: handle the effect at the use site
handle f 0 with
    State.get () resume -> resume 7
end
```

A direct `perform` without a handler still warns via `-Wunhandled-effect`.

### E0203 — Effect-freedom requirement not satisfied

`yonac --require-effect-free` accepts only a closed empty effect row, exhaustive
registered finite-ADT and `Bool` `case` expressions, and direct recursion with
a conservative structural-descent proof. Known operations, open row variables,
imports without row facts, missing alternatives, unproven direct recursion, and
mutual recursion are rejected; a proven empty exported row is recorded in
`.yonai` as `effects -`. Ordinary compilation remains non-fatal:
`--Wincomplete-patterns` warns about missing alternatives. The flag does not
prove general termination or arbitrary non-ADT coverage.

## Parse errors (E03xx)

### E0300 — Unexpected token

The parser encountered a token that doesn't fit the expected syntax. Common causes:
- Missing closing bracket, paren, or `end` keyword
- Extra comma or semicolon
- Reserved word used as an identifier

### E0301 — Invalid syntax

The source code doesn't match any valid Yona syntax. Check expression structure and keyword spelling against the [language specification](/reference/specification/).

### E0302 — Invalid pattern

A pattern in a case expression or function parameter is malformed. Valid pattern forms:

```yona
42              # integer literal
"hello"         # string literal
:ok             # symbol
x               # variable binding
_               # wildcard
(a, b)          # tuple
[h|t]           # head-tail (list)
[]              # empty list
Some x          # constructor
(n : Int)       # typed (sum type)
p1 | p2         # or-pattern
```

## Codegen errors (E04xx)

### E0400 — Failed to emit object file

LLVM could not produce an object file. This is usually an internal compiler error.

### E0401 — Linking failed

The system linker failed to produce an executable. Common causes:
- Missing runtime library (`compiled_runtime.o`)
- Undefined symbols from missing module imports
- System linker not installed

**Fix:** check that the toolchain is installed and imported modules have been compiled; see the [Compiler CLI](/reference/cli/) reference for `YONAC_CC` and `--linker-mode`.

### E0402 — Unsupported expression

The codegen encountered an AST node it cannot compile. This may indicate a language feature that is not yet implemented.

### E0403 — Unknown field

A field access or update refers to a field name that doesn't exist on the ADT.

```yona
type Person = Person { name : String, age : Int }

p.email  # Error: 'email' is not a field of Person
p.name   # OK
```

### E0404 — Pipe requires function

The pipe operator (`|>` or `<|`) requires a function on the receiving side.

```yona
# Error: 42 is not a function
"hello" |> 42

# Fix: pipe into a function
"hello" |> length
```

## Refinement errors (E05xx)

### E0500 — Refinement predicate not satisfied

A function expects a refined type, but the compiler cannot prove that the argument satisfies the refinement predicate.

```yona
type NonEmpty a = { xs : [a] | length xs > 0 }
head : NonEmpty a -> a

# Error: cannot prove 'someList' is non-empty
head someList

# Fix: establish the fact via pattern matching
case someList of
    [h|t] -> head someList   # OK: [h|t] proves non-empty
    []    -> defaultValue
end
```

Passing a literal that obviously satisfies the predicate (e.g. `head [1, 2, 3]`) also works, as does a pattern match or comparison that proves an integer refinement such as `{ n : Int | n > 0 && n < 65536 }`.

## Linearity errors (E06xx)

These are produced by the linearity checker for `Linear` values (see the [Prelude](/reference/prelude/)).

### E0600 — Use after consume

A linear value was used after it was already consumed by a pattern match or function call.

```yona
let conn = Linear (tcpConnect host port) in
case conn of Linear fd -> close fd end   # conn consumed
send conn "hello"                        # Error: already consumed
```

**Fix:** use the value before consuming it — do all work inside the case arm that unwraps it.

### E0601 — Branch inconsistency

A linear value is consumed in one branch of an if/case expression but not the other. Both branches must consume the same linear values.

```yona
# Error: conn consumed in then-branch but not else-branch
if ready then
    case conn of Linear fd -> close fd end
else
    ()   # conn still live here
```

**Fix:** consume the value in every branch (e.g. close it in the else branch too).

### E0602 — Resource leak

A linear value went out of scope without being consumed. This likely means a resource (file, socket, process) is leaked. Emitted as **E0602** under `-Wlinear-leak` (on by default; `--Wno-linear-leak` suppresses; `--Werror` promotes it).

```yona
let conn = Linear (tcpConnect host port) in
42   # Warning E0602: conn never consumed
```

**Fix:** consume the value via pattern match before the end of its scope.

### E0603 — Invalid `@borrow`

`@borrow` marks a parameter as read-only for the function body: it must not be returned, stored in a collection literal, captured by a nested lambda, or used as a case scrutinee (head/tail consumes the sequence). It is only supported on simple identifier parameters.

```yona
# Error: borrowed parameter is returned
let f @borrow s = s in f
```

**Fix:** remove `@borrow`, or change the body so the parameter is only read.

## Accelerator errors (E07xx)

### E0700 — Unlowerable accelerator lambda

Only reported under `yonac --strict-accelerator`, which requires `Std\IntArray` / `Std\FloatArray` `map` / `filter` / `foldl` lambdas to match the fixed [Std\GPU](/stdlib/gpu/) kernel library (`x + k`, `x * k`, `x > k`, sum, float scale). Arbitrary lambdas such as `\x -> x * x` are not compiled to SPIR-V; without the flag they stay on the correct host closure path, while with it they are a hard error so GPU expectations cannot silently diverge from the fixed-kernel ABI.

**Fix:** rewrite the lambda as a fixed kernel (e.g. `map (\x -> x + 1)`, explicit `mapGPU`), or drop `--strict-accelerator` to keep the host path.

## Warning flags

Warnings are controlled via `--Wall`, `--Wextra`, `-w`, and `--Werror` (see the [Compiler CLI](/reference/cli/)).

| Flag | Name | `--Wall` | `--Wextra` |
|------|------|----------|------------|
| `-Wunused-variable` | Unused variable binding | yes | yes |
| `--Wincomplete-patterns` | Non-exhaustive finite-ADT or `Bool` pattern match | yes | yes |
| `--Woverlapping-patterns` | Definitely unreachable arm after an earlier unguarded arm | yes | yes |
| `-Wunhandled-effect` | `perform` without matching `handle` | yes | yes |
| `-Wlinear-leak` | Unconsumed `Linear` value at scope exit (`E0602`; on by default) | yes | yes |
| `-Wshadow` | Variable shadowing | no | yes |
| `-Wmissing-signature` | Function without type annotation | no | yes |
| `-Wunused-import` | Imported name not used | no | yes |
