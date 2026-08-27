# Effect-union solver implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make effect inference lossless for independent higher-order callback
rows while retaining strict recursion, handler, import, and interface soundness.

**Architecture:** Implement a dedicated effect-constraint solver with explicit
join, mask, equality, flexible, derived-least, and opaque nodes. Arrows refer to
solver-owned effects; value-type unification no longer treats effect rows as a
single tail. Schemes and interfaces clone or serialize the complete effect
graph.

**Tech Stack:** C++23, existing `TypeArena`/`Unifier`, doctest, `.yonai`, typed
core, CMake/Ninja.

## Global constraints

- Keep effect aggregation separate from type equality; never unify independent
  callback tails merely to form a union.
- No `VERSION` bump, no vcpkg, no fallback that drops a tail or widens every
  union to an unstructured unknown row.
- Preserve source-compatible closed interfaces and import legacy open metadata
  conservatively.
- Test first: each new behaviour must be observed RED before its implementation.
- Update `docs/todo-list.md`, `CHANGELOG.md`, public effect/type-system docs,
  and this plan in the final change.

---

### Task 1: Define and test the effect algebra

**Files:**
- Create: `include/typechecker/EffectSolver.h`
- Create: `src/typechecker/EffectSolver.cpp`
- Modify: `CMakeLists.txt`
- Modify: `test/type_checker_test.cpp`

- [x] Add solver-only doctests for ACI joins, duplicate-label elimination,
  flexible equality, derived least fixed points, opaque leaves, masks, and
  graph-template cloning.
- [x] Verify the two-callback RED: `use f g n = (f n, g n)` instantiated with
  `State.get` and `Log.log` must summarize both labels without equating `f` and
  `g`.
- [x] Implement `EffectRef`, normalized summaries, joins, masks, equality, and
  cloneable graph templates; run the solver tests green.

### Task 2: Cut arrows and unification over to effect references

**Files:**
- Modify: `include/typechecker/InferType.h`
- Modify: `include/typechecker/TypeArena.h`
- Modify: `src/typechecker/TypeArena.cpp`
- Modify: `include/typechecker/Unification.h`
- Modify: `src/typechecker/Unification.cpp`
- Modify: `test/type_checker_test.cpp`

- [x] Add RED coverage for genuine arrow-effect equality, incompatible closed
  rows, and pretty-printing a normalized multi-source row.
- [x] Replace `arrow_effects`/`effect_rest` and `ERow` value-type unification
  with `EffectRef` equality delegated to the solver.
- [x] Preserve value occurs checks and levels while collecting/freezing the
  effect graph separately; run existing effect-row tests green.

### Task 3: Convert inference, handlers, and recursive SCCs

**Files:**
- Modify: `include/typechecker/TypeChecker.h`
- Modify: `src/typechecker/TypeChecker.cpp`
- Modify: `test/type_checker_test.cpp`
- Modify: `test/yona_script_test.cpp`

- [x] Add RED regressions for independent callback union, three callbacks,
  source-order invariance, symbolic handler subtraction, and recursive-SCC
  propagation.
- [x] Make every function body a derived effect cell; application adds an
  inclusion edge and handling adds a symbolic mask edge.
- [x] Predeclare recursive SCC members with derived cells and solve only these
  cells to a least fixed point. Remove preliminary-tail, provenance-rank, and
  arity-frontier side tables once replacements are green.
- [x] Verify strict direct/mutual structural positives, HOF negatives in either
  arm order, aliases/imports, sibling polymorphism, and default compilation.

### Task 4: Clone schemes and round-trip interfaces

**Files:**
- Modify: `include/typechecker/InferType.h`
- Modify: `src/typechecker/TypeChecker.cpp`
- Modify: `include/Codegen.h`
- Modify: `src/codegen/CodegenModule.cpp`
- Modify: interface parser/emitter files discovered by the existing `.yonai`
  effect tests
- Modify: `test/type_checker_test.cpp`
- Modify: `test/trait_test.cpp`

- [x] Add RED round trips for a two-callback exported helper and for legacy
  `effects |` imports; assert separate graph variables survive instantiation.
- [x] Serialize a versioned deterministic effect-scheme representation, clone
  it on import, and map old interfaces to safe closed/opaque terms.
- [x] Verify cross-module callbacks, selective/wildcard imports, and normal
  trait/interface suites.

### Task 5: Route all consumers through one summary and finish

**Files:**
- Modify: `src/typed_core/Analyze.cpp`
- Modify: `cli/main.cpp` only if deferred effect obligations require it
- Modify: `docs/error-codes.md`
- Modify: `docs/type-system-status.md`
- Modify: `docs/todo-list.md`
- Modify: `CHANGELOG.md`
- Modify: matching `site/src/content/docs/` pages

- [x] Add RED agreement tests: typed core, E0202, strict effect-freedom, and
  interface emission must expose the same sorted labels/open status.
- [x] Finish deferred E0202 obligations after solver finalization with stable
  perform/application origins.
- [x] Run focused solver/type/CLI/interface tests, full doctest, CTest, site
  build, and `git diff --check`; mark all completed TODOs and plan steps only
  after green.
