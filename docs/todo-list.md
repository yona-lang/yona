# Yona-LLVM - Status and Roadmap

## Current Snapshot

- Compiler: Yona -> LLVM IR -> native executable via `yonac`
- REPL: `yona` compile-and-run interactive mode. Script mode (`#!/usr/bin/env yona` + `Std\Process.getArgs`) is planned: [2026-08-19-yona-script-shebang.md](./superpowers/plans/2026-08-19-yona-script-shebang.md)
- Tests: doctest (`gpu_vulkan_device` + optional `gpu_vulkan_mapadd` / mapMul / reduce); codegen `gpu_backend_flags` + `gpu_vulkan_last_note` (child env `YONA_GPU_DISABLE_VULKAN=1`); with `-DYONA_ENABLE_VULKAN=ON`, run `ctest -R doctest_gpu_vulkan -V` (see `CLAUDE.md`); full `tests.exe` per `CLAUDE.md` (`YONA_PATH`, `YONAC_CC` on Windows)
- **Windows dev checklist:** full `clang+llvm-*-windows-msvc` tree for `LLVM_INSTALL_PREFIX`, correct env spelling, `CC`**/*`*CXX` or CMake compiler flags if Clang lives outside that prefix, and `YONAC_CC` or `PATH` for doctest (`tests.exe`) — see **INSTALL.md** (Windows: *Complete Windows LLVM tree*, `YONAC_CC` *and doctest*).
- Windows benchmark run (2026-04-26): 35/35 Yona rows passing, report refreshed, perf deltas reviewed
- macOS benchmark run (2026-08-17): 45/45 Yona rows passing (`docs/benchmark-results-macos.md`)
- **GPU crossover benches:** `bench/run_gpu_compare.py`, `bench/gpu_bench_meta.py`, and `bench/accelerators/`* hot + 10k/5k rows (`.yona` + `.expected`) for map/reduce, filter, columnar pipeline, materialize — see `bench/README.md`, `docs/benchmark-results-windows.md`, and `docs/benchmark-results-macos.md`
- **Vulkan filter compute path:** SPIR-V under `src/runtime/gpu/shaders/filter_*.comp` (embedded `*_spv.inc`), runtime in `gpu_vulkan_ops.c` / `gpu_vulkan_compute.c` / `gpu_cpu.c`; API in `lib/Std/GPU.yona` — see `docs/gpu-architecture.md`
- **GPU / Vulkan (desktop, non-macOS):** roadmap + **documented limitations** (`docs/gpu-architecture.md`: *Vulkan limitations*, *Roadmap implementation status*); Windows `yonac` links `vulkan-1.lib` from CMake-configured path with `VULKAN_SDK` fallback (`INSTALL.md`, `cmake/yona_vulkan_link_cfg.h.in`)
- Runtime backends: Linux + Windows native paths in place
- **Accelerator diagnostics + transparent lowering (**`docs/gpu-transparent-lowering.md`**):**
  `yonac --emit-accelerator-report` → JSON `yona.accelerator_diag.v1` (explicit
  `Std\GPU` sites plus `"kind":"transparent"` kernel rewrites). Codegen rewrites
  inline IntArray/FloatArray `map`/`filter`/`foldl` in the kernel library to the
  Std\GPU ABI (disable with `--no-accelerator-lowering`). API:
  `include/AcceleratorLowering.h`; tests: `accelerator_lowering_*` and
  `accelerator_diagnostic_report_*`.
- `bench/runner.py`**:** `--verify-reference-outputs` / `--reference-verify-langs` for golden `.expected` checks without `yonac`; `--skip-erl` / `YONA_BENCH_SKIP_ERLANG` when Windows OTP crashes; clearer Erlang `erlc` failure diagnostics when BEAM exits `0xC0000005`



## Active Priorities



### 1) Benchmarking hardening — **done**

Reference conformance (`bench/runner.py --verify-reference-outputs`) and Windows
Erlang skip (`--skip-erl` / `YONA_BENCH_SKIP_ERLANG`) are shipped; see
`bench/README.md` and Completed Milestones.



### 2) Platform/runtime closure — **done**

macOS kqueue + MoltenVK/`Std\GPU` portability shipped (`docs/gpu-vulkan-implementation-plan.md`
§11). Metal usually lacks `shaderInt64`; IntArray GPU uses i32 when values fit.



### 3) Distribution readiness

- [x] Copr / AUR / Launchpad + Homebrew tap + Linux/macOS in-process LLD (v0.1.2–v0.1.3) — see Completed Milestones
- [ ] Windows installer productionization (upgrade behavior, signing, final UX polish)
- [ ] `#!/usr/bin/env yona` script mode + `Std\Process.getArgs` after package install — [2026-08-19-yona-script-shebang.md](./superpowers/plans/2026-08-19-yona-script-shebang.md)
- [ ] Final packaging pass for sysroot-based CLI/REPL distribution layout
- [ ] Enable embedded LLD backend by default across supported toolchains (remaining gate: MSVC-compatible LibXml2 on Windows)



### 4) Type system (GitHub #3–#8)

Program of record: issues on `yona-lang/yonac-llvm`. Execution plan:
[2026-08-17-next-plan-of-action.md](./superpowers/plans/2026-08-17-next-plan-of-action.md).
Formal specification (Rocq, parallel research track):
[2026-08-17-yona-rocq-formalization.md](./superpowers/plans/2026-08-17-yona-rocq-formalization.md)
— see § Formal specification below. Do not start CTE (#4) or a full typed-core
API (#7) until the audit (#3) and effect-row story (#8) are honest about what
already works.

- [x] **[#3](https://github.com/yona-lang/yonac-llvm/issues/3) Type-system status audit** — `docs/type-system-status.md` (2026-08-18). Next: #8. Follow-ups from the audit: [#9](https://github.com/yona-lang/yonac-llvm/issues/9) effect decls, [#10](https://github.com/yona-lang/yonac-llvm/issues/10) blocking E0500/E0600, [#11](https://github.com/yona-lang/yonac-llvm/issues/11) `-Wincomplete-patterns`.
- [x] **[#8](https://github.com/yona-lang/yonac-llvm/issues/8) Effect-row inference +** `.yonai` **propagation** — after #3. **Landed 2026-08-19:** closed sets + E0202; unify of effect rows; HOF rest vars; apply-union / wrap; handler subtraction; pretty-print `!{…}`; `.yonai` `FN … effects Fs.read`. **Follow-ups landed same day:** open HOF rest (`effects | hof`, `Effect: imported HOF open rest from .yonai is E0202`, `Interface files preserve exported HOF open rest`); sibling-aware module typecheck (`Interface files preserve sibling-wrapped FN effect rows`, wrap-before-sibling). HOF restore is the `apply f x = f x` shape (first param is the function). Empty-row totality is **#5**; parsed `effect` decls are **#9**. Plan `docs/superpowers/plans/2026-08-19-effect-row-inference.md`.
- [ ] **[#6](https://github.com/yona-lang/yonac-llvm/issues/6) Opaque exported types** — after #3; parallelizable with #8. `export type T opaque` (syntax TBD); hide constructors across modules.
- [ ] **[#5](https://github.com/yona-lang/yonac-llvm/issues/5) Opt-in totality / effect-freedom** — after #8 (empty row must be real). Annotation or flag; facts in `.yonai`. Does **not** evaluate at compile time.
- [ ] **[#7](https://github.com/yona-lang/yonac-llvm/issues/7) Typed-core API** — arch doc after #3; full API after #8. Versioned in-process C++ API (no LLVM headers in the consumer). Defer wire format.
- [ ] **[#4](https://github.com/yona-lang/yonac-llvm/issues/4) Deterministic evaluator (CTE)** — after #5 and either #7 or a documented typechecked-AST subset. Pure total exprs only; no macros / arbitrary native at compile time.

Default series: `#3 → #8 → #5 → #7 → #4`, with **#6** beside #8 after the audit.

### Formal specification (Rocq)

Master plan:
[2026-08-17-yona-rocq-formalization.md](./superpowers/plans/2026-08-17-yona-rocq-formalization.md).
Parallel to §4 compiler work: formalize Yona-Core in Rocq, prove soundness,
extract a verified checker, differentially test it against `yonac` (via the
#7 typed-core dump). Phase 4 modules (rows, effects, async, traits,
refinements, linearity) follow the #3 audit. Phase 6 (Iris / Perceus RC) is
opt-in research.

- [ ] **Phase 0** — `formal/` dune+opam skeleton, Rocq 9.2 pin, CI job, `docs/formal-spec.md`
- [ ] **Phase 1** — Yona-Core syntax / semantics / typing; progress + preservation
- [ ] **Phase 2** — verified unification + Algorithm W; soundness / completeness / principal types
- [ ] **Phase 3** — extracted checker + `yonac --emit-typed-core` + differential harness (feeds #7)
- [ ] **Phase 4** — extension modules: Rows, Effects (#8), Async, Traits, Refine, Linear
- [ ] **Phase 5** — Alectryon docs in `docs/formal/` + status headers on feature docs (feeds #3)
- [ ] **Phase 6** — Iris Perceus RC runtime proofs ([memory-management.md](./memory-management.md))

Related docs: [type-checker-design.md](./type-checker-design.md),
[type-system-plan.md](./type-system-plan.md),
[effects.md](./effects.md),
[refinement-types.md](./refinement-types.md),
[linear-types.md](./linear-types.md),
[row-polymorphism.md](./row-polymorphism.md),
[design-borrow-types.md](./design-borrow-types.md).

### Suggested next steps (rolling)

- [x] **yonac module files with** `##` **preamble** — `is_module_source` in
  `cli/main.cpp` skips `#` line comments before detecting `module`, so
  `yonac -o lib/Std/M.yona lib/Std/M.yona` works for Http-style leading docs.
- [x] **Phase 0 CI bugs** — flaky `binary_seek` / `binary_write_read` and
  `net_runtime_test` TCP SIGSEGV (see Bugs).
- [x] **Type-system audit (#3)** — `docs/type-system-status.md`.
- [ ] **Next language work:** [#6](https://github.com/yona-lang/yonac-llvm/issues/6)
  opaque types, or [#5](https://github.com/yona-lang/yonac-llvm/issues/5)
  totality / empty-row gate now that #8 rows are real. Formal spec track:
  Phase 0 of
  [2026-08-17-yona-rocq-formalization.md](./superpowers/plans/2026-08-17-yona-rocq-formalization.md).

High leverage after the audit: `&T` **/ borrow types**
([design-borrow-types.md](./design-borrow-types.md)), **supervisors-as-handlers**
(after cancel story is frozen), **LLVM coroutine plan** only after cancellation
semantics are frozen. **GPU** Track G surface is **shipped**
([design-gpu-async.md](./design-gpu-async.md), `docs/gpu-architecture.md`);
open follow-ups are under *Language — GPU* below (full arbitrary-lambda SPIR-V,
io_uring/reactor research, occupancy hints, macOS/Windows bench re-capture).
`raiseGpu` GENFN + `x * x` / Vulkan `filterLessThan` landed 2026-08-19.
Effect-row inference for `Gpu` is **#8**
(do not duplicate). Product: **LSP** or **package manager** if adoption beats
runtime research; prefer after typed-core (#7).

### Bugs (open)

- [ ] **Exported fn calling private module helper: E0104 at import site + wrong result, exit 0.** GENFN re-parse cannot resolve unexported names and the documented extern fallback does not kick in; compilation continues and the executable returns 0. Repro: module `Secret` with `helper x = x * x` (unexported) and exported `doubledSquare x = 2 * helper x`; then `yonac -I . -o demo main.yona` where `main.yona` is `import doubledSquare from Secret in doubledSquare 5` → E0104 printed, `./demo` prints `0` (expected `50`), yonac exit 0.
- [ ] **Printing a tuple containing a Seq fails LLVM module verification.** Repro: `yonac -e '(42, [1, 2, 3])' -o t` → `Module verification failed: Call parameter type does not match function signature!` on `yona_rt_print_seq` (seq element loaded as i64, passed where ptr expected).
- [ ] **Imported module function as first-class HOF argument: codegen "Operand is null".** Repro: `yonac -e 'import map from Std\List, length from Std\String in map length ["ab", "abc"]' -I lib` → `Module verification failed: Operand is null` on `call ptr @map(<null operand!>, ptr %seq)`. Wrapping in a lambda (`map (\s -> length s) …`) works. Same failure via `Std\Stream.map length`.
- [ ] **Top-level print of nested Seq prints element pointers.** Repro: `yonac -e '[[1, 2], [3]]' -o t && ./t` → `[651968424, 651968360]` instead of `[[1, 2], [3]]` (inner seqs printed as i64 addresses; heap_flag not consulted by the printer).
- [x] **Docs claimed `foldl`/`foldr` are prelude.** Agent instructions (`CLAUDE.md`, `project-guidance.mdc`) and the public site now import from `Std\List`. `yonac -e 'foldl …'` still E0104 — that is correct.
- [ ] **`:>` (append) parses and type-checks but has no codegen.** Repro: `yonac -e '[1, 2] :> 3'` → `error: unsupported expression type`. Related: `--` (remove) and `in` (membership) are lexed as operator tokens and listed in `docs/language-syntax.md` but rejected by the parser (`yonac -e 'println (2 in [1, 2, 3])'` → E0301).
- [x] **codegen/print:** printing a tuple that mixes `Bool`/`String` fails LLVM module verification (`yona_rt_print_bool` / `yona_rt_print_string` called with `i64` instead of `i1`/`ptr`). Repro: `yonac -e "(true, \"hi\")"` (Yona bool literals are lowercase). Found 2026-08-19 while smoke-testing `Std\\GPU` discovery on macOS. **Fixed:** unbox i64 tuple slots to the LLVM type print helpers expect (`print_tuple_bool_string`).
- [x] **parser:** `perform` as a multi-binding `let` RHS breaks the binding list — `let a = perform Fs.read "x", b = perform Net.post "y" in a` → `E0301 Unexpected token` at the comma. Single-binding `let x = perform Fs.read "x" in x` parses fine. Found while drafting site homepage examples (2026-08-19). **Fixed:** `parse_perform_expr` now stops argument collection at `,` (same as `in`/`end`), so multi-binding `let` can follow (`Parser: perform as multi-binding let RHS`).
- [x] **effects/CLI:** unhandled `perform` through a let-bound lambda is a hard compile error **E0202** (primary at `perform`, note at the apply). Repro: `let plan = \() -> perform Fs.read "/etc/shadow" in plan ()` → `yonac` exits 1, no linked binary. Also `let apply = \f x -> f x in apply (\() -> perform Fs.read …) ()` (`Effect: HOF apply of perform lambda is E0202`). Valid `let f = \x -> perform E.op x in handle f v with …` still typechecks (`Effect: handle covers apply of lambda defined outside handle`, fixture `effect_lambda_handle`). Direct unhandled `perform` stays `-Wunhandled-effect`. Not codegen `handler_stack_` emptiness.
- [x] **effects/codegen:** handler clauses break beyond plain `resume value` — (a) comparing the op argument (`Fs.read path resume -> if path == "x" then resume path else ...`) crashes LLVM codegen (`ICmp` assertion, `Instructions.h:1183`); (b) `raise` inside a clause → "Terminator found in the middle of a basic block"; (c) interpolating a `perform`-bound value inside the handled expression (`"got: {manifest}"`) failed LLVM verification (`string_concat` got `i64` from `perform`, which is typed `INT`). The `(() -> a)` E0100 from the original note was not reproduced (no effect-row typing). **Fixed:** string `==` uses content compare when either side is `STRING`; `string_concat` IntToPtr's i64 slots; handler `CreateRet` skipped after `raise` (`effect_handler_compare_arg`, `effect_handler_raise`, `effect_handler_interp`). Working: canned `resume "lit"`, arg passthrough, real I/O with the arg (`resume (readFile path)` runs correctly). Found 2026-08-19 while drafting site homepage examples.
- [x] **prelude loading:** `Prelude.yonai` is located via cwd-relative `lib/`, ignoring `YONA_PATH` — from any other directory, prelude constructors silently vanish (`Some 42` → `E0104 undefined function 'Some'`; `case … of Some x -> …` misparse → `E0301 Expected '->' after pattern`). Repro: `cd /tmp && YONA_PATH=<repo>/lib yonac --emit-ir <repo>/test/codegen/prelude_none.yona`. **Fixed:** `load_prelude` and `yonac` append `YONA_PATH` directories to the module search list (`Prelude constructors load via YONA_PATH`).
- [x] **docs:** site homepage "shape of the language" and quick-start "pattern matching in ten lines" examples use top-level `type` declarations in expression programs — `yonac` rejects them (`E0301` at `type`); `type` requires a `module` wrapper. Homepage fix folded into the 2026-08-19 homepage rework. **Fixed:** quick-start already uses prelude `Some`/`None` (verified `yonac -e`); internal `docs/pattern-matching.md` and site `learn/pattern-matching.md` now keep `type` inside a `module` and use prelude Option for expression snippets.
- [x] **doctest:** `Codegen E2E` `binary_seek` / `binary_write_read` flakiness (stdout wrong / `0`) — unique object/exe per fixture suffix; plus single-binding `let` `auto_await` for real io_uring File ops (skip `spawn`).
- [x] **doctest:** `net_runtime_test` TCP loopback **SIGSEGV** (Linux io_uring) — accept SQE `socklen_t`* in `sqe.off`; shared ring in `uring_linux.c` (was per-TU `static` in `uring.h`); `ring_await` stashes unmatched CQEs. UDP test matches sync FN API.
- [x] **Windows** `tests`**/Vulkan:** undefined `yona_rt_promise_new` **/** `yona_rt_promise_complete` ( `gpu_stub.c` vs async runtime) — **Fixed:** same APIs + `yona_test_native_promise_immediate` implemented in `async_win32.c` (parity with `async_posix.c`).
- [x] **macOS arm64 build:** Clang 22 rejects `__builtin_setjmp` / `__builtin_longjmp` — **Fixed:** `yona_sjlj_setjmp` / `yona_sjlj_longjmp` in `include/yona/runtime/sjlj.h` (AArch64 inline asm matching llvm.eh.sjlj slots).
- [x] **macOS** `closure_consumed_sort` **/ typed-float:** `FunctionType::get` SIGSEGV after `fn->eraseFromParent()` — body instruction UAF (`getType()` null on Darwin). **Fixed:** snapshot LLVM type before erase; honor annotated return type so `Float -> Float` is not inferred as i64.
- [x] **macOS E2E** `stdlib_file_read` **/** `stdlib_io`:** `/etc/os-release` stub on non-Linux (same as Windows); stdout/stderr writes complete on the submit path so discarded `println` in `do` is visible (Linux io_uring already hits the kernel before submit returns).

Fixed Phase 0 / platform / import-`LINEAR` / LinearityChecker `WithExpr` +
`FunctionExpr` walk bugs are archived under Completed Milestones.



## Backlog (Open, Not Immediate)



### Code quality

- [ ] Relax stream-fusion gating only with benchmark evidence



### Performance

- [ ] LLVM EH migration (`invoke` / `landingpad`) if correctness requires it
- [ ] Profile-guided optimization
- [ ] JIT feasibility/design study (ORC/Cranelift/etc.)



### Language — Safety & Ownership

Shipped: `-Wunmatched-adt`, `@borrow` (E0603) — see Completed Milestones /
`docs/memory-management.md#explicit-borrow`.

- [ ] **Type-level borrows (**`&T`**) and signature carry-over** — follow-up
  to `@borrow` (see `memory-management.md#explicit-borrow`). **Design:**
  [design-borrow-types.md](./design-borrow-types.md) (syntax, `MonoType`,
  unification, `.yonai`, codegen wiring, phases). **Goal:** surface `&T`
  in type syntax and interfaces so cross-module APIs state callee-reads-only
  without per-def `@borrow`; derive `borrowed_params` from zonk’d types.
  **Non-goals for v1:** lifetimes, borrowed ADT fields. **Estimate:** 800–2000
  lines across lexer, `types`/`TypeChecker`, `.yonai`, tests; trait dispatch
  if methods gain `&` params.



### Language — Architecture & Infrastructure

Shipped: per-task-group bump arenas — see Completed Milestones.

- [ ] **Supervisors as effect handlers** — model Erlang-style
  supervision trees via the existing algebraic effect system. A
  supervisor is a `handle ... with` that catches child-task failures
  and decides restart/escalate/ignore. No compiled functional
  language has this. Depends on structured concurrency landing first.
- [ ] **Content-addressed code** (Unison-inspired). Functions
  identified by hash of their AST, not by name. Enables: perfect
  caching (same function = same hash = skip recompile), zero-conflict
  merges, refactoring without breakage. Yona's `.yonai` interface
  files with GENFN source are already halfway there — they embed
  source for cross-module monomorphization. Content-addressing would
  make this principled. Research-phase; significant tooling impact
  (package manager, LSP, VCS integration).



### Language — GPU / Heterogeneous Compute

Yona is **concurrent and non-blocking first**; GPU work should compose
with **async**, **task groups**, and **channels**, not compete with them.
**Design:** [design-gpu-async.md](./design-gpu-async.md). Status table:
`docs/gpu-architecture.md` (*Roadmap implementation status*).

**Track G surface — shipped** (details in Completed Milestones /
`docs/gpu-transparent-lowering.md`): `extern native` / NAT; columnar
`Std\GPU` + Vulkan kernels; `mapGPU` / `reduceGPU` / `mapFloatGPU` /
`reduceFloatGPU`; `mapReduceGraphGPU`; async float fence + early cancel
(**-887**); Vulkan-mapped `PinnedFloats` + `gpuFloatChannel` /
`drainMapFloatGPU`; transparent fixed-kernel lowering; `GpuIssue` /
`checkGpu` / use-site `perform Gpu.*`; `--strict-accelerator` / **E0700**.

**Open follow-ups:**

- [ ] **Arbitrary-lambda → SPIR-V** — today: fixed kernel library lowers to
  Std\GPU ABI (including `x - k` → mapAdd, `0 - x` → mapMul(-1), `x * x` →
  `mapSquare`, `x < k` → `filterLessThan` with a real Vulkan LT mark shader).
  Other lambdas stay on the host path, or `yonac --strict-accelerator`
  errors (**E0700**). Missing: full SPIR-V compiler for arbitrary closures
  (e.g. `\x -> x + x * x`).
- [ ] **Optional io_uring / reactor GPU integration** (research) — fence
  waiter thread is shipped; Option C in `design-gpu-async.md` §3.2
  (interleave GPU completion in the io_uring reap loop) is not. No concrete
  design deliverable beyond the option note.
- [ ] **CPU/GPU occupancy and scheduling hints** (research) — optional
  attributes or stdlib helpers for **wave size**, **shared memory**, or
  “prefer throughput vs latency”; avoid until `Std\GPU` has users and
  profiling data. No concrete design deliverable yet.
- [x] **`perform Gpu` from `Std\GPU` via GENFN + use-site `handle`** — designed
  path (2026-08-19): kernels stay `GpuIssue` / `Result`; `raiseGpu` /
  `withGpuFallback` `perform Gpu.*` and remonomorphize inside a user `handle`
  so clauses bind (effect rows, not a C++ name list). Precompiled export
  raises `:UnhandledEffect` if called with no handler. Full runtime handler
  stack across all precompiled bodies remains out of scope (CPS compile).
- [ ] **macOS / Windows GPU bench re-capture** after Track G — Linux path
  verified in-tree; macOS MoltenVK and Windows Vulkan crossover / pinned
  benches (`bench/run_gpu_compare.py`, including `gpu_pinned_scale_hot`)
  were not re-run in the Track G landing pass (this agent environment is
  Linux-only). Refresh `docs/benchmark-results-macos.md` /
  `docs/benchmark-results-windows.md` when those hosts are available.



### Language — Metaprogramming & Introspection

- [ ] **Multi-Stage Programming** — compile-time computation.
  `static regex_compile pattern = ...` compiles regex at build time.
  Hygienic macros via staging.
- [ ] **Compile-Time Evaluator** — evaluate pure functions at compile time.
  Enables user-defined derive strategies, constant folding, static assertions.
  Requires: subset interpreter for pure Yona expressions (no I/O, no effects).
  Tracked as [#4](https://github.com/yona-lang/yonac-llvm/issues/4); blocked on
  #5 (and ideally #7). See Active Priorities §4.
- [ ] **User-Defined Derives** — traits declare themselves derivable via
  `derive` block that templates over ADT structure. Requires compile-time
  evaluator or external codegen tool reading enriched `.yonai` metadata.
- [ ] **Quasiquotes / Template Expressions** — `quote { expr }` captures
  AST for manipulation. `splice expr` inserts computed AST into code.
  Enables: DSLs, custom syntax extensions, code generation.



### Language/runtime research

- [ ] Gradual typing with contracts
- [ ] LLVM coroutine lowering for async
- [ ] Distributed Yona
- [ ] Serialization system
- [ ] STM

(macOS MoltenVK device init is shipped — Completed Milestones / Active Priorities §2.)



### Tooling

- [ ] Package manager/build tool
- [ ] LSP server
- [ ] **Documentation extraction / generation (think first, don't scrape).** Public Learn/Guides/Reference in `site/src/content/docs/` stay handwritten. `scripts/gendocs.py` is a regex walk of `lib/Std/*.yona` `##` comments and misses `.yonai`/C modules, types, effects, and exports. Design a compiler-aware extractor (`yonac --emit-docs` or successor) so API reference cannot drift from source; until then, keep `##` comments + handwritten site pages updated in the same change (see `keep-docs-up-to-date`).



## Completed Milestones (Condensed)

- [x] Full frontend + LLVM codegen pipeline (modules, generics, traits, ADTs)
- [x] Effects + async + structured concurrency foundations
- [x] Persistent collections + RC/arena/perceus optimizations
- [x] Windows parity closure: runtime backends, tests green, skips removed
- [x] CLI/REPL path hardening (`--sysroot`, `YONA_HOME`, packaged runtime discovery)
- [x] Platform ABI freeze (`YONA_PLATFORM_ABI_VERSION=1`) and platform architecture docs
- [x] Handle-level `Std\File` operations routed through platform wrappers
- [x] CI/CD across Linux/macOS/Windows + Linux arm64 coverage in matrix
- [x] Docker + Homebrew + RPM + DEB + release workflow
- [x] Windows + macOS benchmark matrix parity + RSS / startup probe hardening
- [x] O(1) transfer-scope + unified seq/map transfer bookkeeping; borrow `.yonai` metadata
- [x] Stdlib `.yonai` regeneration; channel deadlock detection
- [x] `-Wunmatched-adt`; `@borrow` / E0603; per-task-group bump arenas
- [x] Phase 0 CI: flaky `binary_seek` / `binary_write_read`; `net_runtime_test` TCP SIGSEGV
- [x] Windows Vulkan promise symbols; macOS SJLJ + Darwin codegen UAF; macOS File/IO stubs
- [x] LinearityChecker type-directed + `.yonai` `LINEAR` overlay + FQN `ModuleCall`
- [x] LinearityChecker walk of `WithExpr` (Closeable discharge) + `FunctionExpr` bodies
- [x] `yonac` `##` preamble modules; type-system audit #3; bench reference check + Erlang skip
- [x] macOS kqueue + MoltenVK `Std\GPU` portability
- [x] `Std\GPU` columnar + Vulkan P0–P3/filter + `--emit-accelerator-report` + crossover benches
- [x] **Track G surface:** mapGPU/graphs/cancel/pins/channels/transparent lowering/`perform Gpu`/E0700
- [x] Transparent kernel library: `x - k` / `0 - x` / `x * x` / `x < k` (`mapSquare` + Vulkan `filterLessThan`)
- [x] `raiseGpu` / `withGpuFallback` GENFN remonomorphize inside user `handle`
- [x] Vulkan ergonomics (timeline probe, `vulkanLastNote`, Windows `vulkan-1.lib` path)
- [x] Linker/distribution + v0.1.2/v0.1.3 packaging (LLD, Copr, Homebrew); Windows WiX scaffold



## Notes

- Benchmark reports:
  - `docs/benchmark-results.md` (Linux baseline)
  - `docs/benchmark-results-windows.md` (Windows reruns)
  - `docs/benchmark-results-macos.md` (macOS Apple Silicon / MoltenVK)
- Stream-fusion evidence gate (required before relaxing fusion constraints):
  - run full benchmark matrix on Linux + Windows with 3 reruns (`-n 10`) and compare medians
  - require >=5% median win on the targeted fusion rows and no correctness/test regressions
  - allow no >3% median regression on non-targeted rows (or require documented root cause + follow-up fix)
- Distribution assumption: normal `yonac` usage should not require an external C compiler;
packaged runtime artifacts are the default path. System C compiler use is
treated as an explicit advanced/fallback mode.
- Formal specification program of record:
  [docs/superpowers/plans/2026-08-17-yona-rocq-formalization.md](./superpowers/plans/2026-08-17-yona-rocq-formalization.md)
  (Rocq theories under `formal/` once Phase 0 lands).
- Process hygiene: update this todo list, the active plan under
  `docs/superpowers/plans/`, and `CHANGELOG.md` after each implementation round.
- Keep this file focused on actionable open work and short milestone summaries.
- `ctest` for `doctest_tests` sets `YONA_COMPILE_GPU_VULKAN=0` so unit tests do
not pick up a stray Vulkan runtime compile from the parent environment; see
`docs/gpu-architecture.md` and `CLAUDE.md` (Run tests).
