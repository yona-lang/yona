# Plan: Vulkan compute for `Std\GPU` (Option A: `find_path` + `VULKAN_SDK` + link loader)

This document is the implementation plan for a **clean, modular, testable, and documented** Vulkan path that:

- Discovers the Vulkan **headers** via `find_path` with **`VULKAN_SDK`** (and platform fallbacks).
- **Links** the platform Vulkan loader import library (Windows: `vulkan-1.lib`; Linux: `libvulkan` / `Vulkan::Vulkan` from `FindVulkan` when available).
- Keeps a **no-SDK** configuration viable for contributors and default CI (CPU-only, existing behavior).
- Extends the current `gpu_cpu.c` / `gpu_vulkan.c` split rather than replacing the whole runtime in one step.

It does **not** implement the work; it constrains how it is done.

---

## 1. Goals and non-goals

**Goals**

- Select Vulkan when the build enables it **and** runtime successfully creates a compute-capable device.
- Preserve **explicit** `upload` / `materialize` boundaries and the existing Yona `Std\GPU` API surface.
- **Fallback** to the existing CPU (scalar/SIMD) path on any failure (missing SDK at build, missing loader at run, no compute queue, out-of-memory, etc.).
- **Document** build-time flags, environment variables, and runtime capability reporting.

**Non-goals (first milestone)**

- Transparent compiler lowering (see `docs/gpu-transparent-lowering.md` separately).
- Multi-GPU, presentation/swapchain, or graphics.
- `filter` with **zero** host/device synchronization (single submit end-to-end); the current path uses two submits so the host can allocate the output length from the inclusive scan tail.

---

## 2. Configuration model (CMake)

### 2.1 Cache option

- Introduce `YONA_ENABLE_VULKAN` (default **`OFF`**) or equivalent, so the default project clone builds **without** any Vulkan requirement.

### 2.2 Discovery (Option A)

When `YONA_ENABLE_VULKAN` is **ON**:

1. **`VULKAN_SDK`**: Prefer the environment variable (document that LunarG’s installer sets it on Windows).
2. **Headers**: `find_path(Vulkan_INCLUDE_DIR NAMES vulkan/vulkan.h HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include" …)` and standard `FindVulkan` / known paths.
3. **Loader library**:
   - **Windows**: `find_library` for `vulkan-1` under `$ENV{VULKAN_SDK}/Lib` and `$ENV{VULKAN_SDK}/Lib32` as needed; or `find_package(Vulkan)` if it sets imported targets.
   - **Linux**: Use **`FindVulkan`** (`find_package(Vulkan)`), or `find_library` for `vulkan` in standard locations, consistent with the distro.

Expose to the build:

- `Vulkan::Headers` (interface) and link target or explicit `VULKAN_LIB` for the loader.

### 2.3 Fail-soft behavior

- If `YONA_ENABLE_VULKAN` is ON but headers/library are not found, **FATAL_ERROR** with a message pointing to `VULKAN_SDK` and the docs — **or** (product choice) **fall back to OFF** with a `CMake` warning. Prefer **fatal** for “I asked for Vulkan” to avoid shipping silent CPU-only “Vulkan” builds; document clearly.

### 2.4 P0: headers + library discovery (no app link to loader)

P0 adds `cmake/YonaVulkan.cmake`, the shared header `include/yona/runtime/gpu_build_config.h` (`-DYONA_COMPILE_GPU_VULKAN=1` when enabled), and compile lines for `compiled_runtime` / `yonac` / test shell builds. The loader **import library** is still **discovered** (Option A) so the SDK is complete, but the **final Yona program is not** linked to `vulkan-1` / `libvulkan` yet, so a missing `vulkan-1.dll` on Windows does not block execution of general programs. `gpu_vulkan_device.c` resolves all `vk*` entry points at runtime via
`vkGetInstanceProcAddr` after `dlopen`/`LoadLibrary` — no import-library link on
shipped binaries. Environment `YONA_COMPILE_GPU_VULKAN=1` + `VULKAN_SDK`
matches CMake when using `yonac` to compile the runtime from sources.

**Unit tests:** CMake appends `YONA_COMPILE_GPU_VULKAN=0` to the CTest
environment for `doctest_tests` so the in-process `compiled_runtime` scratch
build stays aligned with the default (non-Vulkan) `gpu_vulkan.c` path even when
a developer’s shell exports Vulkan build variables.

### 2.5 Generated config header

- Generate (e.g. `include/yona/runtime/gpu_build_config.h` or `build/gpu_build_config.h`) with:

  - `#define YONA_GPU_VULKAN_ENABLED 1` (or 0) so C code and `#include` paths do not depend on command-line length.
  - Optional: `YONA_VULKAN_SDK_VERSION` string for diagnostics.

- **All** TUs that include Vulkan must include this or get `-DYONA_GPU_VULKAN_ENABLED=1` from CMake consistently.

### 2.6 Targets to wire

| Target / artifact | Change |
|-------------------|--------|
| `yona_runtime_static` | `target_include_directories` + `target_compile_definitions` when enabled; **no** loader link in P0 (see §2.4). |
| `add_custom_command` for `compiled_runtime.o` | Append `-I` for `Vulkan_INCLUDE_DIR` and the generated config; define `YONA_GPU_VULKAN_ENABLED` to match. **Linking** happens at final link, not in `cc -c` for the single TU — but any **second** object that uses Vulkan and is linked with user programs may need a split (see Section 3). |
| `tests` | Same include/link as runtime when building test helpers that call Vulkan. |
| `yona_lib` / `yona_lib_static` | N/A for Vulkan C unless a future host-side helper lives in the library; keep Vulkan out of the LLVM C++ code initially. |

**Important implementation detail:** `gpu_vulkan.c` is `#include`d from `compiled_runtime.c` into one `compiled_runtime.o`. While it references **no** `vk` entry points (P0), the final link has **no** extra Vulkan args. When `vk*` are introduced, either add delay-load / `dlopen` or link the loader and document the new dep (see §2.4).

**Embedded `yonac` path:** When `yonac` compiles `compiled_runtime.c` on the fly (no packaged `.o`), it must add the same **`-I`** for the configured Vulkan include and **generated** `gpu_build_config.h`. Practical approaches:

- **A (recommended for parity):** Generate `gpu_build_config.h` into the build dir and require **`VULKAN_SDK`** and **`-I $builddir`** when developers compile from source without CMake artifacts — document this; or
- **B:** Emit a small **`compile_flags.txt` or `yonac` sysroot** next to the compiler that lists extra `-I`/`--D` flags produced by CMake (more tooling).
- **C:** Rely on **prebuilt** `compiled_runtime.o` from CMake for all supported workflows (developer runs `cmake --build` first); `yonac` only uses sources when `compiled_runtime.o` is missing (document the limitation).

The plan should pick **one** and document it in `CLAUDE.md` / `README` under GPU.

---

## 3. Code layout (modular C)

Keep Vulkan-only logic out of `gpu_cpu.c`’s hot loops and avoid a single 3000-line file.

Suggested modules under `src/runtime/gpu/` (or flat `src/runtime/` with clear prefixes — pick one style and stick to it):

| Module | Responsibility |
|--------|------------------|
| `gpu_backend.h` | Opaque `yona_gpu_context`, capability queries, `init` / `shutdown` (process-lifetime, lazy singleton). |
| `gpu_vulkan_device.c` | `VkInstance`, `VkPhysicalDevice`, `VkDevice`, queue family index, `VkQueue`, enabled extensions (none at first, or `VK_KHR_*` as needed for portability). |
| `gpu_vulkan_memory.c` | `VkBuffer`, `vkAllocateMemory`, host-visible vs device-local, staging ring or per-op staging (simple first). |
| `gpu_vulkan_pipeline.c` | `VkShaderModule` from **embedded SPIR-V**, `VkPipeline`, `VkPipelineLayout`, `VkDescriptorSetLayout`, pool. |
| `gpu_vulkan_dispatch.c` | Record command buffer, `vkQueueSubmit`, `vkQueueWaitIdle` or **fence** (use fence for testability; avoid `WaitIdle` in hot paths). |
| `gpu_vulkan_scaffold.c` | Retain or merge current `LoadLibrary` / loader check; **or** replace with `vkGetInstanceProcAddr` after link — when linking, `vkCreateInstance` is resolved via the loader. |

`gpu_cpu.c` becomes the **orchestrator**: if `YONA_GPU_VULKAN_ENABLED` and `context->use_device`, dispatch `mapAdd` / `reduceSum` to Vulkan; else existing CPU. Keep the existing **C ABI** names (`yona_Std_GPU_raw__*`) as the only Yona boundary.

**Threading:** One **mutex** (or per-context queue serial section) around `submit` + wait so multiple Yona tasks do not interleave the same `VkCommandBuffer` or reuse pending buffers unsafely. Document in `docs/gpu-architecture.md`.

**Buffer identity:** Today `Buffer` is host `IntArray` in the `RC_TYPE_ADT` box. For Vulkan you need either:

- A **tagged** representation: e.g. high bit, enum field, or separate ADT constructor `BufferDevice …` in Yona (API change) — *avoid in milestone 1 if possible*; or
- An **internal** C-only tag: the raw pointer in the ADT payload points to a small **header** struct `{ enum backend; union { int64_t* host; yona_vulkan_buffer* dev; } }` with a known layout owned by the runtime. **Milestone 1** can use a **parallel** `struct yona_gpu_buffer` map keyed by a cookie if you must avoid changing the Yona `Buffer` type — but a **clean** design prefers one opaque pointer type in C, documented in `docs/api/GPU.md`.

The plan: **add a sub-milestone** “Buffer tagging in C” before “full GPU map/reduce” so tests can assert host vs device without new Yona surface syntax.

---

## 4. Shaders and SPIR-V (build)

### 4.1 Source layout

- `src/runtime/gpu/shaders/*.comp` (GLSL compute) — one or few kernels to start: e.g. `i64_map_add.comp`, `i64_reduce_sum.comp`.

### 4.2 Build-time compilation

- `find_program(glslc HINTS "$ENV{VULKAN_SDK}/Bin" …)`.
- `add_custom_command` / `add_custom_target` to produce `*.spv` in `${CMAKE_CURRENT_BINARY_DIR}/gpu_spv/` (or in-tree **generated** — prefer **out-of-tree**).
- Optional: `add_dependencies` on `yona_runtime_static` for SPIR-V generation when Vulkan is on.

### 4.3 Embedding SPIR-V in the runtime

- **C array embedding:** `xxd` / Python / small CMake `file(READ ... HEX)` step to generate `gpu_spv_embedded.c` with `static const uint32_t kShader_map_add[] = { ... }` — no filesystem dependency at runtime, easy for `yonac` packaging.
- Document rebuilding shaders when GLSL changes.

### 4.4 CI

- **Linux:** Install Vulkan SDK or use **pre-generated `.spv`** in-repo so CI does not need `glslc` (pick one: **generated-only in CI** is simpler; **full pipeline** on release builds).

---

## 5. Public API and capability reporting (documentation)

- **`backendName`:** Return something like `vulkan` when the active path is device compute; keep `cpu-simd` / `cpu-scalar` otherwise.
- **`hasGpu`:** `1` when `backendName` is using the GPU for at least the operations that are implemented (or define: “device created and not lost”).
- **`vulkanStatus`:** Refine to multiple states if useful: e.g. `vulkan-ok` / `vulkan-unavailable` / `vulkan-failed` (init failed) — only if `Std\GPU` contract is updated and tests/docs match.

Update `docs/api/GPU.md` and `docs/gpu-architecture.md` for:

- `YONA_ENABLE_VULKAN`, `VULKAN_SDK`, `YONA_GPU_DISABLE_VULKAN` (existing) interaction.
- That **end users** still only need a driver; **developers** need SDK for Option A **builds**.

---

## 6. Testing strategy (well-testable)

### 6.1 Layers

1. **Pure C helpers** (optional): e.g. SPIR-V length alignment, buffer size math — `doctest` in `test/` C++ with internal headers declared `YONA_TEST` or `extern "C"` test hooks.
2. **Vulkan-optional integration:** Build `tests` with Vulkan; add a `TEST_CASE` that **skips** if `!yona_gpu_vulkan_init()` or `YONA_GPU_DISABLE_VULKAN=1` — mirrors CI without GPU.
3. **Golden / codegen:** Keep existing `test/codegen/gpu_*.yona` for CPU parity; add one fixture that prints `backendName` and checks numeric results **either** on CPU or GPU (assert same `reduceSum` result). Prefer **value-based** expectations, not string backend names, unless a dedicated `gpu_backend_flags` style test is extended.
4. **Linux CI headless:** Document **Lavapipe** (`VK_ICD_FILENAMES` / Mesa) for true GPU code paths in CI, or accept **CPU-only** CI and run Vulkan on developer machines and Windows CI if available.

### 6.2 Debug validation layers

- Optional: `YONA_VULKAN_VALIDATION=1` sets instance flags to enable `VK_LAYER_KHRONOS_validation` when present. Not default in performance builds.

---

## 7. Phased roll-out (suggested)

| Phase | Deliverable | Exit criteria |
|-------|-------------|-----------------|
| **P0** | CMake `find_path`/`find_library`, generated `gpu_build_config.h`, link loader on static runtime + `yonac` link path updated | Config-only PR; `ctest` green; no SPIR-V yet. |
| **P1** | `vkCreateInstance` … `vkCreateDevice` + compute queue, **no** user kernels | `vulkanStatus` reports success; `hasGpu` still 0 or new internal flag; unit test for init/teardown. |
| **P2** | Buffers + one compute shader (e.g. `mapAdd` only) + readback | `hasGpu=1` when path used; `gpu_*.yona` pass with same results as CPU; **benchmark** row shows Vulkan when enabled. |
| **P3** | `reduceSum`, `mapMul` on device; `filter` compaction on device (prefix on GPU). | Full doc update; `docs/todo-list.md` partial tick. |
| **P4** | Crossover tuning, optional `WaitIdle` removal, **profiling** | Per `docs/gpu-architecture.md` benchmark policy. |

---

## 8. Files to touch (checklist)

- `CMakeLists.txt` — `find_path`, `find_library` / `find_package`, options, `gpu_build_config.h`, `compiled_runtime` compile line, `yona_runtime_static` link, test link.
- `cli/main.cpp` — Embeddable `clang` flags for `compiled_runtime` when P0+ requires Vulkan include path (or document prebuilt-`.o` only).
- `test/yona_link_util.hpp` (if used for link tests) — same Vulkan link as executables.
- `src/compiled_runtime.c` — include new GPU submodules (or keep `#include` pattern).
- `lib/Std/GPU.yonai` / `GPU.yona` — only if capability strings or `hasGpu` semantics change.
- `docs/api/GPU.md`, `docs/gpu-architecture.md`, `CLAUDE.md` or `README` — build instructions.
- `bench/runner.py` / bench docs — if GPU affects paths or env for benchmarks.

---

## 9. Risk register

- **Link mismatch:** `compiled_runtime.o` built with `YONA_GPU_VULKAN_ENABLED=1` but link forgets `vulkan-1` / `-lvulkan` → unresolved symbols. **Mitigation:** single CMake function `yona_link_runtime_vulkan(target)` and reuse everywhere.
- **Developer source-compile path:** `yonac` without CMake loses Vulkan include path. **Mitigation:** generated header + document `VULKAN_SDK`, or only support Vulkan in CMake-built runtimes.
- **ABI drift:** Yona `Buffer` stays `IntArray` but C stores device buffers — need **strict** C-side contract tests. **Mitigation:** P0.5 buffer header struct + tests.

---

## 10. Reference: existing code entry points

- Capability stub: `src/runtime/gpu_vulkan.c`
- CPU ABI: `src/runtime/gpu_cpu.c` (`yona_Std_GPU_raw__*`, `yona_Std_GPU__*`)
- Yona: `lib/Std/GPU.yona`
- Architecture: `docs/gpu-architecture.md`

This plan is the intended roadmap for a **Option A, find_path + VULKAN_SDK, link loader** implementation that remains **modular, testable, and documented** while preserving **CPU fallback** and **optional** SDK for default builds.

---

## 11. Implementation status (2026-04)

**Landed (Windows/Linux + macOS MoltenVK device init):**

- P0–P1 as in §2.4 / §5: runtime `dlopen`/`LoadLibrary` of the loader; no import-library link on the main executable.
- P2–P3 columnar ops: embedded SPIR-V for **`mapAdd`**, **`mapMul`**, block **`reduceSum`**, **`filterGreaterThan`** (mark + prefix + scatter); **cached** compute pipelines in `gpu_vulkan_compute.c`; **`gpu_vulkan_ops.c`** for dispatch; submit serialized with a mutex; **`mapAdd`/`mapMul`/`reduceSum`/`filterGreaterThan`** use **device-local SSBO + staging** when a discrete-style device-local memory type exists (else host-visible SSBO); **`filterGreaterThan`** uses **GPU** inclusive prefix (ping-pong int64 scan), inclusive→exclusive, then scatter (two queue submits; optional **`YONA_GPU_VULKAN_FILTER_CPU_PREFIX=1`** for the legacy CPU prefix).
- **`hasGpu`** reflects successful init + `shaderInt64`; cached until shutdown (`yona_gpu_vulkan_invalidate_capability_cache`).
- **`Std\GPU.vulkanLastNote`** mirrors `yona_gpu_vulkan_device_last_note()`.
- Opt-in env: **`YONA_GPU_VULKAN_COMPUTE`**, **`MAPADD`**, **`MAPMUL`**, **`REDUCE`**, **`FILTER`**, min-length vars (see `docs/api/GPU.md` / `docs/gpu-architecture.md`).

**macOS / MoltenVK:** `yona_gpu_vulkan_open_loader` searches `VULKAN_SDK/lib`, `HOMEBREW_PREFIX/lib`, `/opt/homebrew/lib`, and `/usr/local/lib` for `libvulkan.1.dylib` / `libMoltenVK.dylib` before bare dylib names (dyld does not search Homebrew by default) and `dlopen`s with **`RTLD_GLOBAL`** so the ICD can resolve loader symbols. When `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` are unset, the runtime points them at Homebrew or SDK **`MoltenVK_icd.json`** so the loader enumerates the portability ICD. If the process already **links** the loader (`YONA_HAS_VULKAN` / `gpu_stub`), device init uses the linked **`vkGetInstanceProcAddr`** and falls back to `dlsym` / `RTLD_DEFAULT` when GIPA returns NULL for `vkCreateInstance` (two loader images). Instance creation opts into **`VK_KHR_portability_enumeration`** so `vkEnumeratePhysicalDevices` sees the Metal ICD. Logical devices enable **`VK_KHR_portability_subset`** when enumerated. Apple unified memory does not expose a discrete-only `DEVICE_LOCAL` heap, so columnar ops use the host-visible SSBO path. Metal typically lacks **`shaderInt64`** and **`shaderFloat64`**: `vulkanStatus` may be `vulkan-device` while `hasGpu` remains 0 and GPU int64/f64 kernels do not dispatch. Doctest `gpu vulkan device: try_init device-ready or loader-only` **requires** `try_init == 0` on Apple when a loader is visible.

**Still open:** merging filter’s two submits into one where validation allows; crossover-driven transparent lowering; Metal-native int32/f32 kernels if Apple GPU compute is required without `shaderInt64`.
