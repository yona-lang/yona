# Pattern-Matrix Analysis Design

**Status:** Proposed; implementation requires approval of this document.

## Goal

Replace Yona's shallow unreachable-case-arm check with a typed,
pattern-matrix usefulness analysis. The result must diagnose every arm that
is provably unreachable while preserving conservative behavior for guards and
open domains. The analysis becomes the shared foundation for future complete
exhaustiveness and termination work.

## Scope

This phase improves `-Woverlapping-patterns` only. It preserves the existing
finite-ADT and `Bool` exhaustiveness contract and does not claim general
termination or exhaustive coverage of unbounded/open domains.

It covers unguarded nested patterns for:

- ADT constructors and named-record constructors;
- tuples and tuple fields;
- scalar literals with exact equality (`Bool`, `Int`, `Float`, `String`, and
  `Symbol`);
- sequences, including exact and head-tail forms where their structural shape
  is known;
- dictionaries and records when their required keys/fields establish a
  structural constructor; and
- aliases and or-patterns.

Guards never contribute coverage. A guarded arm is not called unreachable
solely because a prior guarded arm appears to overlap it. A later unguarded
arm is still analyzed against prior unguarded coverage only.

## Architecture

Create a compiler-owned pattern-analysis module independent of LLVM code
generation. It receives AST patterns plus registered type/constructor facts,
normalizes them into a small typed pattern IR, and applies the standard
matrix `useful(matrix, vector)` relation:

1. Start with an empty matrix of prior *unguarded* arms.
2. Normalize each candidate arm into one or more rows; an or-pattern expands
   into alternatives, while an alias preserves its inner pattern.
3. Determine whether at least one candidate row is useful with respect to the
   matrix by specializing rows for a constructor family and by taking the
   default matrix for wildcard-like patterns.
4. If no candidate row is useful, report the arm as unreachable; otherwise
   append its normalized rows to the matrix.

The IR explicitly distinguishes closed constructor families (ADTs, `Bool`,
tuples, exact sequence shapes) from open ones (integers, strings, symbols,
and unconstrained maps). Literal patterns only prove duplicate/full shadowing
when identical literals or a prior structural catch-all cover them. Open
domains never produce a false exhaustive claim.

The public result is:

```cpp
struct PatternAnalysis {
    std::vector<size_t> unreachable_clauses;
    std::optional<Codegen::FiniteCaseCoverage> incomplete;
};
```

`Codegen::analyze_case_patterns` delegates to the shared module, and code
generation consumes its result only for diagnostics. LLVM control-flow
generation is unchanged.

## Diagnostics

`-Woverlapping-patterns` points to the later unreachable arm and says:

```
unreachable pattern: earlier unguarded arms already cover every value it can match
```

No warning is emitted for merely intersecting arms that each retain at least
one possible value. `--Werror` continues to promote the warning. The strict
`--require-effect-free` gate gains no new rejections in this phase.

## Correctness boundaries

- The analyzer is sound-by-construction for every supported pattern form: it
  warns only after proving zero remaining values.
- Unsupported or type-incomplete patterns are opaque/useful, never
  unreachable.
- Guard expressions are opaque and do not establish coverage.
- The module has no LLVM dependency and is callable from future typechecker
  and language-server diagnostics.

## Testing

Tests exercise parsing plus analysis and compiler diagnostics for:

- nested `Option` / `Result` constructor patterns;
- tuple, record, and sequence exact/head-tail combinations;
- duplicate and catch-all-covered scalar literals;
- alias and or-pattern expansion;
- guarded arms retaining reachability;
- partial overlap remaining warning-free;
- `--Wall`, explicit warning control, and `--Werror`; and
- regression preservation for finite-ADT/`Bool` exhaustiveness and the
  strict effect-freedom gate.

The complete CTest suite remains required before completion.

## Documentation

Update `docs/pattern-matching.md`, `docs/error-codes.md`,
`docs/type-system-status.md`, `docs/todo-list.md`, `CHANGELOG.md`, and the
corresponding public site pattern-matching/reference pages. The TODO retains
general termination and open-domain exhaustiveness as separate work.
