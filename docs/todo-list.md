# Yona-LLVM - Status and Roadmap

## Current Snapshot

- Compiler: Yona -> LLVM IR -> native executable via `yonac`
- REPL: `yona` compile-and-run interactive mode
- Tests: doctest (`gpu_vulkan_device` + optional `gpu_vulkan_mapadd` / mapMul / reduce); codegen `gpu_backend_flags` + `gpu_vulkan_last_note` (child env `YONA_GPU_DISABLE_VULKAN=1`); with `-DYONA_ENABLE_VULKAN=ON`, run `ctest -R doctest_gpu_vulkan -V` (see `CLAUDE.md`); full `tests.exe` per `CLAUDE.md` (`YONA_PATH`, `YONAC_CC` on Windows)
- **Local Windows verify (2026-04-26, agent):** `cmake --build out/build/x64-debug --target tests` **fails** because CMake reconfigure cannot find compilers at `C:/local/LLVM/bin/clang.exe` / `clang++.exe` (preset `windows-llvm-toolchain.cmake` default); this shell also had `LLVM_INSTALL_PREFIX=C:/loocal/LLVM` (typo / non-existent). **Stale** `out/build/x64-debug/tests.exe` run without a working `clang` on `PATH` → **207 passed / 29 failed** — `append_runtime_objects` / codegen harness return `RT_COMPILE_ERROR` (no C compiler for runtime `.o` + link). **Action:** install LLVM 22+ at `C:\local\LLVM` or set `LLVM_INSTALL_PREFIX` to the real prefix; ensure `clang` is on `PATH` or set `YONAC_CC` to a full path; then rebuild `tests` and re-run.
- Windows benchmark run (2026-04-26): 35/35 Yona rows passing, report refreshed, perf deltas reviewed
- Runtime backends: Linux + Windows native paths in place

## Active Priorities

### 1) Benchmarking hardening

- [ ] Add a benchmark reference conformance check (validate expected output per language lane)
- [ ] Investigate/resolve local Erlang lane toolchain crash (`erl.exe`/`erlc.exe` exit `0xC0000005`) affecting comparison coverage on this host

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
- [ ] Vulkan `Std\GPU`: device-local + staging for **`reduceSum`** sums buffer (or full dual-buffer path); GPU **`filterGreaterThan`** / compaction; macOS MoltenVK (P1–P3 mapAdd/mapMul/reduce + map staging landed — `docs/gpu-vulkan-implementation-plan.md` §11)
- [ ] Transparent accelerator lowering implementation after crossover data justifies it

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
