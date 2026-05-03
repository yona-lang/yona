# Design: Type-level borrows (`&T`)

**Status:** design note for implementation (see [todo-list.md](./todo-list.md)
— *Type-level borrows (`&T`) and signature carry-over*).  
**Related:** [memory-management.md](./memory-management.md#explicit-borrow)
(`@borrow` first slice), `include/analysis/BorrowEscapeAnalysis.h`.

## 1. Motivation

Today, **callee-reads-only** heap parameters are handled by:

1. **Automatic borrow inference** in codegen (escape analysis), and  
2. **Optional `@borrow`** on parameters — same rules as inference, enforced
   at **E0603**, documented in source.

Neither appears in **types** or **`.yonai`**, so cross-module APIs cannot
state “this parameter is borrowed” except via prose or duplicated `@borrow`
on every defining clause. A type-level **`&T`** (name TBD; `Borrowed T` as an
app is equivalent) fixes signatures, tooling, and a single source of truth
for “pass without claiming ownership / skip Perceus DUP at this boundary.”

## 2. Non-goals (v1)

- **Rust-style lifetimes** — no region variables, no borrow checker beyond
  current escape rules extended to types where needed.
- **Borrowed fields** in ADTs or `&T` stored in `Seq`/`Dict` values.
- **Overloading** solely on `T` vs `&T` for the same function name unless the
  existing trait / instance story can express it cleanly.

## 3. Surface syntax (proposal)

- **Prefix on types only** (mirrors many ML-family notations): `&Seq Int`,
  `&FloatArray`, `&(Int, String)` if tuples ever become borrowable.
- **Parameter and result positions in type signatures** on `let f : …`,
  module `f : …`, `extern`, and trait method signatures.
- **v1 restriction:** allow `&T` only where `T` is a **heap-shaped** type the
  runtime already refcounted (`Seq`, `Set`, `Dict`, `String`, ADT, closure,
  `FloatArray`, `IntArray`, … — exact list = types for which `is_heap_type`
  is true in codegen). **Reject** `&Int`, `&Bool` at kind-check (or erase to
  `Int` in a later revision — prefer explicit reject for clarity).

**Relationship to `@borrow` (v1):**

- **Option A (strict):** if the signature says `&T`, the parameter pattern
  must use `@borrow` (or we infer `@borrow` from `&` and elide the keyword).
- **Option B (loose):** `@borrow` allowed without `&`; `&` implies borrow
  behavior and **implies** `@borrow` checking without requiring the keyword.
- **Recommendation:** **Option B** — parse `&T` as carrying the contract;
  optionally warn if `@borrow` is missing on an identifier param for
  documentation parity. Long term, deprecate redundant `@borrow` when `&`
  is present.

## 4. Internal representation

- **Preferred:** new unary constructor in the monotype AST, e.g.
  `MonoType::MBorrow(MonoTypePtr inner)` or `App("Borrow", {inner})` with a
  reserved name in the type arena (same as `Linear` / `Seq` apps).
- **Arrow:** unchanged currying; `&Seq a -> Int -> Int` is
  `Arrow(MBorrow(App("Seq",a)), Arrow(Int, Int))`.
- **Generalization:** `&α` must not generalize in a way that hides borrow
  under a polymorphic `∀` at the **use** site without the callee knowing —
  follow the same scoping rules as today for value types (likely: borrow
  only on **monomorphic** interface surfaces first; polymorphic `&a` is a
  later extension).

## 5. Type checking and unification

- **Equality with owned `T` at call sites:** when a function expects `&T` and
  the argument has type `T`, **allow** the call (caller keeps ownership;
  callee gets read-only view — same as today’s borrow path). When the callee
  expects `T` (owned transfer / callee-owns), existing Perceus rules apply.
- **Subtyping:** minimal v1 can use **unification with a directed coercion**
  only at apply: “`T` flows to `&T`” is allowed; “`&T` flows to `T`” is **not**
  allowed without an explicit copy (future). Document as one-way coercion,
  not a full subtyping lattice.
- **Return type:** disallow `&T` as the **top-level** result of a function in
  v1 (no dangling borrowed return). Nested inside `Result (&T) e` is also out
  until lifetimes exist.

## 6. Trait and instance methods

- Trait methods may take `&Self` or `&Arg` in signatures; instance resolution
  must compare **after** stripping or matching `&` consistently with
  today’s name + arity matching.
- **Default methods** in traits: same parsing as module functions.

## 7. Codegen

- When the **formal** type of parameter `i` is `&T` (zonk’d), set
  `CompiledFunction::borrowed_params[i] = true` for heap `T` without relying
  solely on `has_escaping_use` — still run escape analysis as a **safety
  check** that the body does not violate borrow (or reuse E0603 logic in the
  typechecker before codegen).
- **Mismatch:** if type says `&T` but body escapes — **error** in typechecker
  (preferred) so codegen stays simple.

## 8. `.yonai` and cross-module

- Extend interface text for `GENFN` / typed exports to print `&` in
  parameter positions consistently with source.
- **Versioning:** if old `.yonai` lacks `&`, treat as owned (`T`); new
  compiler emits `&` where applicable. Document one-way compatibility.

## 9. `extern` / C ABI

- At the C boundary, `&T` and `T` are both **pointers** (or int for small
  ADTs); no ABI change. Documentation only: `&` is a Yona-level contract.

## 10. Implementation phases (execution order)

1. **This document** — reviewed against codebase (`TypeChecker`, `types`,
   `.yonai` emitter/parser, `CodegenFunction`).
2. **Internal only:** introduce `MBorrow` / reserved app, zonk + print, no
   user lexer change — prove `.yonai` round-trip in tests.
3. **Lexer/parser + type parser:** `&` prefix on type atoms; `infer_function`
   binds params from typed patterns including `&`.
4. **Unify + apply:** coercion `T` → `&T` at call sites; errors on bad return.
5. **Codegen:** wire `borrowed_params` from zonk’d param types + retain
   E0603-style check where AST `@borrow` disagrees with type (temporary).
6. **Relax / deprecate:** align `@borrow` with `&` per §3.

## 11. Open questions

- Should **`&` bind tighter than `->`**? (Proposed: yes — `&T -> U` =
  `(&T) -> U`.)
- **`&` under type aliases** — resolve alias then borrow, or forbid alias
  heads in v1?
- **Interaction with refinement types** — `{ x : &Seq Int | … }` deferred.
