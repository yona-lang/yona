# Std.GPU

Std\GPU — accelerated columnar execution.

The initial backend is portable CPU execution over `IntArray` columns. It
keeps explicit upload/materialize boundaries so programs are ready for future
Vulkan or vendor-backed device storage without changing the high-level API.
With `YONA_COMPILE_GPU_VULKAN`, optional Vulkan compute can handle `mapAdd`,
`mapMul`, `reduceSum`, and `filterGreaterThan` (see `docs/gpu-architecture.md`).
`mapAdd`/`mapMul`/`reduceSum`/`filterGreaterThan` prefer device-local SSBOs with staging
when VRAM allows; set `YONA_GPU_VULKAN_HOST_SSBO=1` to force the legacy host-mapped SSBO path.
`filterGreaterThan` uses GPU mark + GPU inclusive prefix + exclusive indices +
GPU scatter when enabled (`YONA_GPU_VULKAN_FILTER` or `YONA_GPU_VULKAN_COMPUTE`).
Set `YONA_GPU_VULKAN_FILTER_CPU_PREFIX=1` to force the older host-side prefix (debug only).

## Types

### Buffer

`type Buffer = Buffer IntArray`

Opaque accelerator buffer. The CPU backend stores an owned IntArray copy.

## Functions

### `backendName : String`

Active backend name. Currently `cpu-simd` or `cpu-scalar`.

### `vulkanStatus : String`

Vulkan status string: `vulkan-unavailable`, `vulkan-loader`, or `vulkan-device`
(device only when built with Vulkan headers and init succeeded).

### `vulkanLastNote : String`

Short hint from the last failed Vulkan init, opt-in int column GPU attempt,
async **`vkWaitForFences`**, or other **`VkResult`** failures on the **`Std\GPU`**
float path / test dispatch (`gpu_stub.c`). After a successful device init
without **`shaderInt64`** (typical MoltenVK / Metal), records that IntArray
GPU kernels use i32 when values fit. Empty after int64-capable success or when
Vulkan was not compiled in. Same source as the C **`yona_gpu_vulkan_device_last_note()`**
helper.

### `vulkanLastIssueKind : Int`

0 = no classified **VkResult** yet; 1 = out-of-memory; 2 = device lost; 3 = other
(updated with **`vulkanLastNote`** when the runtime records a **`VkResult`**).

### `hasGpu : Bool`

True when Vulkan is enabled at build, not disabled by `YONA_GPU_DISABLE_VULKAN`,
and device init succeeds. IntArray kernels use i64 when `shaderInt64` is
available, otherwise i32 when values fit. Result is cached until
`yona_gpu_vulkan_device_shutdown()`.

### `hasSimd : Bool`

True when the CPU backend was built with a known SIMD baseline.

### `vulkanAvailable : Bool`

True when a Vulkan loader is visible to the process (`vulkan-1.dll`,
`libvulkan.so.1`, or on macOS `libvulkan.1.dylib` / `libMoltenVK.dylib`
via `VULKAN_SDK`, `HOMEBREW_PREFIX`, or the lib dir CMake recorded).

### `vulkanTimelineSemaphore : Bool`

True when device init succeeded (see **`hasGpu`** / **`vulkanStatus`**) and the
probe finds timeline semaphores: Vulkan 1.2+ **`timelineSemaphore`** via **`vkGetPhysicalDeviceFeatures2`**
(Vulkan 12 feature chain), or **`VK_KHR_timeline_semaphore`** in the device's extension list
(Vulkan 1.0/1.1 stacks that expose the capability only as an extension). When
**`VK_KHR_synchronization2`** is enabled on the lazy **`gpu_stub`** device, async
float compute may wait on a **timeline semaphore** instead of a fence (**`YONA_GPU_ASYNC_TIMELINE=0`**
forces the legacy fence path).

### `available : () -> Bool`

### `apiVersion : () -> Int`

### `physicalDeviceCount : () -> Int`

### `upload : IntArray -> Buffer`

Copy a host IntArray into accelerator-owned storage.

### `materialize : Buffer -> IntArray`

Copy accelerator-owned storage back to a host IntArray.

### `length : Buffer -> Int`

Number of elements in the buffer.

### `mapAdd : Int -> Buffer -> Buffer`

Add a constant to every element. Vulkan path when `YONA_GPU_VULKAN_MAPADD=1`
or `YONA_GPU_VULKAN_COMPUTE=1` and length ≥ min (default 4096; see docs).

### `mapMul : Int -> Buffer -> Buffer`

Multiply every element by a constant. Vulkan when `YONA_GPU_VULKAN_MAPMUL=1`
or `YONA_GPU_VULKAN_COMPUTE=1` (min length env vars in docs).

### `filterGreaterThan : Int -> Buffer -> Buffer`

Keep values greater than the threshold. Vulkan when `YONA_GPU_VULKAN_FILTER`
or `YONA_GPU_VULKAN_COMPUTE=1` and length thresholds are met (`docs/gpu-architecture.md`).

### `reduceSum : Buffer -> Int`

Sum all values. Vulkan when `YONA_GPU_VULKAN_REDUCE=1` or
`YONA_GPU_VULKAN_COMPUTE=1` (min length in docs); else SIMD/scalar CPU.

### `floatArrayMul2Async : FloatArray -> Int`

Experimental: in-place x2 on `FloatArray` via native promise (see `docs/design-gpu-async.md`).
The C wrapper creates the `VkDevice`/pools on first use (`yona_gpu_vulkan_ctx_init`) when built with Vulkan.

### `floatArrayScaleAsync : Float -> FloatArray -> Int`

In-place multiply each element by `scale` (same Vulkan path as `floatArrayMul2Async`).
