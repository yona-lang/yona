# Yona Standard Library

## Module overview

The generated [API index](../../docs/api/README.md) is the authoritative module,
function, and type inventory. It is regenerated from checked-in interfaces and
source documentation, so this file does not duplicate counts that can drift.

Pure transformations, pattern matching, recursion, and combinator plumbing are
implemented in `.yona` modules. Native entry points are limited to system
access, mutable runtime primitives, external libraries, bit-level layout, and
measured hot loops. A Yona module may therefore expose a small native substrate
while keeping its public composition in Yona; `Std\Gpu`, `Std\Http`, and
`Std\Math` are examples.

## Documentation

Doc comments use `##` prefix in source files. Run `python3 scripts/gendocs.py`
to generate markdown API reference in `docs/api/`.

## Architecture

### Pure Yona modules (`.yona`)
Written in Yona. Can include `extern` declarations for C bindings
(e.g., `extern sqrt : Float -> Float` in Std\Math binds to libm).
Compiled to `.o` + `.yonai` like user modules.

### C runtime modules (`.yonai` only)
Interface files pointing to native entry points in the canonical
`yona_runtime` archive.
Handle string operations, type conversions, I/O, crypto, etc.

### Platform-specific runtime
I/O modules use platform-specific implementations:

```
include/yona/Runtime/Platform/
  Api.h                 # Portable interface
  IoUring.h             # Shared io_uring infrastructure (Linux)
src/Runtime/Platform/
  IoUringLinux.c        # Shared ring and completion registry
  FileLinux.c           # File I/O via io_uring
  NetLinux.c            # TCP/UDP networking via io_uring
  OsLinux.c             # Console I/O, process, environment
```

Current status: Linux and Windows both ship native platform backends with the
same `include/yona/Runtime/Platform/Api.h` ABI. macOS uses the matching
kqueue backend.
