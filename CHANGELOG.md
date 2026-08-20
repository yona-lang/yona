# Changelog

## Unreleased

### Added
- `yona` is a Yona-written runner (shebang `#!/usr/bin/env yona`): compile a
  file, stdin, or `-e` expression via sibling `yonac`, then exec the result.
  No arguments on a TTY starts the C++ REPL (`yona-repl`).
- `yonac -` reads source from stdin (compile only).
- `Std\Process.getArgs`, `executablePath`, `yonaVersion`, `tempDir`,
  `tempFile`, `run`, and `execArgs`. `Std\IO.readStdin` reads stdin to EOF.
- CMake `yona_add_executable` (`cmake/YonaTools.cmake`) compiles Yona tools
  during the build; `tools/yona` is the first consumer.

### Fixed
- `with` expression parser no longer crashes: `parse_expr_until_in()` stops
  before the `in` keyword when parsing the resource, and null checks prevent
  constructing `WithExpr` with a null body (`with fd = 0 in 42`).
- Imported `Std\List.isEmpty` (and other GENFNs) inside a `let`-bound
  function whose parameter is named `rest` no longer fails with
  `undefined function 'cmp'`. Case-pattern bindings are visible to
  free-var analysis, and sibling GENFNs are registered without the
  caller's locals. Sibling registration also skips names already
  imported from another module (`drop` from `Std\String` is not
  replaced by `Std\List.drop`).
- `AFN` calls that are the body of a `let`-bound function are awaited
  before return (`let f cmd = exec cmd in f "echo x"` prints `x`, not
  a Promise pointer).
- Invalid UTF-8 (e.g. `printf '\xc0\x15@'`) is a lex error instead of
  a `yonac` busy-loop.
- CI linker-mode validation tracks `scripts/ci/smoke.yona` (and
  `scripts/**/*.yona` is no longer gitignored). `yonac -e` is gone, so
  the smoke used a file that never reached the checkout.

### Changed
- The LLVM compiler repository is now [yona-lang/yona](https://github.com/yona-lang/yona)
  (same stars and URL as the former GraalVM tree). The GraalVM/Truffle
  implementation, its issues, wiki snapshot, and 0.8.x releases are archived at
  [yona-lang/yona-graalvm](https://github.com/yona-lang/yona-graalvm).
  `yona-lang/yonac-llvm` is an archived pointer.
- **Breaking:** `yonac -e` / `--expression` is removed. One-liners are
  `yona -e '…'`. Inspect IR with `yonac --emit-ir -` or a file.

## v0.1.4 (2026-08-20)

### Fixed
- `let f x = if … else … in body` parses again: `if` then/else no longer
  consume the let-closing `in` as membership (`InExpr`). `2 in [1, 2, 3]`
  and `if 2 in xs then …` still work. Parenthesize membership in a
  then/else that is itself a let-binding RHS: `then (2 in xs)`.
- HAMT set/dict destructors now `rc_dec` heap keys and values
  (`KEY_HEAP`/`VAL_HEAP`), so `{[1], [2]}` and dicts of seqs no longer
  leak inner collections when the map is dropped.
- Applying `Std\Stream.map` (or `toSeq`) to a `Seq` is a compile-time
  **E0100** instead of a runtime SIGSEGV. `.yonai` `SEQ` vs `ADT` tags
  are now used at import so a sequence is not accepted where a Stream
  is required (`toSeq (map length ["ab", "abc"])`). Use `fromSeq`.
- Remonomorphizing `Std\Stream.map` (and other lazy ADT GENFNs) no longer
  SIGSEGVs: the extracted function is detached from the temporary parse
  module so parent walks cannot follow a freed `ModuleDecl`. Imported
  `length` as a value works on Stream (`toSeq (map length (fromSeq xs))`).
- Compiling a module that exports a wrapper around a private helper no
  longer prints a spurious **E0104** while filling `.yonai` FN effect rows
  (`populate_interface_effect_rows` uses sibling-aware `check_module`).
- An imported function used as a first-class value (e.g. `map length xs`)
  now materializes a closure instead of passing a null operand to the HOF.
- Exported functions that call unexported module helpers remonomorphize
  correctly (`doubledSquare` → `50`). Helper source is emitted as GENFN
  (not a public FN). Failed remonomorphize falls back to the precompiled
  extern; `yonac` exits 1 if codegen reported errors instead of linking a
  wrong binary with exit 0.
- Sequence operators `:>` (append), `--` (remove elements of the right from
  the left), and `in` (membership on seq/set, key test on dict) now parse
  and compile. `:>` already type-checked; it was missing codegen (`seq_snoc`).
  `--` and `in` were lexed but dropped by the Pratt parser (E0301 inside
  parens; bare expressions silently kept only the left operand).
- Top-level print of nested sequences (and other heap elements in a Seq)
  no longer dumps raw addresses: `[[1, 2], [3]]` prints as `[[1, 2], [3]]`.
  The seq printer consults `heap_flag` and dispatches on the RC type tag.
- Set/dict print no longer dumps nested heap values as integers:
  `{[1, 2]}` and `{1: [10, 20]}` print the inner sequences. HAMT-backed
  sets keep an `IS_SET` tag bit so `[{1}, {2}]` prints as sets, not
  `{k: 1}` dicts.
- Printing a tuple that contains a Seq (or other pointer-typed slot) is
  no longer an LLVM verifier failure: i64 tuple slots are unboxed to the
  type `yona_rt_print_*` helpers expect (`print_tuple_int_seq`).

### Added
- Applying a function whose body performs an effect with no covering `handle`
  is a compile error **E0202** (points at the introducing `perform`, note at
  the call). Direct unhandled `perform` is still `-Wunhandled-effect`.
- Function arrows unify effect rows (closed sets + open rest). Higher-order
  `apply f x = f x` propagates the argument's effects; wrapping
  `let g = \() -> f ()` unions into `g`; a `handle` subtracts covered ops
  from the enclosing row. Types pretty-print as `(a -> !{Fs.read} b)`.
  Exported functions write closed rows on `.yonai` `FN` lines
  (`effects Fs.read` or `effects Fs.read,Net.post`); importers restore
  them so unhandled apply is **E0202** and a covering `handle` is not.
  FN lines without `effects` stay fresh type vars (stdlib unchanged).
  Open HOF rows write `effects | hof` so `apply f x = f x` still
  propagates the argument's effects after import. Module compile
  typechecks siblings as a unit so `wrap = \() -> readSecret ()`
  records `readSecret`'s row on `wrap`.

### Documentation
- Public Yona 2.0 docs live in-repo at `site/` (Astro Starlight) and deploy
  to Cloudflare Pages via `.github/workflows/docs-site.yml`. Preview locally
  with `cd site && pnpm dev`. The legacy GraalVM-era site stays on
  GitHub Pages at https://yona-lang.github.io/.
- Docs site traffic is measured with cookieless Plausible at
  `plausible.kiket.dev` (`data-domain` `yona-lang.org`).
- Docs examples drop dummy trailing `0` and single-expression `do` wrappers.
  `let … in do` is valid: `let` binds (and may parallelize), `do` sequences.
- Style page: `let` and `do` have different semantics; combining them is
  idiomatic when you need both. The anti-pattern is `let _ = effect`.
- Stdlib reference no longer shows the “generated, do not edit” maintainer
  note on the public pages.
- Stdlib API pages put the full `name : T1 -> T2 -> R` signature in the
  heading (from source annotations or `.yonai` types), not a bare name or
  a parameter list.
- Function docs use `name pats = body` and juxtaposition application; the
  invented `name(x, y) ->` form and `f(x, y)` as a two-arg call are gone.
- `Std\Parallel.pmap` example is a fenced code block on the stdlib page.

### GPU / Std\GPU
- `mapGPU` / `reduceGPU` (Int `Buffer`) and `mapFloatGPU` / `reduceFloatGPU`
  (`FloatArray`) via fixed kernel ADTs (`IntMapOp`, `FloatMapOp`, …).
- `mapReduceGraphGPU`: one Vulkan submit for map→…→reduce with
  `VK_KHR_synchronization2` barriers and timeline wait when enabled
  (`YONA_GPU_VULKAN_GRAPH` / `YONA_GPU_VULKAN_COMPUTE`). Stages are Add, Mul,
  and Square (each a real compute shader; unknown tags refuse the GPU path).
- Async float fence waiter: short-timeout poll; task-group cancel completes the
  promise with **-887** early and discards host writeback while GPU resources drain.
- `PinnedFloats`: prefers Vulkan host-visible mapped memory; malloc fallback;
  `YONA_GPU_PINNED_HOST_MALLOC=1` forces malloc. `pinnedBackend`,
  `mapFloatPinnedGPU`. CPU↔GPU pipelines: `gpuFloatChannel` / `drainMapFloatGPU`.
- Typed GPU failures: `GpuIssue`, `checkGpu`, `withGpuIssue` from `Std\GPU`
  (Result-style). `raiseGpu` / `withGpuFallback` `perform Gpu.*`; GENFN
  remonomorphization inside a user `handle` binds the caller's clauses
  (effect rows on `.yonai`; not a C++ name list). Module-export compile of
  those helpers emits `:UnhandledEffect` if invoked with no handler.
  Direct use-site `perform Gpu.oom` / `deviceLost` / `fail` still works.
- Transparent lowering: inline `Std\IntArray` / `Std\FloatArray` `map` /
  `filter` / `foldl` in the fixed kernel library compile to the Std\GPU ABI
  (`x + k`, `x - k`, `0 - x`, `x * k`, `x * x`, `x > k`, `x < k`, sum,
  float scale/sum). `filterLessThan` and `mapSquare` use real Vulkan shaders
  when compute is enabled (same env as filter / mapMul).
  `yonac --no-accelerator-lowering` keeps host closures.
  `yonac --strict-accelerator` errors (**E0700**) on unlowerable lambdas
  (full arbitrary-lambda SPIR-V still deferred).
- Bench: `gpu_filter_lt_hot`, `gpu_map_square_hot`, `gpu_pinned_scale_hot`
  in `run_gpu_compare.py`.
- `reduceFloatGPU` uses a real GPU block-reduce (`gpu_f64_reduce.comp` /
  `gpu_f32_reduce.comp`) when the stub device is up, else CPU.
- Bench: `bench/accelerators/gpu_pinned_scale_hot.yona` in `run_gpu_compare.py`.

### Type system
- **Effect-row inference (GitHub #8):** function arrows carry latent
  `!{Effect.op}` rows; `handle` subtracts covered ops; `.yonai` emits/parses
  `effects Op1,Op2` and open HOF tails (`effects |r0 0:|r0`). Applying an
  effectful function without a covering handler is **E0202**, pointed at the
  introducing `perform` with a note at the call. Higher-order functions keep
  an open `|r` rest (`\f x -> f x`); recursive functions take the least fixed
  point of `r ~ !{L | r}` instead of an infinite type. See `docs/effects.md`.
- LinearityChecker is type-directed (`Linear _` and products of Linear). There
  is no C++ producer-name allowlist.
- `.yonai` `LINEAR` overlay for imported C stdlib (`openFile`, `tcpConnect`,
  `channel`, `spawn`, and other LINEAR rows). CType stays INT/TUPLE.
- FQN `Pkg\Mod::func` (`ModuleCall`) uses the same import overlay as selective
  and wildcard imports.
- LinearityChecker walks `WithExpr` (bind Linear from the resource; discharge at
  with-exit via Closeable) and `FunctionExpr` bodies (fresh scope; Linear
  parameters from the zonked arrow type). TypeChecker binds `with` names to the
  resource expression’s type (not a fresh unbound var).

## v0.1.3 (2026-08-18)

Packaging-only release so the GitHub Release workflow can finish Homebrew and Copr.

- Homebrew: tap push uses `HOMEBREW_TAP_TOKEN` or `HOMEBREW_TAP_SSH_KEY` (v0.1.2 had neither).
- Copr/RPM: install `libyona_lib.so` and strip the mock BUILD-dir RUNPATH (Fedora `check-rpaths`).
- Linux packaging/CI: `-DYONA_LINK_STATIC_CLI=ON -DCMAKE_SKIP_BUILD_RPATH=ON`.

## v0.1.2 (2026-08-18)

Packaging and in-process LLD so distro and CI builds link without FetchContent
or clang-driver flags passed to raw `ld.lld`.

### Distribution
- Prefer system CLI11, doctest, libxml2, and LLD headers (`-DYONA_FETCH_DEPS=OFF` in packaging).
- Debian: `libcli11-dev`, `liblld-dev`, `libpolly-dev`.
- Homebrew source formula in `akovari/homebrew-tap`.

### Linker
- Ubuntu: Polly package plus every `LLVM_TARGETS_TO_BUILD` for embedded LLD.
- macOS: `ld64.lld` `-arch` / `-platform_version` / `-syslibroot` before inputs.
- Linux: ELF in-process args from the C compiler (CRT, `-L`, `--export-dynamic`).

## v0.1.1 (2026-08-18)

First tagged release of the current tree (`VERSION` 0.1.1). Includes work
landed after the April changelog draft.

### Type system
- Added `docs/type-system-status.md` (GitHub #3 audit): effects, rows,
  linear/refinement checkers, `@borrow`, GENFN borrow masks, exhaustiveness.

### Runtime / platforms
- Linux: shared io_uring, File `auto_await`, GCC-safe SJLJ `longjmp` attributes.
- macOS: kqueue runtime, MoltenVK, Metal i32/f32 GPU kernels.
- Windows: UDP ABI and CI/DIA fixes (see recent `master` history).

### Distribution
- Fedora Copr, AUR `yona-bin`, and Launchpad PPA publish jobs after GitHub Release.
- Maintainer setup: `dist/RELEASING.md`.
- Windows WiX MSI + ZIP on release.

## v0.1.1-draft (2026-04-24)

Kept for history of the April packaging/linker notes.
- Added Fedora Copr, AUR `yona-bin`, and Launchpad PPA publish jobs after GitHub Release.
- Documented one-time maintainer setup in `dist/RELEASING.md`.
- Added Windows WiX v4 installer scaffold (`packaging/windows/`) with MSI build script.
- Extended release workflow with a Windows release job that publishes both ZIP and MSI artifacts.
- Kept Linux/macOS packaging aligned with precompiled runtime artifact shipping.

### Toolchain and Linker
- Modularized embedded linker configuration into `cmake/YonaInProcessLld.cmake`.
- Fixed Windows embedded-LLD libxml resolution by retargeting `LLVMWindowsManifest` to fetched `LibXml2`, avoiding mixed `.a`/`.lib` linkage.
- Added stricter release checks for linker-mode/runtime artifact smoke validation on Windows.

### Documentation
- Updated roadmap/todo status to reflect Windows installer scaffolding and release CI progress.
- Updated installation and architecture docs for current linker/runtime packaging flow.

## v0.1.0 (2025-04-06)

Initial release of the Yona compiler targeting LLVM.

### Language Features
- Newline-aware lexer with juxtaposition-based function application
- Pattern matching: integer, symbol, wildcard, head-tail, tuple, constructor, or-pattern, guards
- Algebraic data types (non-recursive flat, recursive heap-allocated)
- Traits with concrete/constrained instances, default methods, superclass constraints
- Module system with FQN-based imports, interface files (.yonai), cross-module generics
- Exception handling (raise/try/catch via setjmp/longjmp)
- Closures with env-passing convention and closure devirtualization
- String interpolation, do-blocks, pipe operators
- Generators/comprehensions for seq, set, dict with stream fusion

### Performance
- LLVM codegen with optimization levels O0-O3
- Stream fusion: chained comprehensions fused into single loops
- LTO: cross-module inlining of C runtime via llvm::Linker
- Closure devirtualization for known lambdas at HOF call sites
- fastcc calling convention for internal functions
- Branch prediction hints on hot runtime paths
- Benchmark results: list_map_filter at 1.0x C, tak 0.8x (faster than C)

### Data Structures
- Persistent Seq: flat array (<=32) + radix-balanced trie with head chain + tail buffer
- Persistent Dict: HAMT with splitmix64 hash, transient inserts (2.0x C)
- Persistent Set: HAMT-backed, sharing Dict infrastructure (2.1x C)

### Memory Management
- Atomic reference counting (RELAXED inc, ACQ_REL dec)
- Recursive destructors for all container types via heap_mask bitmasks
- Hybrid Perceus DUP/DROP (callee-owns for non-seq, callee-borrows for seq)
- Slab-based pool allocator (5 size classes)
- Arena allocation for non-escaping let-bound values
- Unique-owner optimization (in-place mutation when rc==1)
- io_uring buffer pinning for async I/O safety

### Standard Library (27 modules)
- **Pure Yona (12)**: Option, Result, List, Tuple, Range, Math, Pair, Bool, Test, Collection, Function, Http
- **C runtime (15)**: String, Encoding, Types, IO, File, Process, Random, Json, Crypto, Log, Net, Bytes, Time, Path, Format, Dict, Set, Regex

### I/O Architecture
- io_uring backend (Linux): raw syscalls, submit-and-return, async file/network I/O
- Thread pool with work-stealing for extern async functions
- Non-blocking Process module: spawn, readLine, readAll, wait, kill, writeStdin

### Tooling
- `yonac` compiler CLI with -O0 to -O3, --emit-ir, --emit-obj, debug symbols
- `yona` interactive REPL
- DWARF debug info generation
- Documentation system (doc comments extracted by gendocs.py)
- Benchmark suite with 11 benchmarks and C reference implementations
- CI/CD: GitHub Actions with Linux, macOS, Windows builds

### Testing
- 763 assertions across 75 test cases
- Codegen E2E fixtures (compile -> run -> check output)
- Multi-module linking tests
- Trait/generic cross-module tests
