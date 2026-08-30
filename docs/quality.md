# Local quality tooling

Yona's quality checks are free, local programs. Once the tools are installed,
the checks do not need a network connection or an account with an analysis
service. The same commands are used by developers and automation.

## Commands

Run commands through Python on every platform:

```text
python scripts/quality.py format
python scripts/quality.py format-check
python scripts/quality.py hygiene
python scripts/quality.py naming
python scripts/quality.py architecture
python scripts/quality.py generated
python scripts/quality.py symbols --build-dir out/build/x64-debug
python scripts/quality.py yona-style --build-dir out/build/x64-debug
python scripts/quality.py headers --build-dir out/build/x64-debug
python scripts/quality.py tidy --build-dir out/build/x64-debug
python scripts/quality.py analyze --build-dir out/build/x64-debug
python scripts/quality.py sanitize --preset x64-debug
python scripts/quality.py vulkan --build-dir out/build/x64-debug --preset x64-debug
python scripts/quality.py fuzz-check --build-dir out/build/x64-debug --preset x64-debug
python scripts/quality.py coverage --preset x64-debug
python scripts/quality.py quality --build-dir out/build/x64-debug
```

Linux and macOS may use `scripts/quality.sh`; Windows may use
`scripts/quality.ps1`. `scripts/format.sh` remains a convenience wrapper for the
recursive `format` command.

File-oriented commands accept paths after the command. This is how the local
pre-commit hooks check only the staged files. Passing no paths checks every
tracked file recursively.

The full `quality` command runs hygiene, naming, the canonical architecture and
built-symbol contracts, formatting, the compiler-backed Yona source-style check,
clang-tidy, independent analyzers, sanitizers, Vulkan validation, the short fuzz
smoke suite, and coverage. It is intentionally more expensive than the
pre-commit checks. The blocking `Local Quality Gate` workflow runs these same
commands in parallel static-analysis, sanitizer, fuzzer, and coverage jobs; it
does not upload source to an analysis service.

## Required tools

The native formatter, clang-tidy, dependency scanner, coverage programs, and
compiler must come from LLVM 22.1.x. The driver rejects a different formatter or
clang-tidy version so developers cannot produce tool-version-dependent changes.

Install these free command-line programs and make their executables available on
`PATH`:

- LLVM 22.1.x: `clang-format`, `clang-tidy`, `clang-scan-deps`, `llvm-profdata`,
  and `llvm-cov`.
- CMake, Ninja, Cppcheck, the scan-build suite (`analyze-build`), and
  Include-What-You-Use (`iwyu_tool.py`).
- Gersemi, Ruff, and pre-commit.
- ShellCheck and shfmt.
- PowerShell 7 with PSScriptAnalyzer.
- actionlint and yamllint.
- Node.js and pnpm 10.15.0. The repository pins Prettier, its Astro plugin,
  ESLint, the Astro and TypeScript parsers, and their rule sets in
  `site/package.json`; install them once with
  `pnpm --dir site install --frozen-lockfile`.
- markdownlint-cli2 and typos.
- Vulkan validation layers for Vulkan-enabled runtime checks.
- glslang 16.1.0 for deterministic Vulkan shader regeneration.

## Local fuzzing

The parser, canonical `.yonai` reader, LSP JSON/JSON-RPC framing, PCRE2 regex
adapter, and UTF-8/UTF-16 codecs have libFuzzer harnesses. They use the tracked
seed corpora below `fuzz/Corpus/`; mutations and crash artifacts are written
only below the configured build directory.

Configure Clang fuzzers explicitly, then run either the short deterministic
smoke target or the time-bounded local campaign:

```text
cmake --preset x64-debug -B out/quality/fuzz -DYONA_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build out/quality/fuzz --target fuzz-check
cmake --build out/quality/fuzz --target fuzz
```

`fuzz-check` performs 100 mutations per harness by default. `fuzz` spends 60
seconds per harness. Override those values at configure time with
`YONA_FUZZ_CHECK_RUNS` and `YONA_FUZZ_SECONDS`. Both targets use
`-fsanitize=fuzzer,address,undefined`; after dependencies and tools have been
installed, neither target uses the network or a hosted service.

The quality driver resolves Python console tools from the active interpreter and
Node tools from `site/node_modules/.bin` in addition to `PATH`. The project does
not download tools while a quality command is running. Use the operating-system
package manager, Python virtual environment, or Node tool installation preferred
by the development environment before going offline.

## Compile database and instrumented builds

`yona-style`, `headers`, `tidy`, and `analyze` use an existing build directory;
`yona-style` runs that build's `yonac --check-style`, while the native checks
consume `compile_commands.json`. Every project configure preset exports the
database. Configure a normal debug build first, or pass the build directory
explicitly:

```text
cmake --preset x64-debug-linux
python scripts/quality.py tidy --build-dir out/build/x64-debug-linux
```

`sanitize` and `coverage` create isolated configurations below `out/quality/`;
they never change a normal build. AddressSanitizer is combined with
UndefinedBehaviorSanitizer and leak detection. ThreadSanitizer runs in a
separate configuration on Linux and macOS because it cannot be combined with
AddressSanitizer and is not available in the supported Windows toolchain. When a
configured build is supplied, isolated builds reuse its already installed
FetchContent source directories and configure with
`FETCHCONTENT_FULLY_DISCONNECTED=ON`. Thus a complete local quality run does not
contact the network after dependencies and tools have been installed.

`vulkan` creates a separate Vulkan-enabled build, runs the GPU/Vulkan test slice
with `VK_LAYER_KHRONOS_validation`, and rejects validation errors and VUID
diagnostics. A software Vulkan implementation such as Mesa lavapipe is enough; a
physical GPU is not required. Its dependencies are resolved from the same
previously configured source tree, so the command also remains offline.

## Canonical formatting

clang-format owns C and C++ layout and include sorting. Include blocks are
regrouped in this order: matching header, other Yona headers, LLVM family
headers, other third-party headers, then standard/platform headers. Other file
types are formatted by Gersemi, Ruff, shfmt, or Prettier as appropriate. The
`headers` command compiles every repository header as the first include in an
otherwise empty translation unit, using the configured compiler and include
paths. Platform-only headers are checked only on their owning host platform.

Generated interfaces, lock files, public documentation generated from the
stdlib, and generated shader fragments are excluded in one place in
`scripts/quality.py`. A new generated family must be added there explicitly;
directory-wide or changed-lines baselines are not accepted.

The `symbols` command inspects the aggregate compiler and runtime archives with
LLVM's local `llvm-nm`. It writes deterministic snapshots below
`out/quality/symbols/`, rejects snake- or mixed-case Yona-family exports and
retired ABI/API version getters, and requires the canonical Typed Core,
retain/release, task, channel, and GPU entry points.

GPU shader sources and fragments live together under `src/Runtime/Generated/`.
Regenerate every fragment with `python scripts/generate_gpu_shaders.py --write`;
verify byte-for-byte reproducibility with `python scripts/quality.py generated`.
The generator rejects any glslang release other than 16.1.0 so an upgrade is an
explicit, repository-wide artifact change.

## Pre-commit

The pre-commit configuration contains only `repo: local` hooks, so installing
the hooks does not clone hook repositories:

```text
pre-commit install
pre-commit run --all-files
```

The hooks enforce text hygiene, canonical formatting, compiler-backed Yona
identifier style, path and file naming, header guards, module/file
correspondence, the acyclic CMake component graph, and removal of retired names.
The full analyzer suite remains an explicit `quality` invocation because it
builds and tests multiple instrumented configurations.
