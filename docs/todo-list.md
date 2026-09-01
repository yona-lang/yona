# Yona-LLVM — Outstanding Work

This file contains only actionable, unfinished work. Historical milestones and
shipped-feature status live in `CHANGELOG.md` and the corresponding plans under
`docs/superpowers/plans/`.

## Bugs

### Typed IR Codegen rewrite blockers

Program of record:
[2026-09-01-typed-ir-codegen-rewrite-design.md](./superpowers/specs/2026-09-01-typed-ir-codegen-rewrite-design.md).

- [ ] **Dynamic closure arity cannot implement general currying.** Repro:
  under-apply a runtime-selected closure with more than one remaining argument;
  the loaded arity is treated as non-constant and every remaining argument is
  passed in one call.
- [ ] **Capturing closures disagree with their universal argument ABI.** Repro:
  call a closure that captures an outer value and accepts `Float`; the caller
  uses an FP register while the closure entry reads an integer carrier.
- [ ] **Effect handlers can embed SSA values from an enclosing function.**
  Repro: reference a non-constant outer parameter from a handler clause; the
  separately emitted handler function receives a foreign LLVM value.
- [ ] **Effect arguments, results, and resumptions lose type and ownership.**
  Repro: perform and resume an operation carrying `Float`, Bool, or a managed
  aggregate; handler lowering hard-codes `i64` and `CType::INT`.
- [ ] **Zero- and one-argument async externs use the wrong callback ABI.**
  Repro: invoke `extern async` with a `Float -> Float` signature; the worker
  calls it as `int64_t (*)(int64_t)`.
- [ ] **Escape analysis misses transitive aggregate containment.** Repro:
  `let xs = [1], wrapped = Some xs in wrapped` can return a heap ADT whose
  child was freed with its arena.
- [ ] **Some pattern payload bindings outlive an unretained temporary owner.**
  Repro: `case [["x"]] of [x] -> x end` can release the temporary scrutinee
  while returning its borrowed heap child.
- [ ] **`typeOf` stores unmanaged globals in managed String fields.** Repro:
  inspect the type of an ADT/channel and release the returned type value; ADT
  destruction reads an RC header before an LLVM global string.
- [ ] **Exception lowering corrupts non-string and multi-field ADTs.** Repro:
  raise a nullary, Int-field, or two-field exception; lowering always reads
  field zero as a string pointer and catch reconstructs only that field.
- [ ] **Set/dictionary ownership metadata is installed after insertion.**
  Repro: insert duplicate managed keys or values; replacement occurs while
  heap flags are zero and leaks or mismanages the replaced owner.
- [ ] **Sequence generators consume borrowed named sources.** Repro: reuse a
  uniquely owned named sequence after an unguarded or fused comprehension;
  generator `tail` may mutate it and release elements.
- [ ] **ADT field update applies LLVM aggregate operations to heap pointers.**
  Repro: update a field of a uniform heap ADT; the non-recursive path still
  emits `CreateInsertValue` against the pointer representation.
- [ ] **Guarded and multi-equation functions are not lowered canonically.**
  Repro: define multiple pattern equations or guarded bodies; later patterns
  are discarded and Codegen repeatedly selects only `bodies[0]`.
- [ ] **Closure free-variable discovery is incomplete.** Repro: reference an
  outer variable only beneath `try`, `with`, effects, records, fields, or a
  generator; the generated closure omits the capture.
- [ ] **`with` does not finalize resources when its body raises.** Repro: raise
  after successful acquisition; close is emitted only on a normal,
  unterminated body block.
- [ ] **SJLJ cleanup omits locals, TCO functions, and functions over 16 owners.**
  Repro: raise with a live heap local, from a self-tail-recursive function, or
  from a function with seventeen owned heap parameters.
- [ ] **Non-exhaustive `case` fabricates a result.** Repro: execute a runtime
  no-match edge; lowering feeds zero/null into the result PHI instead of
  raising `MatchError` and cleaning the scrutinee.
- [ ] **Module compilation omits queued asymmetric ownership drops.** Repro:
  compile a module function with branch-asymmetric sequence/map transfer;
  unlike expression compilation, module finalization never flushes the drops.
- [ ] **TCO cleanup state is global to the generated function.** Repro: combine
  multiple recursive branches with a base return; the first emitted tail call
  suppresses cleanup on syntactically later CFG paths.
- [ ] **AArch64 SJLJ restore can overwrite its buffer base register.** Repro:
  compile the inline long-jump helper under enough register pressure for its
  unconstrained input operand to occupy `x19`-`x28`; restoring that register
  redirects subsequent context loads. This path disappears when explicit
  control outcomes replace generated-program SJLJ.
- [ ] **Case guards are not required to be Bool.** Repro: use an `Int` or
  managed value as a `case` clause guard; semantic inference accepts it and
  leaves backend behavior to an invalid implicit convention.
- [ ] **Or-pattern alternatives can bind incompatible environments.** Repro:
  match `left | right` where the alternatives bind different names or bind
  the same name at different types; inference mutates one shared environment
  instead of requiring identical bindings.
- [ ] **Generator conditions are not required to be Bool.** Repro: use an
  `Int`, String, or aggregate as a collection-comprehension condition;
  inference records its type without unifying it with Bool.

- [ ] **The Linux release Prelude artifact build aborts in `yonac`.** Repro:
  `cmake --preset x64-release-linux && cmake --build --preset build-release-linux -j2`
  aborts while building `artifacts/Prelude.o` with `double free or corruption
  (!prev)` followed by `pure virtual method called`.

- [ ] **Windows `Std\Convert` rejects the Bool case expected by its conformance
  suite.** `foundation_Convert_test` reports one failure for “Parse Bool is
  explicit and case sensitive” (`16 passed, 1 failed`) only on Windows.
  Repro: GitHub Actions run `33167091803`, Windows x64 Debug job `98834979501`.
  Compare the runtime/parser result contract and make it platform-independent.

## Build quality

- [ ] **Relax stream-fusion gating only with benchmark evidence.** Run the full
  Linux and Windows benchmark matrix three times (`-n 10`), require a ≥5%
  median win on the targeted fusion rows, no correctness/test regression, and
  no >3% median regression elsewhere without a documented root cause.

## Distribution and toolchains

- [ ] **Productionize the Windows installer.** Finish upgrade behavior, code
  signing, and final UX polish.

- [ ] **Publish a versioned WinGet manifest for each tagged release.** After
  stable GitHub Release URLs and SHA-256 hashes exist for both x64 and ARM64
  MSIs, submit one multi-file manifest set to `microsoft/winget-pkgs`, validate
  with `winget validate` and a clean local or Windows Sandbox install, then
  monitor its validation PR. This is a registry manifest, not an in-repository
  package formula; do not submit it before release assets exist.

- [ ] **Complete the sysroot distribution pass.** Validate the installed
  CLI/REPL layout on every packaged platform without requiring an external C
  compiler for normal `yonac` use.

- [ ] **Enable embedded LLD by default across supported toolchains.** The
  remaining Windows gate is an MSVC-compatible LibXml2 dependency.

## Type system and language safety

- [ ] **[#5](https://github.com/yona-lang/yona/issues/76) Complete the totality
  story.** Extend `yonac --require-effect-free` beyond its existing closed-row,
  finite-domain, and local structural-recursion checks to general termination
  and arbitrary open-domain coverage before making a full totality claim.

- [ ] **[#4](https://github.com/yona-lang/yona/issues/75) Implement a
  deterministic compile-time evaluator.** After #5 and either #7 or a
  documented typechecked-AST subset, evaluate pure total expressions only;
  exclude macros and arbitrary native code at compile time. This is the shared
  prerequisite for multi-stage programming and user-defined derives.

- [ ] **Make `Linear FileHandle` and other resources real payload types.**
  Replace the raw-`Int` runtime-handle path with APIs such as
  `openFile : String -> FileMode -> Linear FileHandle` and
  `spawn : String -> Linear Process`; do the same for TCP/UDP and channel ends.
  Consume resources through `with` or `case Linear h -> …`, implement
  `Closeable` on the payload, and preserve both `LINEAR` and the inner ADT in
  `.yonai`. Promote E0602 leaks to errors. Update the linear/file/process/net/
  channel docs and site pages, with codegen regressions for valid use,
  use-after-consume, and leaks.

- [ ] **Add type-level borrows (`&T`) and interface carry-over.** Follow
  [design-borrow-types.md](./design-borrow-types.md): extend syntax,
  `MonoType`, unification, `.yonai`, and codegen so cross-module APIs state
  callee-reads-only directly and `borrowed_params` derives from zonked types.
  Version one excludes lifetimes and borrowed ADT fields.

### Formal specification (Rocq)

Program of record:
[2026-08-17-yona-rocq-formalization.md](./superpowers/plans/2026-08-17-yona-rocq-formalization.md).

- [ ] **Phase 0:** `formal/` dune/opam skeleton, Rocq 9.2 pin, CI job, and `docs/formal-spec.md`.
- [ ] **Phase 1:** Yona-Core syntax, semantics, and typing; progress and preservation.
- [ ] **Phase 2:** verified unification plus Algorithm W; soundness, completeness, and principal types.
- [ ] **Phase 3:** extracted checker, `yonac --emit-typed-core`, and a differential harness.
- [ ] **Phase 4:** Rows, Effects, Async, Traits, Refine, and Linear modules.
- [ ] **Phase 5:** Alectryon documentation and feature-status headers.
- [ ] **Phase 6:** optional Iris proofs for the Perceus RC runtime.

## Performance and runtime research

- [ ] Migrate to LLVM exception handling (`invoke` / `landingpad`) if correctness requires it.
- [ ] Add profile-guided optimization.
- [ ] Complete a JIT feasibility/design study (ORC, Cranelift, or alternatives).
- [ ] Investigate LLVM coroutine lowering for async after cancellation semantics are frozen.
- [ ] Design gradual typing with contracts.
- [ ] Design distributed Yona.
- [ ] Design a serialization system.
- [ ] Design STM.

## Language architecture

- [ ] **Supervisors as effect handlers.** Model Erlang-style supervision trees using `handle ... with`, where a supervisor catches child failures and chooses restart, escalation, or ignore. Depends on settled structured concurrency semantics.

- [ ] **Content-addressed code.** Research Unison-style AST hashes as function identities for caching, zero-conflict merges, and refactoring, including the package-manager, LSP, and VCS implications.

## GPU and heterogeneous compute

- [ ] **Compile arbitrary lambdas to SPIR-V.** Fixed kernels are supported; arbitrary closures such as `\x -> x + x * x` still stay on the host path or fail under `--strict-accelerator` with E0700.

- [ ] **Evaluate io_uring/reactor GPU integration.** Research interleaving GPU completion with the io_uring reap loop; the current fence waiter thread is the supported implementation.

- [ ] **Design CPU/GPU occupancy and scheduling hints.** Consider optional attributes or stdlib helpers for wave size, shared memory, and throughput-versus-latency after user demand and profiling data justify them.

- [ ] **Re-capture macOS and Windows GPU benchmarks.** Run `bench/run_gpu_compare.py`, including pinned-scale hot cases, on MoltenVK and Vulkan hosts; refresh the platform benchmark reports.

## Metaprogramming

- [ ] **Multi-stage programming.** Define hygienic staging, including a pure `static` computation model and its relationship to the deterministic evaluator.
- [ ] **User-defined derives.** Let traits provide `derive` templates over ADT structure after the evaluator or an enriched `.yonai` codegen interface exists.
- [ ] **Quasiquotes and template expressions.** Add AST `quote` / `splice` facilities for DSLs and generated code.

## Tooling and documentation

- [ ] **Package manager and build tool.** Define dependency resolution, reproducible builds, and project metadata around `.yonai` interfaces.

- [ ] **Rewrite `yls` in Yona as the editor-default server.** Keep the existing C++ `yls --stdio`, VS Code client, and Zed extension as maintained fallback paths until the Yona implementation has protocol and performance parity.

- [ ] **Build compiler-aware API documentation extraction.** Preserve handwritten Learn/Guides/Reference pages, but replace the regex-only `scripts/gendocs.py` path with `yonac --emit-docs` or a successor that understands `.yonai`, C modules, types, effects, and exports. Until then, update source comments and public site pages together.
