# Standard-Library Conformance Suite Design

## Goal

Establish a broad, repeatable conformance suite for every public `Std` module.
Test contracts, assertions, case composition, and result formatting live in
Yona. CTest remains the process-level executor that compiles test programs,
runs them, and applies platform capability labels.

## Non-goals

- Do not add an interpreter, reflection-based test discovery, or a second test
  language.
- Do not duplicate standard-library behavior in C++ assertions.
- Do not make network or GPU functionality silently optional in capable CI.

## Test library

`Std\Test` becomes a pure-Yona module with the following public model:

```yona
type TestResult = Pass String | Fail String
type TestCase = TestCase String (() -> TestResult)
type TestReport = TestReport Int Int [String]

pass : String -> TestResult
fail : String -> TestResult
check : String -> Bool -> TestResult
equalBy : String -> (a -> a -> Bool) -> a -> a -> TestResult
case : String -> (() -> TestResult) -> TestCase
run : [TestCase] -> TestReport
render : TestReport -> String
```

`equalBy` deliberately requires a comparator. This keeps the framework
generic without claiming universal equality or string rendering for values
that do not implement it. Module tests supply comparator/description-specific
checks when their values have specialized semantics (floating-point NaN,
crypto bytes, opaque handles, asynchronous results).

`run` evaluates every test case exactly once, preserves declaration order,
collects all failures, and never short-circuits. `render` has stable lines:

```text
PASS <case name>
FAIL <case name>: <message>
SUMMARY <passed> passed, <failed> failed
```

Individual test programs call `println (render (run cases))`. A non-zero exit
is a host responsibility: the C++ fixture adapter treats any `FAIL` line or a
nonzero process status as failure. This avoids adding effectful process exit
to a pure assertion API.

## Test layout and discovery

Tests live under `test/stdlib/` in one script per module:

```text
test/stdlib/
  pure/Option_test.yona
  collections/Dict_test.yona
  codecs/Json_test.yona
  runtime/File_test.yona
  platform/Net_test.yona
  accelerator/GPU_test.yona
```

Every script imports `case`, `check`, `equalBy`, `run`, and `render` from
`Std\Test`, declares a sequence of named cases, and prints only the rendered
report. Its `.expected` file is the deterministic all-pass summary. The
existing C++ codegen fixture executor gains recursive discovery for this tree;
it does not know individual assertions or module semantics.

`test/stdlib/manifest.md` is the source-of-truth coverage matrix. For each
public module it records its suite path, capability tier, and required contract
families. A C++ manifest test parses this simple tabular format and fails when
a module lacks an existing script and expected-output companion.

## Capability tiers

| Tier | CTest label | Requirement |
|---|---|---|
| Pure | `stdlib-pure` | Always run on every supported platform. |
| Runtime | `stdlib-runtime` | Always run; uses isolated scratch files/processes. |
| Network | `stdlib-network` | Required in capable CI; locally skipped only after a positive capability probe. |
| GPU | `stdlib-gpu` | Required in Vulkan-capable CI; CPU fallback contracts always remain in the default suite. |

Tests use only isolated files beneath the harness scratch root. Process tests
use deterministic commands supplied by the existing platform rewrite helper.
Network tests bind loopback ephemeral ports. GPU tests state whether they test
the portable fallback or a device path.

## Required module contracts

Each manifest entry must cover applicable categories:

- normal result and composition with at least one neighboring `Std` module;
- empty, singleton, boundary-size, and large-input behavior;
- invalid input or documented `Result`/`Option`/exception behavior;
- persistence/aliasing for immutable collections;
- resource ownership, cleanup, and failure-path cleanup for handles;
- ordering and completion behavior for async, channel, process, and network
  operations; and
- platform fallback or capability diagnostics where a feature is optional.

The rollout starts with pure modules (Bool, Option, Result, Pair, Tuple,
Function, Collection, List, Range, Math, Constants, String, Format, Set, and
Dict), then arrays/codecs (ByteArray, IntArray, FloatArray, Encoding, Crypto,
Json, Regex, Path), then runtime/resource modules (File, IO, Process, Time,
Random, Channel, Task, Parallel, Stream), then platform/GPU modules (Net,
Http, GPU, Log, Types). Existing fixtures migrate only when the new suite
expresses the same behavior more clearly; compiler-specific codegen fixtures
stay in `test/codegen/`.

## CI and documentation

CTest registers the conformance adapters with the labels above. CI runs the
default pure/runtime suites on Linux, macOS, and Windows, a loopback-network
job, and a Vulkan-capable GPU job. The manifest summary is emitted in JUnit so
missing modules are visible in CI.

`docs/api/Test.md`, `docs/api/README.md`, the public site stdlib Test page,
and `CHANGELOG.md` describe the new contracts. `docs/todo-list.md` tracks the
parent conformance initiative and one unchecked rollout item per module group;
completed items are removed rather than left stale.
