# Linux Build Stabilization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore a buildable and testable Linux debug baseline after the
source-modularization identifier migration.

**Architecture:** Preserve the canonical public runtime names introduced by
the modularization commit and migrate every Linux consumer to those names. Use
the component object targets as compile regressions, then run the existing
io_uring, file, networking, and native-I/O tests as behavioral regressions.

**Tech Stack:** C11, CMake, Ninja, Clang 22, doctest, Linux io_uring/POSIX I/O.

## Global Constraints

- Do not restore obsolete lowercase `YonaIoContext` fields or weaken public
  constness.
- Do not change runtime behavior while repairing identifier consistency.
- Preserve all unrelated working-tree changes.
- Every production change must follow a witnessed red-green cycle.
- Newly discovered bugs must be recorded immediately in `docs/todo-list.md`
  with a one-line reproduction.
- Update `docs/todo-list.md`, `CHANGELOG.md`, and this plan in the same change.

---

### Task 1: Finalize the AST compile-regression baseline

**Files:**

- Modify: `include/yona/Syntax/Ast.h`
- Modify: `src/Semantics/PatternAnalysis.cpp`
- Modify: `CMakeLists.txt`
- Create: `test/Syntax/AstTest.cpp`
- Modify: `docs/todo-list.md`
- Modify: `CHANGELOG.md`

**Interfaces:**

- Consumes: C++23 `std::nullptr_t` and `yona::ast::LiteralExpr<T>`.
- Produces: a self-contained public AST header and a compile-time assertion
  that `UnitExpr` derives from `LiteralExpr<std::nullptr_t>`.

- [x] **Step 1: Confirm the regression was red before the fix**

Run:

```bash
git show HEAD^:include/yona/Syntax/Ast.h >/dev/null 2>&1 || true
cmake --build --preset build-debug-linux --target yona_syntax
```

Expected historical failure: Clang rejects unqualified `nullptr_t` in
`yona/Syntax/Ast.h`. The current working tree may already be green because this
red state was captured before the plan was written.

- [x] **Step 2: Verify the minimal namespace contract**

The implementation must contain this explicit import in `yona::ast`:

```cpp
using std::nullptr_t;
```

Semantic uses outside `yona::ast` must remain explicitly qualified:

```cpp
LiteralExpr<std::nullptr_t>
```

The regression must assert:

```cpp
static_assert(std::is_base_of_v<LiteralExpr<std::nullptr_t>, UnitExpr>);
```

Run:

```bash
clang++ -std=gnu++23 -Iinclude \
  -Iout/build/x64-debug-linux/_deps/doctest-src \
  -fsyntax-only test/Syntax/AstTest.cpp
cmake --build --preset build-debug-linux --target yona_syntax
```

Expected: both commands exit zero.

- [x] **Step 3: Commit the isolated AST regression fix**

```bash
git add include/yona/Syntax/Ast.h src/Semantics/PatternAnalysis.cpp \
  test/Syntax/AstTest.cpp CMakeLists.txt docs/todo-list.md CHANGELOG.md
git commit -m "fix: restore portable AST null literal type"
```

### Task 2: Preserve constness in grouped io_uring cancellation

**Files:**

- Modify: `src/Runtime/Platform/IoUringLinux.c`
- Test: compiler contract between
  `include/yona/Runtime/Platform/IoUring.h` and the implementation

**Interfaces:**

- Consumes:
  `void YonaRuntimeIoUringCancelGroup(const uint64_t *IoIds, int Count)`.
- Produces: a definition with the identical signature; cancellation reads each
  ID and never mutates the caller-owned array.

- [ ] **Step 1: Reproduce the signature mismatch**

Run:

```bash
cmake --build --preset build-debug-linux \
  --target yona_runtime_platform_io
```

Expected: compilation fails because the header declares `const uint64_t *`
while `IoUringLinux.c` defines `uint64_t *`.

- [ ] **Step 2: Apply the minimal definition repair**

Change only the definition signature:

```c
void YonaRuntimeIoUringCancelGroup(const uint64_t *IoIds, int Count) {
  for (int Index = 0; Index < Count; Index++)
    YonaRuntimeIoUringCancel(IoIds[Index]);
}
```

- [ ] **Step 3: Verify this diagnostic disappears**

Run:

```bash
cmake --build --preset build-debug-linux \
  --target yona_runtime_platform_io 2>&1 | tee /tmp/yona-platform-build.log
! rg "conflicting types for 'YonaRuntimeIoUringCancelGroup'" \
  /tmp/yona-platform-build.log
```

Expected: no grouped-cancellation signature diagnostic. Other platform-I/O
compile errors remain red until Task 3.

- [ ] **Step 4: Commit the isolated contract repair**

```bash
git add src/Runtime/Platform/IoUringLinux.c
git commit -m "fix: preserve io_uring cancel group constness"
```

### Task 3: Complete the Linux platform-I/O identifier migration

**Files:**

- Modify: `src/Runtime/Platform/FileLinux.c`
- Modify: `src/Runtime/Platform/NetLinux.c`
- Test: `test/Runtime/IoReadExactTest.cpp`
- Test: `test/Runtime/NetRuntimeTest.cpp`

**Interfaces:**

- Consumes: canonical `YonaIoContext` fields `Kind`, `FileDescriptor`,
  `Buffer`, `BufferSize`, and `CloseFileDescriptor` from
  `include/yona/Runtime/Platform/IoUring.h`.
- Produces: Linux file/network implementations that use those fields and the
  already-declared UpperCamelCase locals consistently.

- [ ] **Step 1: Capture the component compile regression**

Run:

```bash
cmake --build --preset build-debug-linux \
  --target yona_runtime_platform_io
```

Expected: `FileLinux.c` and `NetLinux.c` fail on removed context fields and
undeclared lowercase variants such as `sqe`, `hints`, `res`, and `addr`.

- [ ] **Step 2: Migrate every context-field consumer**

Apply these exact mappings throughout both files:

```text
Ctx->type  -> Ctx->Kind
Ctx->fd    -> Ctx->FileDescriptor
Ctx->buf   -> Ctx->Buffer
```

Retain the existing canonical fields unchanged:

```c
Ctx->BufferSize
Ctx->CloseFileDescriptor
```

Do not rename the public struct back to its pre-refactor spelling.

- [ ] **Step 3: Make local uses match their declarations**

In `FileLinux.c`, use these declaration spellings consistently:

```text
Sqe, Fd, Buf, Size, Count, Offset, Data, Len, St, Id, N, Total, Ch
```

In `NetLinux.c`, use these declaration spellings consistently:

```text
Hints, Res, Ai, Addr, Sqe, Fd, Buf, AddressLength, MaximumBytes,
NoOperationId, N, Ip
```

Representative corrected submission code is:

```c
struct io_uring_sqe Sqe;
memset(&Sqe, 0, sizeof(Sqe));
Sqe.opcode = IORING_OP_READ;
Sqe.fd = Fd;
uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
```

The blocking byte-read fallback must use the function parameters and allocated
buffer exactly:

```c
ssize_t N =
    pread(Fd, (uint8_t *)(Buf + 1), (size_t)Count, (off_t)Offset);
Buf[0] = N > 0 ? N : 0;
```

- [ ] **Step 4: Verify the platform component compiles**

Run:

```bash
cmake --build --preset build-debug-linux \
  --target yona_runtime_platform_io
```

Expected: the target exits zero with no undeclared-identifier or missing-field
diagnostics.

- [ ] **Step 5: Commit the platform-I/O migration**

```bash
git add src/Runtime/Platform/FileLinux.c src/Runtime/Platform/NetLinux.c
git commit -m "fix: complete Linux platform I/O identifier migration"
```

### Task 4: Complete the native stdlib identifier migration

**Files:**

- Modify: `src/Runtime/Stdlib/Native.c`
- Test: `test/Runtime/IoReadExactTest.cpp`

**Interfaces:**

- Consumes: POSIX `read`/`write` and Windows `_read`/`_write` branches that
  converge on the same post-processor variables.
- Produces: platform branches that define the same `N`/`K` identifiers and use
  the enclosing `Buf`, `Len`, `Cap`, `Fd`, `Got`, `Want`, `S`, and `Off`
  variables.

- [ ] **Step 1: Capture the native stdlib compile regression**

Run:

```bash
cmake --build --preset build-debug-linux --target yona_runtime_stdlib
```

Expected: the POSIX branches fail on lowercase names including `buf`, `len`,
`cap`, `fd`, `got`, `want`, `s`, `off`, `n`, and `k`.

- [ ] **Step 2: Align the stdin-read branch**

The platform conditional in `YonaStdIoReadStdinImpl` must be:

```c
#if defined(_WIN32)
    int N = (int)read(0, Buf + Len, (unsigned)(Cap - Len));
#else
    ssize_t N = read(0, Buf + Len, Cap - Len);
#endif
```

- [ ] **Step 3: Align the exact-read branch**

The platform conditional in `YonaStdIoReadExactBytes` must be:

```c
#if defined(_WIN32)
    int K = (int)_read(Fd, Buf + Got, (unsigned)(Want - Got));
#else
    ssize_t K = read(Fd, Buf + Got, Want - Got);
#endif
```

- [ ] **Step 4: Align the synchronous-write branch**

The platform conditional in `YonaStdIoWriteBytes` must be:

```c
#if defined(_WIN32)
    int K = (int)_write(Fd, S + Off, (unsigned)(N - Off));
#else
    ssize_t K = write(Fd, S + Off, N - Off);
#endif
```

- [ ] **Step 5: Verify the stdlib component compiles**

Run:

```bash
cmake --build --preset build-debug-linux --target yona_runtime_stdlib
```

Expected: the target exits zero with no undeclared-identifier diagnostics.

- [ ] **Step 6: Commit the native stdlib migration**

```bash
git add src/Runtime/Stdlib/Native.c
git commit -m "fix: complete native stdlib identifier migration"
```

### Task 5: Verify runtime behavior and close the build-blocker entries

**Files:**

- Modify: `docs/todo-list.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-31-linux-build-stabilization.md`
- Test: `test/Runtime/IoReadExactTest.cpp`
- Test: `test/Runtime/NetRuntimeTest.cpp`
- Test: `test/Runtime/RuntimeGuardsTest.cpp`

**Interfaces:**

- Consumes: buildable runtime object components and the doctest `tests`
  executable.
- Produces: a verified Linux debug baseline for the seven frontend/codegen bug
  plans that follow this plan.

- [ ] **Step 1: Build the full debug target graph**

Run:

```bash
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux
```

Expected: configuration and compilation exit zero. If a new compiler error is
exposed, record it in `docs/todo-list.md` before changing its source.

- [ ] **Step 2: Run focused runtime regressions**

Run:

```bash
./out/build/x64-debug-linux/tests -tc="IoReadExact"
./out/build/x64-debug-linux/tests -tc="Runtime Net Submit/Await"
./out/build/x64-debug-linux/tests -tc="RuntimeGuards"
```

Expected: every selected doctest case passes with zero failures.

- [ ] **Step 3: Run the complete Linux test preset**

Run:

```bash
ctest --preset unit-tests-linux
```

Expected: CTest completes with zero failed tests, except for failures already
represented by the seven open frontend/codegen entries. Capture the exact
focused filters for those failures before the next subproject plan.

- [ ] **Step 4: Update project records**

Mark these three todo entries complete with their build and focused-test
evidence:

```text
The Linux io_uring implementation disagrees with its public cancel declaration.
The modularized Linux platform I/O sources still use the pre-refactor YonaIoContext fields and inconsistent local identifier casing.
The modularized native stdlib sources contain inconsistent local identifier casing.
```

Add this `CHANGELOG.md` entry under `Unreleased` / `Fixed`:

```markdown
- Linux runtime components again build after the source modularization: the
  io_uring cancellation signature preserves its const contract, and platform
  I/O and native stdlib consumers use the canonical context fields and local
  identifier spellings.
```

Check off every completed step in this plan.

- [ ] **Step 5: Verify formatting and patch integrity**

Run:

```bash
clang-format --dry-run --Werror \
  src/Runtime/Platform/IoUringLinux.c \
  src/Runtime/Platform/FileLinux.c \
  src/Runtime/Platform/NetLinux.c \
  src/Runtime/Stdlib/Native.c \
  include/yona/Syntax/Ast.h \
  src/Semantics/PatternAnalysis.cpp \
  test/Syntax/AstTest.cpp
git diff --check
```

Expected: both commands exit zero.

- [ ] **Step 6: Commit verification records**

```bash
git add docs/todo-list.md CHANGELOG.md \
  docs/superpowers/plans/2026-08-31-linux-build-stabilization.md
git commit -m "docs: record Linux build stabilization"
```

### Task 6: Restore C linkage for platform I/O headers

**Files:**

- Modify: `include/yona/Runtime/Platform/IoUring.h`
- Modify: `include/yona/Runtime/Platform/Kqueue.h`
- Test: `test/Runtime/IoReadExactTest.cpp`

**Interfaces:**

- Consumes: C implementations of the Linux io_uring and macOS kqueue runtime
  APIs.
- Produces: public headers whose function declarations retain C linkage when
  included by C++ tests or consumers.

- [ ] **Step 1: Reproduce the C/C++ linkage failure**

Run:

```bash
cmake --build --preset build-debug-linux --target tests
```

Expected: linking fails on C++-mangled
`YonaRuntimeIoContextPut(unsigned long, YonaIoContext *)` and
`YonaRuntimeIoContextTake(unsigned long)` while the runtime archive exports C
symbols.

- [ ] **Step 2: Add the canonical linkage guards**

After the system includes in both platform headers, add:

```c
#ifdef __cplusplus
extern "C" {
#endif
```

Immediately before each header's final include-guard `#endif`, add:

```c
#ifdef __cplusplus
}
#endif
```

Keep the enum, struct, constant, and every function declaration inside the
linkage block. Do not add ad hoc `extern "C"` declarations to the test.

- [ ] **Step 3: Verify both public headers compile as C++**

Run:

```bash
clang++ -std=gnu++23 -Iinclude -x c++ -fsyntax-only - <<'EOF'
#include "yona/Runtime/Platform/IoUring.h"
#include "yona/Runtime/Platform/Kqueue.h"
EOF
```

Expected: the syntax-only compile exits zero.

- [ ] **Step 4: Verify the tests executable links**

Run:

```bash
cmake --build --preset build-debug-linux --target tests
```

Expected: the `tests` executable links without unresolved platform I/O
registry symbols. If another new build bug appears first, record it immediately
before changing its source.

- [ ] **Step 5: Commit the linkage repair**

```bash
git add include/yona/Runtime/Platform/IoUring.h \
  include/yona/Runtime/Platform/Kqueue.h
git commit -m "fix: restore C linkage for platform I/O headers"
```

## Follow-up Plan Sequence

After this plan produces a buildable test runner, create and execute focused
plans in this order:

1. interface/generic-source dependency preservation;
2. case-lowering termination and refinement ABI correctness;
3. channel, file-handle, and effect-context typing;
4. lifted dictionary-trait ownership.

Each follow-up starts from a freshly witnessed failing doctest or fixture and
ends with the full Linux unit-test preset. The Windows-only `Std\Convert` item
remains outside scope unless it is reproduced on Linux.
