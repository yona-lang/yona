---
title: Accelerators (GPU)
description: Columnar GPU execution with Std\GPU — explicit kernels, transparent lowering of array pipelines, CPU fallback semantics, and typed GPU failures.
---

`Std\GPU` gives Yona programs **columnar accelerated execution** over primitive
numeric arrays (`IntArray`, `FloatArray`). The design has one non-negotiable
property: **the GPU is an optimization, never a semantic**. Every operation has
a CPU implementation (scalar or SIMD) that produces identical results, so
programs run correctly on machines with no GPU, no Vulkan loader, and no SDK.
Whether a given call actually reaches the GPU is decided at run time by
capability detection and size thresholds.

Two things to know up front:

- The device path executes a **fixed kernel library** (add, multiply, square,
  scale, compare-filter, sum). Arbitrary Yona lambdas are *not* compiled to
  SPIR-V yet — unrecognized lambdas run on the host, correctly.
- Everything on this page works in a default build. Sections marked **requires
  the optional Vulkan build** need the runtime compiled with Vulkan headers
  (see [Enabling Vulkan](#enabling-vulkan) below).

## Discovering capabilities

```yona
import backendName, hasGpu, hasSimd, vulkanStatus, vulkanAvailable,
       vulkanLastNote from Std\GPU in
(backendName, hasGpu, vulkanStatus)
```

- `backendName : String` — the CPU backend in use: `"cpu-simd"` (x86 SSE2 /
  AArch64 NEON baseline) or `"cpu-scalar"`.
- `hasGpu : Bool` — `true` when the runtime was built with Vulkan, Vulkan is
  not disabled by `YONA_GPU_DISABLE_VULKAN`, and device init succeeded.
- `vulkanStatus : String` — `"vulkan-unavailable"`, `"vulkan-loader"` (a
  loader is visible to the process), or `"vulkan-device"` (a compute queue was
  created — requires the optional Vulkan build).
- `vulkanLastNote : String` — a short hint from the last failed device init or
  GPU dispatch; empty after success. Useful for logs; prefer the typed
  `GpuIssue` API (below) for control flow.

```bash
yona -e 'import backendName from Std\GPU in backendName'
# => cpu-simd        (on a typical x86-64 host without the Vulkan build)
```

Additional probes: `hasSimd`, `vulkanTimelineSemaphore`, `available ()`,
`apiVersion ()`, and `physicalDeviceCount ()`.

## The explicit API

### Buffers and Int kernels

`Buffer` is an opaque accelerator column. `upload` copies a host `IntArray`
into accelerator-owned storage; `materialize` copies it back. The copies
happen even on the CPU backend, so your program observes transfer-like
ownership boundaries from day one and gains nothing but speed if device
storage appears later.

```yona
import fromSeq from Std\IntArray in
import upload, mapAdd, reduceSum from Std\GPU in
reduceSum (mapAdd 10 (upload (fromSeq [1, 2, 3])))
# => 36
```

Int kernels on `Buffer`: `mapAdd delta`, `mapMul factor`, `mapSquare`,
`filterGreaterThan threshold`, `filterLessThan threshold`, `reduceSum`, plus
`length` and `materialize`.

### Kernel-op ADTs: `mapGPU` and `reduceGPU`

The type-directed entry points take a kernel *descriptor* from a small ADT —
this is the honest encoding of the fixed kernel library:

```yona
type IntMapOp    = Add Int | Mul Int | Square
type IntReduceOp = Sum
type FloatMapOp  = Scale Float | Mul2
type FloatReduceOp = FSum
```

```yona
import fromSeq from Std\IntArray in
import upload, mapGPU, reduceGPU, Add, Sum from Std\GPU in
reduceGPU Sum (mapGPU (Add 10) (upload (fromSeq [1, 2, 3])))
# => 36
```

`mapReduceGraphGPU stages buffer` chains several map stages and a final sum.
On the Vulkan path it batches everything into a single queue submit with
pipeline barriers; elsewhere it applies the stages sequentially:

```yona
import fromSeq from Std\IntArray in
import upload, mapReduceGraphGPU, Add, Mul from Std\GPU in
mapReduceGraphGPU [Add 1, Mul 2] (upload (fromSeq [1, 2, 3, 4, 5]))
# => 40      # sum of (x + 1) * 2
```

### Float kernels

Float maps and reductions work directly on `FloatArray` — no buffer wrapper:

```yona
import fill from Std\FloatArray in
import mapFloatGPU, reduceFloatGPU, Scale, FSum from Std\GPU in
reduceFloatGPU FSum (mapFloatGPU (Scale 2.0) (fill 4 1.5))
# => 12.0
```

Experimental async variants `floatArrayScaleAsync` and `floatArrayMul2Async`
mutate a `FloatArray` in place through a native promise (transparently
awaited). **Requires the optional Vulkan build** to do device work; they
complete on the CPU path otherwise.

### Pinned float arrays

`PinnedFloats` is contiguous host float storage that prefers Vulkan
host-visible *mapped* memory when a device is up, so CPU-side writes are
directly visible to GPU dispatches with no staging copy. Treat the handle as a
resource — wrap it in `Linear` at call sites and always close it:

```yona
import allocPinnedFloats, pinnedSet, pinnedGet, pinnedBackend,
       mapFloatPinnedGPU, closePinnedFloats, Scale from Std\GPU in
let p = allocPinnedFloats 2,
    _ = pinnedSet p 0 1.0,
    _ = pinnedSet p 1 2.0,
    _ = mapFloatPinnedGPU (Scale 10.0) p,
    total = pinnedGet p 0 + pinnedGet p 1,
    _ = closePinnedFloats p
in total
# => 30.0
```

`pinnedBackend p` reports `"vulkan-mapped"` or `"host-malloc"`; set
`YONA_GPU_PINNED_HOST_MALLOC=1` to force malloc. `pinnedToFloatArray` /
`copyFloatArrayToPinned` convert to and from ordinary arrays.

### CPU-to-GPU float pipelines

`gpuFloatChannel n` creates a bounded `FloatArray` channel (linear endpoints,
from `Std\Channel`), and `drainMapFloatGPU op rx tx` pulls chunks from `rx`,
applies a float kernel to each, and sends results to `tx` until the input
channel closes — a ready-made GPU stage for producer/consumer pipelines. It
returns the number of chunks processed.

## Transparent lowering

You do not have to call `Std\GPU` to benefit from it. The compiler recognizes
**inline lambdas** in `Std\IntArray` / `Std\FloatArray` `map`, `filter`, and
`foldl` pipelines that match the fixed kernel library and rewrites them to the
same columnar runtime ABI as the explicit API:

```yona
import map, foldl, fromSeq from Std\IntArray in
foldl (\a b -> a + b) 0 (map (\x -> x * x) (fromSeq [1, 2, 3, 4]))
# => 30      # compiled as mapSquare + reduceSum, GPU-eligible at run time
```

Recognized shapes (Int unless noted): `\x -> x + k`, `\x -> x - k`,
`\x -> 0 - x`, `\x -> x * k`, `\x -> x * x` for `map`; `\x -> x > k` and
`\x -> x < k` for `filter`; `\a b -> a + b` with init `0` for `foldl`; and
`\x -> x * s` (Float map), `\a b -> a + b` with init `0.0` (Float sum).

Not rewritten — these stay on the always-correct host closure path: named
lambdas, shapes outside the library (`\x -> x + x * x`), lambdas containing
effects, `Std\List` operations, and code that already calls `Std\GPU`
explicitly.

Three compiler flags control the pass (see [the CLI
reference](/reference/cli/)):

```bash
# JSON report of every explicit Std\GPU site and every transparent rewrite
yonac pipeline.yona --emit-accelerator-report
# => {"schema":"yona.accelerator_diag.v1", ...,
#     "sites":[{"op":"reduceSum","kind":"transparent","kernel":"reduceSum",...},
#              {"op":"mapSquare","kind":"transparent","kernel":"mapSquare",...}]}

# Disable the rewrite entirely (host closures for everything)
yonac --no-accelerator-lowering -o app pipeline.yona

# Fail compilation with E0700 when an IntArray/FloatArray lambda is NOT
# lowerable, instead of silently keeping the host path
yonac --strict-accelerator -o app pipeline.yona
```

`--strict-accelerator` is the honest mode for teams that want a guarantee:
either the pipeline compiled to the accelerator ABI, or the build failed.
For modules, add `--emit-accelerator-report-with-types` to run the type
checker before the report.

## CPU fallback semantics

Results are **identical** whether a kernel executes on the GPU or the CPU —
filters preserve order, sums agree, and every benchmark's expected output is
checked on both paths. Fallback happens per call, at run time:

- **Int kernels** go to Vulkan only when the device is up, the per-op opt-in
  is set, and the column length meets the minimum (default 4096, tunable —
  see the table below). Otherwise: SIMD/scalar CPU.
- **Float kernels** try the device when init succeeds, else CPU.
- If a device dispatch fails mid-flight, the runtime records the failure
  (`vulkanLastNote`, `vulkanLastIssueKind`) and the CPU path keeps the program
  correct.

You never need a code path per backend. Write the pipeline once; tune the
crossover with environment variables.

## Typed GPU failures

For observability and recovery, classified failures are exposed as an ADT
rather than strings:

```yona
type GpuIssue = GpuOk | GpuOom | GpuDeviceLost | GpuOther Int
```

- `gpuLastIssue : GpuIssue` — the last classified device issue.
- `checkGpu` — `Ok 0` when clean, else `Err issue`, for Result-style plumbing.
- `withGpuIssue on_ok on_issue` — branch without string parsing.

```yona
import checkGpu, GpuOom from Std\GPU in
case checkGpu of
    Ok _        -> "clean"
    Err GpuOom  -> "out of device memory"
    Err _       -> "other issue"
end
# => "clean"      (on a host that has made no failing GPU attempt)
```

For effect-based handling, `raiseGpu issue` performs `Gpu.oom`,
`Gpu.deviceLost`, or `Gpu.fail code`, and `withGpuFallback action` runs the
action then raises the last classified issue if any. Both are designed to sit
inside a user `handle` — cross-module monomorphization binds the caller's
handler clauses (effect rows travel in the `.yonai` interface):

```yona
import withGpuFallback from Std\GPU in
handle
    withGpuFallback (\_ -> runPipeline 0)
with
    Gpu.oom () resume -> resume (retryWithSmallerBatch ())
    Gpu.deviceLost () resume -> resume (restartOnCpu ())
    Gpu.fail code resume -> resume code
    return val -> val
end
```

## Enabling Vulkan

**Everything above works without this section.** The optional Vulkan build
adds a real device path behind the same API.

Build-time (compiling the compiler/runtime from source):

```bash
cmake --preset x64-release-linux -DYONA_ENABLE_VULKAN=ON
cmake --build --preset build-release-linux
# Homebrew users: brew install akovari/tap/yona --with-vulkan
```

When `yonac` compiles the runtime from source on your machine, set
`YONA_COMPILE_GPU_VULKAN=1` (plus `VULKAN_SDK` or `HOMEBREW_PREFIX` so the
Vulkan headers are found) to get the Vulkan-enabled runtime; leave it unset
for the default CPU-only runtime.

Run-time opt-ins and tuning (documented environment variables):

| Variable | Effect |
|----------|--------|
| `YONA_GPU_VULKAN_COMPUTE=1` | Enable the Vulkan path for all Int kernels. |
| `YONA_GPU_VULKAN_MAPADD=1` / `MAPMUL=1` / `REDUCE=1` / `FILTER=1` | Per-kernel opt-in. |
| `YONA_GPU_VULKAN_MIN_LEN` | Minimum column length for the device path (default 4096). |
| `YONA_GPU_VULKAN_MAPADD_MIN_LEN` (and per-op variants) | Per-kernel minimum length. |
| `YONA_GPU_VULKAN_GRAPH=1` / `YONA_GPU_VULKAN_GRAPH_MIN_LEN` | One-submit `mapReduceGraphGPU`. |
| `YONA_GPU_DISABLE_VULKAN=1` | Disable all GPU paths (CPU only). |
| `YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX` | Pick a specific adapter. |
| `YONA_GPU_VULKAN_HOST_SSBO=1` | Force host-visible buffers (debugging/parity). |
| `YONA_GPU_PINNED_HOST_MALLOC=1` | Force malloc for `PinnedFloats`. |
| `YONA_GPU_ASYNC_TIMELINE=0` | Use fences instead of timeline semaphores for async float work. |

Device selection prefers a discrete GPU with a compute queue; `shaderInt64`
and API version break ties. On macOS/MoltenVK, `shaderInt64` and
`shaderFloat64` are usually unavailable — `hasGpu` is still `true`, Int
kernels narrow to i32 when every value fits, and float kernels fall back to
f32; values outside range stay on the CPU.

Implementation note. Vulkan entry points are resolved at run time via
`dlopen`/`LoadLibrary` on the loader — a default-built program has no link-time
Vulkan dependency and runs on loader-less hosts.

## Benchmarking the crossover

`bench/run_gpu_compare.py` in the compiler repository runs the same compiled
program with the GPU forced off and then opted in, checks that both outputs
match the golden file, and prints CPU-vs-GPU wall times. Use it to tune
`YONA_GPU_VULKAN_MIN_LEN` for your hardware. See
[Performance](/guides/performance/) for the general benchmarking harness.

## Limitations

- **Fixed kernel library.** Only the shapes listed above execute on the
  device. There is no SPIR-V compilation of arbitrary Yona lambdas yet;
  unrecognized lambdas run on the host (or fail the build under
  `--strict-accelerator`).
- **Primitive columns only.** No ADTs, strings, closures, effects, or
  exceptions inside kernels.
- **`vulkanLastNote` is a single shared buffer**, not a structured log — use
  `GpuIssue` for control flow.
- Small columns will not win on a GPU: transfer plus launch overhead dominates
  below the minimum-length thresholds, which is exactly why the runtime
  defaults them to 4096 and lets you tune per kernel.
