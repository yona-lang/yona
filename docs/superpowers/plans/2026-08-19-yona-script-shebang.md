# Yona Shebang Scripts and `getArgs` Implementation Record

**Status:** Complete.

## Goal

Support packaged scripts that start with `#!/usr/bin/env yona`, and expose the
program argument vector through `Std\Process.getArgs` for scripts and compiled
executables.

## Canonical architecture

The packaged `yona` runner is implemented in `tools/yona/main.yona`. It finds
the sibling `yonac`, compiles an expression program to a unique temporary
executable, runs it with the script path as `argv[0]`, forwards the remaining
arguments exactly, waits for the result, and removes the temporary artifact.
Module sources are rejected in script mode. A leading `#!` line is already a
normal `#` comment in the language.

All compiler, runner, runtime, and test process launches use an executable plus
an argument vector. No path or user argument is interpreted by an implicit
shell.

Generated entry points receive `argc` and `argv` and initialize the Process
runtime once. `YonaStdProcessGetArgs` returns the initialized vector as a Yona
sequence of managed strings. The first element is the compiled executable path
for a directly launched binary, the script path for `yona file.yona`, and `-e`
for `yona -e`.

## Completed work

- [x] Emit `main(argc, argv)`, initialize process arguments, and expose
  `YonaStdProcessGetArgs` through `lib/Std/Process.yonai`.
- [x] Cover the shebang-as-comment rule and compiled-program `getArgs` behavior.
- [x] Implement `yona file.yona [args...]` with module rejection, sibling
  compiler discovery, exact argument forwarding, inherited standard streams,
  and cleanup.
- [x] Cover normal scripts, `getArgs`, module rejection, missing files, `-e`,
  stdin, and paths/arguments containing shell metacharacters in
  `test/Toolchain/YonaScriptTest.cpp`.
- [x] Document script mode and shebangs in the CLI, quick start, installation,
  syntax, specification, Process API, and changelog.

Frequently executed tools should still be compiled once with `yonac -o`; script
mode intentionally compiles each invocation.
