# Yona Standard Library

## Module Overview

### Pure Yona modules (13)

| Module | Functions | Description |
|--------|-----------|-------------|
| `Std\Option` | 10 | Optional values (`Some a \| None`) |
| `Std\Result` | 11 | Error handling (`Ok a \| Err e`) |
| `Std\List` | 28 | Sequence operations (map, filter, fold, sort, zip) |
| `Std\Tuple` | 9 | 2-tuple operations (fst, snd, swap, curry) |
| `Std\Range` | 11 | Lazy integer ranges with step |
| `Std\Math` | 20 | Num trait (polymorphic abs/max/min), int math, extern float math (sqrt, sin, cos) |
| `Std\Pair` | 10 | ADT-based pairs with named fields |
| `Std\Bool` | 7 | Boolean combinators (not, xor, implies, when) |
| `Std\Test` | 6 | Test assertions |
| `Std\Collection` | 9 | iterate, unfold, window, chunks, dedup, frequencies |
| `Std\Function` | 8 | identity, compose, flip, pipe, fix |
| `Std\Http` | 13 | HTTP client/server — Method/Request/Response ADTs, get, post, serve |
| `Std\GPU` | 10 | Accelerated columnar buffers with CPU/SIMD fallback |

### C runtime modules (15)

| Module | Functions | Description |
|--------|-----------|-------------|
| `Std\String` | 27 | String operations (split, join, trim, replace, repeat) |
| `Std\Encoding` | 7 | base64, hex, URL, HTML escape |
| `Std\Types` | 5 | Type conversions (intToString, toFloat) |
| `Std\IO` | 18 | Console I/O (print, println, readLine, readStdin, readExact) |
| `Std\Utf16` | 3 | UTF-8 byte offsets ↔ LSP UTF-16 positions |
| `Std\File` | 9 | File I/O via io_uring (readFile, writeFile, readFileBytes) |
| `Std\Process` | 16 | getenv, getcwd, exit, getArgs, run, execArgs, … |
| `Std\Random` | 4 | int, float, choice, shuffle |
| `Std\Json` | 8 | Recursive `Json` ADT, parse/stringify, scalar helpers |
| `Std\Crypto` | 4 | sha256, randomBytes, uuid4 |
| `Std\Log` | 6 | Structured logging with levels |
| `Std\Net` | 12 | TCP/UDP via io_uring |
| `Std\Bytes` | 10 | Length-prefixed binary buffers |
| `Std\Time` | 6 | Timestamps, elapsed, sleep, format |
| `Std\Path` | 6 | Path manipulation (join, dirname, basename, extension) |
| `Std\Format` | 1 | String formatting with `{}` placeholders |
| `Std\GPU` | 3 | Vulkan capability discovery only (`available`, `apiVersion`, `physicalDeviceCount`); optional CMake Vulkan on Unix — see `docs/design-gpu-async.md` for the planned compute API |

## Documentation

Doc comments use `##` prefix in source files. Run `python3 scripts/gendocs.py`
to generate markdown API reference in `docs/api/`.

## Architecture

### Pure Yona modules (`.yona`)
Written in Yona. Can include `extern` declarations for C bindings
(e.g., `extern sqrt : Float -> Float` in Std\Math binds to libm).
Compiled to `.o` + `.yonai` like user modules.

### C runtime modules (`.yonai` only)
Interface files pointing to C functions in `src/compiled_runtime.c`.
Handle string operations, type conversions, I/O, crypto, etc.

### Platform-specific runtime
I/O modules use platform-specific implementations:

```
src/runtime/
  include/yona/runtime/platform.h   # Portable interface
  platform/
    include/yona/runtime/uring.h      # Shared io_uring infrastructure (Linux)
    file_linux.c          # File I/O via io_uring
    net_linux.c           # TCP/UDP networking via io_uring
    os_linux.c            # Console I/O, process, environment
```

Current status: Linux and Windows both ship native platform backends with the
same `yona/runtime/platform.h` ABI. macOS kqueue parity remains planned.
