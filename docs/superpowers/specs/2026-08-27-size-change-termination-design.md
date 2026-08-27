# Size-Change Termination Analysis Design

## Goal

Extend `yonac --require-effect-free` with a sound, conservative proof of
structural termination for direct and mutual recursive functions, including
lexicographic decreases over multiple parameters. Normal compilation remains
unchanged.

## Scope

The analyser proves only recursion whose progress is explicit in the AST:

- constructor fields and non-empty sequence tails bound by an unguarded case
  arm are strictly smaller than the scrutinee;
- aliases of those bindings retain the fact;
- a recursive call may use a lexicographically smaller argument vector;
- calls may cross functions within one recursive strongly connected component.

For the first size-change slice, all functions in a recursive component must
have the same arity; parameter positions define the common lexicographic
metric. Components with incompatible arity are conservatively rejected.

It does not infer numeric decreases, trust guards, inspect higher-order calls,
or reason through opaque helpers. Those forms remain rejected under the strict
flag with E0203, rather than being misclassified as total.

## Architecture

Create an LLVM-independent `TerminationAnalysis` module. It receives the
module AST and produces a list of rejected recursive calls and SCC summaries;
the CLI is responsible only for enabling it under `--require-effect-free` and
emitting diagnostics.

### Call graph and components

The analyser collects local direct calls and partitions the graph into strongly
connected components. A non-recursive component needs no termination proof.
For a recursive component, every internal call is checked. This replaces the
current blanket rejection of mutual recursion.

### Size relations

At each call site, the analysis records one relation per caller/callee argument
position:

| Relation | Meaning |
| --- | --- |
| `Strict` | The callee argument is a structural descendant of the caller parameter. |
| `Weak` | The argument is the same caller parameter or a proven alias of it. |
| `Unknown` | No sound relation is available. |

Facts are introduced only by unguarded constructor and head-tail patterns and
propagate through simple lexical aliases. A value can be strictly smaller than
one or more original parameters; unrelated bindings remain `Unknown`.

### Lexicographic cycle proof

A component is accepted only when every directed cycle has a lexicographically
decreasing relation composition: there is a position whose earlier positions
are `Weak` on every edge, whose own relation is never `Unknown`, and which is
`Strict` on at least one edge in the cycle. This permits non-decreasing hops
inside a mutually recursive cycle while ensuring the cycle as a whole descends.
It handles both direct recursion and cycles
such as `even (Succ n) -> odd n; odd (Succ n) -> even n`, plus multi-parameter
functions where a later parameter decreases only after earlier parameters stay
unchanged.

Unknown relations never establish a proof. When different call paths decrease
different positions with no valid lexicographic ordering, the component is
rejected.

## Diagnostics

Under `--require-effect-free`, an unproved recursive edge emits E0203 at the
call site. Its message names the call and explains whether no structural
descent was found or a mutual-recursion cycle lacks a stable lexicographic
decrease. A note identifies the enclosing recursive component and shows a
minimal repair pattern: destructure an argument and pass the bound descendant,
keeping earlier parameters unchanged.

Default builds emit neither errors nor warnings for these programs.

## Testing

Test-first coverage includes:

- accepted direct constructor and head-tail recursion;
- accepted mutual recursion and multi-argument lexicographic recursion;
- aliases of descendants and aliases of unchanged leading arguments;
- rejected original-argument, numeric-decrement, guarded, higher-order, and
  opaque-helper recursion;
- rejected cycles with incompatible decreasing positions;
- module and expression diagnostics, E0203 location/message, LSP publication,
  and full CTest regression coverage.

## Compatibility and documentation

The existing effect-row and finite-pattern checks are unchanged. Documentation
must describe this as structural size-change analysis, not general termination.
`docs/todo-list.md` retains general termination and arbitrary open-domain
coverage as remaining #5 work.
