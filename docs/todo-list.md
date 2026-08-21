# Yona-LLVM - Status and Roadmap

## Bugs (open)

- [ ] `Std\Stream.naturals` remonomorphize cannot see sibling `range`
  after GENFN name isolation. Repro: `import naturals, take, sum from
  Std\Stream in sum (take 10 naturals)` — `undefined function 'range'`
  (`<imported>:3:60`). Same for `stream_pipeline` (`naturals |> map …`).
  Isolation clears importer `extern_functions` and only restores names
  already in `imports_.meta`; `range` is not imported so it is dropped.
  Fixtures: `stdlib_naturals_caf`, `stream_pipeline`.

The `in` terminator vs membership parse cluster (`nested_let`, `perform`/
`handle` RHS, `stdlib_math`) is fixed — `stop_at_in` is now threaded through
`let`, `perform` args, `raise`, and `with` the same way as `if`/`lambda`.

Fixed Phase 0 / platform / import-`LINEAR` / LinearityChecker `WithExpr` +
`FunctionExpr` walk bugs, plus the 2026-08-21 `Std\Json.get` / nested-`try`
fixes, are archived under Completed Milestones.

## Current Snapshot

- Compiler: Yona -> LLVM IR -> native executable via `yonac`
- Runner: Yona-written `yona` (`tools/yona/main.yona`) — shebang
  `#!/usr/bin/env yona`, `yona -e`, piped stdin, `Std\Process.getArgs`.
  No-args TTY starts C++ `yona-repl`. `yonac` compiles only (`yonac -`
  for stdin). Old C++ plan:
  [2026-08-19-yona-script-shebang.md](./superpowers/plans/2026-08-19-yona-script-shebang.md)
  (superseded 2026-08-20).
- Tests: doctest (`gpu_vulkan_device` + optional `gpu_vulkan_mapadd` / mapMul / reduce); codegen `gpu_backend_flags` + `gpu_vulkan_last_note` (child env `YONA_GPU_DISABLE_VULKAN=1`); with `-DYONA_ENABLE_VULKAN=ON`, run `ctest -R doctest_gpu_vulkan -V` (see `CLAUDE.md`); full `tests.exe` per `CLAUDE.md` (`YONA_PATH`, `YONAC_CC` on Windows). Windows `yona`/`yonac` script doctests need `wrap_for_cmd_c` (MSVC `popen`/`cmd /c` quote stripping) and `qarg` for non-path argv (`-e` source keeps `\`); `stdlib_process` uses `PATH` not `HOME`.
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
- [x] `#!/usr/bin/env yona` script mode + `Std\Process.getArgs` after package install — Yona-written `yona` runner; C++ REPL is `yona-repl`
- [ ] Final packaging pass for sysroot-based CLI/REPL distribution layout
- [ ] Enable embedded LLD backend by default across supported toolchains (remaining gate: MSVC-compatible LibXml2 on Windows)



### 4) Type system (GitHub #3–#8)

Program of record: issues on `yona-lang/yona`. Execution plan:
[2026-08-17-next-plan-of-action.md](./superpowers/plans/2026-08-17-next-plan-of-action.md).
Formal specification (Rocq, parallel research track):
[2026-08-17-yona-rocq-formalization.md](./superpowers/plans/2026-08-17-yona-rocq-formalization.md)
— see § Formal specification below. Do not start CTE (#4) or a full typed-core
API (#7) until the audit (#3) and effect-row story (#8) are honest about what
already works.

- [x] **[#3](https://github.com/yona-lang/yona/issues/74) Type-system status audit** — `docs/type-system-status.md` (2026-08-18). Next: #8. Follow-ups from the audit: [#9](https://github.com/yona-lang/yona/issues/80) effect decls, [#10](https://github.com/yona-lang/yona/issues/81) blocking E0500/E0600, [#11](https://github.com/yona-lang/yona/issues/82) `-Wincomplete-patterns`.
- [x] **[#8](https://github.com/yona-lang/yona/issues/79) Effect-row inference +** `.yonai` **propagation** — after #3. **Landed 2026-08-19:** closed sets + E0202; unify of effect rows; HOF rest vars; apply-union / wrap; handler subtraction; pretty-print `!{…}`; `.yonai` `FN … effects Fs.read`. **Follow-ups landed same day:** open HOF rest (`effects | hof`, `Effect: imported HOF open rest from .yonai is E0202`, `Interface files preserve exported HOF open rest`); sibling-aware module typecheck (`Interface files preserve sibling-wrapped FN effect rows`, wrap-before-sibling). HOF restore is the `apply f x = f x` shape (first param is the function). Empty-row totality is **#5**; parsed `effect` decls are **#9**. Plan `docs/superpowers/plans/2026-08-19-effect-row-inference.md`.
- [ ] **[#6](https://github.com/yona-lang/yona/issues/77) Opaque exported types** — after #3; parallelizable with #8. `export type T opaque` (syntax TBD); hide constructors across modules.
- [ ] **[#5](https://github.com/yona-lang/yona/issues/76) Opt-in totality / effect-freedom** — after #8 (empty row must be real). Annotation or flag; facts in `.yonai`. Does **not** evaluate at compile time.
- [x] **[#7](https://github.com/yona-lang/yona/issues/78) Typed-core API** — arch doc after #3; thin slice after #8. Versioned in-process C++ query types + C ABI (no LLVM headers in the consumer). Defer wire format. **Seed 2026-08-20:** `include/typed_core/Query.h` + `yls`. **2026-08-21:** `include/typed_core/abi.h` (`YONA_TYPED_CORE_ABI_VERSION`), example pretty-print backend, `yonac --emit-typed-core`, `docs/typed-core.md`.
- [ ] **[#4](https://github.com/yona-lang/yona/issues/75) Deterministic evaluator (CTE)** — after #5 and either #7 or a documented typechecked-AST subset. Pure total exprs only; no macros / arbitrary native at compile time.
- [ ] **`Linear FileHandle` (and other resources) for real** — today `.yonai`
  marks `openFile` / `spawn` / sockets / channel ends as bare `LINEAR`
  (`Linear a` in docs); the runtime still allocates a `FileHandle` ADT;
  `closeFileHandle` / `readBytes` take `Int` and unpack the fd. Target API:
  `openFile : String -> FileMode -> Linear FileHandle`,
  `spawn : String -> Linear Process`, same for TCP/UDP and channel ends.
  Consume with `with` or `case Linear h -> …`; `Closeable` on the payload,
  no parallel raw-`Int` handle path. `.yonai` must carry `LINEAR` **and**
  the inner ADT. Linearity leaks / use-after-consume must fail the build
  (not warn) on expression programs **and** module bodies — blocked on
  [#10](https://github.com/yona-lang/yona/issues/81). Update
  `docs/linear-types.md`, `docs/api/File.md` / Process / Net / Channel,
  and the site Memory + type-system pages in the same change. Evidence:
  codegen fixtures that typecheck `Linear FileHandle` and reject a leak.

Default series: `#3 → #8 → #5 → #7 → #4`, with **#6** beside #8 after the audit.
`Linear FileHandle` can proceed beside #10 (interface + stdlib) but must not
claim “done” until leaks are errors in modules.

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

- [ ] **Next language work:** [#6](https://github.com/yona-lang/yona/issues/77)
  opaque types, or [#5](https://github.com/yona-lang/yona/issues/76)
  totality / empty-row gate now that #8 rows are real, or **`Linear FileHandle`
  for real** (stdlib + `.yonai` payload types; hard errors with #10). Formal
  spec track: Phase 0 of
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
  Tracked as [#4](https://github.com/yona-lang/yona/issues/75); blocked on
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
- [x] **VS Code extension + `yls` language server** — C++ `yls --stdio` +
  `editors/vscode` (2026-08-20). Plan:
  [2026-08-20-yona-vscode-lsp.md](./superpowers/plans/2026-08-20-yona-vscode-lsp.md).
  Design: [2026-08-20-yls-vscode-design.md](./superpowers/specs/2026-08-20-yls-vscode-design.md).
  Site: [Editor and language server](../site/src/content/docs/guides/editor.md).
  Review fixes (2026-08-20): `#`/`##` module detection, pattern-binding
  index, JSON `\u` decode, workspace roots + watched-file refresh,
  juxtaposition signature help, Windows binary stdio. Cross-file
  go-to-definition for imports and FQN calls, document highlight, and
  import-rename staying in the current file (2026-08-21). Incremental /
  partial AST recovery on parse failure (2026-08-21): hover and related
  queries walk a recovered prefix; published diagnostics stay the original
  parse errors.   Local VSIX packaging is in (`npm run vsix`, CI artifact).
  Marketplace CI publishes on `v*` tags (`VSCE_PAT`). Open VSX publish is
  fully wired (`publish-openvsx` + repository secret `OVSX_PAT`, publisher
  `yona-lang`, extension 0.1.4); the job still no-ops if the secret is
  unset. Stdlib prereqs for a Yona `yls` landed 2026-08-21: recursive
  `Std\Json`, `Std\IO.readExact`, `Std\Utf16`. `yls-yona` uses
  `Std\Json.get` / `asString` / `asInt`. Remaining: Yona rewrite of `yls`
  as the editor default.
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
- [x] `let` RHS `if`/`else` stops at closing `in` (not membership); `2 in xs` still works
- [x] Sequence `:>` / `--` / `in` parse and compile; `perform` as multi-binding `let` RHS
- [x] Collection print: nested seq/set/dict heap flags; tuple slot unbox for print helpers
- [x] Imported HOF values materialize closures; Stream GENFN detached from temp `__Import` module
- [x] GENFN remonomorphize of private helpers; `yonac` exits 1 on codegen errors (not a zero binary)
- [x] Sibling-aware module effect rows (no E0104 on private helpers); Stream.map/toSeq on Seq is E0100
- [x] HAMT destroy `rc_dec`s heap keys/values; prelude constructors load via `YONA_PATH`
- [x] Effect handlers: string `==`, concat of `perform` values, `raise` terminator; apply of unhandled `perform` is E0202
- [x] Docs: `type` in expression programs needs a `module`; `foldl`/`map` are `Std\List` not prelude
- [x] Yona-written `yona` runner (`tools/yona/main.yona`); C++ REPL is `yona-repl`; `yonac -e` removed (`yona -e` / `yonac -`)
- [x] GENFN sibling register: no caller-local capture (`rest`/`cmp` on `isEmpty`); no shadowing other-module imports (`String.drop`)
- [x] AFN as the body of a `let`-bound function auto-awaits (not a Promise pointer)
- [x] Invalid UTF-8 is a lex error; `tokenize()` recovery advances one byte (no `brk` loop)
- [x] `with` parser null-body SIGSEGV: `parse_expr_until_in()` for resource, null checks before `WithExpr` ctor
- [x] `try`/`catch` consumes closing `end` (nested `do`/`let`/`case`, multi-arm
  `catch`); missing `end` and trailing tokens are parse errors, not SIGSEGV
- [x] VS Code extension + C++ `yls` (stdio LSP) + shared TextMate grammar + `typed_core/Query.h`
- [x] Typed-core C ABI + `yonac --emit-typed-core` + pretty-print backend (`docs/typed-core.md`)
- [x] **2026-08-21:** Imported `Std\Json.get` / ADT helpers SIGSEGV from
  expression programs. Repro: `import parse, get as jsonGet, asString from
  Std\Json in case parse src of Ok j -> case jsonGet j "method" of Some v
  -> asString v`. `yonac` failed LLVM verify (`ptr` vs `i64` on the
  unwrapped string) or rejected `JsonObject pairs` at parse time. GENFN
  call sites now propagate `return_subtypes` / `return_adt_name`;
  capitalized pattern names are constructors even before the `.yonai` is
  loaded.
- [x] **2026-08-21:** `Std\Json.get` returned `None` on a multi-key object
  when the same program `import length from Std\String` and also called
  `readExact`. Repro: `import parse, get as jsonGet, asString from
  Std\Json, stdinFd, readExact from Std\IO, length from Std\String in
  case readExact stdinFd 75 of Ok body -> case parse body of Ok j ->
  jsonGet j "method"`. GENFN remonomorphization isolates importer names
  so Prelude Array `length`/`get` in `getPair` are not replaced by
  `Std\String`.
- [x] **2026-08-21:** Nested `try` whose inner `catch` re-raises printed
  `()` instead of the outer handler. Repro: `try (try raise 1 catch _ ->
  raise 2 end) catch _ -> 3 end`. `codegen_try_catch` marks merge
  unreachable when every catch arm terminates.



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
