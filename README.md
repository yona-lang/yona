# Yona

A compiled functional programming language targeting LLVM, featuring persistent data structures, transparent async, pattern matching, and a comprehensive standard library.

## Highlights

- **Compiled to native** via LLVM — performance within 1-2x of C
- **Persistent data structures** — immutable sequences (RBT), dictionaries and sets (HAMT)
- **Transparent async** — no async/await keywords; independent let bindings auto-parallelize
- **Pattern matching** — head-tail, tuples, constructors, or-patterns, guards
- **Algebraic data types** — with traits, default methods, cross-module dispatch
- **Module system** — FQN imports, interface files, cross-module generics
- **38 stdlib modules** — I/O, networking, regex, JSON, crypto, process management, streams, channels, accelerated columns

## Quick Start

```bash
# Install (Fedora/RHEL)
sudo dnf install llvm llvm-devel llvm-libs clang lld cmake ninja-build pcre2-devel

# Build
git clone https://github.com/yona-lang/yonac-llvm.git
cd yonac-llvm
cmake --preset x64-release-linux
cmake --build --preset build-release-linux

# Run
./out/build/x64-release-linux/yonac -e 'let fib n = if n <= 1 then n else fib (n-1) + fib (n-2) in fib 10'
# => 55
```

See [INSTALL.md](INSTALL.md) for macOS, Windows, and Docker instructions.

Prebuilt release artifacts now include:
- Linux/macOS tarballs
- Windows ZIP bundle
- Windows MSI installer (WiX v4-based packaging flow)

## Language Examples

```yona
-- Pattern matching with head-tail decomposition
let foldl fn acc seq =
    case seq of
        [] -> acc
        [h|t] -> foldl fn (fn acc h) t
    end
in foldl (\a b -> a + b) 0 [1, 2, 3, 4, 5]
-- => 15
```

```yona
-- Comprehensions with guards (stream-fused into single loop)
let nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10] in
let doubled = [x * 2 for x = nums] in
[x for x = doubled, if x > 10]
-- => [12, 14, 16, 18, 20]
```

```yona
-- Algebraic data types and pattern matching
type Option a = Some a | None

let map fn opt =
    case opt of
        Some x -> Some (fn x)
        None -> None
    end
in map (\x -> x * 2) (Some 21)
-- => Some 42
```

```yona
-- Transparent async: independent bindings run in parallel
import exec from Std\Process in
let a = exec "echo hello",
    b = exec "echo world"
in a ++ " " ++ b
-- Both commands run concurrently via thread pool
```

```yona
-- Persistent dictionaries (HAMT, O(1) amortized)
import put, get, size from Std\Dict in
let d = put (put (put {} 1 "one") 2 "two") 3 "three" in
(get d 2 "missing", size d)
-- => ("two", 3)
```

```yona
-- Regex (PCRE2 with JIT)
import compile, matches, find from Std\Regex in
let re = compile "([a-z]+)([0-9]+)" in
case find re "test123" of
    [] -> "no match"
    [full | groups] -> full
end
-- => "test123"
```

## Standard Library

| Module | Key Functions |
|--------|---------------|
| `Std\List` | map, filter, fold, foldl, reverse, flatten, zip, take, drop |
| `Std\Dict` | put, get, contains, size, keys |
| `Std\Set` | insert, contains, size, union, intersection, difference |
| `Std\String` | length, split, join, trim, indexOf, replace, toUpper, toLower |
| `Std\Math` | abs, max, min, sqrt, sin, cos, pow, factorial, gcd |
| `Std\Regex` | compile, matches, find, findAll, replace, replaceAll, split |
| `Std\File` | readFile, writeFile, exists, readLines, listDir |
| `Std\Process` | spawn, exec, readLine, readAll, wait, kill, writeStdin |
| `Std\IO` | print, println, eprint, eprintln, readLine, isTty, flush (non-blocking via io_uring) |
| `Std\Json` | parse, stringify |
| `Std\Net` | connect, accept, send, recv |
| `Std\Option` | Some, None, map, unwrapOr |
| `Std\Result` | Ok, Err, map, mapErr, unwrapOr |
| `Std\Stream` | range, map, filter, take, chunksOf, bracket, async, buffered |
| `Std\Channel` | channel, send, recv, close (bounded, linear endpoints) |
| `Std\Task` | spawn, await |
| `Std\Constants\{Num,Math,Platform}` | intMax, pi, pageSize, endianness |

Full API docs: `python3 scripts/gendocs.py` generates [docs/api/](docs/api/).

## Performance

Benchmarks vs equivalent C (gcc -O2), 10 iterations:

| Benchmark | Ratio | Notes |
|-----------|-------|-------|
| par_map | **1.0x** | Parallel comprehension, 20 elements |
| list_map_filter | **1.0x** | Stream fusion eliminates intermediate allocations |
| parallel_async / sequential_async | **1.0x** | io_uring + thread pool |
| tak | **1.1x** | |
| sum_squares | 1.3x | |
| sieve | 1.4x | |
| fibonacci | 2.4x | |
| dict_build / set_build (10K) | 2.2x | HAMT-backed |
| ackermann | 2.6x | |
| queens | 10.6x | Allocation-heavy (43 MB vs 2 MB) — on the investigation list |

Full table: [docs/todo-list.md](docs/todo-list.md#benchmark-results). Reference
impls in C, Erlang, Haskell, Java, JavaScript, Python under
[`bench/reference/`](bench/reference/).

## Architecture

```
Source → Lexer → Parser → AST → Codegen (LLVM IR) → Native Executable
```

- **Lexer**: Newline-aware, juxtaposition-based function application
- **Codegen**: Type-directed with `TypedValue = {Value*, CType}`, monomorphization
- **Memory**: Atomic RC, Perceus-linear callee-owns for all heap types (seqs, sets, dicts, …), pool allocator, arena allocation
- **Async**: io_uring (Linux), thread pool with work-stealing
- **Data structures**: RBT sequences, HAMT dicts/sets with structural sharing

## Documentation

- [Installation Guide](INSTALL.md)
- [Language Syntax Reference](docs/language-syntax.md)

**Feature Guides:**
- [Algebraic Effects](docs/effects.md) — typed, composable side effects with handlers
- [Pattern Matching](docs/pattern-matching.md) — head-tail, constructors, or-patterns, guards
- [Transparent Async](docs/async.md) — auto-parallelization, io_uring, no async/await keywords
- [Persistent Data Structures](docs/persistent-data-structures.md) — RBT sequences, HAMT dicts/sets
- [Traits](docs/traits.md) — type classes with default methods, cross-module dispatch
- [Module System](docs/module-system.md) — FQN imports, interface files, cross-module generics
- [Memory Management](docs/memory-management.md) — atomic RC, Perceus, pool allocator, arena

**Project:**
- [Status & Roadmap](docs/todo-list.md)
- [Benchmark Results](docs/benchmark-results.md)
- [Windows Installer Draft](packaging/windows/README.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)
- [API Reference](docs/api/)

## Building & Testing

```bash
# Debug build
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux

# Run tests
ctest --preset unit-tests-linux

# Run benchmarks (compare vs C; add --compare-erl or --compare=c,erl for Erlang too)
python3 bench/runner.py --compare-c -n 10

# Format code
./scripts/format.sh
```

## License

[GPLv3](LICENSE.txt)
