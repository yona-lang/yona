---
title: Compiler CLI
description: Complete reference for the yonac compiler and the yona REPL, including all flags and environment variables.
---

Yona ships two binaries: `yonac`, the ahead-of-time compiler, and `yona`, an interactive compile-and-run REPL.

## yonac

```bash
yonac [input.yona] [options]
yonac -e "expression" [options]
```

`yonac` compiles Yona source to a native executable via LLVM. If the first non-comment token of the input is `module`, the file is compiled as a **module** to an object file plus a `.yonai` interface file; otherwise it is compiled as an **expression program** and linked into an executable.

### Input

| Option | Description |
|--------|-------------|
| `input` | Positional argument: the input `.yona` file |
| `-e, --expression <expr>` | Compile an expression given on the command line instead of a file |

Exactly one input source is required — a file or `-e`.

### Output

| Option | Description |
|--------|-------------|
| `-o, --output <file>` | Output file name |
| `--emit-ir` | Print LLVM IR to stdout instead of compiling |
| `--emit-obj` | Emit an object file only; do not link |

Default output names when `-o` is omitted:

| Input kind | Default output |
|------------|----------------|
| Expression program | `a.out` (`a.exe` on Windows) |
| Module, or any input with `--emit-obj` | input stem + `.o` (`a.o` for `-e` expressions) |

Compiling a module additionally writes an interface file next to the object file, with the same stem and the `.yonai` extension.

### Optimization and debugging

| Option | Description |
|--------|-------------|
| `-O <n>` | Optimization level, 0–3 (default 2) |
| `-g, --debug` | Emit DWARF debug information |

### Warnings

| Option | Description |
|--------|-------------|
| `--Wall` | Enable common warnings (unused variables, incomplete/overlapping patterns, unhandled effects) |
| `--Wextra` | Enable all warnings (adds shadowing, missing signatures, unused imports) |
| `--Werror` | Treat warnings as errors |
| `-w` | Suppress all warnings |

The individual warning flags and which group enables them are listed on the [error codes](/reference/error-codes/) page.

### Modules

| Option | Description |
|--------|-------------|
| `-I, --include <path>` | Add a module search path (for `.yonai` interface files); repeatable |
| `--sysroot <path>` | Yona distribution root, used to find `lib/` and the runtime objects |

Imports (and `Prelude.yonai`) are resolved by searching, in order: paths given with `-I`, directories in `YONA_PATH`, the input file's directory, the current directory, then `lib/` and `share/yona/lib/` under each discovered distribution root. Distribution roots come from `--sysroot`, the `YONA_HOME` environment variable, and the directory containing the `yonac` executable. `YONA_PATH` is a `:`-separated list on Unix and a `;`-separated list on Windows.

### Accelerators

The compiler transparently lowers recognized `Std\IntArray` / `Std\FloatArray` `map`, `filter`, and `foldl` call sites to the [Std\GPU](/stdlib/gpu/) kernel ABI. These flags inspect or control that lowering:

| Option | Description |
|--------|-------------|
| `--emit-accelerator-report` | Print a JSON report of `Std\GPU`-shaped call sites and exit without generating code. Expression programs are reported after typechecking; modules from an AST scan by default |
| `--emit-accelerator-report-with-types` | With `--emit-accelerator-report` on a module, run the typechecker first so each site can include its inferred type. Module sources only |
| `--no-accelerator-lowering` | Keep IntArray/FloatArray map/filter/foldl on the host closure path; do not rewrite recognized kernels to the `Std\GPU` ABI |
| `--strict-accelerator` | Error (E0700) on IntArray/FloatArray map/filter/foldl lambdas outside the fixed `Std\GPU` kernel library, instead of silently falling back to the host path |

`--emit-accelerator-report` cannot be combined with `--emit-ir` or `--emit-obj`, and `--emit-accelerator-report-with-types` requires `--emit-accelerator-report`.

### Linking

| Option | Description |
|--------|-------------|
| `--linker-mode <mode>` | Linker selection: `auto`, `bundled`, `system`, or `inprocess`. Can also be set via the `YONAC_LINKER_MODE` environment variable; the flag takes precedence |

In `inprocess` mode `yonac` links with an in-process LLD; if that is unavailable or fails, it falls back to the external linker path with a warning (or a hard error when `YONAC_REQUIRE_INPROCESS_LLD` is set).

### Diagnostics and information

| Option | Description |
|--------|-------------|
| `--explain <code>` | Print the detailed explanation for an error code (e.g. `E0100`) and exit |
| `--version` | Print the compiler version and exit |

## yona (REPL)

`yona` is an interactive compile-and-run loop: each line you type is compiled to a temporary native executable, run, and its output printed.

```bash
$ yona
Yona REPL (type expressions, Ctrl-D to exit)
yona> 1 + 2
3
```

- Exit with `Ctrl-D`, `:q`, or `:quit`.
- The REPL honors `YONAC_CC`, `YONAC_LINKER_MODE`, and `YONAC_REQUIRE_INPROCESS_LLD`, and discovers the runtime from the same distribution roots as `yonac` (including `YONA_HOME`).

## Environment variables

| Variable | Effect |
|----------|--------|
| `YONA_HOME` | Additional Yona distribution root; searched for `lib/` (modules, `Prelude`) and packaged runtime objects |
| `YONA_PATH` | Extra module search directories (`Prelude.yonai` and `import … from …`). Separated by `:` on Unix and `;` on Windows. Needed when compiling from a directory that has no cwd-relative `lib/` |
| `YONAC_CC` | C compiler driver used to compile the runtime from source and to drive external linking (default: `cc` on Unix, `clang` on Windows) |
| `YONAC_LINKER_MODE` | Default for `--linker-mode` (`auto`, `bundled`, `system`, `inprocess`) when the flag is not given |
| `YONAC_REQUIRE_INPROCESS_LLD` | When set to `1`/`true`/`yes`/`on`, make a failed or unavailable in-process LLD link a hard error instead of falling back to the external linker |
| `YONA_COMPILE_GPU_VULKAN` | When set to `1` together with `VULKAN_SDK`, compile the runtime from source with Vulkan GPU support enabled; leave unset for the default CPU-only runtime |

## Common workflows

Compile a file to an executable and run it:

```bash
yonac hello.yona -o hello
./hello
```

Evaluate an expression directly:

```bash
yonac -e "1 + 2" -o calc
./calc
```

Inspect the generated LLVM IR:

```bash
yonac --emit-ir -e "import foldl from Std\List in foldl (\acc x -> acc + x) 0 [1, 2, 3]"
```

Get a detailed explanation for an error code:

```bash
yonac --explain E0100
```

Compile a module (producing `Geometry.o` and `Geometry.yonai`), then a program that imports it:

```bash
yonac Geometry.yona
yonac -I . main.yona -o app
```

Build with warnings as errors and debug info:

```bash
yonac --Wall --Werror -g main.yona -o app
```

Audit GPU-acceleratable call sites in a module, with inferred types:

```bash
yonac --emit-accelerator-report --emit-accelerator-report-with-types Stats.yona -I lib
```
