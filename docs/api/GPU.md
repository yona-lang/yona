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

### `type Buffer = Buffer IntArray`

Opaque accelerator buffer. The CPU backend stores an owned IntArray copy.

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
extern raw_filterGreaterThan : Int -> IntArray -> IntArray = "yona_Std_GPU_raw__filterGreaterThan"
```

### `extern`

```yona
extern raw_reduceSum         : IntArray -> Int = "yona_Std_GPU_raw__reduceSum"
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
float path / test dispatch (`gpu_stub.c`). Empty after success or when Vulkan
was not compiled in. Same source as the C **`yona_gpu_vulkan_device_last_note()`**
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
device init succeeds, and `shaderInt64` is available (required for int64 SSBO
kernels). Result is cached until `yona_gpu_vulkan_device_shutdown()`.

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

True when a Vulkan loader is visible to the process.

### `vulkanAvailable`

```yona
vulkanAvailable = raw_vulkanAvailable 0
```

### `vulkanTimelineSemaphore`

```yona
vulkanTimelineSemaphore : Bool
```

True when device init succeeded (see **`hasGpu`** / **`vulkanStatus`**) and the
selected physical device reports Vulkan 1.2 **`timelineSemaphore`** via
**`vkGetPhysicalDeviceFeatures2`** (see **`docs/gpu-architecture.md` § Vulkan limitations**:
1.1-only **`VK_KHR_timeline_semaphore`** stacks are not detected yet). The runtime
still completes work with per-submit **fences**, not timeline waits; this flag is
diagnostics / future batching only.

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

