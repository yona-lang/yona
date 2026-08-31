# Open Bug Stabilization Design

## Scope

Resolve every unchecked entry under `docs/todo-list.md`'s `## Bugs` section
that is reproducible on Linux. The Windows-only `Std\Convert` Bool parsing
failure is excluded unless the same failure can be reproduced easily in the
Linux workspace.

The ten in-scope bugs are:

1. The Linux io_uring cancel declaration/definition mismatch.
2. Stale `YonaIoContext` field access and identifier casing in Linux platform
   I/O.
3. Identifier casing regressions in the native stdlib.
4. Lost native Prelude dependencies while reconstructing generic functions
   from `.yonai` files.
5. Unterminated LLVM blocks in stream-module case lowering.
6. Channel fixtures that violate their typed ownership contract.
7. Binary I/O fixtures that infer `Linear FileHandle` as `Int`.
8. Lifted dictionary trait ownership lowering that can crash.
9. ABI refinement that no longer emits the canonical refined call form.
10. Effectful file fixtures that lose their `Fs.read` handler context.

The already implemented AST `nullptr_t` repair remains part of the working
baseline and must continue to pass its compile regression.

## Strategy

Work in a stabilization ladder. First repair only the source-reorganization
regressions that prevent the compiler and test executable from building. Once
the test runner is available, reproduce each semantic or code-generation bug
independently and fix it through a red-green test cycle. Do not batch unrelated
production changes or update expected output merely to hide a regression.

Each bug follows the same evidence chain:

1. Run the documented reproduction and retain the exact failure.
2. Trace the failure to its owning interface or lowering boundary.
3. Add or strengthen the narrowest automated regression.
4. Run the regression and confirm that it fails for the diagnosed reason.
5. Apply one root-cause fix.
6. Run the focused regression, the affected suite, and the full Linux debug
   test preset before marking the bug complete.

If a reproduction no longer fails after the build blockers are removed, audit
the relevant code and test history before changing anything. Mark an item
complete only when current automated coverage proves its stated contract.

## Component Boundaries

The build-unblocking phase owns only the Linux runtime files and their public
I/O contracts. It must preserve the canonical `YonaIoContext` layout and the
constness/ownership contract of io_uring cancellation rather than restoring
obsolete fields.

The frontend phase keeps fixes at their source boundary:

- interface dependency preservation belongs to the canonical interface and
  generic-source services;
- block termination belongs to exhaustive case lowering;
- channel and file-handle types belong to semantic descriptors and native-call
  lowering;
- lifted trait ownership belongs to dictionary materialization and transfer
  analysis;
- refined calls belong to refinement-aware function ABI lowering;
- effect-handler context belongs to imported/fixture compilation state.

No fix may bypass the type system, weaken ownership checks, suppress LLVM
verification, or broaden runtime C code where the behavior can be expressed in
Yona.

## Verification

The Linux completion gate is:

```bash
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux
ctest --preset unit-tests-linux
```

Focused doctest filters and fixture invocations are run before the full gate for
each bug. `git diff --check` and the repository formatting checks cover every
changed file. Any newly exposed bug is added immediately to
`docs/todo-list.md` with a one-line reproduction before work continues.

The Windows-only conversion item remains unchecked with its native-CI evidence
unless a Linux reproduction is found. A portable static or unit-level
reproduction is sufficient to bring it into scope; Wine availability alone is
not treated as native Windows verification.

## Documentation

Each completed bug is checked off in `docs/todo-list.md` with its regression and
fix summarized. `CHANGELOG.md` records user-visible and build-impacting fixes
under `Unreleased`. Matching plans and public language, CLI, prelude, or stdlib
documentation are updated in the same change whenever behavior would otherwise
be stale.
