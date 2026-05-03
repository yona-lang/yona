# Yona-LLVM - Status and Roadmap

## Current Snapshot

- Compiler: Yona -> LLVM IR -> native executable via `yonac`
- REPL: `yona` compile-and-run interactive mode
- Tests: doctest (`gpu_vulkan_device` + optional `gpu_vulkan_mapadd` / mapMul / reduce); codegen `gpu_backend_flags` + `gpu_vulkan_last_note` (child env `YONA_GPU_DISABLE_VULKAN=1`); with `-DYONA_ENABLE_VULKAN=ON`, run `ctest -R doctest_gpu_vulkan -V` (see `CLAUDE.md`); full `tests.exe` per `CLAUDE.md` (`YONA_PATH`, `YONAC_CC` on Windows)
- **Windows dev checklist:** full **`clang+llvm-*-windows-msvc`** tree for **`LLVM_INSTALL_PREFIX`**, correct env spelling, **`CC`/`CXX`** or CMake compiler flags if Clang lives outside that prefix, and **`YONAC_CC`** or **`PATH`** for doctest (`tests.exe`) — see **INSTALL.md** (Windows: *Complete Windows LLVM tree*, *`YONAC_CC` and doctest*).
- Windows benchmark run (2026-04-26): 35/35 Yona rows passing, report refreshed, perf deltas reviewed
- **GPU crossover benches:** `bench/run_gpu_compare.py`, `bench/gpu_bench_meta.py`, and `bench/accelerators/*` hot + 10k/5k rows (`.yona` + `.expected`) for map/reduce, filter, columnar pipeline, materialize — see `bench/README.md` and `docs/benchmark-results-windows.md`
- **Vulkan filter compute path:** multi-stage SPIR-V under `src/runtime/gpu/shaders/filter_*.comp` (embedded `*_spv.inc`), wired through `gpu_vulkan_ops.c` / `gpu_vulkan_compute.c` / `gpu_cpu.c`, API surface in `lib/Std/GPU.yona` + `docs/gpu-architecture.md` / implementation plan
- Runtime backends: Linux + Windows native paths in place
- **Accelerator diagnostics (`docs/gpu-transparent-lowering.md`):** `yonac --emit-accelerator-report` → JSON `yona.accelerator_diag.v1`; expression programs use `report_kind":"program"` (post typecheck + `solve_constraints`); modules default to **`report_kind":"module_ast"`**; **`--emit-accelerator-report-with-types`** (with the report flag, module sources only) runs **`typecheck_module_for_accelerator_report`** then emits **`report_kind":"module"`** (optional `inferred_type` when HM pretty-print is informative). API: `include/AcceleratorDiag.h`; tests: `accelerator_diagnostic_report_*` in `test/codegen_test.cpp`. Collector uses a heap-held `std::function` so recursive apply-walk does not dangle.
- **`bench/runner.py`:** `--verify-reference-outputs` / `--reference-verify-langs` for golden `.expected` checks without `yonac`; clearer Erlang `erlc` failure diagnostics when BEAM exits `0xC0000005` on Windows hosts

## Active Priorities

### 1) Benchmarking hardening

- [x] Add a benchmark reference conformance check (validate expected output per language lane) — `python3 bench/runner.py [-- FILTER] --verify-reference-outputs [--reference-verify-langs LANGS]` (`LANGS` comma list or `all`; default `c`; does not require `yonac`); exits 1 on mismatch; see `bench/README.md`
- [ ] **Next:** Investigate/resolve local Erlang lane toolchain crash (`erl.exe`/`erlc.exe` exit `0xC0000005` / `-1073741819` / `3221225477`) — **Repro (2026):** `erl.exe -noshell -eval "erlang:display(hello), init:stop()."` and `erlc.exe` on a module-wrapped `bench/reference/fibonacci.erl` both crash before emitting stderr; `where erl` → `C:\\Program Files\\Erlang OTP\\bin\\erl.exe`. Not a `runner.py` regression: BEAM fails outside the bench. Next steps: repair/reinstall OTP for Windows, try another OTP patch release, exclude AV interference, or use **WSL/Linux Erlang** for `--compare-erl` / `--reference-verify-langs erl`; document CI matrix skip for broken Windows OTP if needed

### 2) Platform/runtime closure

- [ ] macOS platform layer (kqueue path + per-OS runtime files)
- [ ] macOS `Std\GPU` Vulkan path: MoltenVK / portability (device init, memory types, shaderInt64 / SPIR-V validation); verify against Windows/Linux before treating GPU compute as cross-desktop

### 3) Distribution readiness

- [ ] Windows installer productionization (upgrade behavior, signing, final UX polish)
- [ ] Final packaging pass for sysroot-based CLI/REPL distribution layout
- [ ] Enable embedded LLD backend by default across supported toolchains (resolve remaining dependency gates, e.g. MSVC-compatible LibXml2 on Windows)
- [ ] Implement true embedded LLD backend for Linux/macOS in-process mode

### Suggested next steps (rolling)

- [x] **yonac module files with `##` preamble** — `is_module_source` in
  `cli/main.cpp` skips `#` line comments before detecting `module`, so
  `yonac -o lib/Std/M.yona lib/Std/M.yona` works for Http-style leading docs.

High leverage near-term: **channel deadlock detection** (debuggability,
smaller than coroutine rewrite), **`&T` / borrow types** — design
[design-borrow-types.md](./design-borrow-types.md) then internal `MonoType`
slice (see item below), **LLVM coroutine plan** only after cancellation
semantics are frozen. **GPU:** [design-gpu-async.md](./design-gpu-async.md)
documents fences / promises / task groups; **`extern native`** + first
`Std\GPU` mul2 promise API are **shipped** — next is higher-level `mapGPU` /
timeline semaphores / group cancel before transparent lowering. Product:
**LSP** or **package manager** if adoption beats runtime research.

### Bugs

- [ ] **doctest:** `Codegen E2E` subcases `binary_seek` / `binary_write_read` sometimes
  return stdout `0` instead of expected totals (102 / 12); repro: `ctest --preset unit-tests-linux`
  or `./tests -tc="Fixture-based codegen tests"` (scratch `/tmp` paths + `rewrite_codegen_fixture_tmp_paths`).
- [ ] **doctest:** `net_runtime_test` TCP loopback case **SIGSEGV** on some Linux runs
  (`TCP connect accept send recv on loopback`); repro: same ctest / `./tests -tc="*Runtime Net*"`.

## Backlog (Open, Not Immediate)

### Code quality

- [ ] Relax stream-fusion gating only with benchmark evidence

### Performance

- [ ] LLVM EH migration (`invoke` / `landingpad`) if correctness requires it
- [ ] Profile-guided optimization
- [ ] JIT feasibility/design study (ORC/Cranelift/etc.)

### Language — Safety & Ownership
- [x] **Use-site handling for algebraic types** — `-Wunmatched-adt` (`--Wall`):
  discarded `App` types except `Seq`/`Set`/`Dict` in non-final `do` steps
  or `let _ = ...` emit a warning (see `RefinementChecker` + `TypeChecker`).
- [x] **Borrow annotations for read-only parameters** (first slice).
  **Shipped:** optional `@borrow` before a parameter (parser + AST),
  enforced with **E0603** via the same escape predicate as borrow
  inference (`analysis/BorrowEscapeAnalysis`). When valid, codegen
  matches **automatic inference** (no extra refcount win vs inferred
  code). **Benefits:** documented callee-reads-only contract; compile
  errors on refactors that would reintroduce `rc_inc`/`rc_dec`; base for
  future `&` / type-level borrows. See
  `docs/memory-management.md#explicit-borrow` and `bench/README.md`
  (parity benchmark vs `list_sum`). **Not yet:** `&` in types, distinct
  borrow types in HM, or inference loosened beyond escape analysis.
- [ ] **Type-level borrows (`&T`) and signature carry-over** — follow-up
  to `@borrow` (see `memory-management.md#explicit-borrow`). **Design:**
  [design-borrow-types.md](./design-borrow-types.md) (syntax, `MonoType`,
  unification, `.yonai`, codegen wiring, phases). **Goal:** surface `&T`
  in type syntax and interfaces so cross-module APIs state callee-reads-only
  without per-def `@borrow`; derive `borrowed_params` from zonk’d types.
  **Non-goals for v1:** lifetimes, borrowed ADT fields. **Estimate:** 800–2000
  lines across lexer, `types`/`TypeChecker`, `.yonai`, tests; trait dispatch
  if methods gain `&` params.

### Language — Architecture & Infrastructure
- [x] **Per-task-group arenas** — each task group allocates from a
  bump arena that's freed wholesale on group completion. Leverages
  existing arena + escape analysis (`include/EscapeAnalysis.h`) +
  the structured concurrency plan. Kills the "raise leaks heap
  values" problem for task groups on `raise` unwind (TLS bind stack
  + `yona_rt_group_end` from `exceptions.c`) and on success via codegen.
  Benchmark: `bench/concurrency/task_group_arena.yona`.
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
Rough order: **explicit device API** → **async completion + cancellation**
→ **multi-stage pipelines** → **transparent lowering** last (needs stable
eligibility + effect “schedule” story). **Design:**
[design-gpu-async.md](./design-gpu-async.md).

- [x] **Compiler: `extern native` + `.yonai` NAT imports** — `ast::ExternPromiseKind`
  (`Sync` / `ThreadPool` / `IoUring` / `NativePtr`), `PromiseAwaitPath` on
  `TypedValue` for `auto_await`, shared **`declare_import_extern_fn`** /
  **`bind_imported_promise_cf`** (`CodegenModule.cpp`). Fixture
  `test/codegen/extern_native_immediate.yona`; `yona_test_native_promise_immediate`
  in `async.c`. Documented in `docs/design-gpu-async.md` §8.0.

- [ ] **Std\GPU + Vulkan runtime (roadmap)** — **Shipped:** `lib/Std/GPU.yona` /
  `lib/Std/GPU.yonai` — columnar **`Buffer`** / IntArray **`mapAdd`/`mapMul`/`reduceSum`**
  / **`filterGreaterThan`**, discovery helpers (`backendName`, `vulkanStatus`,
  `hasGpu`, …), and **`floatArrayMul2Async : FloatArray -> Int`** (`extern native`,
  `Promise Int` at use sites; C `yona_Std_GPU__floatArrayMul2Async` → fence-thread promise).
  Optional CMake `find_package(Vulkan)` + Unix probe; **`src/runtime/gpu_stub.c`**
  (ctx init/shutdown, nop + f64 mul2 SPIR-V from `gpu_nop.comp` /
  `gpu_f64_mul2.comp`, regenerate via `scripts/gen_gpu_nop_spv.sh` /
  `scripts/gen_gpu_f64_mul2_spv.sh`). Optional tests: `YONA_GPU_TEST_DISPATCH=1`,
  `YONA_GPU_TEST_F64_MUL2=1`, `YONA_GPU_TEST_F64_MUL2_ASYNC=1`; `test/gpu_stub_test.cpp`.
  **Next:** `mapGPU` / `reduceGPU` on `FloatArray`/`IntArray`, **timeline semaphores**,
  drop **`vkDeviceWaitIdle`** from hot paths, **task-group cancel** integration,
  pinned buffers / channels, multi-stage graphs, GPU effect/capability bit.
  Longer-term transparent lowering still ~500 lines runtime + SPIR-V/lambda
  codegen (no new syntax for v1 user API). Yona strengths: aliasing discipline,
  effects, unboxed arrays, LTO — see design doc.
- [ ] **Transparent GPU lowering** (future). Compiler automatically
  lowers FloatArray.map/foldl to GPU when the array is large enough
  and the lambda is pure. Transparent, like Yona's transparent async.
  Inspired by: Halide (algorithm/schedule separation — effects as
  schedules), Julia (@cuda JIT specialization — maps to Yona's
  deferred compilation), ArrayFire/Accelerate (combinator fusion →
  GPU dispatch — maps to Yona's stream fusion).
- [ ] **GPU + async: non-blocking submit and completion** — **Partial:** fence
  waiter thread + **`extern native`** + `yona_rt_promise_complete` already avoid
  parking **pool** workers on GPU; still need **timeline semaphores** (or
  equivalent), optional **io_uring / reactor** integration, and **structured
  concurrency** (task-group cancel drops pending / skips callbacks). Task
  threads must never block on **`vkDeviceWaitIdle`** / **`vkQueueWaitIdle`** in
  the common path. Prerequisite: stable cancellation story for async (see
  coroutine item).
- [ ] **Pinned host buffers + channels (CPU↔GPU pipeline)** — optional
  `GpuBuffer` / pinned-memory type produced by `allocReadback` or
  `withPinned …`; **senders/receivers** (or iterators) move chunks between
  CPU producers and GPU kernels without memcpy where the driver allows.
  Interesting for **streaming** (log lines, sensor blocks) without loading
  full `FloatArray` before dispatch. Watch **refcount + async lifetime**:
  buffer must stay alive until GPU completes; align with Perceus
  transfer or explicit `Linear` device handles.
- [ ] **Multi-stage GPU graphs (task-parallel maps)** — compile a **DAG**
  of kernels (map → map → reduce) into a single **Vulkan command buffer**
  or `VK_KHR_synchronization2` barrier graph to reduce host round-trips;
  mirrors **parallel comprehensions** / `Std\Parallel` at a coarser
  granularity. Start after single-kernel `Std\GPU` works.
- [ ] **GPU “capability” or effect bit** — treat **device lost**, **OOM
  on alloc**, and **non-uniform subgroup** assumptions as a tracked
  capability or `perform Gpu.submit` under a **handler** so library code
  can degrade (CPU fallback) without lying about purity. Keeps **effect
  discipline** consistent with the rest of the language.
- [ ] **CPU/GPU occupancy and scheduling hints** (research) — optional
  attributes or stdlib helpers for **wave size**, **shared memory**, or
  “prefer throughput vs latency” — analogous to **branch hints** on CPU;
  avoid until `Std\GPU` has users and profiling data.

### Language — Metaprogramming & Introspection
- [ ] **Multi-Stage Programming** — compile-time computation.
  `static regex_compile pattern = ...` compiles regex at build time.
  Hygienic macros via staging.
- [ ] **Compile-Time Evaluator** — evaluate pure functions at compile time.
  Enables user-defined derive strategies, constant folding, static assertions.
  Requires: subset interpreter for pure Yona expressions (no I/O, no effects).
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
- [ ] Vulkan `Std\GPU`: macOS MoltenVK (`docs/gpu-vulkan-implementation-plan.md` §11)

### Tooling

- [ ] Package manager/build tool
- [ ] LSP server

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
- [x] Windows benchmark matrix parity (all language cells filled) + native peak RSS capture
- [x] Startup probe cache normalization (versioned cache + stale Windows RSS invalidation)
- [x] Windows benchmark refresh + 3x stability reruns (perf deltas + large-file I/O variance)
- [x] O(1) transfer-scope basic block detection (scope-entry ordinal watermark + O(1) droppability checks)
- [x] Unified seq/map transfer bookkeeping into explicit domain-tagged transfer tracking
- [x] Borrow inference metadata across `.yonai` module boundaries, with caller temp cleanup and unwind-safe owned fallbacks
- [x] Stdlib `.yonai` regeneration blockers resolved across all `lib/Std/**/*.yona` modules
- [x] Deterministic channel deadlock detection with managed worker compensation
- [x] Initial `Std\GPU` accelerated columnar API with CPU/SIMD fallback, runtime ABI, tests, docs, and crossover benchmarks
- [x] Optional Vulkan P0: `YONA_ENABLE_VULKAN` + `find_path`/`find_library` (`VULKAN_SDK`), `include/yona/runtime/gpu_build_config.h`, `compiled_runtime`/`yonac`/`yona_link_util` compile parity, `VK_NO_PROTOTYPES` (no app link to loader — see `docs/gpu-architecture.md`)
- [x] Vulkan P1 scaffold: `gpu_vulkan_device.c` (runtime `vkGetInstanceProcAddr` dispatch, compute queue), `vulkan-device` / refined `vulkanStatus`, `yona_lib*` compile parity when `YONA_ENABLE_VULKAN`, doctest `gpu_vulkan_device_test.cpp`
- [x] Vulkan P2 slice: embedded `map_add_int64` SPIR-V (`src/runtime/gpu/shaders/map_add_int64.comp` → `map_add_int64_spv.inc`), `shaderInt64` when available, opt-in `YONA_GPU_VULKAN_MAPADD=1` + `YONA_GPU_VULKAN_MAPADD_MIN_LEN` (default 4096) from `gpu_vulkan_ops.c` / `gpu_vulkan_compute.c` included by `gpu_vulkan_device.c`
- [x] Vulkan P3 extension: `map_mul_int64` + `reduce_block_int64` SPIR-V, `YONA_GPU_VULKAN_MAPMUL` / `REDUCE` / `COMPUTE`, cached pipelines, `hasGpu` + `vulkanLastNote` in `Std\GPU`, codegen env isolation for GPU fixtures
- [x] Vulkan filter pipeline: SPIR-V stages (`filter_mark_int64`, prefix/flags/inclusive-exclusive/scatter) + `*_spv.inc` embeds, runtime dispatch in `gpu_vulkan_ops.c` / `gpu_vulkan_compute.c`, CPU path alignment in `gpu_cpu.c`, device caps in `gpu_vulkan_device.h`
- [x] GPU Vulkan crossover tooling: `bench/run_gpu_compare.py` (hot + 10k/5k accelerators), `--json-report`, `bench/gpu_bench_meta.py`; Windows capture procedure in `docs/benchmark-results-windows.md` (*Std\GPU / Vulkan crossover*)
- [x] `yonac --emit-accelerator-report` — JSON `yona.accelerator_diag.v1` for explicit `Std\GPU` sites (`report_kind`: `program` after typecheck; modules `module_ast` or **`--emit-accelerator-report-with-types`** → `module`); `include/AcceleratorDiag.h`, `typecheck_module_for_accelerator_report`, doctest in `codegen_test.cpp`
- [x] Linker/distribution milestone: dual modes, prebuilt runtime packaging, in-process LLD scaffold, CI reporting, and CMake modularization
- [x] Windows installer milestone: WiX scaffold + Windows release CI artifacts (ZIP + MSI)

## Notes

- Benchmark reports:
  - `docs/benchmark-results.md` (Linux baseline)
  - `docs/benchmark-results-windows.md` (Windows reruns)
- Stream-fusion evidence gate (required before relaxing fusion constraints):
  - run full benchmark matrix on Linux + Windows with 3 reruns (`-n 10`) and compare medians
  - require >=5% median win on the targeted fusion rows and no correctness/test regressions
  - allow no >3% median regression on non-targeted rows (or require documented root cause + follow-up fix)
- Distribution assumption: normal `yonac` usage should not require an external C compiler;
  packaged runtime artifacts are the default path. System C compiler use is
  treated as an explicit advanced/fallback mode.
- Process hygiene: update this todo list after each implementation round.
- Keep this file focused on actionable open work and short milestone summaries.
- `ctest` for `doctest_tests` sets `YONA_COMPILE_GPU_VULKAN=0` so unit tests do
  not pick up a stray Vulkan runtime compile from the parent environment; see
  `docs/gpu-architecture.md` and `CLAUDE.md` (Run tests).
