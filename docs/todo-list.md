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

## Backlog (Open, Not Immediate)

### Code quality

- [ ] Relax stream-fusion gating only with benchmark evidence

### Performance

- [ ] LLVM EH migration (`invoke` / `landingpad`) if correctness requires it
- [ ] Profile-guided optimization
- [ ] JIT feasibility/design study (ORC/Cranelift/etc.)

### Language/runtime research

- [ ] Gradual typing with contracts
- [ ] Supervisors as effect handlers
- [ ] LLVM coroutine lowering for async
- [ ] Distributed Yona
- [ ] Serialization system
- [ ] STM
- [ ] Content-addressed code model
- [ ] Multi-stage programming and compile-time evaluator
- [ ] User-defined derives and quasiquotes
- [ ] Vulkan `Std\GPU`: macOS MoltenVK (`docs/gpu-vulkan-implementation-plan.md` §11)
- [ ] Transparent accelerator lowering: **`yonac --emit-accelerator-report`** (JSON `yona.accelerator_diag.v1`) lists explicit `Std\GPU` sites (`program`, `module_ast`, optional **`--emit-accelerator-report-with-types`** → `module`); extend to future transparent candidates; then implement lowering once archived `--json-report` curves justify it (`docs/gpu-transparent-lowering.md`)

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
