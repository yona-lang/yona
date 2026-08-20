# Yona Style Guide

## Let Bindings

**Don't nest let expressions unnecessarily.** Yona's `let` supports multiple bindings with commas:

```yona
-- Bad: unnecessary nesting
let x = 42 in
let y = x + 1 in
x + y

-- Good: flat multi-binding
let x = 42, y = x + 1 in x + y
```

**Use multi-binding let for independent computations.** Independent bindings run in parallel automatically:

```yona
-- These run concurrently — no async/await needed
let
    a = readFile "foo.txt",
    b = readFile "bar.txt",
    c = httpGet url
in a ++ b ++ c
```

## Do Blocks

**`let` and `do` have different semantics.** `let` binds values —
independent right-hand sides may run in parallel. `do` sequences effects
strictly top to bottom. Combining them is valid when you want both
(parallel bindings, then ordered steps):

```yona
# Good — two reads in flight, then ordered writes
let
    a = readFile "foo.txt",
    b = readFile "bar.txt"
in do
    writeFile "out-a.txt" (process a)
    writeFile "out-b.txt" (process b)
end
```

The anti-pattern is using `let` as a sequencer for an unused effect:

```yona
# Bad — discard-binding to force an effect
let _ = writeFile "out.txt" data in data

# Good — do block for side effects
do
    writeFile "out.txt" data
    data
end
```

**Do blocks support assignments:**

```yona
do
    content = readFile "input.txt"
    result = process content
    writeFile "output.txt" result
    result
end
```

## Imports

**Use comma-separated imports, not nested import expressions:**

```yona
-- Bad: nesting imports
import length from Std\String in
import print from Std\IO in
print (length "hello")

-- Good: comma-separated
import length from Std\String, print from Std\IO in
print (length "hello")
```

## Pattern Matching

**Prefer pattern matching over if-else chains:**

```yona
-- Bad: if-else chain
if x == 0 then "zero" else if x == 1 then "one" else "other"

-- Good: case expression
case x of
    0 -> "zero"
    1 -> "one"
    _ -> "other"
end
```

**Use head-tail patterns for sequence processing:**

```yona
case xs of
    []    -> 0
    [h|t] -> h + sum t
end
```

## Records

**Use anonymous records for structured data:**

```yona
let config = { host = "localhost", port = 8080, debug = true } in
config.host
```

## Prelude Types

**Use prelude types without imports** — `Option`, `Result`, `Linear` are always available:

```yona
case Some 42 of
    Some x -> x
    None   -> 0
end
```

## Resource Management

**Use `with` for automatic resource cleanup:**

```yona
with open "data.txt" as f in
    read f
end
-- f is automatically closed
```

**Use `Linear` for explicit resource tracking:**

```yona
let conn = tcpConnect "host" 8080 in
-- LinearityChecker warns if conn is never consumed
case conn of
    Linear fd -> do; send fd "hello"; close fd; end
end
```

## Parallel Computation

**Use parallel comprehensions for concurrent map:**

```yona
[| processItem item for item = items ]
```

**Use multi-binding let for concurrent I/O:**

```yona
let a = readFile "a.txt", b = readFile "b.txt" in
a ++ b
```

## Effects

**Use algebraic effects for composable side effects:**

```yona
handle
    perform Log.log "starting"
    result = compute ()
    perform Log.log "done"
    result
with
    Log.log msg resume -> do; appendFile "app.log" msg; resume (); end
    return val -> val
end
```

## Naming Conventions

- **Functions**: camelCase (`readFile`, `processItem`)
- **Types**: PascalCase (`Option`, `Result`, `Person`)
- **Constructors**: PascalCase (`Some`, `None`, `Ok`, `Err`)
- **Modules**: PascalCase with backslash separator (`Std\String`, `Std\IO`)
- **Symbols**: lowercase with colon prefix (`:ok`, `:error`, `:none`)
- **Type variables**: single lowercase letters (`a`, `b`, `e`)
