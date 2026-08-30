# Algebraic effects in Yona

**Status (2026-08-27): partial.** `perform` and `handle` parse, typecheck,
and compile as shallow lexical handler dispatch. The compiler now infers
lossless latent-effect unions through higher-order functions, recursive
components, handlers, polymorphic schemes, and module interfaces. Parsed
`effect` declarations and captured delimited continuations are still not
implemented. See [type-system-status.md](type-system-status.md) for the audit
and GitHub [#8](https://github.com/yona-lang/yona/issues/79) for the remaining
language-surface work.

## `perform` and `handle`

`perform Effect.operation value` requests an operation from the nearest
enclosing handler that covers its label. A handler clause receives the
operation arguments and a `resume` value; the optional `return` clause maps a
normal result.

```yona
handle
    let value = perform State.get () in
    value + 1
with
    State.get () resume -> resume 41
    return value -> value
end
# => 42
```

Operations are identified by their `Effect.operation` label. There is no
source-level `effect … end` declaration today: registered operations are a
compiler/library contract, and argument types are checked at the `perform`
site.

Handlers are lexical. A nested handler for the same operation takes priority:

```yona
handle
    handle perform State.get () with
        State.get () resume -> resume 99
        return value -> value
    end
with
    State.get () resume -> resume 0
    return value -> value
end
# => 99
```

## Latent effect rows

Every function arrow has a latent effect expression. Diagnostics print its
normalized summary as `!{Effect.operation}`; an unresolved source appears as
an open tail, `!{|r}`. Source code does not need effect annotations.

```yona
let read = \x -> perform State.get () in
read 0                         # `read` has !{State.get}
```

Internally the checker uses a separate effect-constraint graph rather than a
single row tail. Its operations are associative, commutative, and idempotent
join, symbolic handler masking, and true equality. That distinction matters
for higher-order code:

```yona
let use f g n = (f n, g n) in
use get log 0
```

If `get` performs `State.get` and `log` performs `Log.log`, `use` has the
union of both effects. The callback rows remain independent: a later call can
instantiate `use` with entirely different callbacks without sharing or
unifying their effect variables. Reordering the callbacks does not change the
summary.

Application adds the callee's latent effect expression to the caller; it does
not equate callback effects merely because both are used. A curried function
only evaluates its body after its final source argument. Its earlier partial
application stages are pure, and diagnostics report each uncovered operation
once at the application which actually runs the body.

`handle` creates a symbolic mask, so it also works when a helper's callback
effect is unknown when the helper is defined:

```yona
let use f n = f n,
    get n = do perform State.get (); n end
in
handle use get 0 with
    State.get () resume -> resume 7
    return value -> value
end
```

Recursive function bodies use least-derived effect cells. Pure direct and
mutual recursion therefore closes to an empty row; a callback, imported open
row, or other opaque source remains open and is never defaulted away.

## E0202 — unhandled application effects

Applying an effectful function outside a covering handler is an **E0202**
error. The primary diagnostic points at the originating `perform`; a note
points at the application that lets it escape.

```yona
let read = \x -> perform State.get () in
read 0
# error[E0202]: unhandled effect operation 'State.get'
```

Handle the operation at its use site:

```yona
let read = \x -> perform State.get () in
handle read 0 with
    State.get () resume -> resume 7
    return value -> value
end
```

A direct `perform` with no enclosing function application remains the
`-Wunhandled-effect` warning; it raises `:UnhandledEffect` if reached at
runtime.

## Modules and polymorphism

New `.yonai` files keep the familiar readable summary:

```text
FN YonaTestFxFetch 1 STRING -> STRING effects Fs.read
```

They also carry an `effectscheme` field: a deterministic, normalized
description of every arrow in the exported type, including shared open
variables and handler masks. On import, the whole scheme is cloned with fresh
effect variables, preserving polymorphism and independent callback effects
across module boundaries. The readable `effects …` field is its summary;
missing effect metadata is unknown, never proof of purity.

## Current limitations

- **Handlers are shallow lexical dispatch.** `resume` is an identity-style
  continuation, not a captured delimited continuation. It cannot be stored,
  resumed later, or used for backtracking/generator semantics.
- **`effect` declarations are not parsed.** Operations are labels at
  `perform` and `handle` sites; there is no exported operation-signature
  declaration syntax yet.
- **The strict gate is conservative.** `yonac --require-effect-free` requires
  a closed empty summary, exhaustive finite pattern coverage, and a local
  structural size-change proof. It is not a general termination checker.

See [the public effects guide](/learn/effects/) for the user-facing version,
[error codes](error-codes.md#e0202--unhandled-effect-at-call-site) for E0202,
and [type-system-status.md](type-system-status.md) for implementation detail.
