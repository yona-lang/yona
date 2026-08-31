# Literal Codegen Stabilization Implementation Plan

> Execute with subagent-driven development. Start each production task from a
> focused failing regression, review each implementation independently, and
> keep todo/changelog closure in the combined final stabilization update.

**Goal:** Make every parser/typechecker-supported scalar literal lower to LLVM
and make literal patterns compare values consistently in every pattern shape.

**Architecture:** Byte and character values use the compiler's existing `i64`
scalar carrier. Pattern matching has one typed literal-predicate helper that
understands the carrier representation and is reused by direct, or-pattern,
tuple, constructor, head-tail, and exact-sequence lowering. Control flow stays
owned by each caller: a false predicate branches to that caller's existing
next-arm block and a true predicate continues in a fresh match block.

## Task 1: Lower byte and character expressions

**Files:**

- Modify: `include/yona/Codegen/Codegen.h`
- Modify: `src/Codegen/Codegen.cpp`
- Modify: `src/Codegen/CodegenExpr.cpp`
- Modify: `src/Codegen/CodegenUtils.cpp`
- Modify: `src/Codegen/CodegenFunction.cpp`
- Add or modify focused fixtures under: `test/Fixtures/Codegen/`

- [x] **Step 1: Add focused RED coverage**

Add scalar, tuple, and sequence-value fixtures using byte and character
literals. Confirm parsing and typing succeed but core dispatch reports
`unsupported expression type`.

- [x] **Step 2: Lower both literals through the scalar carrier**

Add explicit byte and character codegen operations returning their unsigned
byte / code-point values as `i64` with the existing scalar ABI tag. Add both
AST kinds to core dispatch and to every return-type prepass used before
function creation. Do not invent new heap layouts or public runtime entry
points.

- [x] **Step 3: Verify and commit**

Run the focused fixtures plus literal/parser/typechecker controls, then commit
as `fix: lower byte and character literals`.

## Task 2: Centralize literal predicates across pattern shapes

**Files:**

- Modify: `include/yona/Codegen/Codegen.h`
- Modify: `src/Codegen/CodegenCase.cpp`
- Add or modify focused fixtures under: `test/Fixtures/Codegen/`

- [ ] **Step 1: Add cross-shape RED coverage**

For integer, symbol, byte, character, float, string, boolean, and unit
literals, cover equal and same-typed unequal cases where meaningful. Exercise
direct value patterns, or-pattern alternatives, tuple and nested tuple fields,
constructor fields, head-tail sequence heads, and exact-sequence elements.
Retain integer and symbol controls so the fix cannot regress existing paths.

- [ ] **Step 2: Add one carrier-aware literal predicate helper**

Generate integer equality for integer, symbol, byte, character, and boolean;
ordered floating equality for floats after restoring the double carrier; and
runtime content equality for strings after restoring pointers. Unit always
matches because it has one value. Reject unsupported literal kinds explicitly
rather than silently treating them as matches.

- [ ] **Step 3: Route every literal-pattern path through the helper**

Use the same helper in direct value, or-pattern, tuple/nested-tuple,
constructor/nested-constructor-field, head-tail, and exact-sequence matching.
Preserve each caller's existing length/tag checks, identifier bindings,
transfer/drop scopes, and ownership behavior. Each successful comparison
continues in a fresh block; each failure uses the existing next-arm block.

- [ ] **Step 4: Verify ownership and matching suites**

Run the focused literal fixtures, exact sequence edge cases, ADT/constructor
cases, case-expression tests, and relevant memory/ownership controls. Commit as
`fix: compare literals in every pattern shape`.

## Task 3: Close the literal-codegen batch

**Files:**

- Modify: this plan
- Modify: `docs/todo-list.md`
- Modify: `docs/superpowers/specs/2026-08-31-open-bug-stabilization-design.md`
- Modify: `CHANGELOG.md`
- Modify: `.superpowers/sdd/progress.md`

- [ ] **Step 1: Run focused and full Linux gates**

Build the debug preset; run all literal/pattern, case, ADT, and fixture suites;
then run the full CTest preset and `git diff --check`.

- [ ] **Step 2: Record combined results later**

Close the byte/character expression, general literal-pattern, and exact
sequence-pattern todo entries only with passing evidence. Include these results
in the user-requested combined final stabilization documentation update rather
than a standalone closure commit.
