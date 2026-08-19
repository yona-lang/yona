---
title: Effects
description: Algebraic effects — perform operations, handle them at the call site, and let the compiler infer effect rows on function types.
---

<span class="yona-status yona-status--partial">Partial</span> — `perform`,
`handle`, and inferred effect rows work today; handlers are shallow in-scope
dispatch (`resume` is an identity continuation, not a captured continuation),
and standalone `effect` declarations do not parse yet.

Algebraic effects separate *what* a computation requests from *how* the
request is served. A function says `perform Log.log msg`; the **caller**
decides whether that means a file append, a network call, or nothing at all.
The same function works in production, in tests, and in a REPL without
changing a line.

## `perform` and `handle`

`perform Effect.op arg` requests operation `Effect.op` from the nearest
enclosing `handle` that covers it. A handler clause receives the operation's
arguments plus a `resume` continuation; calling `resume value` returns
`value` to the `perform` site and continues the handled expression:

```yona
handle
    let x = perform State.get () in
    x + 1
with
    State.get () resume -> resume 41
    return val -> val
end
# => 42
```

Reading the example: the body performs `State.get ()`. The handler's
`State.get` clause answers with `resume 41`, so the `perform` expression
evaluates to `41`, the body continues, and produces `42`. The `return`
clause then transforms the body's normal result — here it is the identity.

The general shape is:

```yona
handle <body> with
    Effect.op args resume -> <clause body>
    ...
    return val -> <return body>
end
```

- Operations are identified by their `Effect.op` label at the `perform`
  site. There is no separate declaration step today — `effect ... end`
  declaration blocks are planned but do not parse yet.
- The `return val -> …` clause runs on the body's *normal* result (not on
  each `resume`). It is optional and defaults to identity.
- Nested handlers shadow outer ones for the operations they cover: the
  innermost covering `handle` wins.

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
# => 99   (the inner handler answers)
```

## Effect rows on function types

Function arrows carry a **latent effect row**: the set of operations the
function may perform, written `!{Effect.op}` between the arrow and the
result type. Rows are **inferred by the compiler** — you never write them
in source, but you will see them in diagnostics and interface files:

```yona
\x -> perform State.get ()     # inferred: a -> !{State.get} Int
\f x -> f x                    # inferred: (a -> !{|r} b) -> a -> !{|r} b
```

The inference rules, precisely:

- **`perform`** adds its `Effect.op` label to the row of the function being
  inferred, when no covering `handle` is in scope at that point.
- **`handle` subtracts** the operations its clauses cover. Operations not
  covered escape into the enclosing row.
- **Application unions** the callee's latent row into the ambient row of
  the caller.
- **Higher-order functions keep open rows.** A function like `apply f x = f x`
  gets an open rest `!{|r}` that unifies with its argument's row — passing
  an effectful function through `map` or `apply` does not lose the effect.
- **Recursive functions** get the least fixed point of their own row, so
  self-application does not produce an infinite type.

## E0202 — unhandled effects are errors

Applying a function whose row is not fully covered by any surrounding
handler is a compile-time **error** (`E0202`), not a warning. The primary
diagnostic points at the **introducing `perform`**, with a note at the call
that let the effect escape:

```yona
let f = \x -> perform State.get () in    # f : a -> !{State.get} Int
f 0
# error[E0202]: unhandled effect State.get
#   --> points at `perform State.get ()`
#   note: applied here with no covering handler
```

The fix is a handler at the use site:

```yona
let f = \x -> perform State.get () in
handle f 0 with
    State.get () resume -> resume 7
    return val -> val
end
# => 7
```

A *direct* `perform` with no handler in scope (not mediated through a
function application) compiles with a `-Wunhandled-effect` warning and
raises `:UnhandledEffect` at runtime if reached.

## Rows cross module boundaries

When a module is compiled, the inferred rows of its exported functions are
written into the `.yonai` interface file and restored on import, so E0202
checking works across modules exactly as within one file:

```text
FN yona_My_Mod__fetch 1 STRING -> STRING effects Db.query,Log.log
FN yona_My_Mod__apply 2 FUNCTION INT -> INT effects |r0 0:|r0
```

The second line shows an open higher-order row: `apply`'s latent row `|r0`
is shared with its first parameter's row, so importing `apply` and passing
it an effectful function joins that function's operations into your row —
and E0202 still fires if nothing handles them.

## Worked example: GPU fallback

`Std\GPU` uses effects to let *you* decide what a GPU failure means.
Kernels report issues as ordinary values; `raiseGpu` converts an issue into
a `perform Gpu.*`, designed to be answered by a handler at your call site:

```yona
import raiseGpu, GpuOom from Std\GPU in
handle
    do
        raiseGpu GpuOom       # performs Gpu.oom ()
        0
    end
with
    Gpu.oom () resume -> resume ()          # e.g. log and fall back to CPU
    Gpu.deviceLost () resume -> resume ()
    Gpu.fail code resume -> resume ()
    return val -> 1
end
# => 1
```

Step by step: `raiseGpu GpuOom` performs `Gpu.oom ()`; the handler resumes
with `()`, so the `do` block continues and yields `0`; the `return` clause
maps that to `1`. A different caller could resume differently — retry,
abort, or switch backends — without touching the kernel code.
`withGpuFallback action` wraps this pattern: it runs `action`, then performs
the matching `Gpu.*` operation for the last classified issue (no-op on
success).

## Current limitations

Stated plainly, because they shape what you can write today:

- **Handlers are shallow, in-scope dispatch.** `resume` is an identity
  continuation: the clause computes a value and execution continues at the
  `perform` site. You cannot capture `resume`, call it later, call it twice,
  or decline to call it to abort with a different value — patterns like
  backtracking, generators-via-effects, and resumable exceptions that need a
  first-class delimited continuation are not expressible yet.
- **`effect` declarations do not parse.** Operations exist only as
  `Effect.op` labels at `perform` and `handle` sites; there is no place to
  declare operation signatures, so argument types are checked structurally
  at each site.

## Why effects

Compared with the usual alternatives:

- **Versus exceptions:** a handler can `resume`, so the computation
  continues after the operation — exceptions can only unwind.
- **Versus dependency injection:** no interfaces, containers, or mock
  frameworks; the handler *is* the injected behavior, scoped lexically.
- **Versus monads:** effectful code is direct style — no transformer
  stacks, no lifting, and pure code pays nothing.

Testing falls out for free — handle the effect with canned data:

```yona
let getUsers = \() -> perform Db.query "SELECT * FROM users" in
handle getUsers () with
    Db.query sql resume -> resume [("alice", 30), ("bob", 25)]
    return val -> val
end
# => [("alice", 30), ("bob", 25)]   (no database anywhere)
```

See the [specification](/reference/specification/) for the formal typing
rules, and [Concurrency](/learn/concurrency/) for the built-in
`Cancel.check` effect used by cooperative cancellation.
