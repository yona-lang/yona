# Design: GPU + async (concurrency-first)

**Status:** design note for `Std\Gpu` and follow-ons (see
[todo-list.md](./todo-list.md) — _Language — GPU / Heterogeneous Compute_).
**Principle:** Yona is **non-blocking and structured-concurrency-first**; GPU
submit/wait must **not** park worker threads on `vkDeviceWaitIdle` /
`vkQueueWaitIdle` in the common path.

## 1. Goals

- **Explicit `Std\Gpu` API first** — user-visible `mapGpu` / `reduceGpu` (or
  similar) with clear cost models before compiler magic.
- **Composable with `async` / `Promise` / task groups** — completion is
  awaitable; cancellation tears down in-flight GPU work safely.
- **Correct refcounting** — host buffers (pinned or not) live until the GPU
  queue finishes reads/writes; no UAF on early `raise` or group cancel.

## 2. Non-goals (first ship)

- **Full SPIR-V from arbitrary Yona lambdas** — the compiler rewrites only the
  **fixed kernel library** (inline IntArray/FloatArray map/filter/foldl) to the
  Std\Gpu ABI (`docs/gpu-transparent-lowering.md`). Arbitrary closures stay on
  the host path by default (`\x -> x + x * x` and similar);
  **`yonac --strict-accelerator`** rejects them with **E0700** (honest typed
  rejection; no silent wrong answers). `\x -> x * x` is in the kernel library.
- **Multi-GPU / MGPU** selection UI.
- **Effect-row inference for `Gpu`** — `perform Gpu.*` / handlers ship now; full
  effect rows remain GitHub #8.

## 3. Submit / completion model

### 3.1 Return type

- `submitCompute : … -> Promise GpuResult` (exact name TBD) — **reuse** existing
  `Promise` machinery where possible with a **discriminant** or side channel so
  `async_await` knows this promise waits on a **fence** (or timeline semaphore),
  not only io_uring or thread-pool work.

### 3.2 Vulkan mapping

- Each submit records a **command buffer** + optional **signal semaphore**
  (timeline `uint64_t` counter preferred over binary fences for chaining).
- **Completion:** a **dedicated waiter** path:
  - **Option A:** small `poll(2)` / `epoll` on timeline semaphore FDs if
    exposed, or
  - **Option B:** background thread calling `vkWaitForFences` with short
    timeouts and pushing completions to the same completion queue the runtime
    already uses for async, or
  - **Option C:** interleave GPU fence check in the **io_uring reap loop** only
    if integration stays simple.

**Rule:** Yona **task pool worker threads** never block in `vkQueueWaitIdle`.

### 3.3 Progress / `await`

- `await` on a GPU-backed promise: suspend (today: thread-pool future; later:
  coroutine) until fence signals; then resolve value / run user callback on the
  executor.

## 4. Structured concurrency + cancellation

- Associate each in-flight GPU submission with a **`task_group` id** (or cancel
  token pointer) at submit time.
- On **group cancel / end:**
  - stop scheduling **new** GPU work for that group;
  - for pending: either let them finish then discard callbacks, or use
    `vkDeviceWaitIdle` only on **shutdown** path (acceptable) — prefer **skip
    completion** and **release** host buffers only after known GPU idle for
    those command buffers (pool reset or `vkQueueWaitIdle` in teardown path
    only).

Document **exact** semantics (lossy cancel vs drain) in the stdlib API.

## 5. Memory: pinned buffers and channels

- **Pinned host memory:** `PinnedFloats` with explicit close — prefers Vulkan
  **host-visible** mapped memory when `YonaRuntimeGpuVulkanContextInitialize`
  succeeds; malloc fallback otherwise (`YONA_GPU_PINNED_HOST_MALLOC=1` forces
  malloc). `pinnedBackend` reports `"vulkan-mapped"` / `"host-malloc"`.
  `mapFloatPinnedGpu` scales in place on the mapped/host pointer.
- **Channels:** `gpuFloatChannel` / `drainMapFloatGpu` reuse `Std\Channel` for
  bounded `FloatArray` chunk pipelines into GPU map kernels — same lifetime
  rules as other channel payloads.

## 6. Effects and failures

- **`Gpu` effect ops** (`perform Gpu.oom` / `Gpu.deviceLost` / `Gpu.fail code`)
  are registered at prelude load. `Std\Gpu` kernels return `GpuIssue` / `Result`
  (`checkGpu` / `withGpuIssue`). `raiseGpu` / `withGpuFallback` `perform` those
  ops; GENFN remonomorphization inside a user `handle` binds the caller's
  clauses (effect rows on `.yonai`). Direct use-site `perform` still works.
  There is no runtime handler stack across precompiled objects.

## 7. Multi-stage graphs

- **Shipped:** `mapReduceGraphGpu` batches map→…→reduce into one command buffer
  with sync2 barriers (`docs/gpu-architecture.md`). Further graph shapes
  (arbitrary DAG, multi-queue) remain future work.

## 8. Implementation phases (execution order)

### 8.0 Extern `Promise` lowering (compiler)

GPU and other runtimes sometimes return a **`YonaTaskRef`** that is **not**
created by the thread pool (`YonaRuntimeAsyncCall`). The compiler models all
promise-carrying `extern` variants with **`ast::ExternPromiseKind`** in
`include/yona/Syntax/Ast.h`, and records the awaited inner type as
**`promise_inner_type`** on both **`ModuleFunctionMeta`** and
**`CompiledFunction`** (`include/yona/Codegen/Codegen.h`).

Every pointer-backed promise ABI also carries a hidden, mandatory
**`YonaTypeDescriptor`** selected from that inner type. The task copies the
descriptor and owns the producer's completed result. `YonaRuntimeTaskAwait`
consumes an ungrouped task and transfers its reference;
`YonaRuntimeTaskAwaitKeep` retains a separate caller reference while the group
keeps and later releases the original. Error and cancellation completion use a
null payload, so there is no untyped cross-task ownership path.

| `ExternPromiseKind` | Yona keyword    | C ABI return (typical)        | Await                                  |
| ------------------- | --------------- | ----------------------------- | -------------------------------------- |
| `Sync`              | _(none)_        | concrete `T`                  | —                                      |
| `ThreadPool`        | `extern async`  | inner `T` (pool wraps the fn) | `YonaRuntimeTaskAwait` on pool promise |
| `IoUring`           | `extern io`     | `i64` io_uring cookie         | `YonaRuntimeIoAwait`                   |
| `NativePtr`         | `extern native` | `YonaTaskRef`                 | `YonaRuntimeTaskAwait`                 |

`.yonai` interface rows use **`FN`**, **`AFN`**, **`IO`**, or **`NAT`** before
the mangled symbol (`src/Codegen/CodegenModule.cpp`). At call sites,
**`TypedValue`** carries **`PromiseAwaitPath`** (`AsyncPtr` vs `IoUring`) so
**`auto_await`** dispatches to the correct runtime primitive
(`src/Codegen/CodegenApply.cpp`). AFN/IO/NAT imports share
**`declare_import_extern_fn`** + **`bind_imported_promise_cf`** to avoid
duplicated registration logic.

The canonical implementation compiles the GPU component once into the
`yona_runtime` archive. `lib/Std/Gpu.yona` exposes `available` and
`physicalDeviceCount` for discovery plus the fixed-kernel API. CMake's
`YONA_ENABLE_VULKAN` option controls whether the component is built with Vulkan
headers and the loader dependency; tests and generated programs consume that
same archive.

`src/Runtime/Gpu/Stub.c` owns the long-lived Vulkan context, async float
submission, and fence/timeline completion. `src/Runtime/Gpu/VulkanDevice.c`,
`VulkanCompute.c`, `VulkanOperations.c`, and `VulkanLoader.c` own device state,
pipelines, operations, and loader discovery. Shaders and generated fragments
live under `src/Runtime/Generated/` and reproduce through
`scripts/generate_gpu_shaders.py`.

`floatArrayMul2Async` and `floatArrayScaleAsync` return opaque `YonaTaskRef`
handles completed by the GPU fence thread through the Concurrency component.
Their hidden result descriptors follow the same transfer/retain/release rules
as worker-pool and `Std\Task.spawn` results.
Task-group cancellation completes the promise with **-887** while submitted
device work drains safely. Pinned float storage, typed channels,
`mapReduceGraphGpu`, and transparent lowering all use this same runtime path.
Arbitrary-lambda SPIR-V remains future work; `--strict-accelerator` reports
E0700 when no fixed-kernel lowering exists.

## 9. References (implementation)

- `src/Runtime/Concurrency/ChannelPosix.c`,
  `src/Runtime/Concurrency/ChannelWin32.c`, and task-group teardown.
- `docs/channels.md`, `docs/memory-management.md` (RC + pinning).
- Vulkan: timeline semaphores (`VK_KHR_timeline_semaphore`), synchronization2.
