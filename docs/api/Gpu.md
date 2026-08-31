# Std.Gpu

Std\Gpu — accelerated columnar execution.

The initial backend is portable CPU execution over `IntArray` columns. It
keeps explicit upload/materialize boundaries so programs are ready for future
Vulkan or vendor-backed device storage without changing the high-level API.
With CMake `-DYONA_ENABLE_VULKAN=ON`, optional Vulkan compute can handle `mapAdd`,
`mapMul`, `mapSquare`, `reduceSum`, `filterGreaterThan`, and `filterLessThan`
(see `docs/gpu-architecture.md`).
`mapAdd`/`mapMul`/`mapSquare`/`reduceSum`/`filterGreaterThan`/`filterLessThan`
prefer device-local SSBOs with staging when VRAM allows; set
`YONA_GPU_VULKAN_HOST_SSBO=1` to force host-visible mapped storage.
Filter kernels use GPU mark + GPU inclusive prefix + exclusive indices +
GPU scatter when enabled (`YONA_GPU_VULKAN_FILTER` or `YONA_GPU_VULKAN_COMPUTE`).

User-facing `mapGpu` / `reduceGpu` dispatch a **fixed kernel library** via
op ADTs (`IntMapOp` / `IntReduceOp` / `FloatMapOp` / `FloatReduceOp`) — not
arbitrary Yona lambdas to SPIR-V (see `docs/design-gpu-async.md` §2).
`mapReduceGraphGpu` batches map stages (+ reduce) into one Vulkan submit with
synchronization2 barriers when the device supports it.

Transparent lowering: `Std\IntArray` / `Std\FloatArray` `map` / `filter` /
`foldl` whose lambdas match the fixed kernel library (`x + k`, `x - k`,
`0 - x`, `x * k`, `x * x`, `x > k`, `x < k`, sum) compile to the same runtime ABI as
`mapAdd` / `mapMul` / `mapSquare` / `filterGreaterThan` / `filterLessThan` /
`reduceSum` / `mapFloatGpu` / `reduceFloatGpu`. Device vs CPU is decided at run time
(`YONA_GPU_VULKAN_MIN_LEN` for IntArray kernels; float uses the Vulkan device
when init succeeds, else CPU). Disable with `yonac --no-accelerator-lowering`.
Arbitrary lambdas stay on the host closure path by default; `yonac
--strict-accelerator` rejects them (E0700) instead of silently keeping the
host path.

`PinnedFloats` prefers Vulkan host-visible mapped memory when the Vulkan
device is up; set `YONA_GPU_PINNED_HOST_MALLOC=1` to force malloc. CPU↔GPU
float pipelines use `gpuFloatChannel` + `drainMapFloatGpu` (Std\Channel).
Typed failures: `GpuIssue` + `checkGpu` / `withGpuIssue` from this module
(Result-style; no `perform` in native bindings). `raiseGpu` /
`withGpuFallback` `perform Gpu.*`; GENFN remonomorphization inside a user
`handle` binds the caller's clauses (effect rows on `.yonai`). Direct
use-site `perform Gpu.oom` / `Gpu.deviceLost` / `Gpu.fail` still works.

## Types

### Buffer

`type Buffer = Buffer IntArray`

Opaque accelerator buffer. The CPU backend stores an owned IntArray copy.

### IntMapOp

`type IntMapOp = Add Int | Mul Int | Square`

Fixed int map kernels (constructor tag is the runtime discriminant).

### IntReduceOp

`type IntReduceOp = Sum`

Fixed int reduce kernels.

### FloatMapOp

`type FloatMapOp = Scale Float | Mul2`

Fixed float map kernels.

### FloatReduceOp

`type FloatReduceOp = FloatSum`

Fixed float reduce kernels.

### PinnedFloats

`type PinnedFloats = PinnedFloats Int`

Host contiguous float staging (handle). Prefer `Linear` wrapping at call
sites; call `closePinnedFloats` when finished. Prefers Vulkan host-visible
mapped memory when `YonaRuntimeGpuVulkanContextInitialize` succeeds; otherwise malloc.
Override with `YONA_GPU_PINNED_HOST_MALLOC=1`. Query via `pinnedBackend`.

### GpuIssue

`type GpuIssue = GpuOk | GpuOom | GpuDeviceLost | GpuOther Int`

Classified GPU failure from `vulkanLastIssueKind` (0/1/2/3).

## Functions

### `backendName : String`

Active backend name. Currently `cpu-simd` or `cpu-scalar`.

### `vulkanStatus : String`

Vulkan status string: `vulkan-unavailable`, `vulkan-loader`, or `vulkan-device`
(device only when built with Vulkan headers and init succeeded).

### `vulkanLastNote : String`

Short hint from the last failed Vulkan init, opt-in int column GPU attempt,
async **`vkWaitForFences`**, or other **`VkResult`** failures on the **`Std\Gpu`**
float path / test dispatch (`src/Runtime/Gpu/Stub.c`). After a successful device init
without **`shaderInt64`** (typical MoltenVK / Metal), records that IntArray
GPU kernels use i32 when values fit. Empty after int64-capable success or when
Vulkan was not compiled in. Same source as the C **`YonaRuntimeGpuVulkanDeviceLastNote()`**
helper.

### `vulkanLastIssueKind : Int`

0 = no classified **VkResult** yet; 1 = out-of-memory; 2 = device lost; 3 = other
(updated with **`vulkanLastNote`** when the runtime records a **`VkResult`**).

### `hasGpu : Bool`

True when Vulkan is enabled at build, not disabled by `YONA_GPU_DISABLE_VULKAN`,
and device init succeeds. IntArray kernels use i64 when `shaderInt64` is
available, otherwise i32 when values fit. Result is cached until
`YonaRuntimeGpuVulkanDeviceShutdown()`.

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
**`VK_KHR_synchronization2`** is enabled on the lazily initialized Vulkan device, async
float compute may wait on a **timeline semaphore** instead of a fence (**`YONA_GPU_ASYNC_TIMELINE=0`**
forces fence synchronization).

### `available : () -> Bool`

Integer discovery ABI (`YonaStdGpu*` in `src/Runtime/Gpu/Cpu.c`; full Vulkan
probe in `src/Runtime/Gpu/Stub.c` when linked).

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

### `mapSquare : Buffer -> Buffer`

Square every element (`x * x`). Vulkan when `YONA_GPU_VULKAN_MAPMUL=1` or
`YONA_GPU_VULKAN_COMPUTE=1` (same min-length env as `mapMul`).

### `filterGreaterThan : Int -> Buffer -> Buffer`

Keep values greater than the threshold. Vulkan when `YONA_GPU_VULKAN_FILTER`
or `YONA_GPU_VULKAN_COMPUTE=1` and length thresholds are met (`docs/gpu-architecture.md`).

### `filterLessThan : Int -> Buffer -> Buffer`

Keep values strictly less than the threshold. Vulkan when `YONA_GPU_VULKAN_FILTER`
or `YONA_GPU_VULKAN_COMPUTE=1` (same min-length env as `filterGreaterThan`).

### `reduceSum : Buffer -> Int`

Sum all values. Vulkan when `YONA_GPU_VULKAN_REDUCE=1` or
`YONA_GPU_VULKAN_COMPUTE=1` (min length in docs); else SIMD/scalar CPU.

### `mapGpu : IntMapOp -> Buffer -> Buffer`

Type-directed int map over a `Buffer` (fixed kernel library).

### `reduceGpu : IntReduceOp -> Buffer -> Int`

Type-directed int reduce over a `Buffer`.

### `mapFloatGpu : FloatMapOp -> FloatArray -> FloatArray`

Type-directed float map (GPU scale kernel when available, else CPU).

### `reduceFloatGpu : FloatReduceOp -> FloatArray -> Float`

Type-directed float reduce (GPU block-reduce when the Vulkan device is up,
else CPU).

### `mapReduceGraphGpu : [IntMapOp] -> Buffer -> Int`

One-submit map chain then `reduceSum` when the Vulkan graph path is available;
otherwise applies maps then reduce on the CPU/backend path.

### `allocPinnedFloats : Int -> PinnedFloats`

Allocate `n` contiguous floats (Vulkan-mapped when available, else malloc).

### `closePinnedFloats : PinnedFloats -> Int`

Release pinned storage.

### `pinnedLength : PinnedFloats -> Int`

### `pinnedGet : PinnedFloats -> Int -> Float`

### `pinnedSet : PinnedFloats -> Int -> Float -> Int`

### `pinnedToFloatArray : PinnedFloats -> FloatArray`

### `copyFloatArrayToPinned : FloatArray -> PinnedFloats -> Int`

### `pinnedBackend : PinnedFloats -> String`

`"vulkan-mapped"` or `"host-malloc"` (or `"invalid"`).

### `mapFloatPinnedGpu : FloatMapOp -> PinnedFloats -> Int`

In-place float map on pinned storage (GPU scale when available, else CPU).

### `gpuFloatChannel : Int -> (Linear (Sender FloatArray), Linear (Receiver FloatArray))`

Bounded `FloatArray` channel for CPU→GPU pipelines (`Std\Channel`).

### `drainMapFloatGpu : FloatMapOp -> Receiver FloatArray -> Sender FloatArray -> Int`

Drain `rx` until closed: `mapFloatGpu op` each chunk and `send` to `tx`.
Returns the number of chunks processed.

### `gpuLastIssue : GpuIssue`

Map `vulkanLastIssueKind` to `GpuIssue`.

### `checkGpu : Result (Int, GpuIssue)`

`Ok 0` when the last classified Vulkan issue is none; else `Err` with `GpuIssue`.

### `withGpuIssue : (() -> a) -> (GpuIssue -> a) -> a`

Branch on the last classified Vulkan issue without string-parsing `vulkanLastNote`.

### `raiseGpu : GpuIssue -> ()`

Convert `GpuIssue` to `perform Gpu.*`. Designed for a user `handle` at the
use site: GENFN remonomorphizes this body so `perform` binds the caller's
clauses. The precompiled module object raises `:UnhandledEffect` if invoked
with no handler (stdlib kernels still return `GpuIssue` / `Result`).

### `withGpuFallback : (() -> a) -> a`

Run `action`, then `raiseGpu` on the last classified Vulkan issue (`GpuOk` is a no-op).

### `floatArrayMul2Async : FloatArray -> Int`

Experimental: in-place x2 on `FloatArray` via native promise (see `docs/design-gpu-async.md`).
The C wrapper creates the `VkDevice`/pools on first use (`YonaRuntimeGpuVulkanContextInitialize`) when built with Vulkan.

### `floatArrayScaleAsync : Float -> FloatArray -> Int`

In-place multiply each element by `scale` (same Vulkan path as `floatArrayMul2Async`).
