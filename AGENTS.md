# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Build Commands

### Prerequisites

- LLVM 22+ (Fedora: `sudo dnf install llvm llvm-devel llvm-libs cmake ninja-build cli11-devel doctest-devel pcre2-devel lld-devel`)
- CMake 3.15+, Ninja
- C++23 capable compiler
- Optional — `Std\Gpu` Vulkan **discovery** probe (Linux/macOS): Vulkan SDK / loader so CMake finds `Vulkan::Vulkan` (Fedora: `sudo dnf install vulkan-devel vulkan-loader-devel`; macOS: `brew install molten-vk vulkan-headers vulkan-loader`). Without it, GPU counts stay at zero. After installing those packages, run **`cmake --preset …` again**; the configure log records the discovered Vulkan include and loader-library paths. On macOS the runtime `dlopen`s Homebrew/SDK `libvulkan.1.dylib` or `libMoltenVK.dylib` and enables portability instance/device extensions. Metal usually has no `shaderInt64`; `hasGpu` is still 1 when the device is ready, and IntArray GPU kernels use i32 when values fit. User-facing compute (`mapGpu`, `Promise`, fences) is specified in `docs/design-gpu-async.md`.

### Configure and build (Linux)
```bash
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux
```
Replace `linux` with `macos` or omit the suffix for Windows. Replace `debug` with `release` for release builds.

Optional GPU: add `-DYONA_ENABLE_VULKAN=ON` and set `VULKAN_SDK` to the LunarG root (macOS: `brew install molten-vk vulkan-headers vulkan-loader` is enough; CMake uses `HOMEBREW_PREFIX` or `brew --prefix`) so CMake builds the canonical `yona_runtime` archive with Vulkan support (see `docs/gpu-architecture.md`). Default builds do not require it. Reconfigure and rebuild after changing this option; installed tools never compile runtime sources. With Vulkan enabled at configure time, `Std\Gpu.vulkanStatus` may return `vulkan-device` after runtime `dlopen`/`LoadLibrary` dispatch creates a compute queue. Metal usually has no `shaderInt64`; `hasGpu` is 1 when the device is ready and IntArray GPU kernels use i32 when values fit.

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

**Functions:** `identity`, `const`, `flip`, `compose`

`foldl`, `foldr`, `map`, and `filter` are **not** prelude — import them from `Std\List`.

**Adding to the prelude** (unified — one source of truth):
- For native functions: add the implementation to its owning `src/Runtime/`
  component and add the `FN` line to `lib/Prelude.yonai`
- For Yona functions: edit `lib/Prelude.yona`, recompile with `yonac lib/Prelude.yona`, move `.yonai` to `lib/`
- No manual registration needed — `semantics::InterfaceCatalog` reads `.yonai`
  and installs parser/type-checker contracts; `Codegen::loadPrelude()` loads
  only LLVM lowering metadata.

### Run tests
```bash
# Via CTest
ctest --preset unit-tests-linux

# Directly
./out/build/x64-debug-linux/tests

# Run a specific test (doctest subcase filter)
./out/build/x64-debug-linux/tests -tc="TestName"
```

On Windows, set `YONA_PATH` to `…/test/code;…/lib` and **`YONAC_CC`** to the full path of **`clang.exe`** from the **same complete** `clang+llvm-*-windows-msvc` tree as **`LLVM_INSTALL_PREFIX`** (or put that `bin` on `PATH`). CI appends `C:\LLVM\bin` to `PATH` and sets `YONAC_CC` for the same reason. See **INSTALL.md** (Windows): *Complete Windows LLVM tree* and *`YONAC_CC` and doctest* — incomplete LLVM installs (`LLVMExports.cmake` missing `LTO.lib`, `llvm-ar.exe`, etc.) break `find_package(LLVM)`; **`LLVM_INSTALL_PREFIX`** must not contain typos.

When CMake is configured with `-DYONA_ENABLE_VULKAN=ON`, CTest also registers
`doctest_gpu_vulkan`, which runs `tests -tc=*gpu?vulkan*` against the same
Vulkan-enabled `yona_runtime` archive used by generated programs. Example:
`ctest --test-dir out/build/x64-debug -R doctest_gpu_vulkan -V` (adjust the
build directory for your preset).

Vulkan device selection, opt-in env vars, and diagnostics are documented in
`docs/gpu-architecture.md` and `docs/api/Gpu.md` (`YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX`,
`YonaRuntimeGpuVulkanDeviceLastNote()`, `Std\Gpu.vulkanLastNote`).

### Format code
```bash
./scripts/format.sh  # runs clang-format on include/, src/, test/, cli/
```

### Generate API docs
```bash
python3 scripts/gendocs.py  # extracts ## comments → docs/api/
```

## Architecture Overview

Yona language compiler using LLVM. Pipeline: source management → lexer/parser
→ AST → semantic model → typed IR → LLVM IR/object generation → toolchain link.

### Key Design Patterns

**Newline-aware lexer**: Newlines are significant tokens (`YNEWLINE`) that delimit expressions in case arms, do-blocks, and module function bodies. Semicolons are equivalent to newlines. Inside brackets (`()`, `[]`, `{}`), newlines are suppressed (treated as whitespace). After binary operators and continuation tokens (`->`, `=`, `,`), the following newline is suppressed to allow natural line continuation. This enables juxtaposition-based function application (`f x y` instead of requiring `f(x, y)`) without ambiguity at expression boundaries.

**Visitor pattern with generic return types**: `AstVisitor<ResultType>` is a templated base class. AST nodes implement `accept()` that dispatches to the correct `visit()` overload.

**LLVM Codegen** (`src/Codegen/Codegen.cpp`): Type-directed code generation using `TypedValue = {Value*, CType}`. Every codegen method returns both an LLVM value and its type tag. Functions are compiled with deferred compilation — stored as AST at definition, compiled at call site where argument types are known (monomorphization). Closures use env-passing convention: `{fn_ptr, ret_tag, arity, num_caps, heap_mask, cap0, ...}` heap arrays, functions take `(ptr env, args...)`. The `yonac` CLI compiles Yona source to native executables via LLVM.

**Algebraic Data Types**: `type Option a = Some a | None` — named constructors with typed fields. ADT fields support function type signatures: `type Stream a = Next a (() -> Stream a) | End`. Constructors are first-class functions. Non-recursive ADTs use flat structs `{i64 tag, payload}`; recursive ADTs and ADTs with function fields are heap-allocated.

**Module System**: Modules are top-level declarations (`ModuleDecl`), not expressions. No `as`/`end` — modules end at EOF. `export` statements: `export func`, `export type Name`, `export f from Mod`. Module-level `extern` declarations for C bindings (`extern sqrt : Float -> Float`). Compile to native object files with C-ABI exports. Interface files (`.yonai`) provide cross-module type metadata. Exported functions include source text in `.yonai` (`GENFN_BEGIN`/`GENFN_END`) for cross-module monomorphization — when call-site types differ from the pre-compiled signature, the source is re-parsed and compiled locally with actual types.

**Memory Management**: Atomic reference counting with recursive destructors and Perceus-linear callee-owns ABI. See `docs/memory-management.md` for full details. Key points: All heap objects use `YonaRuntimeAllocate` with a `[refcount, type_tag_encoded]` header. Atomic `rc_inc`/`rc_dec` (`__atomic_fetch_add/sub`). Recursive destructors free children via `heap_mask` bitmasks (closures, ADTs, tuples, seqs, sets, dicts). Unified Perceus: all heap types follow callee-owns at call sites — single-use args transferred without DUP, runtime consume paths rc_dec on path-copy. Per-branch `transferred_seqs_` / `transferred_maps_` scoping in if-expressions and case arms handles asymmetric transfers with SSA dominance preserved via pre_blocks snapshotting. Unique-owner in-place optimization (rc==1) for seq cons/tail and HAMT put. Weak self-references break recursive closure cycles. io_uring buffer pinning for async I/O safety. Escape analysis (`include/yona/Codegen/EscapeAnalysis.h`) for arena allocation of non-escaping values.

**Symbol interning**: Symbols (`:ok`, `:none`) are interned to `i64` IDs at compile time. Comparison is `icmp eq i64` (1 cycle). Pattern matching on symbols generates integer switch/select.

### Core Components

- **AST** (`include/yona/Syntax/Ast.h`): Node hierarchy rooted at `AstNode`. Major branches: `ExprNode` (expressions), `PatternNode` (pattern matching), `ScopedNode` (scope-creating). Each node carries a `SourceRange` resolved through its parsed source manager.
- **Codegen** (`src/Codegen/Codegen.cpp`): LLVM IR generation with `TypedValue` system. Supports literals, arithmetic, functions, closures, recursion, case expressions, tuples, sequences, symbols, sets, dicts, ADTs, or-patterns, higher-order functions, partial application, generators/comprehensions. Runtime components compile once into the `yona_runtime` archive.
- **Type System** (`include/yona/Model/Types.h`): Variant-based types including builtins, function types, product/sum types, record types, ADTs, and named types. The codegen uses `CType` + `TypedValue` with compile-time type inference and monomorphization.
- **Pattern Matching**: `CaseExpr` contains a target expression and vector of `CaseClause(pattern, body)`. Pattern types include value, tuple, seq, head-tail (`[h|t]`), dict, record, constructor (`Some x`), `as` binding (`@`), and or-patterns.
- **Module System**: FQN-based. Modules (`ModuleDecl` in AST) are top-level declarations, not expressions. They compile to object files with C-ABI exports via name mangling (`YonaPkgModFunc`). Interface files (`.yonai`) for cross-module type-safe linking. Three import styles: selective, wildcard, FQN calls (`Mod::func`). Cross-module generics via GENFN source in `.yonai` — on-demand re-parse and monomorphization when call-site types differ. Trait instance methods have `ExternalLinkage` for cross-module trait dispatch.

### Build Targets

- `yona_support`, `yona_model`, `yona_syntax`, `yona_interface`,
  `yona_semantics`, `yona_typed_ir`, `yona_codegen_llvm`, and
  `yona_toolchain`: Acyclic compiler components.
- `yona_typed_core` and `yona_lsp`: Canonical semantic adapters.
- `yona_lib` (shared) / `yona_lib_static`: Aggregate compiler libraries built
  from the component object targets.
- `yonac`: Compiler executable (links the aggregate compiler library + CLI11)
- `yona`: Yona-written runner (`tools/yona/main.yona`) — shebang / `-e` / stdin
- `yona-repl`: C++ REPL executable; started by `yona` on a TTY
- `yona_runtime`: Canonical aggregate runtime archive linked into generated programs
- `tests`: Test executable (links `yona_lib_static` + doctest)

### Dependencies

- **LLVM 22+**: Code generation backend
- **CLI11**: Command-line parsing — system package (`cli11-devel` / `libcli11-dev` / Homebrew `cli11`); FetchContent only if `-DYONA_FETCH_DEPS=ON` (default) and no package is found
- **doctest**: Test framework — system package (`doctest-devel` / `doctest-dev` / Homebrew `doctest`); same FetchContent fallback. Distro/Homebrew builds pass `-DYONA_FETCH_DEPS=OFF` and `-DBUILD_TESTING=OFF`.

### Yona Idioms (IMPORTANT — follow these when writing Yona code)

- **Don't nest let expressions.** Use multi-binding: `let x = 1, y = 2 in x + y`
- **`let` and `do` have different semantics.** `let` binds values
  (independent RHSs may parallelize); `do` sequences effects. Combining
  them is valid when you need both (`let a = …, b = … in do … end`). Do
  not use `let _ = effect` to sequence, and do not wrap a single
  expression in `do`. Use `do ... end` for ordered side effects
- **Use comma-separated imports:** `import a from X, b from Y in ...`
- **Use `with` for resources:** `with h = openFile "f" Read in ... end`
- **Use parallel comprehensions:** `[| f x for x = xs ]` for concurrent processing
- **Use `Std\List.foldl` for aggregation:** `import foldl from Std\List in foldl (\a b -> a + b) 0 xs` (tail-recursive, compiled to a loop)
- **Use iterators for streaming:** `readLines`, `chars`, `split` return `Iterator` (O(1) memory)
- **Prelude needs no import:** `Some`, `None`, `Ok`, `Err`, `Linear`, `Iterator`, `identity`, `const`, `flip`, `compose`
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

### Keep docs up to date

When finishing a feature or bugfix, update the matching plan under
`docs/superpowers/plans/`, `docs/todo-list.md`, and `CHANGELOG.md` in the
**same change**. Use `Unreleased` in the changelog if `VERSION` is unchanged.
Fix feature docs (`docs/*.md`) when they would otherwise contradict the code.
Also update the **public site** (`site/src/content/docs/`) whenever
user-facing language, CLI, prelude, or stdlib behavior changes. Learn /
Guides / Reference pages there are handwritten and are the published source
of truth — do not leave them stale because an internal doc already exists.
After `lib/Std/*.yona` API comment changes, run `python3 scripts/gendocs.py`.
Preview with `cd site && pnpm dev`.
Verify examples against `yonac`, not folklore.
Do not silently leave stale checkboxes.

### Development Workflow

- New language features require changes across: Lexer → Parser → AST → Codegen
- AST modifications must update `include/yona/Syntax/Ast.h`,
  `include/yona/Syntax/AstVisitor.h`, and codegen.
- Tests are in `test/` — codegen E2E fixtures in `test/Fixtures/Codegen/*.yona` + `*.expected`
- ADT tests in `test/Codegen/AdtTest.cpp`, trait/cross-module tests in `test/Semantics/TraitTest.cpp`
- Stdlib in `lib/Std/` — 26 modules: 12 pure Yona + 14 C runtime (see docs/todo-list.md for full list)
- Stdlib fixture tests in `test/Fixtures/Codegen/stdlib_*.yona` + `*.expected`
- Platform-specific runtime in `src/Runtime/Platform/` —
  `include/yona/Runtime/Platform/IoUring.h` (shared io_uring, Linux),
  `include/yona/Runtime/Platform/Api.h`, `FileLinux.c`, `NetLinux.c`, and
  `OsLinux.c`
- API docs generated by `python3 scripts/gendocs.py` → `docs/api/`
