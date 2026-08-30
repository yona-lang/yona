# Vulkan Runtime Implementation Record

This document records the canonical Vulkan implementation for `Std\Gpu` and
the remaining work. The runtime has one source layout, one C ABI, and one CMake
configuration path.

## Configuration

`YONA_ENABLE_VULKAN` is `OFF` by default. When enabled,
`cmake/YonaVulkan.cmake` requires both `vulkan/vulkan.h` and a platform Vulkan
loader:

- Windows: `vulkan-1.lib`, normally from `VULKAN_SDK`.
- Linux: `libvulkan` from the system Vulkan development package or SDK.
- macOS: the Vulkan loader or MoltenVK from Homebrew or `VULKAN_SDK`.

An explicitly enabled build fails configuration when either dependency is
missing. Default builds compile the same GPU component without Khronos headers
and use the CPU/SIMD implementation.

The configured GPU objects compile once into `yona_runtime_gpu`, which the
aggregate `yona_runtime` archive consumes. The compiler, runner, REPL, tests,
installed tools, and generated programs all link that archive. No tool compiles
runtime sources on demand.

## Canonical source layout

| File | Responsibility |
|------|----------------|
| `include/yona/Runtime/Gpu/Api.h` | Public `Std\Gpu` C entry points and ownership contract. |
| `include/yona/Runtime/Gpu/BuildConfig.h` | Build-time Vulkan feature gate. |
| `include/yona/Runtime/Gpu/VulkanDevice.h` | Internal device, capability, and operation declarations. |
| `src/Runtime/Gpu/Cpu.c` | CPU/SIMD kernels and the backend-neutral `Std\Gpu` boundary. |
| `src/Runtime/Gpu/Stub.c` | Vulkan context, async float submission, and fence/timeline completion. |
| `src/Runtime/Gpu/VulkanLoader.c` | Platform loader discovery and MoltenVK ICD setup. |
| `src/Runtime/Gpu/VulkanDevice.c` | Physical/logical device selection and synchronized lifecycle. |
| `src/Runtime/Gpu/VulkanCompute.c` | Cached shader modules, layouts, and compute pipelines. |
| `src/Runtime/Gpu/VulkanOperations.c` | Columnar command recording, submission, and readback. |
| `src/Runtime/Gpu/VulkanInternal.h` | Component-private shared Vulkan state. |
| `src/Runtime/Generated/` | Canonically named GLSL sources and generated SPIR-V fragments. |

The device, pipeline, and operation sources are separate translation units.
Cross-file declarations live in component headers; source files do not include
other source files.

## Runtime behavior

`Std\Gpu` always has a CPU implementation. Vulkan is an optimization selected
only when all of these conditions hold:

1. The build enabled Vulkan.
2. `YONA_GPU_DISABLE_VULKAN` does not disable it.
3. Loader and device initialization succeed.
4. The operation is supported by the selected device.
5. The input reaches the configured crossover threshold.

The runtime scores compute-capable physical devices, preferring discrete
devices and useful integer/float shader capabilities. Override selection with
`YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX` for diagnostics.

Metal commonly lacks `shaderInt64` and `shaderFloat64`. A ready MoltenVK device
still makes `hasGpu` true; integer operations use i32 shaders when values and
results fit, and float operations use the f32 path when necessary.

Device state is protected during initialization, submission, asynchronous
completion, and shutdown. Shutdown waits for active users before destroying
Vulkan objects. Async float operations return opaque `YonaTaskRef` handles and
copy a mandatory `YonaTypeDescriptor` for the result before completing through
the Concurrency component. Task-group cancellation may
complete the promise before submitted device work drains, but resource cleanup
continues safely and cancelled writeback is discarded.

## Implemented kernels

The Vulkan path includes embedded shaders for:

- integer add, multiply, square, and block sum;
- greater-than and less-than filtering through mark, prefix, and scatter;
- float scale/multiply-two and block sum;
- fixed multi-map plus reduce command-buffer graphs;
- the internal no-op lifecycle probe.

`mapGpu`, `reduceGpu`, `mapFloatGpu`, and `reduceFloatGpu` accept fixed kernel
ADTs. Transparent lowering recognizes the same fixed library. Arbitrary Yona
lambdas stay on the host path or produce E0700 under `--strict-accelerator`.

## Generated shaders

Handwritten shader inputs and generated fragments live together under
`src/Runtime/Generated/` with UpperCamelCase names. Run:

```bash
python3 scripts/generate_gpu_shaders.py --check
python3 scripts/generate_gpu_shaders.py --write
```

The check writes to temporary output and compares bytes, so verification never
mutates the source tree. The generator records the required `glslangValidator`
version and keeps generated output deterministic.

## Validation

Configure with `-DYONA_ENABLE_VULKAN=ON` to register the optional Vulkan
doctests. They use the same `yona_runtime` archive as generated programs and
skip only when no usable loader/device exists. Value-based tests compare Vulkan
results with the CPU implementation.

Set `YONA_VULKAN_VALIDATION=1` to request
`VK_LAYER_KHRONOS_validation` when installed. Quality CI also exercises GPU
lifecycle stress under ThreadSanitizer and validates generated shaders. See
`docs/quality.md` and `docs/gpu-architecture.md` for the complete commands and
environment controls.

## Remaining work

The following work remains deliberately open and is also tracked in
`docs/todo-list.md`:

- compile arbitrary Yona lambdas to SPIR-V;
- evaluate integration of GPU completion with the io_uring/reactor loop;
- design CPU/GPU occupancy and scheduling hints after profiling and demand;
- refresh macOS and Windows crossover benchmarks on representative hardware.

These items do not require a second runtime representation or API.
