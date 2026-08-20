# Design: GPU + async (concurrency-first)

**Status:** design note for `Std\GPU` and follow-ons (see [todo-list.md](./todo-list.md)
— *Language — GPU / Heterogeneous Compute*).  
**Principle:** Yona is **non-blocking and structured-concurrency-first**; GPU
submit/wait must **not** park worker threads on `vkDeviceWaitIdle` /
`vkQueueWaitIdle` in the common path.

## 1. Goals

- **Explicit `Std\GPU` API first** — user-visible `mapGPU` / `reduceGPU` (or
  similar) with clear cost models before compiler magic.
- **Composable with `async` / `Promise` / task groups** — completion is
  awaitable; cancellation tears down in-flight GPU work safely.
- **Correct refcounting** — host buffers (pinned or not) live until the GPU
  queue finishes reads/writes; no UAF on early `raise` or group cancel.

## 2. Non-goals (first ship)

- **Full SPIR-V from arbitrary Yona lambdas** — the compiler rewrites only the
  **fixed kernel library** (inline IntArray/FloatArray map/filter/foldl)
  to the Std\GPU ABI (`docs/gpu-transparent-lowering.md`). Arbitrary
  closures stay on the host path by default (`\x -> x + x * x` and similar);
  **`yonac --strict-accelerator`** rejects them with **E0700** (honest typed
  rejection; no silent wrong answers). `\x -> x * x` is in the kernel library.
- **Multi-GPU / MGPU** selection UI.
- **Effect-row inference for `Gpu`** — `perform Gpu.*` / handlers ship now;
  full effect rows remain GitHub #8.

## 3. Submit / completion model

### 3.1 Return type

- `submitCompute : … -> Promise GpuResult` (exact name TBD) — **reuse**
  existing `Promise` machinery where possible with a **discriminant** or
  side channel so `async_await` knows this promise waits on a **fence** (or
  timeline semaphore), not only io_uring or thread-pool work.

### 3.2 Vulkan mapping

- Each submit records a **command buffer** + optional **signal semaphore**
  (timeline `uint64_t` counter preferred over binary fences for chaining).
- **Completion:** a **dedicated waiter** path:
  - **Option A:** small `poll(2)` / `epoll` on timeline semaphore FDs if
    exposed, or
  - **Option B:** background thread calling `vkWaitForFences` with short
    timeouts and pushing completions to the same completion queue the
    runtime already uses for async, or
  - **Option C:** interleave GPU fence check in the **io_uring reap loop**
    only if integration stays simple.

**Rule:** Yona **task pool worker threads** never block in `vkQueueWaitIdle`.

### 3.3 Progress / `await`

- `await` on a GPU-backed promise: suspend (today: thread-pool future; later:
  coroutine) until fence signals; then resolve value / run user callback on
  the executor.

## 4. Structured concurrency + cancellation

- Associate each in-flight GPU submission with a **`task_group` id** (or
  cancel token pointer) at submit time.
- On **group cancel / end:**  
  - stop scheduling **new** GPU work for that group;  
  - for pending: either let them finish then discard callbacks, or use
    `vkDeviceWaitIdle` only on **shutdown** path (acceptable) — prefer
    **skip completion** and **release** host buffers only after known GPU
    idle for those command buffers (pool reset or `vkQueueWaitIdle` in
    teardown path only).

Document **exact** semantics (lossy cancel vs drain) in the stdlib API.

## 5. Memory: pinned buffers and channels

- **Pinned host memory:** `PinnedFloats` with explicit close — prefers Vulkan
  **host-visible** mapped memory when `yona_gpu_vulkan_ctx_init` succeeds;
  malloc fallback otherwise (`YONA_GPU_PINNED_HOST_MALLOC=1` forces malloc).
  `pinnedBackend` reports `"vulkan-mapped"` / `"host-malloc"`.
  `mapFloatPinnedGPU` scales in place on the mapped/host pointer.
- **Channels:** `gpuFloatChannel` / `drainMapFloatGPU` reuse `Std\Channel` for
  bounded `FloatArray` chunk pipelines into GPU map kernels — same lifetime
  rules as other channel payloads.

## 6. Effects and failures

- **`Gpu` effect ops** (`perform Gpu.oom` / `Gpu.deviceLost` / `Gpu.fail code`)
  are registered at prelude load. `Std\GPU` kernels return `GpuIssue` /
  `Result` (`checkGpu` / `withGpuIssue`). `raiseGpu` / `withGpuFallback`
  `perform` those ops; GENFN remonomorphization inside a user `handle` binds
  the caller's clauses (effect rows on `.yonai`). Direct use-site `perform`
  still works. There is no runtime handler stack across precompiled objects.

## 7. Multi-stage graphs

- **Shipped:** `mapReduceGraphGPU` batches map→…→reduce into one command buffer
  with sync2 barriers (`docs/gpu-architecture.md`). Further graph shapes
  (arbitrary DAG, multi-queue) remain future work.

## 8. Implementation phases (execution order)

### 8.0 Extern `Promise` lowering (compiler)

GPU and other runtimes sometimes return a **`yona_promise_t*`** that is **not**
created by the thread pool (`yona_rt_async_call`). The compiler models all
promise-carrying `extern` variants with **`ast::ExternPromiseKind`** in
`include/ast.h`, and records the awaited inner type as **`promise_inner_type`**
on both **`ModuleFunctionMeta`** and **`CompiledFunction`** (`include/Codegen.h`).

| `ExternPromiseKind` | Yona keyword | C ABI return (typical) | Await |
|---------------------|--------------|------------------------|-------|
| `Sync` | *(none)* | concrete `T` | — |
| `ThreadPool` | `extern async` | inner `T` (pool wraps the fn) | `yona_rt_async_await` on pool promise |
| `IoUring` | `extern io` | `i64` io_uring cookie | `yona_rt_io_await` |
| `NativePtr` | `extern native` | `yona_promise_t*` | `yona_rt_async_await` |

`.yonai` interface rows use **`FN`**, **`AFN`**, **`IO`**, or **`NAT`** before the
mangled symbol (`src/codegen/CodegenModule.cpp`). At call sites, **`TypedValue`**
carries **`PromiseAwaitPath`** (`AsyncPtr` vs `IoUring`) so **`auto_await`**
dispatches to the correct runtime primitive (`src/codegen/CodegenApply.cpp`).
AFN/IO/NAT imports share **`declare_import_extern_fn`** + **`bind_imported_promise_cf`**
to avoid duplicated registration logic.

0. **Shipped:** `lib/Std/GPU.yona` + `src/runtime/gpu_stub.c` export `available`,
   `apiVersion`, and `physicalDeviceCount`. Yona calls use a `UNIT` placeholder
   at the LLVM/C boundary. `yonac` detects modules after skipping `#` line
   comments (same as the lexer). **Optional Vulkan:** CMake `find_package(Vulkan)`
   defines `YONA_HAS_VULKAN` and links `Vulkan::Vulkan` on **any** host where
   `find_package(Vulkan)` succeeds (Linux, macOS, Windows with the SDK/loader).
   The runtime then performs a **one-time** `VkInstance` probe (portability
   enumeration on Apple) to set `physicalDeviceCount` and `available` when any
   enumerated device exposes `VK_QUEUE_COMPUTE_BIT`, then destroys the instance
   (discovery only — no long-lived `VkDevice` in this phase). Codegen E2E still
   compiles `gpu_stub.c` without `YONA_HAS_VULKAN`, so fixture output stays
   deterministic. **`floatArrayMul2Async`** / **`floatArrayScaleAsync`** (phase below)
   are the shipped user-facing **`extern native`** compute hooks (`scale` chooses the
   factor; **`mul2`** is `scale = 2`).
1. **Vulkan init + single queue + fixed nop compute pipeline** in C runtime —
   `yona_gpu_vulkan_ctx_init` / `yona_gpu_vulkan_ctx_shutdown` in `gpu_stub.c`
   (C-only; not `Std\GPU` externs): `VkInstance`, `VkDevice`, compute `VkQueue`,
   `VkCommandPool`, embedded SPIR-V from `gpu_nop.comp` (regenerate via
   `scripts/gen_gpu_nop_spv.sh`). **`yona_gpu_vulkan_dispatch_nop_once`** records
   one `vkCmdDispatch(1,1,1)` and blocks on `vkWaitForFences` — **internal / tests
   only** (set `YONA_GPU_TEST_DISPATCH=1` for doctest); not for Yona worker threads.
   **`yona_gpu_vulkan_float64_buffer_scale_inplace`** / **`mul2_inplace`** (C-only) copy a
   contiguous `double[]` slice to a host-visible SSBO, run the fixed `shaderFloat64`
   scale kernel (`gpu_f64_mul2.comp` — push constants: element count + `double scale`;
   regenerate via `scripts/gen_gpu_f64_mul2_spv.sh`), then copy back — same layout
   as Yona `FloatArray` payload (`mul2` is `scale = 2.0`). **`scale_async`** /
   **`mul2_async`** return a **`yona_promise_t`** completed on a **dedicated GPU fence thread**
   (`gpu_stub.c`), not the thread pool — uses `yona_rt_promise_new` /
   `yona_rt_promise_complete` (`async.c`). Doctests: `YONA_GPU_TEST_F64_MUL2=1`,
   `YONA_GPU_TEST_F64_MUL2_ASYNC=1`. The **`yona_Std_GPU__floatArray*Async`**
   wrappers call **`yona_gpu_vulkan_ctx_init()`** before enqueue (idempotent)
   so benches and programs do not rely on a separate C init call. **Shipped next:**
   `mapGPU` / `reduceGPU` / `mapFloatGPU` / `mapReduceGraphGPU`, sync2 graphs,
   early cancel, `PinnedFloats` — see `docs/gpu-architecture.md`.
2. **Yona `Std\GPU` surface** — **`floatArrayMul2Async : FloatArray -> Int`** and
   **`floatArrayScaleAsync : Float -> FloatArray -> Int`** (`extern native`,
   `Promise Int` at use sites) wrap the fence-thread promise;
   synchronous `mapGPU` remains acceptable only behind a **debug** flag until
   higher-level **`mapGPU` / `reduceGPU`** land.
3. **Fence + promise integration** — §3–§4 (partially satisfied by the fence waiter
   + `yona_rt_promise_complete` path above; extend for cancel / device-lost).
   When a promise was registered with a **task group** and **`yona_rt_group_cancel`**
   runs before the fence thread observes completion, **`gpu_stub`** completes the
   promise with result **`-887`** and `is_error=1` after `vkWaitForFences`
   succeeds (submission is **not** dropped; buffers may already be updated).
4. **Pinned / channel pipelines** — §5 (**shipped:** Vulkan-mapped `PinnedFloats`,
   `gpuFloatChannel` / `drainMapFloatGPU`).
5. **Graph batching** — §7.
6. **Transparent lowering** — shipped for the fixed kernel library
   (`docs/gpu-transparent-lowering.md`). Arbitrary-lambda SPIR-V remains later;
   `--strict-accelerator` / E0700 is the honest rejection path.

## 9. References (implementation)

- `src/runtime/channel.c`, async / task group teardown.
- `docs/channels.md`, `docs/memory-management.md` (RC + pinning).
- Vulkan: timeline semaphores (`VK_KHR_timeline_semaphore`), synchronization2.
