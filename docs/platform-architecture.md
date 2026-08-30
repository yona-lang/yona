# Platform architecture (runtime)

## Goals

- **Linux**: io_uring–backed async file and network I/O (`FileLinux.c`, `NetLinux.c`), POSIX process APIs (`OsLinux.c`).
- **macOS**: kqueue–backed async file and network I/O (`KqueueMacOs.c`, `FileMacOs.c`, `NetMacOs.c`), POSIX process APIs (`OsMacOs.c`). File submits run on a worker pool and wake `YonaRuntimeKqueueAwait` through a kqueue pipe; sockets use `EVFILT_READ` / `EVFILT_WRITE`. Same `YonaRuntimePlatform*` / `YonaRuntimeIoAwait` ABI as Linux.
- **Windows**: Native Win32 and UCRT (`FileWindows.c`, `OsWindows.c`, `NetWindows.c`). Socket and file submit paths integrate with `YonaRuntimeIoAwait` via IOCP-backed completion where appropriate, with direct-result IDs retained for ordering-sensitive operations.
- **No pthread on Windows**: Async and channels live under
  `src/Runtime/Concurrency/` (`AsyncWin32.c` / `ChannelWin32.c`) and compile into
  the `yona_runtime_concurrency` component.

## Public runtime headers

- `include/yona/Runtime/Platform/Api.h` — portable `YonaRuntimePlatform*` / process ABI.
- `include/yona/Runtime/Platform/IoUring.h` — Linux-only io_uring API + shared `YonaIoContext` layout.
- `src/Runtime/Platform/IoUringLinux.c` — the single ring and `io_ctx` table (must not be header-static; file/net/os are separate TUs).
- `include/yona/Runtime/Platform/Kqueue.h` — macOS kqueue API + the same `YonaIoContext` layout.
- `src/Runtime/Platform/KqueueMacOs.c` — the single kqueue, worker pool, and `io_ctx` table.
- `include/yona/Runtime/Platform/SjLj.h` — `YONA_SJLJ_SETJMP` / `yonaSjLjLongJump` (AArch64 inline asm; `__builtin_*` elsewhere).

## Platform boundary

`Api.h` is the canonical runtime platform contract. New platforms
must implement every symbol in this grouped inventory:

- **Async submit/await**: `YonaRuntimeIoAwait`, `YonaRuntimePlatform*_submit`,
  fd submit helpers, fd string writes, fd line reads.
- **Filesystem (path-based)**: `read/write/append`, existence, remove, size,
  directory listing.
- **Filesystem (handle-based)**: open/close/seek/tell/advance/flush/truncate.
- **Process and environment**: `getenv/getcwd/exec/exec_status/setenv/hostname`,
  `exit_process`, and process handle lifecycle (`spawn/readLine/readAll/wait/kill/writeStdin/closeStdin/pid/destroy`).
- **Console and platform constants**: console line read and constant providers
  (page size, cpu count, endianness, os name, arch).

Contract changes must update every active platform backend in the same change.

## CMake selection

- `cmake/YonaComponents.cmake` owns explicit source lists; runtime sources are
  never discovered with a recursive glob.
- Exactly one platform set is selected for
  `yona_runtime_platform_io` (`WIN32`, `APPLE`, or Linux), while the matching
  async/channel pair is selected for `yona_runtime_concurrency`.
- Core, collections, codecs, concurrency, GPU, and platform I/O compile once as
  focused object targets and are aggregated into the single `yona_runtime`
  archive.
- Windows links **`ws2_32`** for Winsock.

## Runtime component boundaries

- `yona_runtime_core` owns reference counting, closures, ADTs, exceptions, and
  native stdlib entry points.
- `yona_runtime_collections` owns sequences and HAMTs;
  `yona_runtime_codecs` owns JSON, regex, and UTF conversion.
- `yona_runtime_concurrency` owns tasks, promises, and channels.
- `yona_runtime_gpu` owns capability discovery, device state, and kernels.
- `yona_runtime_platform_io` owns platform entry points
  (`YonaRuntimePlatform*`, native Net/File/Process operations, and I/O completion).
- Cross-component declarations live in runtime headers; ad-hoc source-file
  recompilation is not a supported interface.

## CLI/REPL linker plan

- `include/yona/Toolchain/LinkerPlan.h` + `src/Toolchain/LinkerPlan.cpp` define shared linker-mode
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
- CMake produces exactly one aggregate runtime archive under build-local
  `runtime/`: `yona_runtime.lib` on Windows or `libyona_runtime.a` on Unix.
  CLI, REPL, tests, and packages consume that archive directly. Missing
  archives are hard errors; there is no source or loose-object fallback.

## Windows compile flags

- **`NOMINMAX`** and **`WIN32_LEAN_AND_MEAN`** are set in CMake for `WIN32` so Windows headers do not break C++ standard library min/max.

## `YonaRuntimeIoRegisterDirectResult`

- A shared helper used by `src/Runtime/Platform/FileWindows.c` and
  `NetWindows.c` registers an opaque pointer (or integer cast through
  `intptr_t`) for a high direct-result ID range; `YonaRuntimeIoAwait` in
  `FileWindows.c` completes those immediately.

## Codegen transfer scopes

- Branch-sensitive Perceus transfer tracking now uses a scope-entry basic-block
  ordinal watermark (`CodegenExpr` transfer scope helpers) so cross-branch
  droppability checks are O(1) per value.
- This keeps if/case compensating drops precise without repeatedly materializing
  full pre-scope basic-block membership sets.
