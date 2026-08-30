# Std.Test

Yona-native test cases, reports, and assertions.

Test programs build a sequence of named `TestCase` values, call `run`, and
print `render report`. Assertions stay pure: failures are returned as data
so a suite runs every case and reports all failures in declaration order.

```yona
import testCase, check, equalBy, run, render from Std\Test,
println from Std\Io
in
let intEqual a b = a == b,
cases = [
testCase "truth" (\_ -> check "true is accepted" true),
testCase "comparison" (\_ -> equalBy "two equals two" intEqual 2 2)
]
in do
println (render (run cases))
0
end
```

The report contains one line per case followed by
`SUMMARY 2 passed, 0 failed`.

The compiler repository keeps executable suites under `test/stdlib/` and a
complete public-module matrix in `test/stdlib/manifest.md`. The default
CTest run executes the whole matrix; `stdlib-network` and `stdlib-gpu`
labels expose the capability-sensitive slices directly.

## Types

### TestResult

`type TestResult = Pass String | Fail String`

A single assertion result. `Pass` and `Fail` both retain a useful message.

### TestCase

`type TestCase = TestCase { name: String, thunk: (() -> TestResult) }`

A named deferred assertion. The thunk is evaluated only by `run`.

### TestReport

`type TestReport = TestReport { passed: Int, failed: Int, lines: Seq String }`

Aggregate counts and stable output lines in execution order.

## Functions

### `pass : String -> TestResult`

Construct a successful assertion result.

### `fail : String -> TestResult`

Construct a failed assertion result.

### `check : String -> Bool -> TestResult`

Pass when `condition` is true, otherwise fail with `message`.

### `equalBy : String -> (a -> a -> Bool) -> a -> a -> TestResult`

Compare `expected` and `actual` with an explicit equality predicate.

The caller supplies equality so opaque values, floats, byte arrays, and
user ADTs can choose the comparison contract appropriate to that API.

### `testCase : String -> (() -> TestResult) -> TestCase`

Name a deferred assertion for inclusion in a suite.

### `run : Seq TestCase -> TestReport`

Execute all cases, retaining a stable line per case and all failures.

### `render : TestReport -> String`

Render a report as stable line-oriented text suitable for fixtures and CI.
