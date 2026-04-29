# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Prerequisites

- LLVM 22+ (Fedora: `sudo dnf install llvm llvm-devel llvm-libs cmake ninja-build`)
- CMake 3.10+, Ninja
- C++23 capable compiler

### Configure and build (Linux)
```bash
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux
```
Replace `linux` with `macos` or omit the suffix for Windows. Replace `debug` with `release` for release builds.

Optional GPU: add `-DYONA_ENABLE_VULKAN=ON` and set `VULKAN_SDK` to the LunarG root so the runtime compiles against `vulkan/vulkan.h` (see `docs/gpu-architecture.md`). Default builds do not require it. To match that optional build when compiling the runtime from source (e.g. `yonac` without a packaged `compiled_runtime.o`), set `YONA_COMPILE_GPU_VULKAN=1` **and** `VULKAN_SDK`; leave both unset for the default CPU-only runtime headers. With Vulkan enabled at configure time, `Std\GPU.vulkanStatus` may return `vulkan-device` after runtime `dlopen`/`LoadLibrary` dispatch creates a compute queue — still **no** import-library link on the main executable.

### Compiler flags
- `-O0` to `-O3` — optimization level (default O2)
- `-g` — emit DWARF debug info
- `--Wall`, `--Wextra`, `--Werror`, `-w` — warning control
- `--emit-ir` — print LLVM IR instead of compiling
- `--emit-obj` — emit object file only (don't link)
- `-I path` — module search paths for `.yonai` files
- `--explain E0100` — show detailed explanation for an error code

### Prelude
Available in all programs without imports (from `lib/Prelude.yona`):

**Types:** `Linear a`, `Option a` (Some/None), `Result a e` (Ok/Err), `Iterator a`

**Functions:** `identity`, `const`, `flip`, `compose`, `foldl`, `foldr`

**Adding to the prelude** (unified — one source of truth):
- For C functions: add implementation in `compiled_runtime.c`, add `FN` line to `lib/Prelude.yonai`
- For Yona functions: edit `lib/Prelude.yona`, recompile with `yonac lib/Prelude.yona`, move `.yonai` to `lib/`
- No manual registration needed — `load_prelude()` reads `.yonai` and registers in parser, codegen, and type checker automatically

### Run tests
```bash
# Via CTest
ctest --preset unit-tests-linux

# Directly
./out/build/x64-debug-linux/tests

# Run a specific test (doctest subcase filter)
./out/build/x64-debug-linux/tests -tc="TestName"
```

On Windows, set `YONA_PATH=…/test/code;…/lib` and ideally `YONAC_CC` to your LLVM
`clang.exe` (CTest does this for you). If you have been experimenting with
optional Vulkan runtime builds, **unset** `YONA_COMPILE_GPU_VULKAN` (or set it
to `0`) before running `tests.exe` manually so the scratch `compiled_runtime`
object matches the default non-Vulkan configuration. CTest always injects
`YONA_COMPILE_GPU_VULKAN=0` for `doctest_tests`.

When CMake is configured with `-DYONA_ENABLE_VULKAN=ON`, CTest also registers
`doctest_gpu_vulkan`, which runs `tests -tc=*gpu?vulkan*` **without** forcing
`YONA_COMPILE_GPU_VULKAN=0` (so the filter matches device + optional mapAdd /
mapMul / reduce doctests against the Vulkan-enabled `yona_lib_static`). Example:
`ctest --test-dir out/build/x64-debug -R doctest_gpu_vulkan -V` (adjust the
build directory for your preset).

Vulkan device selection, opt-in env vars, and diagnostics are documented in
`docs/gpu-architecture.md` and `docs/api/GPU.md` (`YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX`,
`yona_gpu_vulkan_device_last_note()`, `Std\GPU.vulkanLastNote`).

### Format code
```bash
./scripts/format.sh  # runs clang-format on include/, src/, test/, cli/
```

### Generate API docs
```bash
python3 scripts/gendocs.py  # extracts ## comments → docs/api/
```

## Architecture Overview

Yona language compiler using LLVM. Pipeline: Lexer → Parser → AST → Codegen (LLVM IR) → Native executable.

### Key Design Patterns

**Newline-aware lexer**: Newlines are significant tokens (`YNEWLINE`) that delimit expressions in case arms, do-blocks, and module function bodies. Semicolons are equivalent to newlines. Inside brackets (`()`, `[]`, `{}`), newlines are suppressed (treated as whitespace). After binary operators and continuation tokens (`->`, `=`, `,`), the following newline is suppressed to allow natural line continuation. This enables juxtaposition-based function application (`f x y` instead of requiring `f(x, y)`) without ambiguity at expression boundaries.

**Visitor pattern with generic return types**: `AstVisitor<ResultType>` is a templated base class. AST nodes implement `accept()` that dispatches to the correct `visit()` overload.

**LLVM Codegen** (`src/Codegen.cpp`): Type-directed code generation using `TypedValue = {Value*, CType}`. Every codegen method returns both an LLVM value and its type tag. Functions are compiled with deferred compilation — stored as AST at definition, compiled at call site where argument types are known (monomorphization). Closures use env-passing convention: `{fn_ptr, ret_tag, arity, num_caps, heap_mask, cap0, ...}` heap arrays, functions take `(ptr env, args...)`. The `yonac` CLI compiles Yona source to native executables via LLVM.

**Algebraic Data Types**: `type Option a = Some a | None` — named constructors with typed fields. ADT fields support function type signatures: `type Stream a = Next a (() -> Stream a) | End`. Constructors are first-class functions. Non-recursive ADTs use flat structs `{i64 tag, payload}`; recursive ADTs and ADTs with function fields are heap-allocated.

**Module System**: Modules are top-level declarations (`ModuleDecl`), not expressions. No `as`/`end` — modules end at EOF. `export` statements: `export func`, `export type Name`, `export f from Mod`. Module-level `extern` declarations for C bindings (`extern sqrt : Float -> Float`). Compile to native object files with C-ABI exports. Interface files (`.yonai`) provide cross-module type metadata. Exported functions include source text in `.yonai` (`GENFN_BEGIN`/`GENFN_END`) for cross-module monomorphization — when call-site types differ from the pre-compiled signature, the source is re-parsed and compiled locally with actual types.

**Memory Management**: Atomic reference counting with recursive destructors and Perceus-linear callee-owns ABI. See `docs/memory-management.md` for full details. Key points: All heap objects use `rc_alloc` with a `[refcount, type_tag_encoded]` header. Atomic `rc_inc`/`rc_dec` (`__atomic_fetch_add/sub`). Recursive destructors free children via `heap_mask` bitmasks (closures, ADTs, tuples, seqs, sets, dicts). Unified Perceus: all heap types follow callee-owns at call sites — single-use args transferred without DUP, runtime consume paths rc_dec on path-copy. Per-branch `transferred_seqs_` / `transferred_maps_` scoping in if-expressions and case arms handles asymmetric transfers with SSA dominance preserved via pre_blocks snapshotting. Unique-owner in-place optimization (rc==1) for seq cons/tail and HAMT put. Weak self-references break recursive closure cycles. io_uring buffer pinning for async I/O safety. Escape analysis (`include/EscapeAnalysis.h`) for arena allocation of non-escaping values.

**Symbol interning**: Symbols (`:ok`, `:none`) are interned to `i64` IDs at compile time. Comparison is `icmp eq i64` (1 cycle). Pattern matching on symbols generates integer switch/select.

### Core Components

- **AST** (`include/ast.h`): Node hierarchy rooted at `AstNode`. Major branches: `ExprNode` (expressions), `PatternNode` (pattern matching), `ScopedNode` (scope-creating). Each node tracks `SourceContext` for error reporting.
- **Codegen** (`src/Codegen.cpp`): LLVM IR generation with `TypedValue` system. Supports literals, arithmetic, functions, closures, recursion, case expressions, tuples, sequences, symbols, sets, dicts, ADTs, or-patterns, higher-order functions, partial application, generators/comprehensions. Compiled runtime in `src/compiled_runtime.c`.
- **Type System** (`include/types.h`): Variant-based types including builtins, function types, product/sum types, record types, ADTs, and named types. The codegen uses `CType` + `TypedValue` with compile-time type inference and monomorphization.
- **Pattern Matching**: `CaseExpr` contains a target expression and vector of `CaseClause(pattern, body)`. Pattern types include value, tuple, seq, head-tail (`[h|t]`), dict, record, constructor (`Some x`), `as` binding (`@`), and or-patterns.
- **Module System**: FQN-based. Modules (`ModuleDecl` in AST) are top-level declarations, not expressions. They compile to object files with C-ABI exports via name mangling (`yona_Pkg_Mod__func`). Interface files (`.yonai`) for cross-module type-safe linking. Three import styles: selective, wildcard, FQN calls (`Mod::func`). Cross-module generics via GENFN source in `.yonai` — on-demand re-parse and monomorphization when call-site types differ. Trait instance methods have `ExternalLinkage` for cross-module trait dispatch.

### Build Targets

- `yona_lib` (shared) / `yona_lib_static`: Core library (lexer, parser, AST, codegen)
- `yonac`: Compiler executable (links `yona_lib` + CLI11)
- `yona`: REPL executable — compile-and-run mode (links `yona_lib`)
- `tests`: Test executable (links `yona_lib_static` + doctest)

### Dependencies

- **LLVM 22+**: Code generation backend
- **CLI11**: Command-line argument parsing (fetched via FetchContent)
- **doctest**: Testing framework (fetched via FetchContent)

### Yona Idioms (IMPORTANT — follow these when writing Yona code)

- **Don't nest let expressions.** Use multi-binding: `let x = 1, y = 2 in x + y`
- **Don't wrap `do` in `let`.** Use `do ... end` directly for side effects
- **Use comma-separated imports:** `import a from X, b from Y in ...`
- **Use `with` for resources:** `with open "f" as h in ... end`
- **Use parallel comprehensions:** `[| f x for x = xs ]` for concurrent processing
- **Use `foldl` for aggregation:** `foldl (\a b -> a + b) 0 xs` (prelude, loop-based, no stack overflow)
- **Use iterators for streaming:** `readLines`, `chars`, `split` return `Iterator` (O(1) memory)
- **Prelude needs no import:** `Some`, `None`, `Ok`, `Err`, `Linear`, `Iterator`, `foldl`, `foldr`, `identity`, `const`, `flip`, `compose`
- See `docs/style-guide.md` for the full guide

### Stdlib implementation rule (IMPORTANT)

**Anything that can be written in Yona must be written in Yona.** Drop to C
only when there is no way to express the operation in pure Yona — typically
because it needs an OS syscall (file I/O, network, processes, time, signals),
mutable state primitives the language doesn't expose (atomics, locks, channel
buffers), bit-level layout control (byte arrays, hashing, crypto), an
external C library binding (PCRE2, OpenSSL, libxml2), or performance-critical
hot loops with measured wins (matrix kernels, codec inner loops). Pure data
transformations, pattern matching, recursion, and combinator plumbing all
belong in `.yona` files. The C runtime should be the substrate, not the
default. When in doubt: write the Yona version first, profile if needed, and
only then consider lowering to C.

### Bug-tracking rule (IMPORTANT)

**Whenever you discover a bug — parser, codegen, runtime, anything — append
it to `docs/todo-list.md` immediately, with a one-line repro, and stop to
ask which bug(s) to fix.** Working around a bug silently buries the
information; a bug list with reproductions accumulates the data we need to
prioritize compiler work. Don't keep coding past a fresh bug discovery
without first noting it and checking which one to attack next.

### Development Workflow

- New language features require changes across: Lexer → Parser → AST → Codegen
- AST modifications must update the header (`ast.h`), visitor (`ast_visitor.h`), and codegen
- Tests are in `test/` — codegen E2E fixtures in `test/codegen/*.yona` + `*.expected`
- ADT tests in `test/adt_test.cpp`, trait/cross-module tests in `test/trait_test.cpp`
- Stdlib in `lib/Std/` — 26 modules: 12 pure Yona + 14 C runtime (see docs/todo-list.md for full list)
- Stdlib fixture tests in `test/codegen/stdlib_*.yona` + `*.expected`
- Platform-specific runtime in `src/runtime/platform/` — `include/yona/runtime/uring.h` (shared io_uring, Linux), `include/yona/runtime/platform.h`, `file_linux.c`, `net_linux.c`, `os_linux.c`
- API docs generated by `python3 scripts/gendocs.py` → `docs/api/`
