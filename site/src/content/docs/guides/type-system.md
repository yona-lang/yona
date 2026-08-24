---
title: The type system
description: A complete map of Yona's static type system — inference, ADTs, traits, rows, effects, linearity, and refinements.
---

Yona is statically typed with full type inference. You rarely write a type
annotation; the compiler reconstructs principal types, monomorphizes
generic code to concrete machine code, and layers several extensions on the
core: algebraic data types, traits, row-polymorphic records, effect rows,
linear types, and refinement checks. This page maps the whole system and is
honest about which layers are complete and which are still partial.

Status badges next to each heading reflect the current implementation, not
the design goal.

## Hindley–Milner core <span class="yona-status yona-status--stable">Stable</span>

The foundation is Hindley–Milner inference with let-polymorphism. Every
well-typed expression has a **principal type** — the most general type of
which every other valid type is an instance — and the compiler finds it
without annotations:

```yona
let twice f x = f (f x) in
twice (\n -> n + 1) 40              # => 42
```

`twice` is inferred as `(a -> a) -> a -> a`. A `let`-bound function is
generalized over the type variables not fixed by its environment and may be
used at several different types in the same scope:

```yona
let pair x y = (x, y) in
(pair 1 2, pair "a" "b")            # => ((1, 2), ("a", "b"))
```

### Monomorphization

Polymorphism is a compile-time phenomenon only. At each call site the
compiler instantiates the function at the concrete argument types and
compiles a specialized native version — the same strategy as Rust generics
or C++ templates, and unlike the uniform boxed representation of OCaml or
Haskell. Polymorphic code therefore pays no boxing, no tags, and no dynamic
dispatch at runtime.

*Implementation note.* Functions are stored as AST at definition and
compiled at the call site where argument types are known. Exported generic
functions carry their source text in `.yonai` interface files so importing
modules can re-instantiate them at new types (see
[Modules and interfaces](/guides/modules-interfaces/)).

## Algebraic data types <span class="yona-status yona-status--stable">Stable</span>

ADTs declare a closed set of constructors with typed fields. Constructors
are first-class functions, and pattern matching is the elimination form:

```yona
type Shape = Circle Float | Rect Float Float

let area s = case s of
    Circle r   -> 3.14159 * r * r
    Rect w h   -> w * h
end in
area (Rect 3.0 4.0)                  # => 12.0
```

Fields may have function types (`type Lazy a = Cons a (() -> Lazy a) |
Empty`), and types may be recursive and polymorphic. The prelude types
`Option a`, `Result a e`, `Linear a`, and `Iterator a` are ordinary ADTs.

### Exhaustiveness <span class="yona-status yona-status--partial">Partial</span>

The compiler warns when an `Option`/`Result`/other ADT value is silently
discarded in a `do` block or bound to `_` (`-Wunmatched-adt`, enabled by
`--Wall`). It also warns for a finite-ADT `case` that misses constructors
(`--Wincomplete-patterns`, also enabled by `--Wall`). A wildcard arm closes
coverage; a guarded arm does not. `--require-effect-free` turns missing
constructors in those finite-ADT cases into E0203 errors, alongside its
closed-empty effect-row requirement. It does not prove termination, overlap
freedom, or non-ADT coverage.

## Traits <span class="yona-status yona-status--stable">Stable</span>

Traits are type classes resolved entirely at compile time — static
dispatch, superclass constraints (`trait Eq a => Ord a`), default methods,
constrained instances (`instance Show a => Show (Option a)`), and
multi-parameter traits:

```yona
trait Eq a
    eq  : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = if eq x y then false else true    # default method
end
```

Because dispatch is monomorphized, trait calls cost the same as direct
calls — and there are no runtime trait objects. The full treatment,
including auto-derive, is in [Traits](/guides/traits/).

## Record-row polymorphism <span class="yona-status yona-status--stable">Stable</span>

Record types unify by **row**: a function that reads a field accepts any
record that has that field, and the rest of the record stays polymorphic:

```yona
let greet r = "hello, " ++ r.name in
greet { name = "Alice", age = 30 }   # => "hello, Alice"
```

`greet` is inferred as `{ name : String | r } -> String` — the row variable
`r` stands for "whatever other fields the record has". Missing fields and
field type mismatches are compile-time errors. Record rows are structural;
they are distinct from the effect rows on function arrows below.

One limitation: open row variables on records are not yet printed into
`.yonai` interface signatures, so cross-module functions may show
concretized record types even where the checker inferred an open row.

## Effect rows <span class="yona-status yona-status--partial">Partial</span>

Function arrows carry a **latent effect row** — the set of effect
operations the function may perform, written `!{Effect.op}`:

```yona
(\x -> perform State.get ())         # : a -> !{State.get} Int
```

The rules:

- **`perform Effect.op`** adds the label to the ambient row when no
  enclosing `handle` covers it.
- **`handle … with …`** subtracts the operations its clauses cover;
  anything left escapes to the outer row.
- **Application** unions the callee's latent row into the caller's row. At
  the top level of a program, applying a function whose row is not fully
  handled is error **E0202**, reported at the introducing `perform` with a
  note at the call site.
- **Higher-order functions** keep an *open rest* `|r`: `apply : (a -> !{|r}
  b) -> a -> !{|r} b`, so passing an effectful function threads its row
  through and E0202 still fires at the outermost unhandled point.
- **Recursive definitions** solve `r ~ !{L | r}` as the least fixed point
  `r := !{L}` rather than reporting an infinite type.

```yona
# The call site must handle the latent effects of f
handle f 0 with
    State.get () resume -> resume 7
    return val -> val
end                                   # => 7
```

Rows survive module boundaries: exported functions record
`effects Fs.read` (closed) or `effects | hof` (`apply f x = f x`) on the
`.yonai` `FN` line. Imports restore that row for call-site E0202.
Siblings are typechecked as a unit, so wrapping an effectful helper
exports the helper's row. A missing `effects` field means unknown, not
pure.

Honest limitations: `effect Name … end` declarations do not parse yet (an
operation's identity is its `Effect.op` label at the `perform` site),
handlers are shallow in-scope dispatch rather than captured delimited
continuations, and an empty row is not yet usable as a totality/purity
guarantee. See [Effects](/learn/effects/) for the practical guide.

## Linear types <span class="yona-status yona-status--partial">Partial</span>

`Linear a` marks a value that must be consumed **exactly once** — file
handles, sockets, process handles, channel endpoints. Pattern matching on
the `Linear` constructor is the consumption point; rebinding transfers the
obligation:

```yona
let conn = Linear (tcpConnect "host" 8080) in
let conn2 = conn in                  # obligation transferred to conn2
send conn "hello"                    # error E0600: conn already consumed
```

A flow-sensitive linearity checker tracks each linear binding as live or
consumed, requires branches of `if`/`case` to agree on what they consume
(E0601), and warns when a linear value is still live at scope exit — a
resource leak. `with` is the idiomatic consumer and discharges the
obligation automatically. Details and examples are in
[Memory and linearity](/guides/memory/).

Honest limitations: `Linear` is a prelude ADT tracked by a dedicated
checker, not a first-class linear arrow in the HM core. Use-after-consume
(**E0600**) and branch inconsistency (**E0601**) fail compilation on
expression programs and modules (`--Wno-linear` skips). Resource leaks are
**E0602** warnings (`-Wlinear-leak`, on by default).

### `@borrow` parameters <span class="yona-status yona-status--stable">Stable</span>

`@borrow` before a parameter declares a read-only, non-escaping contract:
the callee may use the value but not return it, store it, or capture it in
a closure. The compiler verifies the contract (error **E0603** on
violation) and skips reference-count traffic for the parameter. Borrow
information is inferred automatically even without the annotation; writing
`@borrow` documents the contract and turns a future violation into a
compile error instead of a silent deoptimization.

```yona
import foldl from Std\List in
let sum @borrow xs = foldl (\a b -> a + b) 0 xs in
sum [1, 2, 3]                        # => 6
```

## Refinement types <span class="yona-status yona-status--partial">Partial</span>

A refinement checker proves simple value-level facts and reports error
**E0500** when an operation's precondition cannot be established:
`head`/`tail` on a sequence not proven non-empty, and division by a value
not proven non-zero.

```yona
let first xs = case xs of
    [h|t] -> h                       # h proven present by the pattern
    []    -> 0
end in
first [7, 8]                         # => 7
```

Facts flow from pattern matches (`[h|t]` proves non-empty) and literals (a
non-zero literal divisor is accepted). Honest limitations: refinement
syntax like `{ x : Int | x > 0 }` parses but predicates are not enforced at
function signatures, refinements are erased before codegen, and they do not
appear in `.yonai`. `yonac` **exits non-zero** on E0500 (`--Wno-refinement`
skips the checker) for both expression programs and modules.

## What is checked where

Yona has two compilation entry points, and they differ in which passes run:

- **Expression programs** (a `.yona` file whose top level is an
  expression, or `yona -e` / `yonac -`): parse → HM type checking →
  refinement and linearity checkers (fail the compile on E0500/E0600/E0601) →
  codegen. All diagnostics described on this page can appear.
- **Module compilation** (a `.yona` file declaring `module …`): parse → HM
  type checking → the same refinement and linearity checkers → codegen,
  producing a native object file plus a `.yonai` interface.

The `.yonai` interface is the contract at module boundaries. It carries
each export's arity and types, effect rows (including open rests), inferred
borrow masks, `LINEAR` markers on resource-producing functions, trait and
instance tables, ADT definitions, and the source text of generic functions
for cross-module monomorphization. Importers re-check calls against these
signatures, so a type error at a module boundary is caught at the caller
even though the callee was compiled separately.

## Summary

| Layer | Status |
|-------|--------|
| HM inference, principal types, monomorphization | Stable |
| ADTs and pattern matching | Stable |
| Case exhaustiveness diagnostics | Partial |
| Traits (static dispatch, superclasses, defaults) | Stable |
| Record-row polymorphism | Stable |
| Effect rows, E0202 | Partial |
| Linear types, E0600/E0601 | Partial |
| `@borrow`, E0603 | Stable |
| Refinements, E0500 | Partial |

The [language specification](/reference/specification/) is the normative
reference for the stable layers.
