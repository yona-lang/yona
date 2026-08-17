# Platform architecture (runtime)

## Goals

- **Linux**: io_uring–backed async file and network I/O (`file_linux.c`, `net_linux.c`), POSIX process APIs (`os_linux.c`).
- **macOS**: kqueue–backed async file and network I/O (`kqueue_macos.c`, `file_macos.c`, `net_macos.c`), POSIX process APIs (`os_macos.c`). File submits run on a worker pool and wake `kq_await` through a kqueue pipe; sockets use `EVFILT_READ` / `EVFILT_WRITE`. Same `yona_platform_*` / `yona_rt_io_await` ABI as Linux.
- **Windows**: Native Win32 and UCRT (`file_windows.c`, `os_windows.c`, `net_windows.c`). Socket and file submit paths integrate with `yona_rt_io_await` via IOCP-backed completion where appropriate, with direct-result IDs retained for ordering-sensitive operations.
- **No pthread on Windows**: Async and channels live under `src/runtime/platform/` (`async_win32.c` / `channel_win32.c`) and are pulled in via `#include` from `compiled_runtime.c`.

## Public runtime headers

- `include/yona/runtime/platform.h` — portable `yona_platform_*` / process ABI.
- `include/yona/runtime/uring.h` — Linux-only io_uring API + shared `io_context_t` layout.
- `src/runtime/platform/uring_linux.c` — the single ring and `io_ctx` table (must not be header-static; file/net/os are separate TUs).
- `include/yona/runtime/kqueue.h` — macOS kqueue API + the same `io_context_t` layout.
- `src/runtime/platform/kqueue_macos.c` — the single kqueue, worker pool, and `io_ctx` table.
- `include/yona/runtime/sjlj.h` — `yona_sjlj_setjmp` / `yona_sjlj_longjmp` (AArch64 inline asm; `__builtin_*` elsewhere).

## Frozen platform ABI (v1)

`platform.h` is now treated as a versioned runtime ABI contract
(`YONA_PLATFORM_ABI_VERSION=1`). New platforms should implement every symbol in
this grouped inventory:

- **Async submit/await**: `yona_rt_io_await`, `yona_platform_*_submit`,
  fd submit helpers, fd string writes, fd line reads.
- **Filesystem (path-based)**: `read/write/append`, existence, remove, size,
  directory listing.
- **Filesystem (handle-based)**: open/close/seek/tell/advance/flush/truncate.
- **Process and environment**: `getenv/getcwd/exec/exec_status/setenv/hostname`,
  `exit_process`, and process handle lifecycle (`spawn/readLine/readAll/wait/kill/writeStdin/closeStdin/pid/destroy`).
- **Console and platform constants**: console line read and constant providers
  (page size, cpu count, endianness, os name, arch).

Policy:

- ABI-breaking changes must increment `YONA_PLATFORM_ABI_VERSION`.
- Non-breaking additive changes require implementations in all active platform
  backends before merging.

## CMake selection

- All `src/runtime/platform/*.c` are **removed** from the globbed library sources, then the correct platform TUs are **appended** per OS (`WIN32` vs `APPLE` vs Linux; Linux includes `uring_linux.c`, macOS includes `kqueue_macos.c`).
- Under `src/runtime/platform/`, the files `async_posix.c`, `async_win32.c`, `channel_posix.c`, and `channel_win32.c` are excluded from the library glob with the other platform `.c` sources because they are **included** from `compiled_runtime.c`, not compiled as separate TUs.
- Windows links **`ws2_32`** for Winsock.

## `compiled_runtime.c` boundary

- Shared Yona runtime (RC, stdlib shims, HTTP helpers, etc.) lives here.
- Platform entry points (`yona_platform_*`, `yona_Std_Net__*`, process spawn, `yona_rt_io_await`) live under `src/runtime/platform/` and are compiled as their own `.c` files.
- Async/channel implementations are **included** at the end of `compiled_runtime.c` so promise and channel types stay in one link unit: `_WIN32` → win32 sources; else → POSIX sources.
- Handle-oriented file operations used by `Std\File` in `compiled_runtime.c`
  now route through platform wrappers (open/close/seek/tell/flush/truncate),
  removing remaining per-OS `#if` branches from this path.

## CLI/REPL linker plan

- `include/LinkerPlan.h` + `src/LinkerPlan.cpp` define shared linker-mode
  selection for `yonac` and `yona`.
- Supported modes are `auto`, `bundled`, `system`, and `inprocess`.
- In `auto`, the toolchain prefers bundled `lld` when found under discovered
  sysroots (`bin/` or `llvm/bin/`), and falls back to the external/system
  linker if none is packaged.
- `bundled` requires packaged `lld` and fails fast when missing; `system`
  always uses the host toolchain linker path.
- `inprocess` is an opt-in embedded-linker path; current builds may fall back
  to the external linker flow when embedded LLD support is not compiled in.
- Current CMake default requests embedded LLD (`YONA_ENABLE_INPROCESS_LLD=ON`),
  but configure-time dependency checks may auto-disable it on toolchains that
  are missing required linker deps (for example, MSVC-compatible LibXml2 on
  Windows for LLVM Windows-manifest support).
- Embedded-linker dependency detection and Windows LibXml2 fallback are now
  centralized in `cmake/YonaInProcessLld.cmake` to keep top-level CMake logic
  readable and packaging behavior consistent.
- `YONAC_REQUIRE_INPROCESS_LLD=1` forces hard failure when in-process linker
  mode is unavailable or fails, preventing silent fallback in strict CI gates.
- CMake now produces prebuilt runtime artifacts under build-local `runtime/`
  (`compiled_runtime.o`, `crt_*.o`, and `yona_runtime.lib` /
  `libyona_runtime.a`) so distribution packaging can ship an object/sysroot
  layout that avoids runtime C recompilation in normal CLI/REPL usage.

## Windows compile flags

- **`NOMINMAX`** and **`WIN32_LEAN_AND_MEAN`** are set in CMake for `WIN32` so Windows headers do not break C++ standard library min/max.

## `yona_io_register_direct_result`

- Shared helper used by `file_windows.c` and `net_windows.c`: registers an opaque pointer (or integer cast through `intptr_t`) for a high direct-result ID range; `yona_rt_io_await` in `file_windows.c` completes those immediately.

## Codegen transfer scopes

- Branch-sensitive Perceus transfer tracking now uses a scope-entry basic-block
  ordinal watermark (`CodegenExpr` transfer scope helpers) so cross-branch
  droppability checks are O(1) per value.
- This keeps if/case compensating drops precise without repeatedly materializing
  full pre-scope basic-block membership sets.
