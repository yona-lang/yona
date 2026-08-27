---
title: Effects
description: Algebraic effects — perform operations, handle them at the call site, and check unhandled latent effects at apply (E0202).
---

<span class="yona-status yona-status--partial">Partial</span> — `perform`,
`handle`, inferred latent-effect unions, higher-order propagation, recursion,
and module-interface round trips work today. Applying an unhandled effectful
function is **E0202**. Handlers are shallow in-scope dispatch (`resume` is an
identity continuation, not a captured continuation), and standalone `effect`
declarations are not implemented yet.

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

Function arrows carry a **latent effect expression**: the operations the
function may perform plus any open sources. The compiler infers it from
`perform` and applications. You never write it in source; diagnostics show its
normalized summary as `!{Effect.op}` (or `!{|r}` when it remains open):

```yona
\x -> perform State.get ()     # a -> !{State.get} b
\f x -> f x                    # propagates f's (possibly open) effect
```

What is implemented today:

- **`perform`** inside a lambda, when no covering `handle` is in scope,
  is recorded on that function's row.
- **Application unions** every callee source into the enclosing function —
  `let apply = \f x -> f x` and `let g = \() -> f ()` propagate effects
  without equating independent callbacks.
- **`handle` subtracts** the operations its clauses cover. Ops not covered
  escape into the enclosing row.
- **`handle` covers apply.** Applying the function inside a handler for
  those operations is accepted — including
  `let f = \x -> perform E.op x in handle f v with …`.
- **Application of an uncovered row is E0202** (below).
- **Direct `perform`** with no enclosing lambda and no handler stays a
  `-Wunhandled-effect` warning.

Pure direct and mutual recursive components use a least-derived summary;
higher-order and imported opaque sources remain open. New `.yonai` files carry
`effectscheme v2`, a deterministic normalized description of every arrow,
shared open source, and mask. Import clones it with fresh variables. Legacy
closed `effects` metadata is retained, and legacy open metadata is restored
conservatively.

## E0202 — unhandled effects are errors

Applying a function whose row is not fully covered by any surrounding
handler is a compile-time **error** (`E0202`), not a warning. The primary
diagnostic points at the **introducing `perform`**, with a note at the
application that let the effect escape. Curried partial applications are pure;
the final source application reports each escaping operation once:

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

`.yonai` `FN` lines retain a readable closed summary:

```
FN yona_Test_Fx__fetch 1 STRING -> STRING effects Fs.read
```

New interfaces also append `effectscheme v2`, which preserves all arrow
positions, independent open variables, and handler masks. That lets helpers
such as `use f g n = (f n, g n)` cross a module boundary without losing either
callback effect or making sibling instantiations share state. Older
`effects | hof` interfaces still import as conservative open rows. A missing
`effects` field stays unknown, never pure.

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
- **A missing `effects` field means unknown, not pure.** Old interfaces are
  supported conservatively; only `effectscheme v2` carries the complete
  higher-order graph.

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
