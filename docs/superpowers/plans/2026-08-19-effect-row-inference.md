# Effect-row inference (#8) — implementation plan

**Status (2026-08-20):** Slices 1–3 **shipped** — local inference, handler
subtract, pretty-print, `.yonai` `effects` (closed labels + open `|rN` /
per-param rows), E0202 with perform-origin spans, HOF open-rest threading,
recursion least-fixed-point unify. Follow-up after the `in` terminator
parse fix: `perform State.get ()` is 0-arg (Unit is not a payload);
recursive self-apply skips `apply_callee_effects`; two-function HOFs
keep independent rests; `ρ ~ {L | ρ}` closes the rest instead of E0101.
#8 acceptance is met; empty-row totality is
[#5](https://github.com/yona-lang/yona/issues/76), parsed `effect`
decls are [#9](https://github.com/yona-lang/yona/issues/80).

## Goal

Effect rows are inferred on function arrows, normalized, written to `.yonai`,
restored on import, and checked when applying an effectful callee outside a
covering `handle`.

## Slice 1 (this change)

1. **Representation** — `Arrow` carries sorted `Effect.op` labels + optional open
   `effect_rest` row variable (closed when `effect_rest == nullptr`).
2. **Inference** — ambient escaping row while inferring; `perform` adds
   unhandled ops; `handle` subtracts covered ops; function arrows capture body
   row; apply unions callee latent effects; branches share ambient (union).
3. **Unify / pretty** — effect-row unify with open rests; display
   `(a -> !{State.get} b)` (omit empty closed `!{}`).
4. **`.yonai`** — trailing `effects Op1,Op2` on FN/AFN/IO/NAT; restore into
   `ImportedFnSig` / arrow latent effects.
5. **Diagnostics** — **E0202** when applying a function whose latent effects
   are not covered by the current handler stack (call site + effect names).
6. **Tests** — local inference, handler subtract, nested/partial, HOF open rest,
   `.yonai` round-trip, E0202 negative.
7. **Docs** — `effects.md`, `type-system-status.md`, plan/todo/CHANGELOG.

## Non-goals (later slices)

- Parsed `effect` decls (#9)
- Dynamic runtime handler search for `perform` inside precompiled `Std\GPU`
  bodies (codegen); rows make types honest first
- Totality / empty-row gate (#5)
- Arbitrary-lambda SPIR-V

## TDD fixtures (doctest)

| Case | Expect |
|------|--------|
| `\() -> perform State.get ()` | type `(() -> !{State.get} Int)` |
| handle covers get | empty latent row |
| nested handle partial | remaining ops escape |
| apply imported `effects Gpu.oom` without handle | E0202 |
| `.yonai` emit/parse round-trip | labels preserved |

## Exit for Slice 1

Acceptance criteria partially met: local inference, deterministic display,
handler elimination tests, `.yonai` round-trip, E0202 with call-site context.

## Slice 2 (HOF + recursion, 2026-08-19)

1. **Open-rest threading** — applying a function-typed parameter joins its
   latent rest into the enclosing arrow (`\f x -> f x` : `(a -> !{|r} b) -> a -> !{|r} b`).
2. **Multi-rest union** — two applied function parameters keep distinct open
   tails; call-site instantiation unions closed rows (E0202 for both).
3. **Recursion LFP** — `r ~ !{L | r}` binds `r := !{L}` instead of E0101;
   self-only rests on recursive bindings are closed so pure `let f x = f …`
   is not `!{|r}`.
4. **Tests** — `Effect row: HOF *`, `Effect row: recursive *`,
   `Unifier: effect-row occurs *` in `test/type_checker_test.cpp`.

## Slice 3 (open-row `.yonai` + E0202 origins, 2026-08-19)

1. **Open-row `.yonai`** — `effects |r0 0:|r0` (result row + per-param rows;
   same `rN` is the same rest variable). Imported HOFs restore shared open
   tails so `apply g` joins `g`’s latent row.
2. **E0202 origins** — arrows carry introducing `perform` spans; the error
   points at `perform`, with a note at the escaping call.

## Follow-ups (not #8)

- Parsed `effect` decls remain [#9](https://github.com/yona-lang/yona/issues/80);
  totality / empty-row gate remains [#5](https://github.com/yona-lang/yona/issues/76).
