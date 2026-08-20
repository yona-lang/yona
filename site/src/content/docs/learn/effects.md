---
title: Effects
description: Algebraic effects — perform operations, handle them at the call site, and check unhandled latent effects at apply (E0202).
---

<span class="yona-status yona-status--partial">Partial</span> — `perform`,
`handle`, and **closed** latent effect sets on lambdas work today. Applying
an unhandled effectful function is **E0202**. Handlers are shallow in-scope
dispatch (`resume` is an identity continuation, not a captured continuation).
Open rows and standalone `effect` declarations are not implemented yet.

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

Function arrows carry a **latent effect row**: the operations the function
may perform, plus an optional open rest. The compiler infers this from
`perform` and from applying other effectful functions. You never write the
row in source; it appears in diagnostics as `!{Effect.op}`:

```yona
\x -> perform State.get ()     # a -> !{State.get} b
\f x -> f x                    # (a -> !{|r} b) -> a -> !{|r} b
```

What is implemented today:

- **`perform`** inside a lambda, when no covering `handle` is in scope,
  is recorded on that function's row.
- **Application unions** the callee's uncovered ops (and open rest) into
  the enclosing function — so `let apply = \f x -> f x` and
  `let g = \() -> f ()` propagate effects.
- **`handle` subtracts** the operations its clauses cover. Ops not covered
  escape into the enclosing row.
- **`handle` covers apply.** Applying the function inside a handler for
  those operations is accepted — including
  `let f = \x -> perform E.op x in handle f v with …`.
- **Application of an uncovered row is E0202** (below).
- **Direct `perform`** with no enclosing lambda and no handler stays a
  `-Wunhandled-effect` warning.

Closed and open HOF rows on exported `FN` lines are restored on import.
A least-fixed-point story beyond generalizing the rest var is not
implemented.

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

`.yonai` `FN` lines may carry a closed set, an open rest, or both:

```
FN yona_Test_Fx__fetch 1 STRING -> STRING effects Fs.read
FN yona_Test_Hof__apply 2 STRING -> STRING effects | hof
```

`effects | hof` is the `apply f x = f x` shape: the first parameter is
a function, and applying it propagates that argument's effects. A
missing `effects` field stays unknown (fresh type vars), so existing
stdlib interfaces are unchanged. Module compile typechecks siblings as
a unit, so `wrap = \() -> readSecret ()` records `readSecret`'s row on
`wrap`. Importing and applying an effectful export is **E0202** unless
a `handle` at the import/apply site covers every listed op.

## Worked example: GPU fallback

`Std\GPU` uses effects to let *you* decide what a GPU failure means.
Kernels report issues as ordinary values; `raiseGpu` converts an issue into
a `perform Gpu.*`, designed to be answered by a handler at your call site:

```yona
import raiseGpu, GpuOom from Std\GPU in
handle
    raiseGpu GpuOom       # performs Gpu.oom ()
with
    Gpu.oom () resume -> resume ()          # e.g. log and fall back to CPU
    Gpu.deviceLost () resume -> resume ()
    Gpu.fail code resume -> resume ()
    return val -> 1
end
# => 1
```

Step by step: `raiseGpu GpuOom` performs `Gpu.oom ()`; the handler resumes
with `()`, so the handled expression yields `()`; the `return` clause maps
that to `1`. A different caller could resume differently — retry, abort, or
switch backends — without touching the kernel code.
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
- **A missing `effects` field means unknown, not pure.** Closed sets and
  `effects | hof` are restored; `\x f -> f x` (function not first) is
  not a serialized HOF shape.

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
