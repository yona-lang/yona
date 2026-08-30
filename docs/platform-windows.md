# Windows runtime platform notes

This document tracks Windows-specific runtime behavior for file/network/process
paths and the current async backend state.

## Runtime units

- `src/Runtime/Platform/FileWindows.c`
- `src/Runtime/Platform/NetWindows.c`
- `src/Runtime/Platform/OsWindows.c`
- `src/Runtime/Concurrency/AsyncWin32.c`
- `src/Runtime/Concurrency/ChannelWin32.c`

All of the above implement the portable ABI from
`include/yona/Runtime/Platform/Api.h`.

## Async model on Windows

`YonaRuntimeIoAwait()` is implemented in `FileWindows.c` and serves two ID types:

- **IOCP-backed pending operations** for overlapped file/socket submits.
- **Direct/offload result IDs** for operations that complete via a registered
  result pointer or integer cast through `intptr_t`.

Direct-result IDs are intentionally retained for operations where submit-time
side-effect ordering must match existing semantics.

## File/path status

- `YonaRuntimePlatformSubmitFileRead`: IOCP-backed overlapped path with fallback.
- `YonaRuntimePlatformSubmitFileWrite`: direct-result submit.
- FD submit APIs are currently direct-result based and preserve side-effect
  ordering semantics expected by existing tests.

## Networking status

- TCP `send`/`recv` and byte variants use overlapped Winsock + IOCP completion
  and return `io_await` IDs.
- TCP `connect`/`accept` use `ConnectEx`/`AcceptEx` overlapped completion and
  return `io_await` IDs.
- UDP `udpSendTo`/`udpRecv` are synchronous (`FN` in `Net.yonai`), matching
  Linux/macOS: they return a byte count and a string pointer, not `io_await` IDs.
- `httpGet` composes async submit/await socket ops on top of the same IOCP
  completion path.

## Process/handle notes

- Winsock startup is guarded by `InitOnceExecuteOnce`.
- Socket handles are represented as `int64_t` through `(intptr_t)` casts.
- Current process stdlib behavior is implemented in
  `src/Runtime/Platform/OsWindows.c`; async pipe
  behavior follows the same submit/await contract as other runtime paths.

## Cancellation and task groups

Structured concurrency and cancellation are implemented in
`src/Runtime/Concurrency/AsyncWin32.c` and `ChannelWin32.c`, and interact with
runtime exception unwind.
Task-group arena lifecycle and raise-unwind parity are tested in
`PerceusExceptionCleanup`.
