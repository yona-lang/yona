# Totality Completion Design

## Goal

Complete the remaining practical slices of GitHub #5 in a safe order:

1. detect definitely overlapping pattern arms;
2. prove exhaustiveness for small non-ADT finite domains; and
3. accept only directly, structurally decreasing self-recursion under
   `--require-effect-free`.

The compiler must never claim a program is total when its analysis lacks a
proof. Existing effect-row and finite-ADT checks remain unchanged.

## Staged architecture

### 1. Overlapping patterns

Add shared pattern-coverage facts beside `Codegen::finite_case_coverage`.
For an unguarded arm, a later arm is definitely shadowed when every value it
can match is already covered by earlier unguarded arms. The first slice handles
wildcards/identifier patterns, repeated constructors, repeated literal values,
and alternatives in an `or` pattern. Guarded arms never contribute coverage,
and are never reported as shadowed.

`--Woverlapping-patterns` (and `--Wall`) emits a warning at the later arm.
`--Werror` promotes it using the existing diagnostic machinery. The check is a
warning only; it is not a totality error.

### 2. Finite non-ADT exhaustiveness

Extend the shared coverage analysis to domains whose complete set is known
without type-level range reasoning:

- `Bool`: `True` and `False`;

The first release deliberately makes no exhaustive claims for symbols,
integers, floats, strings, byte values, tuples, sequences, dictionaries, sets,
or records: their domain is open or requires product-space reasoning. A
wildcard/identifier arm remains universally exhaustive. Missing finite-domain
alternatives retain `--Wincomplete-patterns` in ordinary compilation and become
E0203 under `--require-effect-free`.

### 3. Structural self-recursion

Under `--require-effect-free`, build a module-local call graph before checking
function bodies. Reject every multi-function strongly connected component:
mutual recursion is deliberately outside this first proof system. For a direct
self-cycle, permit calls only when each recursive call passes a value
structurally extracted from one of its own parameters by an unguarded
constructor or non-empty sequence pattern. The analysis follows lexical aliases
of those destructured values. Recursive closure captures, numeric decrement,
and calls with the original parameter are not proven terminating and therefore
produce E0203.

This is a conservative syntactic termination check, not a general termination
prover. It does not change normal compilation or introduce annotations.

## Diagnostics and compatibility

- Default compilation behavior is preserved.
- `--Wall` enables overlap and incompleteness warnings; `--Werror` promotes
  them.
- `--require-effect-free` requires closed empty effects, exhaustive proven finite
  ADT and Bool matches, and structurally terminating direct recursion.
- Existing ADT coverage stays finite and constructor-based. Guards do not
  establish exhaustiveness or termination facts.

## Testing

Each stage adds direct analysis tests and `yonac` CLI tests for warning,
`--Werror`, strict E0203 rejection, and accepted positive examples. Full CTest
remains the final gate. Documentation will explicitly list unsupported domains
and recursion forms so the totality claim remains accurate.
