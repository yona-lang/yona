# Linear Resources Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace public raw resource handles with typed `Linear` payload APIs for files, processes, sockets, and channel endpoints.

**Architecture:** Resource payload ADTs remain the runtime boundary; `Linear` is the ownership capability presented by creation APIs. Non-terminal operations transfer a typed payload forward with their result, while terminal operations consume it. Interfaces encode the outer `LINEAR` marker and the exact inner ADT so the linearity checker can enforce use and leak rules uniformly.

**Tech Stack:** Yona stdlib/interface format, C runtime and platform backends, C++ type/linearity checker, doctest, CTest, Astro documentation.

## Global Constraints

- This is intentionally breaking: remove public raw `Int`/pointer resource paths.
- Preserve native fd/handle values only inside runtime implementation helpers.
- Make leaks of resource constructors hard E0602 errors; use-after-consume remains E0600.
- Update generated `.yonai`, internal docs, published site pages, changelog, and todo in the same change.
- Run the complete CTest preset and `git diff --check` before committing.

---

### Task 1: Encode precise linear resource types in interfaces

**Files:**
- Modify: `lib/Prelude.yona`, `lib/Prelude.yonai`
- Modify: `lib/Std/File.yonai`, `lib/Std/Process.yonai`, `lib/Std/Net.yonai`, `lib/Std/Channel.yona`, `lib/Std/Channel.yonai`
- Modify: `src/typechecker/LinearityChecker.cpp`, `test/type_checker_test.cpp`

**Interfaces:**
- Produces `Linear FileHandle`, `Linear Process`, `Linear Socket`, `Linear (Sender a)`, and `Linear (Receiver a)` constructor facts.
- Produces resource-only E0602 errors rather than optional warnings.

- [ ] **Step 1: Write failing type-checker tests**

Add fixtures that assert the imported constructors retain their payload:

```cpp
CHECK(imported_linear_leaks(
  "import openFile from Std\\File in let h = openFile \"f\" Read in h"));
CHECK(imported_linear_leaks(
  "import spawn from Std\\Process in let p = spawn \"true\" in p"));
```

Add module-body coverage and tests that `with` discharges each resource.

- [ ] **Step 2: Run focused linearity tests**

Run: `./out/build/x64-debug-linux/tests -tc='LinearityChecker:*resource*'`

Expected: the new payload-precision and hard-leak assertions fail before interface changes.

- [ ] **Step 3: Implement precise interface metadata and hard resource leaks**

Write interfaces with payload-bearing forms, for example:

```text
FN yona_Std_File__openFile 2 STRING ADT -> LINEAR ADT FileHandle
FN yona_Std_Process__spawn 1 STRING -> LINEAR ADT Process
```

Teach the linearity checker to classify only resource constructors as mandatory
to consume, preserving ordinary `Linear a` diagnostics elsewhere.

- [ ] **Step 4: Verify focused type checking**

Run: `cmake --build --preset build-debug-linux --target tests && ./out/build/x64-debug-linux/tests -tc='LinearityChecker:*resource*'`

Expected: resource leak and use-after-consume tests report E0602/E0600; `with`
is accepted.

- [ ] **Step 5: Commit**

```bash
git add lib/Prelude.yona lib/Prelude.yonai lib/Std/*.yonai lib/Std/Channel.yona src/typechecker/LinearityChecker.cpp test/type_checker_test.cpp
git commit -m "feat: encode typed linear resource interfaces"
```

### Task 2: Make File and Process APIs transfer ownership

**Files:**
- Modify: `src/compiled_runtime.c`, `include/yona/runtime/platform.h`
- Modify: `src/runtime/platform/os_linux.c`, `src/runtime/platform/os_macos.c`, `src/runtime/platform/os_windows.c`
- Modify: `lib/Std/File.yonai`, `lib/Std/Process.yonai`, `test/codegen/*file*`, `test/*process*`

**Interfaces:**
- Consumes `Linear FileHandle` / `Linear Process` and produces payload/result pairs for continuing operations.
- Terminal close/wait operations consume their payload.

- [ ] **Step 1: Add failing typed ABI fixtures**

Add Yona fixtures that open a file, perform two operations by threading the
handle, and close it through `with`; add equivalent spawn/read-or-wait coverage.

- [ ] **Step 2: Run the focused fixtures**

Run: `./out/build/x64-debug-linux/tests -tc='*file*|*process*'`

Expected: fixtures fail because the current ABI accepts a raw handle and does
not return it after a non-terminal operation.

- [ ] **Step 3: Implement payload extraction and transfer-returning runtime calls**

Keep `fh_fd` and platform process handles private. Replace public raw-handle
entry points with typed payload entry points and update the matching `.yonai`
signatures to return `(Payload, Result)` where the resource remains usable.

- [ ] **Step 4: Verify behavior and ownership**

Run: `cmake --build --preset build-debug-linux && ./out/build/x64-debug-linux/tests -tc='*file*|*process*'`

Expected: filesystem/process behavior stays correct and no fixture uses a raw
descriptor.

- [ ] **Step 5: Commit**

```bash
git add src/compiled_runtime.c include/yona/runtime/platform.h src/runtime/platform/os_*.c lib/Std/File.yonai lib/Std/Process.yonai test
git commit -m "feat: transfer file and process ownership"
```

### Task 3: Make network and channel APIs transfer ownership

**Files:**
- Modify: `src/runtime/platform/net_linux.c`, `src/runtime/platform/net_macos.c`, `src/runtime/platform/net_windows.c`
- Modify: `src/runtime/platform/channel_posix.c`, `src/runtime/platform/channel_win32.c`
- Modify: `lib/Std/Net.yonai`, `lib/Std/Channel.yona`, `lib/Std/Channel.yonai`
- Modify: `test/net_runtime_test.cpp`, `test/codegen/channel_*.yona`

**Interfaces:**
- Socket send/receive and endpoint send/receive preserve ownership by returning the resource.
- Close consumes `Socket`, `Sender a`, or `Receiver a`; raw channel helpers are private.

- [ ] **Step 1: Add failing typed socket and channel fixtures**

Cover a TCP/UDP socket threaded through send/receive/close and a channel pair
threaded through send/receive/close without exposing `Channel` or `Int`.

- [ ] **Step 2: Run focused network and channel tests**

Run: `./out/build/x64-debug-linux/tests -tc='*net*|*channel*'`

Expected: the transfer-returning fixtures fail against the old raw ABI.

- [ ] **Step 3: Implement typed adapters and remove public raw helpers**

Retain platform fd/channel functions as internal helpers. Make exported Yona
ABI functions extract the payload, perform work, and return the payload with
the result when ownership continues. Remove `raw_*` from `Std\\Channel` exports.

- [ ] **Step 4: Verify focused behavior**

Run: `cmake --build --preset build-debug-linux && ./out/build/x64-debug-linux/tests -tc='*net*|*channel*'`

Expected: platform networking and channel scheduling tests pass through typed
resource APIs.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/platform/net_*.c src/runtime/platform/channel_*.c lib/Std/Net.yonai lib/Std/Channel.yona lib/Std/Channel.yonai test
git commit -m "feat: transfer network and channel ownership"
```

### Task 4: Remove raw stdio surface and complete documentation

**Files:**
- Modify: `lib/Std/IO.yona`, `lib/Std/IO.yonai`, `tools/yona/main.yona`, `tools/yls/main.yona`
- Modify: `docs/linear-types.md`, `docs/api/File.md`, `docs/api/Process.md`, `docs/api/Net.md`, `docs/api/Channel.md`, `docs/todo-list.md`, `CHANGELOG.md`
- Modify: `site/src/content/docs/guides/memory.md`, `site/src/content/docs/guides/concurrency.md`, `site/src/content/docs/guides/traits.md`, `site/src/content/docs/guides/type-system.md`

**Interfaces:**
- Public stdio helpers remain ergonomic but no public `stdinFd`/`stdoutFd`/`stderrFd` resource path exists.

- [ ] **Step 1: Add failing compiler fixtures for removed raw APIs**

Add fixtures that use typed stdin/stdout handles and assert old names such as
`stdinFd` are rejected after migration.

- [ ] **Step 2: Remove raw stdio exports and migrate internal callers**

Replace direct descriptor exposure with private runtime-backed standard streams
or typed resource constructors, then update `yona` and `yls` callers.

- [ ] **Step 3: Regenerate API documentation**

Run: `python3 scripts/gendocs.py`

Expected: generated API pages match the new signatures.

- [ ] **Step 4: Update user documentation and roadmap**

Document unwrapping, transfer-returning operations, `with`, E0600/E0602, and
the removal of raw resource paths. Remove the completed todo item only after
all its acceptance criteria are met.

- [ ] **Step 5: Run full verification**

Run: `ctest --preset unit-tests-linux --output-on-failure && git diff --check`

Expected: all tests pass and the diff has no whitespace errors.

- [ ] **Step 6: Commit**

```bash
git add lib/Std/IO.yona lib/Std/IO.yonai tools docs site CHANGELOG.md test
git commit -m "docs: complete linear resource migration"
```
