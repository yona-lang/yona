# Standard-Library Conformance Suite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Yona-native, manifest-enforced conformance suite that gives every public `Std` module deterministic behavioral and edge-case coverage.

**Architecture:** `Std\Test` owns test cases, results, reporting, and assertions in pure Yona. Test scripts under `test/stdlib/` import that API and print a stable report. The existing doctest codegen harness recursively discovers scripts and validates their exact all-pass reports; CTest labels separate default, network, and Vulkan-capable execution.

**Tech Stack:** Yona standard library and compiler, C++23/doctest, CMake/CTest, Markdown/Astro documentation.

**Status:** Completed 2026-08-26. Recursive discovery and the manifest cover
all 40 public modules. The pure/runtime suite, labeled network/GPU slices,
focused ownership/platform regressions, generated API, and public test guide
are synchronized and verified together.

## Global Constraints

- Test semantics, test composition, and failures are implemented in Yona; C++ only compiles, executes, discovers, and validates scripts.
- Retain compiler/codegen fixtures in `test/Fixtures/Codegen/`; do not rewrite them as library tests.
- `test/stdlib/manifest.md` lists every row in `docs/api/README.md`; a missing script or expected report is a test failure.
- Default CTest runs all pure and portable runtime suites. Network and Vulkan device suites are required in capable CI jobs and locally skip only after an explicit capability probe.
- All filesystem/process tests use `test/_scratch` paths rewritten by `test/Toolchain/YonaLinkUtil.h` and leave no files behind.
- Use test-first changes, update `CHANGELOG.md`, `docs/todo-list.md`, generated API docs, and `site/src/content/docs/stdlib/test.md` in the same change.
- Run `ctest --preset unit-tests-linux --output-on-failure` and `git diff --check` before every commit.

---

### Task 1: Establish the pure-Yona test contract

**Files:**
- Modify: `lib/Std/Test.yona`, `lib/Std/Test.yonai`
- Modify: `test/Codegen/CodegenTest.cpp`
- Create: `test/stdlib/framework/Test_test.yona`, `test/stdlib/framework/Test_test.expected`
- Modify: `docs/api/Test.md`, `docs/api/README.md`, `site/src/content/docs/stdlib/test.md`, `CHANGELOG.md`

**Interfaces:**
- Produces `TestResult = Pass String | Fail String`, `TestCase`, and `TestReport`.
- Produces `pass`, `fail`, `check`, `equalBy`, `testCase`, `run`, and `render` from `Std\Test`.

- [x] **Step 1: Add the failing Yona framework fixture**

Create `test/stdlib/framework/Test_test.yona`:

```yona
import testCase, check, equalBy, run, render from Std\Test in
let intEqual a b = a == b,
    cases = [
        testCase "truth" (\_ -> check "true is accepted" true),
        testCase "comparator" (\_ -> equalBy "two equals two" intEqual 2 2),
        testCase "all cases run" (\_ -> check "later case ran" true)
    ]
in println (render (run cases))
```

Create its expected output:

```text
PASS truth
PASS comparator
PASS all cases run
SUMMARY 3 passed, 0 failed
```

- [x] **Step 2: Verify red**

Run: `./out/build/x64-debug-linux/tests -tc='Fixture-based codegen tests' -sc=Test_test`

Expected: the fixture is not discovered yet, and direct compilation fails because `Std\Test.case`/`run`/`render` do not exist.

- [x] **Step 3: Implement the exact `Std\Test` ADTs and combinators**

Replace tuple/symbol results with:

```yona
type TestResult = Pass String | Fail String
type TestCase = TestCase String (() -> TestResult)
type TestReport = TestReport Int Int [String]

pass name = Pass name
fail message = Fail message
check message value = if value then Pass message else Fail message
equalBy message equals expected actual =
    if equals expected actual then Pass message else Fail message
testCase name body = TestCase name body
```

Implement `run` with `Std\List.foldl`: call every thunk exactly once, retain
failures in declaration order, and return counts plus rendered failure details.
Implement `render` with stable `PASS`, `FAIL`, and `SUMMARY` lines. Expose only
the canonical `check`/`equalBy` assertion API.

- [x] **Step 4: Make generated interface metadata match the new API**

Compile `lib/Std/Test.yona` with `yonac`, replace `lib/Std/Test.yonai`, and run `python3 scripts/gendocs.py`.

- [x] **Step 5: Teach the fixture executor to discover `test/stdlib` recursively**

In `test/Codegen/CodegenTest.cpp`, extract the current fixture iteration into a helper accepting a root directory. Use `fs::recursive_directory_iterator`, accept only `.yona` files with an adjacent `.expected`, derive a unique artifact suffix from the root-relative path with separators changed to `_`, and register `test/Fixtures/Codegen` plus `test/stdlib` in separate doctest suites. Preserve existing scratch-path rewriting and stdin/environment special cases.

- [x] **Step 6: Verify green and document**

Run: `cmake --build --preset build-debug-linux --target tests && ./out/build/x64-debug-linux/tests -tc='Stdlib conformance fixtures'`

Expected: `Test_test` prints the four stable lines above and all existing codegen fixtures remain green.

Update `docs/api/Test.md`, the API index count/description, and the public `stdlib/test` page with the case/result/report contract and a complete example.

- [x] **Step 7: Commit**

```bash
git add lib/Std/Test.yona lib/Std/Test.yonai test/Codegen/CodegenTest.cpp test/stdlib/framework docs/api/Test.md docs/api/README.md site/src/content/docs/stdlib/test.md CHANGELOG.md
git commit -m "feat: add yona stdlib test contract"
```

### Task 2: Enforce the public-module coverage manifest

**Files:**
- Create: `test/stdlib/manifest.md`
- Modify: `test/Codegen/CodegenTest.cpp`
- Modify: `docs/todo-list.md`, `CHANGELOG.md`

**Interfaces:**
- Every API-index module has one manifest row: `module | tier | script | contracts`.
- Produces doctest `stdlib manifest has complete suite coverage`.

- [x] **Step 1: Write the failing manifest test**

Add a doctest that reads `test/stdlib/manifest.md`, extracts the first column of each non-header pipe row, and compares its set to the 37 module names in `docs/api/README.md`. For each row, require `test/stdlib/<script>.yona` and `.expected` to exist. Require tier to be exactly `pure`, `runtime`, `network`, or `gpu`.

- [x] **Step 2: Verify red**

Run: `./out/build/x64-debug-linux/tests -tc='stdlib manifest has complete suite coverage'`

Expected: FAIL because the manifest and module scripts do not exist.

- [x] **Step 3: Create the complete manifest and placeholder-free scripts**

Create a row for every `Std` module: Bool, ByteArray, Channel, Collection,
Crypto, Dict, Encoding, File, FloatArray, Format, Function, Gpu, Http,
IntArray, Io, Json, List, Log, Math, Net, Option, Pair, Parallel, Path,
Process, Random, Range, Regex, Result, Set, Stream, String, Task, Test, Time,
Tuple, Types, and Utf16. Each script must contain at least one real named
contract case; scripts are expanded in Tasks 3–6, never empty placeholders.

- [x] **Step 4: Add the todo hierarchy**

Add one parent `stdlib conformance suite` item and concrete unchecked rollout
items for pure, collection/codec, runtime, and network/GPU groups. State the
manifest is enforcing script presence, not a claim that every documented
function is already exhaustively tested.

- [x] **Step 5: Verify and commit**

Run: `ctest --preset unit-tests-linux --output-on-failure`

```bash
git add test/stdlib/manifest.md test/stdlib test/Codegen/CodegenTest.cpp docs/todo-list.md CHANGELOG.md
git commit -m "test: require stdlib conformance manifest"
```

### Task 3: Cover pure values, functions, and persistent collections

**Files:**
- Create: `test/stdlib/pure/{Bool,Option,Result,Pair,Tuple,Function,Collection,List,Range,Math,Constants,String,Format}_test.yona` and matching `.expected`
- Create: `test/stdlib/collections/{Set,Dict}_test.yona` and matching `.expected`
- Modify: `test/stdlib/manifest.md`, `docs/todo-list.md`, `CHANGELOG.md`

**Interfaces:**
- Each test program returns a passing `Std\Test.TestReport` for documented normal, boundary, and composition contracts.

- [x] **Step 1: Add failing cases before each implementation dependency**

For every pure module, add named tests for normal operation, empty/singleton
input, boundary behavior, and composition. Examples that must appear:

```yona
testCase "Option.map preserves None" (\_ -> check "None remains None" (map (\x -> x + 1) None == None))
testCase "Result.flatMap keeps error" (\_ -> check "Err is unchanged" (isErr (flatMap (\x -> Ok (x + 1)) (Err "bad"))))
testCase "Dict.put preserves old map" (\_ -> check "persistent update" (get "a" old == Some 1 && get "a" updated == Some 2))
testCase "Set union deduplicates" (\_ -> check "one member" (size (union {1} {1}) == 1))
```

- [x] **Step 2: Run each group before changing library code**

Run: `./out/build/x64-debug-linux/tests -tc='Stdlib conformance fixtures' -sc='*Option_test*|*Dict_test*|*String_test*'`

Expected: any incorrect documented behavior is surfaced as a failing Yona test; record each new compiler/runtime bug in `docs/todo-list.md` and stop for priority confirmation before fixing it.

- [x] **Step 3: Fix only demonstrated library defects and add regressions**

Implement fixes in the owning `.yona` stdlib module where possible. Keep C changes restricted to an OS syscall, layout, external-library, or measured hot loop requirement. Add an edge regression alongside the failing case.

- [x] **Step 4: Verify and commit**

Run: `ctest --preset unit-tests-linux --output-on-failure`

```bash
git add lib/Std test/stdlib/pure test/stdlib/collections test/stdlib/manifest.md docs/todo-list.md CHANGELOG.md
git commit -m "test: cover pure stdlib contracts"
```

### Task 4: Cover arrays, codecs, parsing, and path behavior

**Files:**
- Create: `test/stdlib/codecs/{ByteArray,IntArray,FloatArray,Encoding,Crypto,Json,Regex,Path,Types,Utf16}_test.yona` and matching `.expected`
- Modify: `test/stdlib/manifest.md`, `docs/todo-list.md`, `CHANGELOG.md`

**Interfaces:**
- Tests cover encoding round trips, invalid inputs, Unicode boundaries, array bounds/slices, JSON recursive values, and regex no-match/multiple-match behavior.

- [x] **Step 1: Add failing contract cases**

Include base64/hex/URL malformed input paths, empty and non-ASCII byte arrays,
JSON nested object/array and invalid syntax, surrogate-pair and CRLF UTF-16
positions, regex empty/no/multiple matches, and path joining/normalization
boundaries. Use `Result`/`Option` pattern matching rather than string-matching
error internals unless the public API specifies an exact message.

- [x] **Step 2: Run the codec fixture group**

Run: `./out/build/x64-debug-linux/tests -tc='Stdlib conformance fixtures'`

Expected: all codec scripts print their stable summary; any fault gets a
one-line `docs/todo-list.md` reproduction before a fix.

- [x] **Step 3: Verify and commit**

Run: `ctest --preset unit-tests-linux --output-on-failure`

```bash
git add lib/Std test/stdlib/codecs test/stdlib/manifest.md docs/todo-list.md CHANGELOG.md
git commit -m "test: cover stdlib codecs and arrays"
```

### Task 5: Cover portable runtime, resource, and concurrency modules

**Files:**
- Create: `test/stdlib/runtime/{File,Io,Process,Time,Random,Channel,Task,Parallel,Stream,Log}_test.yona` and matching `.expected`
- Modify: `test/Codegen/CodegenTest.cpp`, `test/Toolchain/YonaLinkUtil.h`, `test/stdlib/manifest.md`, `docs/todo-list.md`, `CHANGELOG.md`

**Interfaces:**
- Runtime scripts use scratch-isolated paths and verify normal, error, cleanup, ordering, and cross-module behavior.

- [x] **Step 1: Add scratch setup and path rewrites before scripts**

Extend `ScratchFiles` and `rewrite_codegen_fixture_tmp_paths` with one unique
path per File/IO scenario. Extend the artifact environment helper only when a
test needs deterministic process input. Never use a host home directory or a
shared `/tmp` name.

- [x] **Step 2: Add failing resource and scheduling cases**

Cover missing files, empty files, binary seek/read/write, process exit codes,
argument forwarding, channel capacity/try-receive/close, stream empty/take/
zip, deterministic random bounds, and task/parallel completion. Every opened
resource is consumed using the current typed `Linear` API; include failure-path
cleanup cases as soon as transfer ownership APIs land.

- [x] **Step 3: Run runtime fixtures and fix demonstrated defects**

Run: `./out/build/x64-debug-linux/tests -tc='Stdlib conformance fixtures'`

Expected: portable runtime suite passes on Linux/macOS/Windows using existing
platform rewrite rules. Append newly discovered bugs to the todo list before
making any implementation change.

- [x] **Step 4: Verify and commit**

Run: `ctest --preset unit-tests-linux --output-on-failure`

```bash
git add lib/Std src/Runtime test/stdlib/runtime test/Codegen/CodegenTest.cpp test/Toolchain/YonaLinkUtil.h test/stdlib/manifest.md docs/todo-list.md CHANGELOG.md
git commit -m "test: cover portable stdlib runtime contracts"
```

### Task 6: Add required capable network and GPU tiers

**Files:**
- Create: `test/stdlib/network/{Net,Http}_test.yona` and matching `.expected`
- Create: `test/stdlib/gpu/gpu_test.yona` and matching `.expected`
- Modify: `CMakeLists.txt`, `.github/workflows/cmake-multi-platform.yml`, `test/stdlib/manifest.md`, `docs/todo-list.md`, `CHANGELOG.md`

**Interfaces:**
- Produces CTest labels `stdlib-network` and `stdlib-gpu`; capable CI jobs run them without skip fallback.

- [x] **Step 1: Add capability-probe tests and local skip behavior**

Network tests bind loopback port zero, use the selected port for TCP/UDP/HTTP,
and report a single explicit `SKIP capability: loopback sockets unavailable`
only when `socket(AF_INET, ...)` is denied. GPU tests cover CPU fallback in
the default suite and use `vulkanStatus` plus a successful device operation in
the GPU suite; they report the runtime device note when unavailable.

- [x] **Step 2: Register labels and capable CI jobs**

Add CTest entries that invoke the dedicated network and GPU fixture cases,
label them `stdlib-network` and `stdlib-gpu`, and run them in the normal
multi-platform matrix. Vulkan-enabled builds additionally retain the focused
device-operation slice; the default GPU conformance suite proves the portable
fallback and capability-reporting contracts.

- [x] **Step 3: Verify and commit**

Run: `ctest --preset unit-tests-linux --output-on-failure`

Run when configured: `ctest --test-dir out/build/x64-debug-linux -L stdlib-gpu -V`

```bash
git add CMakeLists.txt .github/workflows/cmake-multi-platform.yml test/stdlib/network test/stdlib/gpu test/stdlib/manifest.md docs/todo-list.md CHANGELOG.md
git commit -m "test: add capable stdlib network and gpu tiers"
```

### Task 7: Finish documentation and enforce the completed matrix

**Files:**
- Modify: `docs/api/Test.md`, `docs/api/README.md`, `site/src/content/docs/stdlib/test.md`, `site/src/content/docs/stdlib/index.md`, `docs/todo-list.md`, `CHANGELOG.md`
- Test: `test/Codegen/CodegenTest.cpp`

**Interfaces:**
- Public docs specify how users write and run Yona tests, and the manifest test proves every public module owns a suite.

- [x] **Step 1: Add documentation assertions**

Extend the manifest doctest to require every public module row has a nonempty
contracts column and that its declared tier matches a known label. This turns
future module additions into a red test until they add a real suite.

- [x] **Step 2: Remove completed rollout entries and document tier behavior**

Remove completed conformance rollout items from `docs/todo-list.md`; retain
only concrete unmet contracts. Update API and site Test pages with a runnable
example, report format, comparator rationale, fixture location, and local vs
capable-CI behavior.

- [x] **Step 3: Final verification and commit**

Run: `python3 scripts/gendocs.py && cmake --build --preset build-debug-linux && ctest --preset unit-tests-linux --output-on-failure && git diff --check`

```bash
git add docs site test/Codegen/CodegenTest.cpp CHANGELOG.md
git commit -m "docs: complete stdlib conformance guide"
```
