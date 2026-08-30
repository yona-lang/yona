---
title: Compiler CLI
description:
  Complete reference for the yonac compiler and the yona REPL, including all
  flags and environment variables.
---

Yona ships `yonac` (ahead-of-time compiler), `yona` (compile-and-run runner and
shebang target), `yona-repl` (interactive REPL, started by `yona` when stdin is
a TTY), and `yls` (language server). Editor setup is documented in
[Editor and language server](/guides/editor/).

## yonac

```bash
yonac [input.yona] [options]
yonac - [options]          # source from stdin
```

`yonac` compiles Yona source to a native executable via LLVM. If the first
non-comment token of the input is `module`, the file is compiled as a **module**
to an object file plus a `.yonai` interface file; otherwise it is compiled as an
**expression program** and linked into an executable.

### Input

| Option  | Description                                                       |
| ------- | ----------------------------------------------------------------- |
| `input` | Positional argument: the input `.yona` file, or `-` to read stdin |

Exactly one input source is required — a file or `-`. `yonac` never runs the
result. For a one-liner, use `yona -e`.

### Output

| Option                | Description                                                                                              |
| --------------------- | -------------------------------------------------------------------------------------------------------- |
| `-o, --output <file>` | Output file name                                                                                         |
| `--emit-ir`           | Print LLVM IR to stdout instead of compiling                                                             |
| `--emit-obj`          | Emit an object file only; do not link                                                                    |
| `--emit-typed-core`   | Print a typed-core dump (resolved names, types, effects, linearity, spans) and exit without LLVM codegen |

Default output names when `-o` is omitted:

| Input kind                             | Default output                      |
| -------------------------------------- | ----------------------------------- |
| Expression program                     | `a.out` (`a.exe` on Windows)        |
| Module, or any input with `--emit-obj` | input stem + `.o` (`a.o` for stdin) |

Compiling a module additionally writes an interface file next to the object
file, with the same stem and the `.yonai` extension.

### Optimization and debugging

| Option        | Description                         |
| ------------- | ----------------------------------- |
| `-O <n>`      | Optimization level, 0–3 (default 2) |
| `-g, --debug` | Emit DWARF debug information        |

### Warnings

| Option                    | Description                                                                                                                                                                                                                                                                                                  |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `--Wall`                  | Enable common warnings (unused variables, incomplete/overlapping patterns, unhandled effects, linear leaks)                                                                                                                                                                                                  |
| `--Wextra`                | Enable all warnings (adds shadowing, missing signatures, unused imports)                                                                                                                                                                                                                                     |
| `--Werror`                | Treat warnings as errors                                                                                                                                                                                                                                                                                     |
| `-w`                      | Suppress all warnings                                                                                                                                                                                                                                                                                        |
| `--Wincomplete-patterns`  | Warn when a finite ADT `case` misses constructors (also enabled by `--Wall`)                                                                                                                                                                                                                                 |
| `--Woverlapping-patterns` | Warn when earlier unguarded arms cover every value a later case arm can match; aliases, alternatives, nested constructors, tuples, exact/head–tail sequences, scalar literals, and finite ADT/Bool alternatives are analyzed (also enabled by `--Wall`)                                                      |
| `--Wno-refinement`        | Skip refinement checking (E0500 nonempty / nonzero proofs)                                                                                                                                                                                                                                                   |
| `--Wno-linear`            | Skip linearity checking (E0600 / E0601 / E0602)                                                                                                                                                                                                                                                              |
| `--Wno-linear-leak`       | Disable E0602 resource-leak warnings (`-Wlinear-leak`, on by default)                                                                                                                                                                                                                                        |
| `--require-effect-free`   | Require a closed empty effect row, exhaustive registered finite-ADT and `Bool` `case`s, and a sound structural size-change proof for every local direct or mutual recursive SCC, including lexicographic multi-parameter descent. Unproved forms fail with E0203; this is not a general termination checker. |

The individual warning flags and which group enables them are listed on the
[error codes](/reference/error-codes/) page.

The size-change proof uses structural descendants bound by unguarded constructor
or non-empty sequence patterns; simple lexical aliases preserve those facts. It
conservatively rejects numeric decreases, guarded descent, opaque/helper and
higher-order recursion, incompatible-arity SCCs, cycles with mixed incompatible
decreases. The strict gate also does not prove arbitrary open-domain coverage.

### Modules

| Option                 | Description                                                                   |
| ---------------------- | ----------------------------------------------------------------------------- |
| `-I, --include <path>` | Add a module search path (for `.yonai` interface files); repeatable           |
| `--sysroot <path>`     | Yona distribution root, used to find `lib/` and the canonical runtime archive |

Imports (and `Prelude.yonai`) are resolved by searching, in order: paths given
with `-I`, directories in `YONA_PATH`, the input file's directory, the current
directory, then `lib/` and `share/yona/lib/` under each discovered distribution
root. Distribution roots come from `--sysroot`, the `YONA_HOME` environment
variable, and the directory containing the `yonac` executable. `YONA_PATH` is a
`:`-separated list on Unix and a `;`-separated list on Windows.

### Accelerators

The compiler transparently lowers recognized `Std\IntArray` / `Std\FloatArray`
`map`, `filter`, and `foldl` call sites to the [Std\Gpu](/stdlib/gpu/) kernel
ABI. These flags inspect or control that lowering:

| Option                                 | Description                                                                                                                                                                   |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--emit-accelerator-report`            | Print a JSON report of `Std\Gpu`-shaped call sites and exit without generating code. Expression programs are reported after typechecking; modules from an AST scan by default |
| `--emit-accelerator-report-with-types` | With `--emit-accelerator-report` on a module, run the typechecker first so each site can include its inferred type. Module sources only                                       |
| `--no-accelerator-lowering`            | Keep IntArray/FloatArray map/filter/foldl on the host closure path; do not rewrite recognized kernels to the `Std\Gpu` ABI                                                    |
| `--strict-accelerator`                 | Error (E0700) on IntArray/FloatArray map/filter/foldl lambdas outside the fixed `Std\Gpu` kernel library, instead of silently falling back to the host path                   |

`--emit-accelerator-report` cannot be combined with `--emit-ir` or `--emit-obj`,
and `--emit-accelerator-report-with-types` requires `--emit-accelerator-report`.
`--emit-typed-core` cannot be combined with `--emit-ir`, `--emit-obj`, or
`--emit-accelerator-report`.

### Linking

| Option                 | Description                                                                                                                                                |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--linker-mode <mode>` | Linker selection: `auto`, `bundled`, `system`, or `inprocess`. Can also be set via the `YONAC_LINKER_MODE` environment variable; the flag takes precedence |

In `inprocess` mode `yonac` links with an in-process LLD; if that is unavailable
or fails, it falls back to the external linker path with a warning (or a hard
error when `YONAC_REQUIRE_INPROCESS_LLD` is set).

### Diagnostics and information

| Option             | Description                                                              |
| ------------------ | ------------------------------------------------------------------------ |
| `--explain <code>` | Print the detailed explanation for an error code (e.g. `E0100`) and exit |
| `--version`        | Print the compiler version and exit                                      |

## yona (runner)

`yona` compiles source with the sibling `yonac` to a temporary executable, runs
it, then deletes the temp file. It is not an interpreter. The driver source is
`tools/yona/main.yona`, built by `yona_add_executable` in
`cmake/YonaTools.cmake`.

```bash
yona [script.yona [args…]]
yona - [args…]
yona -e 'expression' [args…]
yona --repl
```

| Invocation                  | Behavior                                                             |
| --------------------------- | -------------------------------------------------------------------- |
| `yona` on a TTY             | Start `yona-repl`                                                    |
| `yona` with piped stdin     | Compile stdin and run                                                |
| `yona file.yona args`       | Compile the file and run; `getArgs` sees the script path then `args` |
| `yona -`                    | Compile stdin and run; program argv[0] is `-`                        |
| `yona -e 'expr'`            | Compile the expression and run; program argv[0] is `-e`              |
| `yona --repl`               | Start the REPL even if stdin is a pipe                               |
| `yona --help` / `--version` | Usage or the same version string as `yonac --version`                |

```bash
$ yona -e '1 + 2'
3
```

Shebang (Unix). The lexer treats `#` as a line comment, so `#!` is legal:

```yona
#!/usr/bin/env yona
import println from Std\Io in
println "hello"
```

The file must be an **expression program**, not a `module`. Each run compiles;
for something you invoke often, `yonac -o tool tool.yona`. Windows has no
shebang; `yona script.yona args` is the same code path.

`YONA_PATH` and `YONA_HOME` are read by `yonac` when `yona` compiles.

## yona-repl

Interactive compile-and-run loop. Each line is compiled to a temporary native
executable, run, and its output printed.

```bash
$ yona
Yona REPL (type expressions, Ctrl-D to exit)
yona> 1 + 2
3
```

- Exit with `Ctrl-D`, `:q`, or `:quit`.
- Honors `YONAC_CC`, `YONAC_LINKER_MODE`, and `YONAC_REQUIRE_INPROCESS_LLD`, and
  discovers the runtime from the same distribution roots as `yonac`.

## Environment variables

| Variable                      | Effect                                                                                                                                                                                       |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `YONA_HOME`                   | Additional Yona distribution root; searched for `lib/` (modules, `Prelude`) and the canonical runtime archive                                                                                |
| `YONA_PATH`                   | Extra module search directories (`Prelude.yonai` and `import … from …`). Separated by `:` on Unix and `;` on Windows. Needed when compiling from a directory that has no cwd-relative `lib/` |
| `YONAC_CC`                    | Compiler driver used only for external final linking (default: `cc` on Unix, `clang` on Windows)                                                                                             |
| `YONAC_LINKER_MODE`           | Default for `--linker-mode` (`auto`, `bundled`, `system`, `inprocess`) when the flag is not given                                                                                            |
| `YONAC_REQUIRE_INPROCESS_LLD` | When set to `1`/`true`/`yes`/`on`, make a failed or unavailable in-process LLD link a hard error instead of falling back to the external linker                                              |

## Common workflows

Compile a file to an executable and run it:

```bash
yonac hello.yona -o hello
./hello
```

Evaluate an expression directly:

```bash
yona -e "1 + 2"
```

Compile a snippet from stdin to an executable without running it:

```bash
printf '%s\n' '1 + 2' | yonac - -o calc
./calc
```

Inspect the generated LLVM IR:

```bash
printf '%s\n' 'import foldl from Std\List in foldl (\acc x -> acc + x) 0 [1, 2, 3]' | yonac --emit-ir -
```

Get a detailed explanation for an error code:

```bash
yonac --explain E0100
```

Compile a module (producing `Geometry.o` and `Geometry.yonai`), then a program
that imports it:

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

## yls

```bash
yls [--stdio] [-I path]
```

`yls` is the Yona language server. Editors speak LSP 3.17 over stdin/stdout with
`Content-Length` framing. `-I path` adds a module search directory, matching
`yonac -I`. Discovery from the VS Code extension uses `PATH`,
`YONA_HOME/bin/yls`, then the directory that contains `yonac`.

`yls`, VS Code, and Zed do not currently expose a strict `--require-effect-free`
configuration. Run `yonac --require-effect-free` explicitly for strict E0203
validation. Ordinary parse, type, refinement, and linearity diagnostics remain
shared between the compiler and language server.
