# Warning cleanup design

## Goal

Reduce reproducible build warnings without changing compiler semantics, and
turn warnings that need design work into an actionable backlog.

## Scope

1. Fix mechanical warnings in production and test code: deprecated API uses,
   stale comments, and headers that are obsolete under the project C++ level.
2. Add explicit switch handling only when the existing neighboring behavior
   establishes a safe, semantics-preserving outcome.
3. Do not silence warnings globally or weaken warning flags.
4. Add a `docs/todo-list.md` entry for each remaining warning family that
   requires an ABI, ownership, type-system, or accelerator-lowering decision.

## Verification

Build the debug compiler and tests, compare warning output before and after,
run focused tests for edited compiler paths, and run `git diff --check`.
