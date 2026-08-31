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

## Task 1A: Enforce Unicode scalar escape values

**Files:**

- Modify: `src/Syntax/Lexer.cpp`
- Modify: `src/Syntax/ParserPattern.cpp`
- Modify: `test/Syntax/LexerTest.cpp`
- Modify or add focused parser/codegen fixtures under: `test/`

- [x] **Step 1: Add boundary and pattern RED coverage**

Prove U+10FFFF and U+1F600 are accepted, while surrogate endpoints U+D800 and
U+DFFF plus U+110000 are rejected as invalid character literals. Add a
non-ASCII character-pattern round trip that currently exposes the narrowing
cast in `ParserPattern`. Cover the same surrogate and out-of-range rejections
for string escapes because both literal forms share the Unicode decoder.

- [x] **Step 2: Validate escapes and preserve the full token value**

After accumulating a Unicode escape, reject surrogate code points and values
above U+10FFFF. Construct pattern `CharacterExpr` nodes from the full lexer
`char32_t` value through the AST's character carrier, matching expression
parsing. Apply the same scalar invariant to string escapes so the scanner never
encodes invalid UTF-8 from an escaped value.

- [x] **Step 3: Verify and commit**

Run Lexer, parser/pattern, and character codegen fixtures including string
rejection and non-BMP controls. Commit as
`fix: validate Unicode character literals`.

## Task 1B: Make character AST printing parseable

**Files:**

- Modify: `src/Syntax/Ast.cpp`
- Modify: focused AST/parser tests under `test/Syntax/`

- [x] **Step 1: Add print-round-trip RED coverage**

Construct and print character nodes for U+03BB, U+1F600, U+10FFFF, ASCII quote,
backslash, newline, and NUL. Reparse every printed spelling and assert the same
scalar value. Confirm non-ASCII currently emits unsupported `\x...` escapes.

- [x] **Step 2: Emit canonical lexer-supported escapes**

Keep existing short escapes for ASCII controls and delimiters. Emit `\u` plus
exactly four uppercase hex digits for remaining BMP values and `\U` plus
exactly eight for supplementary values. Preserve and restore the caller's
stream formatting flags and fill character.

- [x] **Step 3: Verify and commit**

Run AST/parser formatting tests and the Unicode fixture, then commit as
`fix: print parseable character literals`.

## Task 1C: Reject malformed raw UTF-8 safely

**Files:**

- Modify: `src/Syntax/Lexer.cpp`
- Modify only if a decoded-width result is required: `include/yona/Syntax/Lexer.h`
- Modify: `test/Syntax/LexerTest.cpp`

- [x] **Step 1: Add raw-byte RED coverage**

Test overlong `C0 80`, `E0 80 80`, and `F0 80 80 80`, an encoded surrogate
`ED A0 80`, and above-U+10FFFF `F4 90 80 80`. Assert `INVALID_CHARACTER`,
bounded recovery, and no exception/crash; retain valid two-, three-, and
four-byte boundary controls.

- [x] **Step 2: Validate decoding before cursor advancement**

Reject illegal lead-byte ranges, non-minimal encodings, surrogate scalars, and
values above U+10FFFF in the UTF-8 decoder. Make cursor advancement use the
validated encoded width (or an equivalent lead-byte-derived width), so an
invalid/overlong result cannot desynchronize `Current` from `SourceText`.
Recovery must consume at least one raw byte and stay within the source range.

- [x] **Step 3: Verify and commit**

Run the full Lexer suite, parser recovery controls, and malformed-input CLI
repros, then commit as `fix: reject malformed UTF-8 source`.

## Task 1D: Preserve source positions when rescanning literals

**Files:**

- Modify: `src/Syntax/Lexer.cpp`
- Modify: `test/Syntax/LexerTest.cpp`

- [ ] **Step 1: Add exact-location RED coverage**

Place invalid raw UTF-8 at known columns inside character and string literals,
including after a multibyte prefix on a later line. Assert the diagnostic range
starts at the invalid byte; confirm literal rescanning currently reports one
column late.

- [ ] **Step 2: Rewind complete lexer position state**

When `scan_token` delegates to a scanner that reconsumes the opening quote,
restore `Current`, `Line`, and `Column` together to the token start. Prefer one
narrow helper over ad-hoc assignments. Do not alter number/symbol paths that
resume after an already-consumed ASCII prefix.

- [ ] **Step 3: Verify and commit**

Run Lexer location/recovery tests, malformed-input CLI repros, and parser
diagnostic controls. Commit as `fix: preserve literal diagnostic positions`.

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
