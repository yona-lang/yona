# Yona Compiler Error Codes

Every compiler error includes a code like `[E0100]`. Use `yonac --explain E0100` to see a detailed explanation with examples.

## Type Errors (E01xx)

### E0100 — Type mismatch

Two types that should be compatible are not.

```yona
-- Error: Int and String cannot be unified
1 + "hello"

-- Fix: ensure both operands have the same type
1 + 2
```

Common causes:
- Operator applied to incompatible types (`Int + String`)
- If branches return different types (`if true then 1 else "no"`)
- Function called with wrong argument type
- Sequence with mixed element types (`[1, "two", 3]`)

### E0101 — Infinite type

A type variable would need to contain itself, creating an infinite type. This happens when an expression's type depends on itself in a circular way (occurs check failure).

```yona
-- Error: f is applied to itself
let f x = f in f
```

### E0102 — Tuple size mismatch

A tuple pattern has a different number of elements than the tuple being matched.

```yona
-- Error: 3-tuple matched against 2-tuple pattern
case (1, 2, 3) of (a, b) -> a end

-- Fix: match all elements
case (1, 2, 3) of (a, b, c) -> a end
```

### E0103 — Undefined variable

A variable is used but not defined in the current scope. The compiler will suggest similar names if a close match exists.

```
error: undefined variable 'lenght'; did you mean 'length'? [E0103]
```

```yona
-- Variables are only visible within their defining scope
let x = 42 in x  -- OK
x                 -- Error: x is not in scope
```

### E0104 — Undefined function

A function is called but has not been defined or imported. Check for typos or missing imports.

```
error: undefined function 'prnt'; did you mean 'print'? [E0104]
```

### E0105 — No trait instance

A trait method is called on a type that doesn't implement the trait.

```yona
-- Error: no instance for 'Num String'
abs "hello"

-- Fix: use a type that has a Num instance
abs (-42)
```

### E0106 — Missing trait instances

A trait is used but no instances have been registered for it at all. This usually means the trait definition is missing or not imported.

## Effect Errors (E02xx)

### E0200 — Unhandled effect operation

A `perform` calls an effect operation, but no `handle...with` block in scope provides a handler.

```yona
-- Error: no handler for State.get
perform State.get ()

-- Fix: wrap in a handle block
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
-- Effect declares: put : s -> ()
-- Error: put expects 1 argument, got 0
perform State.put

-- Fix: pass the required argument
perform State.put 42
```

### E0202 — Unhandled effect at call site

A function whose type includes latent effects (`!{Effect.op}`) is applied
where those operations are not covered by a surrounding `handle ... with`.
The primary diagnostic points at the introducing `perform`; a note marks
the call that lets the effect escape.

```yona
-- f : a -> !{State.get} Int
let f = (\x -> perform State.get ()) in
f 0   -- Error: points at `perform State.get`

-- Fix: handle the effect at the use site
handle f 0 with
    State.get () resume -> resume 7
end
```

Direct `perform` without a handler still warns via `-Wunhandled-effect`.

### E0203 — Effect-freedom requirement not satisfied

`yonac --require-effect-free` requires a closed empty effect row, exhaustive
registered finite-ADT and `Bool` cases, and a conservative termination proof.
Known operations, open row variables, imports without row facts, missing
alternatives, unproven direct recursion, and mutual-recursion cycles are
rejected; `.yonai` records a proven empty export row as `effects -`. A direct
self-call is accepted only when it receives a variable destructured from an
unguarded constructor arm (or a lexical alias of that variable). Ordinary
compilation remains non-fatal: `--Wincomplete-patterns` warns about missing
alternatives. The gate does not prove general termination or arbitrary non-ADT
coverage.

## Parse Errors (E03xx)

### E0300 — Unexpected token

The parser encountered a token that doesn't fit the expected syntax. Common causes:
- Missing closing bracket, paren, or `end` keyword
- Extra comma or semicolon
- Reserved word used as identifier

### E0301 — Invalid syntax

The source code doesn't match any valid Yona syntax. Check expression structure and keyword spelling.

### E0302 — Invalid pattern

A pattern in a case expression or function parameter is malformed.

Valid patterns:
```yona
42              -- integer literal
"hello"         -- string literal
:ok             -- symbol
x               -- variable binding
_               -- wildcard
(a, b)          -- tuple
[h|t]           -- head-tail (list)
[]              -- empty list
Some x          -- constructor
(n : Int)       -- typed (sum type)
p1 | p2         -- or-pattern
```

## Codegen Errors (E04xx)

### E0400 — Failed to emit object file

LLVM could not produce an object file. This is usually an internal compiler error.

### E0401 — Linking failed

The system linker (`cc`) failed to produce an executable. Common causes:
- Missing runtime library (`compiled_runtime.o`)
- Undefined symbols from missing module imports
- System linker not installed

### E0402 — Unsupported expression

The codegen encountered an AST node it cannot compile. This may indicate a language feature that is not yet implemented.

### E0403 — Unknown field

A field access or update refers to a field name that doesn't exist on the ADT.

```yona
type Person = Person { name : String, age : Int }

p.email  -- Error: 'email' is not a field of Person
p.name   -- OK
```

### E0404 — Pipe requires function

The pipe operator (`|>` or `<|`) requires a function on the receiving side.

```yona
-- Error: 42 is not a function
"hello" |> 42

-- Fix: pipe into a function
"hello" |> length
```

## Warning Flags

Warnings are controlled via `-Wall`, `-Wextra`, `-w`, and `--Werror`.
`--Wno-refinement` skips E0500; `--Wno-linear` skips E0600/E0601/E0602;
`--Wno-linear-leak` disables only E0602.

| Flag | Name | `-Wall` | `-Wextra` |
|------|------|---------|-----------|
| `-Wunused-variable` | Unused variable binding | yes | yes |
| `--Wincomplete-patterns` | Non-exhaustive finite-ADT or `Bool` pattern match | yes | yes |
| `--Woverlapping-patterns` | Definitely unreachable arm after an earlier unguarded arm | yes | yes |
| `-Wunhandled-effect` | `perform` without matching `handle` | yes | yes |
| `-Wlinear-leak` | Unconsumed `Linear` value at scope exit (**E0602**; on by default) | yes | yes |
| `-Wshadow` | Variable shadowing | no | yes |
| `-Wmissing-signature` | Function without type annotation | no | yes |
| `-Wunused-import` | Imported name not used | no | yes |
