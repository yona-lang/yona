# Std.GPU

Std\GPU — accelerated columnar execution.

The initial backend is portable CPU execution over `IntArray` columns. It
keeps explicit upload/materialize boundaries so programs are ready for future
Vulkan or vendor-backed device storage without changing the high-level API.
With `YONA_COMPILE_GPU_VULKAN`, optional Vulkan compute can handle `mapAdd`,
`mapMul`, `mapSquare`, `reduceSum`, `filterGreaterThan`, and `filterLessThan`
(see `docs/gpu-architecture.md`).
`mapAdd`/`mapMul`/`mapSquare`/`reduceSum`/`filterGreaterThan`/`filterLessThan`
prefer device-local SSBOs with staging when VRAM allows; set
`YONA_GPU_VULKAN_HOST_SSBO=1` to force the legacy host-mapped SSBO path.
Filter kernels use GPU mark + GPU inclusive prefix + exclusive indices +
GPU scatter when enabled (`YONA_GPU_VULKAN_FILTER` or `YONA_GPU_VULKAN_COMPUTE`).
Set `YONA_GPU_VULKAN_FILTER_CPU_PREFIX=1` to force the older host-side prefix (debug only).

User-facing `mapGPU` / `reduceGPU` dispatch a **fixed kernel library** via
op ADTs (`IntMapOp` / `IntReduceOp` / `FloatMapOp` / `FloatReduceOp`) — not
arbitrary Yona lambdas to SPIR-V (see `docs/design-gpu-async.md` §2).
`mapReduceGraphGPU` batches map stages (+ reduce) into one Vulkan submit with
synchronization2 barriers when the device supports it.

Transparent lowering: `Std\IntArray` / `Std\FloatArray` `map` / `filter` /
`foldl` whose lambdas match the fixed kernel library (`x + k`, `x - k`,
`0 - x`, `x * k`, `x * x`, `x > k`, `x < k`, sum) compile to the same runtime ABI as
`mapAdd` / `mapMul` / `mapSquare` / `filterGreaterThan` / `filterLessThan` /
`reduceSum` / `mapFloatGPU` / `reduceFloatGPU`. Device vs CPU is decided at run time
(`YONA_GPU_VULKAN_MIN_LEN` for IntArray kernels; float uses the stub device
when init succeeds, else CPU). Disable with `yonac --no-accelerator-lowering`.
Arbitrary lambdas stay on the host closure path by default; `yonac
--strict-accelerator` rejects them (E0700) instead of silently keeping the
host path.

`PinnedFloats` prefers Vulkan host-visible mapped memory when the stub
device is up; set `YONA_GPU_PINNED_HOST_MALLOC=1` to force malloc. CPU↔GPU
float pipelines use `gpuFloatChannel` + `drainMapFloatGPU` (Std\Channel).
Typed failures: `GpuIssue` + `checkGpu` / `withGpuIssue` from this module
(Result-style; no `perform` in the precompiled object). `raiseGpu` /
`withGpuFallback` `perform Gpu.*`; GENFN remonomorphization inside a user
`handle` binds the caller's clauses (effect rows on `.yonai`). Direct
use-site `perform Gpu.oom` / `Gpu.deviceLost` / `Gpu.fail` still works.

## Types

### `type Buffer = Buffer IntArray`

Opaque accelerator buffer. The CPU backend stores an owned IntArray copy.

### `type IntMapOp = Add Int | Mul Int | Square`

Fixed int map kernels (constructor tag is the runtime discriminant).

### `type IntReduceOp = Sum`

Fixed int reduce kernels.

### `type FloatMapOp = Scale Float | Mul2`

Fixed float map kernels.

### `type FloatReduceOp = FSum`

Fixed float reduce kernels.

### `type PinnedFloats = PinnedFloats Int`

Host contiguous float staging (handle). Prefer `Linear` wrapping at call
sites; call `closePinnedFloats` when finished. Prefers Vulkan host-visible
mapped memory when `yona_gpu_vulkan_ctx_init` succeeds; otherwise malloc.
Override with `YONA_GPU_PINNED_HOST_MALLOC=1`. Query via `pinnedBackend`.

### `type GpuIssue = GpuOk | GpuOom | GpuDeviceLost | GpuOther Int`

Classified GPU failure from `vulkanLastIssueKind` (0/1/2/3).

## Functions

### `extern`

```yona
extern raw_backendName       : Int -> String   = "yona_Std_GPU_raw__backendName"
```

### `extern`

```yona
extern raw_vulkanStatus      : Int -> String   = "yona_Std_GPU_raw__vulkanStatus"
```

### `extern`

```yona
extern raw_vulkanLastNote    : Int -> String   = "yona_Std_GPU_raw__vulkanLastNote"
```

### `extern`

```yona
extern raw_hasGpu            : Int -> Bool     = "yona_Std_GPU_raw__hasGpu"
```

### `extern`

```yona
extern raw_hasSimd           : Int -> Bool     = "yona_Std_GPU_raw__hasSimd"
```

### `extern`

```yona
extern raw_vulkanAvailable   : Int -> Bool     = "yona_Std_GPU_raw__vulkanAvailable"
```

### `extern`

```yona
extern raw_vulkanTimelineSemaphore : Int -> Bool = "yona_Std_GPU_raw__vulkanTimelineSemaphore"
```

### `extern`

```yona
extern raw_vulkanLastIssueKind     : Int -> Int    = "yona_Std_GPU_raw__vulkanLastIssueKind"
```

### `extern`

```yona
extern raw_upload            : IntArray -> IntArray = "yona_Std_GPU_raw__upload"
```

### `extern`

```yona
extern raw_materialize       : IntArray -> IntArray = "yona_Std_GPU_raw__materialize"
```

### `extern`

```yona
extern raw_length            : IntArray -> Int = "yona_Std_GPU_raw__length"
```

### `extern`

```yona
extern raw_mapAdd            : Int -> IntArray -> IntArray = "yona_Std_GPU_raw__mapAdd"
```

### `extern`

```yona
extern raw_mapMul            : Int -> IntArray -> IntArray = "yona_Std_GPU_raw__mapMul"
```

### `extern`

```yona
extern raw_mapSquare         : IntArray -> IntArray = "yona_Std_GPU_raw__mapSquare"
```

### `extern`

```yona
extern raw_filterGreaterThan : Int -> IntArray -> IntArray = "yona_Std_GPU_raw__filterGreaterThan"
```

### `extern`

```yona
extern raw_filterLessThan    : Int -> IntArray -> IntArray = "yona_Std_GPU_raw__filterLessThan"
```

### `extern`

```yona
extern raw_reduceSum         : IntArray -> Int = "yona_Std_GPU_raw__reduceSum"
```

### `extern`

```yona
extern raw_mapFloatOp        : FloatMapOp -> FloatArray -> FloatArray = "yona_Std_GPU_raw__mapFloatOp"
```

### `extern`

```yona
extern raw_reduceFloatGPU    : FloatArray -> Float = "yona_Std_GPU_raw__reduceFloatGPU"
```

### `extern`

```yona
extern raw_mapReduceGraph    : IntArray -> IntArray -> Int = "yona_Std_GPU_raw__mapReduceGraph"
```

### `extern`

```yona
extern raw_allocPinnedFloats : Int -> Int = "yona_Std_GPU_raw__allocPinnedFloats"
```

### `extern`

```yona
extern raw_closePinnedFloats : Int -> Int = "yona_Std_GPU_raw__closePinnedFloats"
```

### `extern`

```yona
extern raw_pinnedLength      : Int -> Int = "yona_Std_GPU_raw__pinnedLength"
```

### `extern`

```yona
extern raw_pinnedGet         : Int -> Int -> Float = "yona_Std_GPU_raw__pinnedGet"
```

### `extern`

```yona
extern raw_pinnedSet         : Int -> Int -> Float -> Int = "yona_Std_GPU_raw__pinnedSet"
```

### `extern`

```yona
extern raw_pinnedToFloatArray : Int -> FloatArray = "yona_Std_GPU_raw__pinnedToFloatArray"
```

### `extern`

```yona
extern raw_copyFloatArrayToPinned : FloatArray -> Int -> Int = "yona_Std_GPU_raw__copyFloatArrayToPinned"
```

### `extern`

```yona
extern raw_pinnedBackend     : Int -> String = "yona_Std_GPU_raw__pinnedBackend"
```

### `extern`

```yona
extern raw_mapFloatPinnedOp  : FloatMapOp -> Int -> Int = "yona_Std_GPU_raw__mapFloatPinnedOp"
```

### `backendName`

```yona
backendName : String
```

Active backend name. Currently `cpu-simd` or `cpu-scalar`.

### `backendName`

```yona
backendName = raw_backendName 0
```

### `vulkanStatus`

```yona
vulkanStatus : String
```

Vulkan status string: `vulkan-unavailable`, `vulkan-loader`, or `vulkan-device`
(device only when built with Vulkan headers and init succeeded).

### `vulkanStatus`

```yona
vulkanStatus = raw_vulkanStatus 0
```

### `vulkanLastNote`

```yona
vulkanLastNote : String
```

Short hint from the last failed Vulkan init, opt-in int column GPU attempt,
async **`vkWaitForFences`**, or other **`VkResult`** failures on the **`Std\GPU`**
float path / test dispatch (`gpu_stub.c`). After a successful device init
without **`shaderInt64`** (typical MoltenVK / Metal), records that IntArray
GPU kernels use i32 when values fit. Empty after int64-capable success or when
Vulkan was not compiled in. Same source as the C **`yona_gpu_vulkan_device_last_note()`**
helper.

### `vulkanLastNote`

```yona
vulkanLastNote = raw_vulkanLastNote 0
```

### `hasGpu`

```yona
hasGpu : Bool
```

True when Vulkan is enabled at build, not disabled by `YONA_GPU_DISABLE_VULKAN`,
and device init succeeds. IntArray kernels use i64 when `shaderInt64` is
available, otherwise i32 when values fit. Result is cached until
`yona_gpu_vulkan_device_shutdown()`.

### `hasGpu`

```yona
hasGpu = raw_hasGpu 0
```

### `hasSimd`

```yona
hasSimd : Bool
```

True when the CPU backend was built with a known SIMD baseline.

### `hasSimd`

```yona
hasSimd = raw_hasSimd 0
```

### `vulkanAvailable`

```yona
vulkanAvailable : Bool
```

True when a Vulkan loader is visible to the process (`vulkan-1.dll`,
`libvulkan.so.1`, or on macOS `libvulkan.1.dylib` / `libMoltenVK.dylib`
via `VULKAN_SDK`, `HOMEBREW_PREFIX`, or the lib dir CMake recorded).

### `vulkanAvailable`

```yona
vulkanAvailable = raw_vulkanAvailable 0
```

### `vulkanLastIssueKind`

```yona
vulkanLastIssueKind : Int
```

0 = no classified **VkResult** yet; 1 = out-of-memory; 2 = device lost; 3 = other
(updated with **`vulkanLastNote`** when the runtime records a **`VkResult`**).

### `vulkanLastIssueKind`

```yona
vulkanLastIssueKind = raw_vulkanLastIssueKind 0
```

### `vulkanTimelineSemaphore`

```yona
vulkanTimelineSemaphore : Bool
```

True when device init succeeded (see **`hasGpu`** / **`vulkanStatus`**) and the
probe finds timeline semaphores: Vulkan 1.2+ **`timelineSemaphore`** via **`vkGetPhysicalDeviceFeatures2`**
(Vulkan 12 feature chain), or **`VK_KHR_timeline_semaphore`** in the device's extension list
(Vulkan 1.0/1.1 stacks that expose the capability only as an extension). When
**`VK_KHR_synchronization2`** is enabled on the lazy **`gpu_stub`** device, async
float compute may wait on a **timeline semaphore** instead of a fence (**`YONA_GPU_ASYNC_TIMELINE=0`**
forces the legacy fence path).

### `vulkanTimelineSemaphore`

```yona
vulkanTimelineSemaphore = raw_vulkanTimelineSemaphore 0
```

### `extern`

```yona
extern raw_stdGpuAvailable : Int -> Bool = "yona_Std_GPU__available"
```

Integer discovery ABI (`yona_Std_GPU__*` in `gpu_cpu.c`; full Vulkan probe in `gpu_stub.c` when linked).

### `extern`

```yona
extern raw_stdGpuApiVersion : Int -> Int = "yona_Std_GPU__apiVersion"
```

### `extern`

```yona
extern raw_stdGpuPhysicalDeviceCount : Int -> Int = "yona_Std_GPU__physicalDeviceCount"
```

### `available`

```yona
available : () -> Bool
```

### `available`

```yona
available () = raw_stdGpuAvailable 0
```

### `apiVersion`

```yona
apiVersion : () -> Int
```

### `apiVersion`

```yona
apiVersion () = raw_stdGpuApiVersion 0
```

### `physicalDeviceCount`

```yona
physicalDeviceCount : () -> Int
```

### `physicalDeviceCount`

```yona
physicalDeviceCount () = raw_stdGpuPhysicalDeviceCount 0
```

### `upload`

```yona
upload : IntArray -> Buffer
```

Copy a host IntArray into accelerator-owned storage.

### `upload`

```yona
upload values = Buffer (raw_upload values)
```

### `materialize`

```yona
materialize : Buffer -> IntArray
```

Copy accelerator-owned storage back to a host IntArray.

### `materialize`

```yona
materialize buffer =
```

### `length`

```yona
length : Buffer -> Int
```

Number of elements in the buffer.

### `length`

```yona
length buffer =
```

### `mapAdd`

```yona
mapAdd : Int -> Buffer -> Buffer
```

Add a constant to every element. Vulkan path when `YONA_GPU_VULKAN_MAPADD=1`
or `YONA_GPU_VULKAN_COMPUTE=1` and length ≥ min (default 4096; see docs).

### `mapAdd`

```yona
mapAdd delta buffer =
```

### `mapMul`

```yona
mapMul : Int -> Buffer -> Buffer
```

Multiply every element by a constant. Vulkan when `YONA_GPU_VULKAN_MAPMUL=1`
or `YONA_GPU_VULKAN_COMPUTE=1` (min length env vars in docs).

### `mapMul`

```yona
mapMul factor buffer =
```

### `filterGreaterThan`

```yona
filterGreaterThan : Int -> Buffer -> Buffer
```

Keep values greater than the threshold. Vulkan when `YONA_GPU_VULKAN_FILTER`
or `YONA_GPU_VULKAN_COMPUTE=1` and length thresholds are met (`docs/gpu-architecture.md`).

### `filterGreaterThan`

```yona
filterGreaterThan threshold buffer =
```

### `filterLessThan`

```yona
filterLessThan : Int -> Buffer -> Buffer
```

Keep values strictly less than the threshold. Vulkan when `YONA_GPU_VULKAN_FILTER`
or `YONA_GPU_VULKAN_COMPUTE=1` (same min-length env as `filterGreaterThan`).

### `filterLessThan`

```yona
filterLessThan threshold buffer =
```

### `mapSquare`

```yona
mapSquare : Buffer -> Buffer
```

Square every element (`x * x`). Vulkan when `YONA_GPU_VULKAN_MAPMUL=1` or
`YONA_GPU_VULKAN_COMPUTE=1` (same min-length env as `mapMul`).

### `mapSquare`

```yona
mapSquare buffer =
```

### `reduceSum`

```yona
reduceSum : Buffer -> Int
```

Sum all values. Vulkan when `YONA_GPU_VULKAN_REDUCE=1` or
`YONA_GPU_VULKAN_COMPUTE=1` (min length in docs); else SIMD/scalar CPU.

### `reduceSum`

```yona
reduceSum buffer =
```

### `mapGPU`

```yona
mapGPU : IntMapOp -> Buffer -> Buffer
```

Type-directed int map over a `Buffer` (fixed kernel library).

### `mapGPU`

```yona
mapGPU op buffer =
```

### `reduceGPU`

```yona
reduceGPU : IntReduceOp -> Buffer -> Int
```

Type-directed int reduce over a `Buffer`.

### `reduceGPU`

```yona
reduceGPU op buffer =
```

### `mapFloatGPU`

```yona
mapFloatGPU : FloatMapOp -> FloatArray -> FloatArray
```

Type-directed float map (GPU scale kernel when available, else CPU).

### `mapFloatGPU`

```yona
mapFloatGPU op arr = raw_mapFloatOp op arr
```

### `reduceFloatGPU`

```yona
reduceFloatGPU : FloatReduceOp -> FloatArray -> Float
```

Type-directed float reduce (GPU block-reduce when the stub device is up, else CPU).

### `reduceFloatGPU`

```yona
reduceFloatGPU op arr =
```

### `encodeIntMapStages`

```yona
encodeIntMapStages stages =
```

Encode `IntMapOp` stages as an IntArray of (op, arg) pairs: op 0 = Add, 1 = Mul, 2 = Square.

### `mapReduceGraphGPU`

```yona
mapReduceGraphGPU stages buffer =
```

One-submit map chain then `reduceSum` when the Vulkan graph path is available;
otherwise applies maps then reduce on the CPU/backend path.

### `allocPinnedFloats`

```yona
allocPinnedFloats : Int -> PinnedFloats
```

Allocate `n` contiguous floats (Vulkan-mapped when available, else malloc).

### `allocPinnedFloats`

```yona
allocPinnedFloats n = PinnedFloats (raw_allocPinnedFloats n)
```

### `closePinnedFloats`

```yona
closePinnedFloats : PinnedFloats -> Int
```

Release pinned storage.

### `closePinnedFloats`

```yona
closePinnedFloats pf =
```

### `pinnedLength`

```yona
pinnedLength : PinnedFloats -> Int
```

### `pinnedLength`

```yona
pinnedLength pf =
```

### `pinnedGet`

```yona
pinnedGet : PinnedFloats -> Int -> Float
```

### `pinnedGet`

```yona
pinnedGet pf i =
```

### `pinnedSet`

```yona
pinnedSet : PinnedFloats -> Int -> Float -> Int
```

### `pinnedSet`

```yona
pinnedSet pf i v =
```

### `pinnedToFloatArray`

```yona
pinnedToFloatArray : PinnedFloats -> FloatArray
```

### `pinnedToFloatArray`

```yona
pinnedToFloatArray pf =
```

### `copyFloatArrayToPinned`

```yona
copyFloatArrayToPinned : FloatArray -> PinnedFloats -> Int
```

### `copyFloatArrayToPinned`

```yona
copyFloatArrayToPinned arr pf =
```

### `pinnedBackend`

```yona
pinnedBackend : PinnedFloats -> String
```

`"vulkan-mapped"` or `"host-malloc"` (or `"invalid"`).

### `pinnedBackend`

```yona
pinnedBackend pf =
```

### `mapFloatPinnedGPU`

```yona
mapFloatPinnedGPU : FloatMapOp -> PinnedFloats -> Int
```

In-place float map on pinned storage (GPU scale when available, else CPU).

### `mapFloatPinnedGPU`

```yona
mapFloatPinnedGPU op pf =
```

### `gpuFloatChannel`

```yona
gpuFloatChannel n =
```

Bounded `FloatArray` channel for CPU→GPU pipelines (`Std\Channel`).

### `drainMapFloatGPU`

```yona
drainMapFloatGPU op rx tx =
```

Drain `rx` until closed: `mapFloatGPU op` each chunk and `send` to `tx`.
Returns the number of chunks processed.

### `gpuLastIssue`

```yona
gpuLastIssue =
```

Map `vulkanLastIssueKind` to `GpuIssue`.

### `checkGpu`

```yona
checkGpu =
```

`Ok 0` when the last classified Vulkan issue is none; else `Err` with `GpuIssue`.

### `withGpuIssue`

```yona
withGpuIssue on_ok on_issue =
```

Branch on the last classified Vulkan issue without string-parsing `vulkanLastNote`.

### `raiseGpu`

```yona
raiseGpu issue =
```

Convert `GpuIssue` to `perform Gpu.*`. Designed for a user `handle` at the
use site: GENFN remonomorphizes this body so `perform` binds the caller's
clauses. The precompiled module object raises `:UnhandledEffect` if invoked
with no handler (stdlib kernels still return `GpuIssue` / `Result`).

### `withGpuFallback`

```yona
withGpuFallback action =
```

Run `action`, then `raiseGpu` on the last classified Vulkan issue (`GpuOk` is a no-op).

### `extern`

```yona
extern native floatArrayMul2Async : FloatArray -> Int = "yona_Std_GPU__floatArrayMul2Async"
```

Experimental: in-place x2 on `FloatArray` via native promise (see `docs/design-gpu-async.md`).
The C wrapper creates the `VkDevice`/pools on first use (`yona_gpu_vulkan_ctx_init`) when built with Vulkan.

### `extern`

```yona
extern native floatArrayScaleAsync : Float -> FloatArray -> Int = "yona_Std_GPU__floatArrayScaleAsync"
```

In-place multiply each element by `scale` (same Vulkan path as `floatArrayMul2Async`).

