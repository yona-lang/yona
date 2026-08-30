# Linear Types

**Status (2026-08-24):** **partial.** `Linear` is a Prelude ADT. Use-after-consume
is **E0600** via `LinearityChecker` (`yonac` exits non-zero on expressions and
modules). **E0602** resource leaks use `-Wlinear-leak` (on by default; not
`-Wunhandled-effect`). Not an HM type. Producers are **type-directed** (`Linear _` or a product of
Linear) — there is no C++ name allowlist. `.yonai` may mark FN/IO/AFN returns
as `LINEAR` (CType stays INT/TUPLE). `WithExpr` and `FunctionExpr` bodies are
walked: `with` binds then discharges the resource via Closeable; lambdas /
local fns get a fresh linear scope (including Linear parameters when the
zonked arrow type is known). Evidence: [type-system-status.md](type-system-status.md) §4.

## Overview

Linear types ensure resource handles (files, sockets, processes) are consumed exactly once — preventing use-after-close and resource leaks at compile time.

In Yona, `Linear` is a regular ADT:

```yona
type Linear a = Linear a
```

Wrapping a value in `Linear` creates a compile-time obligation. The only way to extract the inner value is pattern matching, which is the consumption point:

```yona
let conn = Linear (tcpConnect "localhost" 8080) in
case conn of
    Linear fd ->
        let data = recv fd 1024 in
        do; close fd; data; end
end
```

## Why an ADT?

Instead of a special type system construct, `Linear` is a plain ADT because:

- **Uses existing infrastructure** — parser, codegen, pattern matching all work unchanged
- **Pattern matching is the natural consumption mechanism** — `case x of Linear fd -> ...` unwraps and consumes in one step
- **First-class** — Linear values can be stored, returned, passed as arguments
- **Idiomatic** — consistent with how Yona handles other wrapper types (Option, Result)

## Rules

| Rule | Description |
|------|-------------|
| **Single consume** | A `Linear` value must be pattern-matched exactly once |
| **Transfer** | `let y = x` where `x` is linear transfers the obligation (x consumed, y live) |
| **No aliasing** | After transfer, the old name cannot be used |
| **Branch consistency** | Both branches of if/case must consume the same linear values |
| **Scope exit** | **E0602** (`-Wlinear-leak`) if a linear value is live at scope exit (resource leak) |

## Error Examples

### E0600 — Use after consume

```yona
let conn = Linear (tcpConnect "host" 8080) in
let conn2 = conn in     -- conn consumed (transferred to conn2)
send conn "hello"        -- ERROR: conn was already consumed
```

### E0601 — Branch inconsistency

```yona
if ready then
    case conn of Linear fd -> close fd end  -- consumed
else
    0                                        -- NOT consumed
-- ERROR: linear value 'conn' consumed in then-branch but not else-branch
```

### E0602 — Resource leak

```yona
let conn = Linear (tcpConnect "host" 8080) in
42    -- E0602: linear value 'conn' not consumed — possible resource leak
```

## Stdlib Integration

### Producer Functions

These stdlib functions return `LINEAR` (or a tuple of Linear) in `.yonai`.
The interface preserves the payload type — for example `openFile` returns
`LINEAR(ADT(FileHandle))` — so the checker tracks an inferred
`Linear FileHandle`, not a name-based special case:

| Module | Function | Creates |
|--------|----------|---------|
| **File** | `openFile` | `FileHandle` |
| **Net** | `tcpConnect` | Socket handle |
| **Net** | `tcpListen` | Server socket |
| **Net** | `tcpAccept` | Client socket |
| **Net** | `udpBind` | UDP socket |
| **Process** | `spawn` | Process handle |
| **Channel** | `channel` | `(Sender, Receiver)` tuple of Linear |

### Usage Pattern

```yona
-- Network: connect, use, close
let conn = tcpConnect "api.example.com" 80 in
let response = recv conn 4096 in    -- borrow (no consume)
do
    close conn                       -- consume
    response
end

-- Process: spawn, use, wait
let proc = spawn "ls" ["-la"] in
let output = readAll proc in         -- borrow
do
    wait proc                        -- consume
    output
end
```

### With `with` Blocks

The `with` expression handles resource cleanup at **codegen** (Closeable) and
**LinearityChecker** discharges the bound Linear at with-exit (so the
idiomatic `with h = openFile … in …` does not leak-warn). Nested Linears
created inside the body are still tracked. Module function bodies are
walked the same way as expression programs.

```yona
-- Recommended pattern for simple resource usage
with conn = tcpConnect "host" 8080 in
    recv conn 4096
end
-- conn automatically closed by 'with' (codegen + linearity discharge)
```

## Composing with Effects

```yona
effect NetIo
    connect : String -> Int -> Linear Int
    send : Int -> String -> Int
    close : Int -> ()
end

fetchData host port =
    let conn = perform NetIo.connect host port in
    case conn of
        Linear fd ->
            let data = perform NetIo.send fd "GET / HTTP/1.1\r\n" in
            do; perform NetIo.close fd; data; end
    end
```

## What This Doesn't Do

**No borrow checker**: Yona has RC for memory safety. Linear types only track resource lifecycle, not memory ownership. Borrowing functions (send, recv, read) use the handle without consuming it.

**No session types**: Multi-step protocol enforcement (Open -> Read* -> Close) would require type-level state machines. Yona's algebraic effects are a better fit for protocol modeling.

**No uniqueness types**: Yona's RC system already provides unique-owner in-place mutation when `rc==1`. Static uniqueness tracking would add complexity for zero performance gain.

**No linear closures:** Closures cannot capture linear values. Use `with` blocks for resource-scoped operations.

## Architecture

The `LinearityChecker` is a flow-sensitive AST walker (similar to the RefinementChecker):

1. Identifies linear values from inferred types (`Linear _` or a product of
   Linear). The `Linear` constructor is a fallback when types are missing.
   Imported C functions use a `.yonai` `LINEAR` overlay (`ImportTypeSource`),
   including selective import, wildcard import, and FQN `Pkg\Mod::func`
   (`ModuleCall`).
2. Tracks a `LinearEnv` mapping variable names to `Live` or `Consumed` status
3. Pattern match on `Linear x` consumes the value
4. `let y = x` transfers the obligation (x consumed, y live)
5. At scope exit, emits **E0602** (`-Wlinear-leak`) for live (unconsumed) values
6. At each use, errors if the value is already consumed
7. In if/case, checks that both branches consume the same set
8. Walks `WithExpr` (bind Linear from the resource expr; discharge at exit via
   Closeable) and `FunctionExpr` bodies (fresh scope; Linear params from the
   zonked function type). Module top-level functions and instance methods are
   walked the same way (`yonac` fails the compile on E0600/E0601).

The checker is a compile-time-only pass. Codegen is unchanged — RC handles the actual memory management.
