# Type-Directed Equality Design

## Goal

Make `==` and `!=` safe and semantic for values whose equality can be derived
from their concrete Yona type. In particular, `Option`, `Result`, tuples, and
nested algebraic data types must compare without relying on LLVM aggregate
layout or pointer identity.

## Semantics

- Integers, booleans, symbols, units, and floats retain their current scalar
  comparison semantics.
- Strings compare their contents through `Eq_String.eq`.
- Tuples compare arity and corresponding elements recursively.
- ADTs compare constructor tags first. Values with the same constructor compare
  each declared field recursively using constructor field metadata.
- `!=` is the logical negation of the same equality operation.
- Functions, promises, linear resources, channels, and fields whose concrete
  type is unavailable are not comparable. The compiler emits one source-located
  diagnostic rather than generating malformed or pointer-based equality.

## Compiler Architecture

`Codegen::emit_typed_equality` is the single lowering entry point. It accepts
two `TypedValue`s plus the best available field shape. It dispatches by Yona
`CType`, not by incidental LLVM types.

For non-recursive ADTs, it extracts the flat tag and fields. For recursive ADTs,
it uses the existing runtime tag/field accessors. Constructor metadata in
`types_.adt_constructors` supplies field CTypes and nested tuple/function shapes.
Only fields belonging to the selected constructor participate; padding in the
flat maximum-arity representation is ignored.

Imported generic functions must preserve the concrete return ADT name and field
subtypes already carried by `TypedValue`. If that information is absent, the
comparison is rejected rather than guessed.

## Ownership

Equality borrows both operands. It never increments, decrements, stores, or
transfers either value. Runtime string comparison is read-only. Aggregate field
extraction produces aliases valid for the comparison expression only.

## Diagnostics

Unsupported equality reports the operand type and a concrete remedy: provide an
explicit comparator (for example through `Std\Test.equalBy`) or compare an
observable field. The diagnostic is emitted once at the operator location and
codegen returns a well-formed failure value; no `unreachable` callback body is
left in otherwise successful compilation.

## Tests

Focused red/green tests cover:

- `None == None`, `Some 1 == Some 1`, and unequal constructors/payloads;
- `Option String` content equality across distinct allocations;
- `Result`, tuples, and a nested user ADT;
- `!=` for each supported aggregate family;
- unsupported function-field equality producing one diagnostic;
- imported `Std\Option.map` values inside `Std\Test`, proving ownership remains
  intact through callback execution and report rendering.

The existing `test/stdlib/pure/Option_test.yona` remains the end-to-end
regression. Full codegen and CTest suites must pass before the todo bug is
removed.
