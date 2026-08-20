# Algebraic Effects in Yona

**Status (2026-08-19):** **partial.** `perform` / `handle` parse, typecheck, and
compile as in-scope handler dispatch. Effect rows unify (closed + open rest);
HOF and wrapping propagate; `handle` subtracts; unhandled apply is **E0202**.
Closed `.yonai` `FN … effects` rows and open `effects | hof` are written
and restored on import. Sibling exports inherit applied helpers' rows.
`effect` declarations and CPS resume are **not**. Evidence:
[type-system-status.md](type-system-status.md) §1–2.
Rows: GitHub [#8](https://github.com/yona-lang/yona/issues/79).

## The Problem

In most languages, side effects are invisible. A function `processOrder(order)` might read from a database, write to a log, send an email, throw an exception, or modify global state — and you can't tell from its signature.

This creates three practical problems:

1. **Testing**: You need mock frameworks, dependency injection containers, and interface hierarchies just to test a function that reads from a database.

2. **Composition**: Error handling via try/catch doesn't compose. Logging is a cross-cutting concern that pollutes every function signature. State must be threaded manually or hidden in globals.

3. **Flexibility**: If your function hardcodes `println` for logging, you can't switch to file logging, structured logging, or silent logging without changing the function.

Haskell addresses this with monads, but monad transformer stacks are verbose and don't compose well. Go uses interfaces, but they don't track which effects a function performs. Rust uses traits and generics, but requires manual plumbing.

## The Solution: Typed, Composable Effects

Yona's algebraic effect system makes side effects explicit, composable, and handleable:

```yona
-- Declare what operations an effect provides
effect Database
    query : String -> Seq
    insert : String -> Seq -> Bool
end

-- Functions perform effect operations (like calling an interface method)
processOrder order =
    let items = perform Database.query order.id in
    do
        perform Log.log ("Processing " ++ order.id)
        perform Database.insert "results" items
    end

-- The CALLER decides how effects are handled
handle processOrder myOrder with
    Database.query sql resume -> resume (real_db_query sql)
    Database.insert t d resume -> resume (real_db_insert t d)
    Log.log msg resume -> do; appendFile "app.log" msg; resume (); end
    return val -> val
end
```

The same function works with different handlers — production, testing, staging — without any code changes.

## Syntax

### Declaring Effects

```yona
effect State s
    get : () -> s
    put : s -> ()
end

effect Console
    readLine : () -> String
    print : String -> ()
end

effect Failure e
    fail : e -> a
end
```

An effect declares a set of operations with typed signatures. The type parameter (`s`, `e`) is instantiated when the effect is handled.

### Performing Operations

```yona
let count = perform State.get () in
perform State.put (count + 1)
perform Console.print "hello"
```

`perform` calls an effect operation. It suspends the current computation until a handler provides the result via `resume`.

### Handling Effects

```yona
handle <body> with
    Effect.operation args resume -> <handler body>
    ...
    return val -> <return body>
end
```

- **Handler clauses**: Match effect operations by name. `resume` is the continuation — calling `resume value` returns `value` to the `perform` site and continues execution.
- **Return clause**: Transforms the final result of the body. Optional (defaults to identity).

## Examples

### Testing Without Mocks

```yona
effect Database
    query : String -> Seq
end

getUsers = perform Database.query "SELECT * FROM users"

-- Production
handle getUsers with
    Database.query sql resume -> resume (postgresql_query conn sql)
    return val -> val
end

-- Test: no database needed
handle getUsers with
    Database.query sql resume -> resume [("alice", 30), ("bob", 25)]
    return val -> val
end
```

### Error Handling That Composes

```yona
effect Failure
    fail : String -> a
end

parseConfig path =
    let content = readFile path in
    case validateJson content of
        :error -> perform Failure.fail ("Invalid config: " ++ path)
        result -> result
    end

-- Strategy 1: abort with default
handle parseConfig "config.yaml" with
    Failure.fail msg resume -> defaultConfig
    return val -> val
end

-- Strategy 2: collect errors and continue
handle parseConfig "config.yaml" with
    Failure.fail msg resume ->
        do; logError msg; resume defaultValue; end
    return val -> val
end
```

The same `parseConfig` works with "abort", "default", or "collect errors" — decided by the caller.

### Pure State

```yona
effect State s
    get : () -> s
    put : s -> ()
end

counter =
    let n = perform State.get () in
    do
        perform State.put (n + 1)
        perform State.get ()
    end

-- Run with initial state 0
handle counter with
    State.get () resume -> resume 42
    State.put s resume -> resume ()
    return val -> val
end
-- Result: 42
```

Functions use "stateful" operations without any actual mutation. The handler provides the behavior.

### Logging Without Pollution

```yona
effect Log
    log : String -> ()
end

processPayment amount =
    do
        perform Log.log "payment_start"
        result = chargeCard amount
        perform Log.log ("charged: " ++ show amount)
        result
    end

-- Production: file logging
handle processPayment 100 with
    Log.log msg resume -> do; appendFile "app.log" msg; resume (); end
    return val -> val
end

-- Test: silent
handle processPayment 100 with
    Log.log msg resume -> resume ()
    return val -> val
end
```

### Nested Handlers

Inner handlers shadow outer handlers for the same effect:

```yona
handle
    handle perform State.get () with
        State.get () resume -> resume 99
        return val -> val
    end
with
    State.get () resume -> resume 0
    return val -> val
end
-- Result: 99 (inner handler wins)
```

## How It Works

Yona uses **continuation-passing style (CPS) transformation** at compile time. No runtime stack manipulation, no garbage collector, no overhead for code that doesn't use effects.

When you write:
```yona
let x = perform State.get () in
x + 1
```

The compiler transforms this into:
```yona
handler_get (\x -> x + 1)
```

The "rest of the computation" (`x + 1`) becomes a closure passed to the handler. The handler calls this closure with `resume value` to continue execution.

### Key Properties

- **Zero cost when not used**: No effect system overhead in code that doesn't use `perform`
- **One-shot continuations**: Calling `resume` exactly once is the common case and has zero overhead beyond a closure call
- **Reference counted**: Continuation closures participate in Yona's RC system — no leaks, no GC needed
- **Compiled to native**: Effects compile to direct closure calls via LLVM, not interpreted

## Comparison

| Language | Approach | Trade-off |
|----------|----------|-----------|
| **Yona** | Native algebraic effects via CPS | Zero-cost one-shot continuations, compiled to LLVM |
| Haskell | Monads / MTL | Transformer stacks, lifting boilerplate |
| OCaml 5 | Algebraic effects | Runtime (not ahead-of-time compiled) |
| Koka | Algebraic effects | Compiles to C (not LLVM), smaller ecosystem |
| Scala ZIO | Effect types as library | JVM overhead, not native |
| Rust | Traits + generics | Manual plumbing, no handler composition |
| Go | Interfaces | No effect tracking, no continuations |

## Module System

Effects can be exported from modules:

```yona
module Std\Effects

export effect State
export effect Failure

effect State s
    get : () -> s
    put : s -> ()
end

effect Failure e
    fail : e -> a
end
```

Import and use:
```yona
import effect State from Std\Effects in
handle perform State.get () with
    State.get () resume -> resume 42
    return val -> val
end
```
