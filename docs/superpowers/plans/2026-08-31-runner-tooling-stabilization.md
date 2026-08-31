# Runner and Tooling Stabilization Implementation Plan

> Execute with subagent-driven development. Every production task starts from
> the focused failing regression, receives a separate review, and is committed
> independently. Todo/changelog/plan closure remains one combined final update.

**Goal:** Make every `yona` execution mode compile and run with its documented
argument vector, clean temporary files on all exits, and make the missing
clang-format regression exercise the real formatter resolver.

**Architecture:** `Std\\Process.run` keeps its existing contract that the
argument sequence excludes `argv[0]`. A new narrow cross-platform
`runWithArgv0` primitive separates executable lookup from the child-visible
argument zero. The Yona runner owns each temporary artifact in the closest
non-exiting scope and removes it before propagating status. Tooling tests retain
the shell/Python prerequisites they need while deliberately excluding only
clang-format.

---

## Task 1: Add an explicit-child-argv0 process primitive

**Files:**

- Modify: `include/yona/Runtime/Platform/Api.h`
- Modify: `src/Runtime/Platform/OsLinux.c`
- Modify: `src/Runtime/Platform/OsMacOs.c`
- Modify: `src/Runtime/Platform/OsWindows.c`
- Modify: `lib/Std/Process.yonai`
- Modify: `test/Runtime/PlatformApiLinkTest.cpp` or the nearest process ABI test
- Modify: `test/Fixtures/Codegen/stdlib_process_run.yona`
- Modify: matching expected fixture output if the new assertions require it

- [x] **Step 1: Add failing ABI/behavior coverage**

Add a focused process regression for
`runWithArgv0 : String -> String -> Seq -> Int` that launches a small child and
proves its first visible argument is the explicit value while later arguments,
including spaces/metacharacters, are preserved. Retain a control proving
ordinary `run` still supplies the executable as argv0 and treats its sequence
as arguments after it.

- [x] **Step 2: Implement POSIX argument-vector separation**

Generalize the Linux/macOS internal argument-vector builder to accept a
separate argument-zero value. Route existing `run` through it with
`ArgumentZero = File`; expose `YonaStdProcessRunWithArgv0` with the explicit
value. Preserve current fork/exec/wait status and allocation semantics.

- [x] **Step 3: Implement Windows executable/argv0 separation**

Build a quoted command line whose first token is explicit argv0 while passing
the real normalized executable as `lpApplicationName`. If a bare PATH name
needs resolution, use `SearchPathA`, rebuild the mutable command line, and call
`CreateProcessA` with the resolved executable. Never fall back to
`CreateProcessA(NULL, customCommandLine, ...)`, which would try to execute the
script-visible argv0.

- [x] **Step 4: Verify all platform sources and commit**

Build the runtime/tests graph, run the focused process ABI/runtime tests, and
compile the non-host platform sources where existing cross-platform syntax
targets permit. Commit as `feat: run processes with explicit argv0`.

## Task 2: Repair Yona compilation, argument forwarding, and cleanup

**Files:**

- Modify: `tools/yona/main.yona`
- Modify: `test/Toolchain/YonaScriptTest.cpp`

- [x] **Step 1: Strengthen runner RED coverage**

Retain the already-red file, shebang, stdin, and `-e` cases. Strengthen argument
tests to assert complete vectors:

- file: `[script-path, foo, argument with spaces]`;
- stdin: `[-, foo]`;
- expression: `[-e, foo, argument with metacharacters]`.

Add failed stdin and failed `-e` compilation cases with a unique isolated
`TMPDIR`/`TMP`/`TEMP`; assert nonzero status and no remaining `yona-src*` or
`yona-run*` files.

- [x] **Step 2: Remove duplicate compiler argv0**

Compile with `run yonac [sourcePath, "-o", tmpExe]`; do not pass `yonac` again
inside the argument sequence. Change the REPL replacement to
`execArgs repl []` for the same reason.

- [x] **Step 3: Run children with explicit argv0**

Import and call `runWithArgv0 tmpExe argv0 userArgs` so generated programs see
the script path, `-`, or `-e` exactly as published. Preserve child exit status.

- [x] **Step 4: Make temporary ownership non-bypassable**

Make `compileToTemp` return the output path and status instead of calling
`exit`. It removes the temp executable on failure. stdin/`-e` callers always
remove their temp source before branching on compile status; `runTemp` removes
the executable after the child returns and only then exits. Missing-tool paths
must report status 127 without bypassing caller cleanup.

- [x] **Step 5: Verify all runner modes and commit**

Run all `yona *` toolchain cases, including module rejection, missing files,
unknown flags, version, file/shebang/stdin/`-e`, full argv vectors, and cleanup.
Commit as `fix: preserve runner arguments and temporary ownership`.

## Task 3: Make the missing-clang-format test hermetic

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `test/Toolchain/YonaScriptTest.cpp`
- Modify only if a narrow resolver test hook is required: `scripts/quality.py`

- [x] **Step 1: Retain the exact current failure**

Run `format script fails clearly when clang-format is unavailable` and confirm
`PATH=/nonexistent` makes `/bin/sh` fail at external `dirname`, so the test
never reaches Python or clang-format resolution.

- [x] **Step 2: Build an isolated prerequisite PATH**

Pass absolute discovered Python and `dirname` paths to the test through CMake.
Create a unique temporary bin containing only symlinks/copies named `python3`
and `dirname`, invoke `format.sh` (narrowed to clang-format if supported), and
exclude clang-format without removing the prerequisites needed to reach
`quality.py`.

- [x] **Step 3: Assert the canonical resolver diagnostic**

Assert nonzero status, the specific missing required `clang-format` message,
and absence of `Done`. If Python's scripts-directory fallback makes the test
host-dependent, add one narrowly named test-only resolver override rather than
duplicating formatter detection in `format.sh`.

- [x] **Step 4: Verify tooling tests and commit**

Run the focused format test, quality-script tests, and the full Toolchain
suite. Commit as `test: isolate missing clang-format detection`.

## Task 4: Document and close runner/tooling bugs

**Files:**

- Modify: `docs/api/Process.md`
- Modify: `docs/todo-list.md`
- Modify: `docs/superpowers/specs/2026-08-31-open-bug-stabilization-design.md`
- Modify: this plan
- Modify: `CHANGELOG.md`
- Modify: `.superpowers/sdd/progress.md`
- Regenerate as applicable: `docs/api/`

- [x] **Step 1: Document `runWithArgv0` without weakening existing contracts**

Add the explicit argv0 primitive to Process API docs; keep `run` documented as
excluding argv0. Existing CLI/site text already states the desired script,
stdin, and `-e` vectors and should remain unchanged. Run
`python3 scripts/gendocs.py` and inspect generated changes.

- [x] **Step 2: Run completion gates**

Run the process/runtime, runner/toolchain, and formatting tests, then the full
Linux build/CTest gate and `git diff --check`.

- [x] **Step 3: Record one combined update**

Close the runner status-109, temporary-source leak, and missing-clang-format
bugs only with passing evidence; add the Unreleased changelog entries and
update plan/design status in one combined documentation commit.

- [x] **Step 4: Commit**

Commit as `docs: record runner and tooling stabilization`.
