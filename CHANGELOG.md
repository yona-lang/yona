# Changelog

## v0.1.1 (2026-08-18)

First tagged release of the current tree (`VERSION` 0.1.1). Includes work
landed after the April changelog draft.

### Type system
- Added `docs/type-system-status.md` (GitHub #3 audit): effects, rows,
  linear/refinement checkers, `@borrow`, GENFN borrow masks, exhaustiveness.

### Runtime / platforms
- Linux: shared io_uring, File `auto_await`, GCC-safe SJLJ `longjmp` attributes.
- macOS: kqueue runtime, MoltenVK, Metal i32/f32 GPU kernels.
- Windows: UDP ABI and CI/DIA fixes (see recent `master` history).

### Distribution
- Fedora Copr, AUR `yona-bin`, and Launchpad PPA publish jobs after GitHub Release.
- Maintainer setup: `dist/RELEASING.md`.
- Windows WiX MSI + ZIP on release.

## v0.1.1-draft (2026-04-24)

Kept for history of the April packaging/linker notes.
- Added Fedora Copr, AUR `yona-bin`, and Launchpad PPA publish jobs after GitHub Release.
- Documented one-time maintainer setup in `dist/RELEASING.md`.
- Added Windows WiX v4 installer scaffold (`packaging/windows/`) with MSI build script.
- Extended release workflow with a Windows release job that publishes both ZIP and MSI artifacts.
- Kept Linux/macOS packaging aligned with precompiled runtime artifact shipping.

### Toolchain and Linker
- Modularized embedded linker configuration into `cmake/YonaInProcessLld.cmake`.
- Fixed Windows embedded-LLD libxml resolution by retargeting `LLVMWindowsManifest` to fetched `LibXml2`, avoiding mixed `.a`/`.lib` linkage.
- Added stricter release checks for linker-mode/runtime artifact smoke validation on Windows.

### Documentation
- Updated roadmap/todo status to reflect Windows installer scaffolding and release CI progress.
- Updated installation and architecture docs for current linker/runtime packaging flow.

## v0.1.0 (2025-04-06)

Initial release of the Yona compiler targeting LLVM.

### Language Features
- Newline-aware lexer with juxtaposition-based function application
- Pattern matching: integer, symbol, wildcard, head-tail, tuple, constructor, or-pattern, guards
- Algebraic data types (non-recursive flat, recursive heap-allocated)
- Traits with concrete/constrained instances, default methods, superclass constraints
- Module system with FQN-based imports, interface files (.yonai), cross-module generics
- Exception handling (raise/try/catch via setjmp/longjmp)
- Closures with env-passing convention and closure devirtualization
- String interpolation, do-blocks, pipe operators
- Generators/comprehensions for seq, set, dict with stream fusion

### Performance
- LLVM codegen with optimization levels O0-O3
- Stream fusion: chained comprehensions fused into single loops
- LTO: cross-module inlining of C runtime via llvm::Linker
- Closure devirtualization for known lambdas at HOF call sites
- fastcc calling convention for internal functions
- Branch prediction hints on hot runtime paths
- Benchmark results: list_map_filter at 1.0x C, tak 0.8x (faster than C)

### Data Structures
- Persistent Seq: flat array (<=32) + radix-balanced trie with head chain + tail buffer
- Persistent Dict: HAMT with splitmix64 hash, transient inserts (2.0x C)
- Persistent Set: HAMT-backed, sharing Dict infrastructure (2.1x C)

### Memory Management
- Atomic reference counting (RELAXED inc, ACQ_REL dec)
- Recursive destructors for all container types via heap_mask bitmasks
- Hybrid Perceus DUP/DROP (callee-owns for non-seq, callee-borrows for seq)
- Slab-based pool allocator (5 size classes)
- Arena allocation for non-escaping let-bound values
- Unique-owner optimization (in-place mutation when rc==1)
- io_uring buffer pinning for async I/O safety

### Standard Library (27 modules)
- **Pure Yona (12)**: Option, Result, List, Tuple, Range, Math, Pair, Bool, Test, Collection, Function, Http
- **C runtime (15)**: String, Encoding, Types, IO, File, Process, Random, Json, Crypto, Log, Net, Bytes, Time, Path, Format, Dict, Set, Regex

### I/O Architecture
- io_uring backend (Linux): raw syscalls, submit-and-return, async file/network I/O
- Thread pool with work-stealing for extern async functions
- Non-blocking Process module: spawn, readLine, readAll, wait, kill, writeStdin

### Tooling
- `yonac` compiler CLI with -O0 to -O3, --emit-ir, --emit-obj, debug symbols
- `yona` interactive REPL
- DWARF debug info generation
- Documentation system (doc comments extracted by gendocs.py)
- Benchmark suite with 11 benchmarks and C reference implementations
- CI/CD: GitHub Actions with Linux, macOS, Windows builds

### Testing
- 763 assertions across 75 test cases
- Codegen E2E fixtures (compile -> run -> check output)
- Multi-module linking tests
- Trait/generic cross-module tests
