# Changelog

## Unreleased

### Fixed

- Restored every reproducible build and quality gate: generated TextMate
  grammars are synchronized, Vulkan loader lookup compiles, macOS kqueue
  declarations agree, standalone interface fuzzing links its model dependency,
  and CI language-server smoke/report scripts use their tracked underscore
  names. Embedded LLD archives are now linked in dependency order so shared
  compiler consumers resolve the driver implementation.
- Parser error paths now retain ownership while recovering from malformed
  module names, function clauses, and `let` bindings. The ASan/LSan fuzz gate
  completes without the former leaks or non-progress timeout.
- Reformatted the C/C++ source tree with the project's Clang 22 formatter so
  the repository formatting contract is clean.

- CMake-built Yona tools now link the current build's generated Prelude object
  instead of an ignored, potentially stale `lib/Prelude.o`, so local builds do
  not mix obsolete `yona_rt_*` calls with the `YonaRuntime*` runtime archive.
  `yonac` applies the same active-sysroot precedence when called by the runner
  or directly, ahead of arbitrary module-path objects. Installed sysroots now
  ship and validate that artifact as well.
- Completed the Linux open-bug stabilization sweep. Literal lowering now
  validates Unicode and compares every supported scalar consistently across
  direct, alternative, tuple, constructor, record, exact-sequence, and
  head-tail patterns, including recursive composite heads and failed-match
  cleanup.
- Canonical interfaces now preserve structural types, zero-arity/effect rows,
  generic owner dependencies, recursive specialization identity, and stable
  scalar ABIs. `Std\Json` exposes precise recursive `Result`, `Option`, tuple,
  and sequence shapes, preventing returned parsed trees from being released by
  an enclosing case cleanup.
- Generated programs now balance aggregate, tuple, constructor, record,
  sequence, channel, ByteArray, root-result, exception, closure-capture, and
  nested-let ownership. Arena destruction recursively releases managed
  children, arena sequences initialize their complete flat-layout metadata,
  and branch merges normalize borrowed and owned provenance.
- Async file/network cancellation and submission failures release their owned
  contexts and buffers on Linux and macOS; native observational array contracts
  carry complete borrow masks and generated binary-I/O programs release their
  temporary buffers.
- The Yona runner now preserves the documented child `argv[0]`, forwards user
  arguments once, propagates child status, and removes temporary sources and
  executables on success and failure. Formatter detection tests retain their
  prerequisites while excluding only `clang-format`.
- Stdlib and fixture contracts were regenerated or corrected for Bool,
  Parallel, Channel, Time, Log, Http, Json, File, GPU, traits, streams, and
  effectful I/O. `Std\Parallel.pfor` now returns its documented processed-item
  count. The Linux debug build and complete CTest preset are green.
- Synchronous and io-completion extern declarations can record exact
  non-consuming parameter contracts with `borrow "01..."`; generated
  interfaces preserve those masks. Task-backed externs reject masks until they
  can pin arguments through completion. The JSON parse/stringify observers now
  release temporary caller-owned inputs correctly in generated programs.
- Raised heap exceptions now transfer an explicit owner through catches,
  rethrows, and async task groups; catch payload bindings are clause-scoped and
  remain valid when returned from a handler.
- The regenerated `Std\Gpu` interface preserves `drainMapFloatGpu`'s
  `FloatMapOp`, `Receiver FloatArray`, and `Sender FloatArray` parameters, so
  typed GPU channel pipelines compile through both Vulkan and CPU fallback
  paths.
- Linux runtime components again build after the source modularization: the
  io_uring cancellation signature preserves its const contract, and platform
  I/O and native stdlib consumers use the canonical context fields and local
  identifier spellings.
- Platform I/O declarations now retain C linkage for C++ consumers and share
  one `IoContext.h` ABI contract, so Linux and macOS headers compose without
  duplicate type definitions and the runtime registry links into C++ tests.
- Linux descriptor byte-I/O fallbacks allocate asynchronous contexts only
  after successful io_uring submission, eliminating the two contexts formerly
  leaked by blocking fallback reads and writes.
- Installed CMake consumers now place generated Yona executables in a dedicated
  `bin/` directory, and installed shared-library CLI tools use a relocatable
  sibling-library RPATH instead of loading an incompatible host library.
- Fixture-generated programs now link the exact Vulkan loader selected during
  configuration after the static runtime archive, preserving direct MoltenVK
  library identities and resolving Vulkan symbols in GPU conformance tests.
- `nullptr_t` uses now remain in the appropriate standard or AST namespace,
  restoring builds with libstdc++ 16 after the namespace-import cleanup.

### Changed

- `cmake --install` now exports only Yona's declared package surface; bundled
  FetchContent dependencies no longer leak their own incomplete install rules.

- Task and native-promise results now cross threads only through a copied
  `YonaTypeDescriptor`. Completion transfers the producer reference,
  standalone await transfers task ownership, grouped `awaitKeep` retains an
  independent caller reference, and task-group cleanup releases unobserved,
  errored, and GPU-fence results. Async calls, owned invocation contexts,
  `Std\Task.spawn`, and native GPU promises all require the same hidden result
  descriptor; cancellation completes with no fabricated payload.
- Terminal-size probing now returns `{0, 0}` when the platform cannot query
  standard output, rather than reading uninitialized Windows console metadata.

- Interface parsing and Prelude/type-import installation now belong to
  `semantics::InterfaceCatalog`. LLVM codegen consumes only canonical lowering
  metadata, while interface GENFN source is parsed by the Semantics-owned
  generic source service with explicit constructor metadata.
- Compiler, runner, typed-core, and generated-program tests now launch child
  processes through the same shell-free `executeProcess` API as production
  tooling. The API supports captured stdout/stderr, explicit stdin, and
  inherited-environment overrides; Windows no longer needs a force-included
  `popen` shim.
- `.yonai` files now use the sole canonical `InterfaceModule` reader and writer.
  Records begin with `MODULE`, functions and generic owners use local Yona keys,
  and trait implementations and generic Yona dependencies carry structural
  module/local identities. Codegen no longer parses interface text, recovers
  identities from generated symbols, or falls back to `.yona` source.
- Trait declarations and instances now carry only their complete type-parameter
  and instance-head vectors throughout the AST, semantic registry, and codegen.
  Singular duplicate fields and partial-head repair paths were removed.
- `CodegenSession` now owns each compilation's LLVM context, module, builder,
  target machine, diagnostics, error count, and derivation-strategy registry.
  `Codegen` consumes that owned session, and isolation tests verify that
  modules, diagnostics, and custom derivations cannot leak between compilations.
  Obsolete declarations for the removed `YonaRuntimeSetPut` and
  `YonaRuntimeDictionarySet` entry points are gone.
- `Std\Process.exec`, `execStatus`, `spawn`, `run`, and `execArgs` now take an
  executable plus an argument sequence (excluding `argv[0]`). Runtime process
  launch never interprets a command string through a shell; callers that want
  shell syntax must invoke a shell explicitly.
- Parser results now own their immutable source storage. `parseModule` and
  `parseExpression` return `ParsedModule`/`ParsedExpression` values containing a
  shared `SourceManager`, `SourceId`, and AST; lexer tokens and diagnostics
  carry `SourceRange` values, so caller buffers may be released immediately. The
  old stream-based parser entry points and source-location aliases were removed.
- `SemanticModel` is now the shared, source-backed semantic index for tooling.
  It owns strong `BindingId` values and exposes definitions, references,
  inferred types, effects, ownership, and diagnostics keyed by `SourceRange`.
  `yls` no longer owns a second lexical resolver or depends on TypedCore or LLVM
  codegen; TypedCore projects facts from the same model. Both analysis clients
  load Prelude and imported `.yonai` contracts through the Semantics-owned
  `InterfaceCatalog`.
- Function types now carry exactly one solver-owned `EffectRef`. The
  transitional `ERow` value-type payload, duplicate row projections, mirrored
  union-find bindings, and migration-only tests were removed; effect equality,
  open variables, joins, masks, printing, and schemes all use the effect solver
  directly.
- Type-checker imports now consume mandatory canonical type descriptors.
  `VAR(name)` is the only generic-variable form and `INT` always means `Int`;
  tag/linearity guessing was removed. ADT registration likewise requires an
  exact field shape for every constructor, so pattern inference no longer
  guesses fields from type parameters when metadata is absent.
- `yona_typed_ir` is now a compiled component with strong value identifiers,
  semantic type/effect/ownership facts, and explicit trivial, borrowed, owned,
  and transferred runtime ownership states. Its module/function builders enforce
  source, result, transfer, and linear-value invariants before backend lowering.
- The standard-library acronym spellings are canonicalized as `Std\Gpu` and
  `Std\Io`. GPU operation names now treat the acronym as a word (`mapGpu`,
  `reduceGpu`, `mapFloatGpu`, `reduceFloatGpu`, `mapReduceGraphGpu`,
  `mapFloatPinnedGpu`, and `drainMapFloatGpu`); module filenames, native
  symbols, interfaces, fixtures, tools, examples, and documentation use only the
  canonical names.
- The typed-core C adapter now exposes only canonical `YonaTypedCore...` types
  and functions with UpperCamelCase public fields. ABI version constants, the
  version getter, and the version field were removed. `.yonai` effect schemes
  are deterministic and unversioned; redundant nominal-return suffixes and bare
  linear markers were removed from the writer, reader, and checked-in
  interfaces.
- Windows CMake presets now fall back to `C:\Program Files\LLVM` when neither
  `LLVM_INSTALL_PREFIX` nor `LLVM_DIR` is configured, while preserving explicit
  cache and environment overrides. Fresh Windows configurations also select
  Clang from that complete LLVM tree unless a compiler is explicitly chosen.
- Runtime components now compile once into the canonical `yona_runtime` static
  archive. CLI, REPL, tests, installers, distro packages, Homebrew, release
  archives, and Docker images consume that archive; runtime sources and loose
  intermediate objects are no longer distributed or rebuilt as a fallback.
  Core, Collections, Concurrency, Platform I/O, Codecs, GPU, and Stdlib native
  entry points are independent object components with explicit source lists.
  Runtime sources, public headers, and generated shaders now live in the
  component-owned `src/Runtime/*`, `include/yona/Runtime/*`, and
  `src/Runtime/Generated/` trees with canonical UpperCamelCase names.
- `Std\Gpu` discovery now exposes availability and physical-device count only;
  the synthetic capability-version function and native getter were removed. Vulkan
  API-version fields remain internal capability inputs for device selection.
- Sets and dictionaries now use one canonical HAMT representation owned by the
  runtime Collections component. The flat-set tag, inert indexed builders,
  conversion shims, and unused chunked-sequence tag were removed.
- Vulkan filtering now always performs mark, prefix, and scatter on the GPU; the
  retired host-prefix diagnostic branch and its environment switch were removed.

### Fixed

- Public source-management and refinement-analysis headers now use macro-safe
  `numeric_limits` calls, so they compile when Windows `min` and `max` macros
  are already visible.
- `ThreadPool::submit_async<void>` now invokes void tasks before fulfilling
  their promise, so the documented `future<void>` form compiles and propagates
  task exceptions normally.
- The unused `first_defined_optional<T>` declaration was removed from the
  public syntax utilities; it previously accepted arbitrary template arguments
  but provided a linkable definition only for `std::any`.
- The LSP JSON parser now rejects non-whitespace after the first complete JSON
  value while continuing to accept trailing whitespace. Successful parses
  clear stale caller-provided error text.
- Packaged interfaces now carry a mandatory module identity, local function
  keys, structural cross-module references, and validated constructor metadata.
  In particular, `Std\\Json` records its actual maximum constructor arity, and
  every checked-in interface round-trips through the canonical reader and writer
  byte-for-byte.
- Shared semantic indexing no longer invokes an empty constructor-catalog
  callback when visiting case patterns; constructor and linear-pattern inputs
  now remain safe for both `yls` and TypedCore consumers.
- Package-qualified calls such as `Std\Gpu::available ()` now parse into the
  canonical `FqnExpr`/`ModuleCall` application instead of treating `::` as
  sequence cons. Accelerator diagnostics recognize that canonical `Std\Gpu`
  binding rather than the retired all-caps module spelling.
- Linear values consumed inside nested case expressions now propagate their
  consumed state through every enclosing case when all branches agree. This
  removes the false E0602 leak for both endpoints returned by
  `Std\Gpu.gpuFloatChannel`, while mixed branch consumption still emits E0601.
- `Std\Gpu.gpuFloatChannel` now has a concrete `FloatArray` endpoint type and
  constructs its channel with the RC payload descriptor, so imported calls lower
  without an unconstrained channel type and retain buffered arrays.
- `yonac` and `yona-repl` now pass exact argument vectors to child processes,
  eliminating shell interpretation of user-controlled paths. Generated-program
  tests cover literal shell metacharacters and captured output.
- Long left-associated `++` expressions now type-check and lower iteratively,
  preventing the Windows Debug stack overflow in `foundation_Traits_test`.
- Windows-generated doctest executables now embed an explicit `asInvoker`
  manifest, preventing installer detection from spuriously requiring elevation.
- Windows file-line iterators now preserve the native-state finalizer slot,
  preventing teardown crashes in iterator fixtures.
- Compiler tests now resolve the Prelude from the configured repository path and
  normalize generated-interface newlines, so they are independent of the CTest
  working directory and Windows CRLF translation.
- LLVM target machines are now owned by each code-generation session and are
  released deterministically when that session ends, eliminating the repeated
  compiler-session leak under LSan.

### Added

- Trait-aware `yls` symbols, navigation, completion, semantic tokens, and safe
  instance-explanation actions. The public `tree-sitter-yona` grammar and
  `zed-yona` extension provide first-class Zed support backed by the same
  `yls --stdio` server as VS Code.
- The Prelude now provides coherent foundational `Eq`, `Ord`, `Hash`, `Show`,
  `Array`, `Sized`, `Iterable`, `Foldable`, `Semigroup`, `Monoid`, `From`,
  `TryFrom`, `Parse`, `Send`, and `Shareable` traits. Operators dispatch
  statically through `Eq`/`Ord`; immutable collections receive constrained
  lifted instances; concurrency markers are checked and erased. Native arrays
  may be moved through `Send`, while synchronized channel endpoints may also be
  shared across spawned tasks.
- `Std\Convert`, `Std\Iterator`, and `Std\TraitLaws` provide witness-directed
  structured conversions, single-pass native collection adapters, and reusable
  executable law suites with rendered counterexamples. The recursive stdlib
  conformance manifest now covers all 40 public modules, with directly
  addressable `stdlib-network` and `stdlib-gpu` CTest labels.
- `Std\Test` now provides pure `TestResult`, `TestCase`, and `TestReport` ADTs
  plus `pass`, `fail`, `check`, `equalBy`, `testCase`, `run`, and deterministic
  `render`. The initial Yona-native framework fixture covers full and empty
  reports without stopping after the first failure.
- `Std\File.openFile` now materializes the documented `Linear FileHandle`
  wrapper at runtime. Imported calls can be consumed with
  `case Linear file -> …`; the interface retains the nested payload as
  `LINEAR(ADT(FileHandle))`.
- `export type T opaque` exports an ADT's nominal type without exposing its
  constructors. Public smart constructors and observers remain callable,
  including generic exports that use the hidden representation internally.
- `--Wincomplete-patterns` (also enabled by `--Wall`) reports missing
  constructors in finite ADT `case` expressions. A wildcard arm satisfies the
  check; guarded arms do not. `--Werror` now correctly makes these diagnostics
  fail compilation.
- `yonac --require-effect-free` also rejects incomplete matches over registered
  finite ADTs with E0203. This applies to expression programs and module
  function bodies; wildcard arms satisfy coverage and guarded arms do not.
- `--Woverlapping-patterns` now reports definitely unreachable case arms (and
  `--Werror` promotes it). `--require-effect-free` also checks `Bool` cases,
  accepts direct structural recursion through an unguarded constructor match,
  and rejects unproven direct or mutual recursion with E0203.
- Overlap diagnostics now use a compiler-owned structural pattern analysis:
  aliases, alternatives, nested constructors, tuples, exact and head–tail
  sequences, and scalar literals are compared soundly; complete closed root
  `Bool` and ADT families also make a later catch-all arm unreachable. `yls`
  publishes the same warning range, so VS Code and Zed receive it through LSP.
- CI and tagged releases now build native Windows ARM64 artifacts alongside
  Windows x64 (`.zip` and `.msi`), and explicitly build the Apple Silicon
  `macos-arm64` archive. Intel macOS is not part of this hosted release matrix.

### Changed

- FetchContent now uses doctest 2.5.3 and CLI11 2.7.2; the Windows embedded-LLD
  fallback uses the latest tagged libxml2 release, 2.15.3.
- Constructor patterns now preserve a declared tuple field as one field:
  `Box ((first, second))` matches `type Box = Box (Int, Int)`, while the spread
  form `Box (first, second)` receives a concrete shape correction. E0100 now
  reports constructor, field, declared shape, parsed shape, and one focused fix
  without duplicate diagnostics.
- Empty `{}` inside a string remains literal for placeholder APIs such as
  `Std\Format.format`; `{{` and `}}` escape literal braces adjacent to normal
  `{name}` / `{(expression)}` interpolation.
- Parser recovery after an invalid case-pattern binding now skips the malformed
  arm rather than emitting repeated follow-on E0301 diagnostics for its arrow
  and body.
- Repeated `yonac -I <path>` options now each consume one module directory,
  rather than swallowing the input source as another include path.
- `yonac --require-effect-free` establishes the #5 effect-freedom gate: it
  accepts only closed empty effect rows and exhaustive registered finite-ADT
  matches (E0203 otherwise), plus direct and mutual local recursion proved by
  conservative structural size-change analysis with lexicographic parameters.
  Exported empty rows are preserved as `.yonai` `effects -`; interfaces without
  a row remain unknown and are rejected by the gate. It deliberately remains
  conservative: it does not prove general termination, numeric measures,
  opaque/higher-order termination, or arbitrary non-ADT coverage; overlap
  warnings only cover arms definitely unreachable after an earlier unguarded
  arm.
- `yonac` now exits non-zero on refinement **E0500** and linearity **E0600** /
  **E0601** (expression programs and modules). Previously those diagnostics were
  emitted on expressions and ignored, and module compile skipped both checkers.
  Opt out with `--Wno-refinement` or `--Wno-linear`.
- Linear resource leaks emit **E0602** (`-Wlinear-leak`, on by default) instead
  of `-Wunhandled-effect`. Suppress with `--Wno-linear-leak`; `--Werror` still
  promotes them to errors.

### Fixed

- Branch-transfer cleanup now queues asymmetric sequence drops until function
  CFG construction completes, then proves dominance before inserting them. This
  prevents native Windows LLVM 23 from analyzing detached or unterminated `if`,
  `case`, and `try/catch` successors while compiling the bundled tools.
- Windows x64 and ARM64 builds now bundle pinned PCRE2 10.47 beside the runtime
  archive, so every released `Std\Regex` interface has its matching C runtime
  symbols without vcpkg or a host PCRE2 installation.
- In-process LLD now receives the bundled PCRE2 archive as a raw argv path; the
  external compiler path remains shell-quoted. This keeps the native Windows
  linker-mode validation independent of path quoting.
- Fixture links that fail in doctest now rerun once with linker stderr visible,
  and `yonac` prints an LLVM stack trace on a native crash, so CI reports the
  concrete linker failure or compiler frame rather than only `LINK_ERROR` or
  `Access violation`.
- Deferred-function ABI refinement now atomically replaces the provisional LLVM
  function, migrates every compiler reference, and erases the provisional
  declaration. Nested lowering therefore keeps valid references while the module
  contains exactly one canonical source function.
- Imported-GENFN isolation now migrates every active compiler-cache and scope
  snapshot when a deferred function refines its provisional return ABI. This
  prevents nested generic sequence equality from restoring an erased LLVM
  `Function*` and crashing in LLVM's function-type validation.
- Native Windows builds no longer redefine `WIN32_LEAN_AND_MEAN` after CMake has
  already supplied it globally, keeping the new ARM64 and x64 CI logs free of
  avoidable compiler warnings.
- Effect inference now uses a dedicated lossless constraint solver: independent
  higher-order callback rows form a true union rather than dropping or equating
  a later source; handlers mask symbolically; recursive bodies use least-derived
  cells; and the canonical `effectscheme` field preserves all arrow effects,
  masks, and shared sources across `.yonai` imports. Curried partial
  applications are pure until their final source argument, so E0202 no longer
  duplicates the same operation at every stage. Wildcard imports shadow
  same-named local module functions during dependency-SCC discovery, and
  package-qualified selective imports preserve the package/module separator when
  locating interfaces.
- Windows CMake now provides the Zlib, zstd, and DIA SDK targets required by the
  official LLVM package _before_ loading `LLVMConfig.cmake`. It discovers normal
  installations first and uses the project's pinned CMake source fallback only
  when dependency fetching is enabled; no external package manager is required.
- The Windows-LLVM prerequisite contract now loads its isolated mock package
  directly instead of inheriting a host `LLVM_DIR`, and injects its test target
  architecture rather than mutating an already-selected Visual Studio generator.
  Code generation now checks whether a basic block ends in a terminator before
  querying it, keeping derived Prelude functions valid with LLVM 23 on native
  Windows and Apple Silicon.
- Windows async worker exception boundaries now use the shared target-aware SJLJ
  abstraction, so native ARM64 builds do not rely on the unsupported x64-only
  compiler builtin.
- Fresh-clone test builds now generate their Prelude object in the build tree,
  track every standard-library fixture source, and enforce a source/expected
  contract for every codegen fixture. LSP fixture navigation resolves the
  repository `lib` directory independent of CTest's working directory.
- `TypeChecker.cpp` now directly includes the standard algorithm facilities it
  uses, fixing strict macOS Clang builds.
- Generic collection identity now survives empty-first nested literals, Set/Dict
  runtime interfaces, recursive List filtering/sorting, explicit imported
  trait-method callbacks, and higher-order law suites. Contextually typed `{}`
  returns allocate the inferred Dict kind, and consuming `::` preserves
  multi-use sequence bindings.
- Generic imported functions and trait instances retain complete nested type
  identities across sibling specialization, collection extraction, callbacks,
  and LLVM optimization. This prevents Float/Bool ABI mismatches, invalid lifted
  ADT dispatch, declaration use-list corruption, and heap-value iterator
  ownership failures exposed by the foundational law matrix.
- API generation now joins multiline Yona arrow signatures, so published
  `Std\TraitLaws` contracts include every callback, sample, and result type.
- The formatting script now fails fast with an actionable diagnostic when
  `clang-format` is unavailable instead of printing a false success message.
- Generic higher-order calls now compile deferred lambda arguments using the
  callee signature instantiated at the call site. In particular, an unannotated
  `String` comparator passed to imported `Std\Test.equalBy` now uses text
  equality rather than integer pointer comparison.
- Recursive ownership analysis now sees through import wrappers, distinguishes
  closure call targets from forwarded values, and computes recursive borrow
  contracts to a fixed point. This removes the `Std\Test` use-after-free,
  nested-case dominance failures, and the unnecessary recursive `foldl` DUP.
- ADT patterns preserve tuple, parameterized collection, named-record, and
  callable field metadata through typechecking, codegen, and `.yonai` fallback;
  failed speculative private-GENFN recompilation no longer leaks diagnostics.
- Runtime sequence users now honor the canonical two-word header in
  `Std\String.join`, `Std\Format.format`, HTTP URL parsing, and all platform
  `listDir` implementations. URL parsing uses an ownership-aware internal ADT.
- Heap-valued set extraction and union/intersection/difference now retain and
  release elements correctly, propagate HAMT flags, and transfer the consumed
  left operand exactly once from generated code.
- Project-owned LLVM 22 deprecations, incomplete enum switches, and a nested
  comment warning have been removed. The remaining doctest/libstdc++ warning is
  tracked in `docs/todo-list.md`.
- Module interfaces now serialize bare `Seq`, `Set`, and `Dict` extern type
  annotations as collection ABI tags rather than as nominal ADTs. This keeps
  imported `Std\Regex.find`, `findAll`, and `split` results usable as sequences.

## v0.1.6 (2026-08-21)

### Fixed

- VS Code extension `npm test` compiles TypeScript first. `out/` is gitignored
  and `npm run lint` is `--noEmit`, so a clean clone (and CI) no longer fails
  with `Cannot find module …/out/test/run.js`.
- Windows `yls-yona` stdio smoke: `Std\Io.readExact` / `writeBytes` now
  `_setmode` CRT stdin/stdout to binary (same as C++ `yls`). Text-mode CRLF
  translation was swallowing `\r` so the server never saw `\r\n\r\n` and exited
  before an LSP header. Retag after v0.1.5 Release died on that smoke before
  Marketplace / Open VSX / GitHub Release ran.
- CMake `Collect test results` no longer exits 1 under `set -e` when no
  `doctest*.xml` exists (smoke failure used to hide the real error).

## v0.1.5 (2026-08-21)

### Added

- **`yls-yona` transport slice** — Yona-written LSP stdio server
  (`tools/yls/main.yona`, CMake `yls-yona`). Speaks `Content-Length` framing via
  `Std\Io.readExact` / `writeBytes`, JSON-RPC via
  `Std\Json.parse`/`stringify`/`get`, and `initialize` / `initialized` /
  `shutdown` / `exit` / `textDocument/didOpen`. Unknown methods return JSON
  null. Capabilities are transport-only (`textDocumentSync`); hover and
  definition still need C++ `yls`. The editor default is unchanged. Smoke:
  `scripts/ci/smoke_yls_yona.py`.
- **`Std\Json` recursive ADT** — `JsonNull` / `JsonBool` / `JsonInt` /
  `JsonFloat` / `JsonString` / `JsonArray` / `JsonObject` (objects keep member
  order). `parse` returns `Result Json String`; `stringify` emits compact JSON.
  C ABI in `include/yona/Runtime/Codecs/Json.h` (depth 64, 16 MiB cap, `\u`
  surrogate pairs). Scalar helpers (`stringifyString`, …) stay.
- **`Std\Io.readExact(fd, n)`** — pipe-safe stream `read()` loop (not
  seek/`pread`). Returns `Ok` bytes or `Err` (`unexpected eof` /
  `negative count`). Accepts a raw fd (`stdinFd`) or `FileHandle`. Needed for
  LSP `Content-Length` framing on stdin.
- **`Std\Utf16`** — `offsetToLine`, `offsetToCharacter`, `positionToOffset`.
  Matches the C++ `yls` UTF-16 mapper (CRLF as one break, non-BMP as two units).
  Documented C ABI: `include/yona/Runtime/Codecs/Utf16.h`.
- **Typed-core C adapter** (`include/yona/TypedCore/Abi.h`): in-process query of
  resolved names, inferred types, effect rows, linearity, and source spans with
  no LLVM types in the public header. Example non-LLVM backend
  `YonaTypedCorePrettyPrint` dumps a deterministic textual summary.
  `yonac --emit-typed-core` prints that dump and exits without LLVM codegen.
  Architecture: `docs/typed-core.md`.
- `yona` is a Yona-written runner (shebang `#!/usr/bin/env yona`): compile a
  file, stdin, or `-e` expression via sibling `yonac`, then exec the result. No
  arguments on a TTY starts the C++ REPL (`yona-repl`).
- `yonac -` reads source from stdin (compile only).
- `Std\Process.getArgs`, `executablePath`, `yonaVersion`, `tempDir`, `tempFile`,
  `run`, and `execArgs`. `Std\Io.readStdin` reads stdin to EOF.
- CMake `yona_add_executable` (`cmake/YonaTools.cmake`) compiles Yona tools
  during the build; `tools/yona` is the first consumer.
- **`yls` language server** and a VS Code / Cursor extension at
  `editors/vscode`. `yls --stdio` publishes parse/type/refinement/linearity
  diagnostics, hover, definition (including imported names and FQN calls to the
  source `.yona` / `.yonai`), references, document highlight, completion,
  symbols, semantic tokens, rename (local aliases only for imports), signature
  help, inlay hints, call hierarchy, and explain code actions. The TextMate
  grammar is shared with the site (`site/grammars/yona.tmLanguage.json`).
  Protocol types live in `include/yona/Lsp/Protocol.h` (no LLVM headers), while
  binding identity lives in `include/yona/Semantics/SemanticModel.h`. `yls`
  treats `#`/`##` headers like `yonac` (`check_module`), indexes function
  parameters and other patterns for rename/hover, decodes JSON `\uXXXX`
  (including surrogate pairs), looks behind a space for juxtaposition signature
  help, uses `initialize` workspace roots in module search, and re-analyzes open
  buffers on `workspace/didChangeWatchedFiles`. Hover and related queries treat
  LSP ranges as end-exclusive. `yonac` and `yls` share `is_module_source` in
  `include/yona/Syntax/ModuleSource.h`. Windows stdio is binary so
  `Content-Length` stays in sync. The extension setting is `yona.trace.server`
  (vscode-languageclient). On parse failure, `yls` keeps the original parse
  diagnostics and recovers a partial AST (suffixes such as ` 0` / ` in 0` /
  ` end` / ` then 0 else 0`, or truncating the last token/line) so hover,
  definition, highlight, and completion still work on the prefix that parsed.
  Incomplete `if` no longer crashes the server (`IfExpr` allows a null then/else
  while the parser reports E0301). Go-to-definition on `Module.fn` works in the
  defining file, resolves `import Pkg\\Mod as M in M.fn`, and does not treat a
  later local binding as the imported name.
- VS Code extension local VSIX packaging (`npm run vsix` / `npm run package` in
  `editors/vscode`, `@vscode/vsce` + `ovsx`). PR/push CI builds the `.vsix`
  artifact and does not publish.
- Release CI publishes the VS Code extension to the Visual Studio Marketplace
  and Open VSX on `v*` tags (`vsce` / `ovsx publish --packagePath`, publisher
  `yona-lang`, secrets `VSCE_PAT` and `OVSX_PAT`). Both jobs still no-op if
  their secret is unset so a missing token does not fail the release.

### Fixed

- Imported `Std\Json.get` / `asString` / `asInt` from expression programs no
  longer fail LLVM verify (`ptr` vs `i64`) or reject `JsonObject` patterns.
  GENFN call sites now keep `return_subtypes`; capitalized names in patterns are
  constructors even when the `.yonai` is loaded after parse. Remonomorphizing a
  GENFN no longer lets the importer's `length` / `get` (e.g.
  `import length from Std\String`) shadow Prelude Array methods used by
  `Std\Json.getPair`, which made `jsonGet` return `None` on multi-key objects in
  `yls-yona`. `yls-yona` uses those helpers instead of raw-text field scans.
- GENFN name isolation no longer hides sibling functions in the defining module.
  Remonomorphizing `Std\Stream.naturals` can see `range` again
  (`sum (take 10 naturals)`). Isolation now copies the mangled name before
  clearing importer `extern_functions`, so a CAF path cannot dangle and drop the
  module prefix. Importer aliases such as `import length from Std\String` still
  cannot shadow Prelude Array `length` inside `Std\Json.getPair`.
- `Std\String.fromChars` now reads sequence elements (`YonaRuntimeSequenceGet`)
  instead of the flat header word, so `[123]` is `{` and not a leading NUL.
- Nested `try` whose inner `catch` re-raises now reaches the outer handler
  (`try (try raise 1 catch _ -> raise 2 end) catch _ -> 3 end` prints `3`, not
  `()`). `codegen_try_catch` no longer returns the try-body value when every
  catch arm terminates.
- `try`/`catch` now consumes its closing `end`, so a nested `try` no longer
  steals `do`/`case`/`let` terminators. Missing `end` or `catch` is a parse
  error (previously leftover `end` was ignored, and `try 42 end` SIGSEGV'd).
  Trailing tokens after an expression are rejected. Multiple catch arms
  (`catch p -> e` / one `catch` with several clauses) are kept.
- Nested `let`, `perform`/`raise` as a let-binding RHS, and `with` bodies no
  longer swallow a terminator `in` as membership.
  `let y = let z = 1 in z * 2 in y` and
  `let plan = \() -> perform Fs.read "x" in plan ()` parse again.
  `2 in [1, 2, 3]` and `if 2 in xs then …` still work. Parenthesize membership
  in a let-binding RHS that is itself a `let`/`if`/`lambda`/
  `perform`/`raise`/`with`: `then (2 in xs)`, `let z = 1 in (2 in xs)`.
- `perform State.get ()` is a 0-argument operation (the `()` is not a payload).
  Recursive functions capture body effects as a closed row (`!{State.get}`)
  instead of an unsound open rest or an infinite type.
- `with` expression parser no longer crashes: `parse_expr_until_in()` stops
  before the `in` keyword when parsing the resource, and null checks prevent
  constructing `WithExpr` with a null body (`with fd = 0 in 42`).
- Imported `Std\List.isEmpty` (and other GENFNs) inside a `let`-bound function
  whose parameter is named `rest` no longer fails with
  `undefined function 'cmp'`. Case-pattern bindings are visible to free-var
  analysis, and sibling GENFNs are registered without the caller's locals.
  Sibling registration also skips names already imported from another module
  (`drop` from `Std\String` is not replaced by `Std\List.drop`).
- `AFN` calls that are the body of a `let`-bound function are awaited before
  return (`let f cmd = exec cmd in f "echo x"` prints `x`, not a Promise
  pointer).
- Invalid UTF-8 (e.g. `printf '\xc0\x15@'`) is a lex error instead of a `yonac`
  busy-loop.
- CI linker-mode validation tracks `scripts/ci/smoke.yona` (and
  `scripts/**/*.yona` is no longer gitignored). `yonac -e` is gone, so the smoke
  used a file that never reached the checkout.
- Windows `yona` / `yonac` doctest cases pass executable paths and arguments
  directly, so module separators and paths are preserved without command-shell
  quoting. `stdlib_process` checks `PATH` instead of `HOME` (unset on Windows
  CI), and `Std\Path` treats `\` as a separator on Windows so the runner can
  find sibling `yonac`.

### Changed

- The LLVM compiler repository is now
  [yona-lang/yona](https://github.com/yona-lang/yona) (same stars and URL as the
  former GraalVM tree). The GraalVM/Truffle implementation, its issues, wiki
  snapshot, and 0.8.x releases are archived at
  [yona-lang/yona-graalvm](https://github.com/yona-lang/yona-graalvm).
  `yona-lang/yonac-llvm` is an archived pointer.
- **Breaking:** `yonac -e` / `--expression` is removed. One-liners are
  `yona -e '…'`. Inspect IR with `yonac --emit-ir -` or a file.

## v0.1.4 (2026-08-20)

### Fixed

- `let f x = if … else … in body` parses again: `if` then/else no longer consume
  the let-closing `in` as membership (`InExpr`). `2 in [1, 2, 3]` and
  `if 2 in xs then …` still work. Parenthesize membership in a then/else that is
  itself a let-binding RHS: `then (2 in xs)`.
- HAMT set/dict destructors now `rc_dec` heap keys and values
  (`KEY_HEAP`/`VAL_HEAP`), so `{[1], [2]}` and dicts of seqs no longer leak
  inner collections when the map is dropped.
- Applying `Std\Stream.map` (or `toSeq`) to a `Seq` is a compile-time **E0100**
  instead of a runtime SIGSEGV. `.yonai` `SEQ` vs `ADT` tags are now used at
  import so a sequence is not accepted where a Stream is required
  (`toSeq (map length ["ab", "abc"])`). Use `fromSeq`.
- Remonomorphizing `Std\Stream.map` (and other lazy ADT GENFNs) no longer
  SIGSEGVs: the extracted function is detached from the temporary parse module
  so parent walks cannot follow a freed `ModuleDecl`. Imported `length` as a
  value works on Stream (`toSeq (map length (fromSeq xs))`).
- Compiling a module that exports a wrapper around a private helper no longer
  prints a spurious **E0104** while filling `.yonai` FN effect rows
  (`populate_interface_effect_rows` uses sibling-aware `check_module`).
- An imported function used as a first-class value (e.g. `map length xs`) now
  materializes a closure instead of passing a null operand to the HOF.
- Exported functions that call unexported module helpers remonomorphize
  correctly (`doubledSquare` → `50`). Helper source is emitted as GENFN (not a
  public FN). Failed remonomorphize falls back to the precompiled extern;
  `yonac` exits 1 if codegen reported errors instead of linking a wrong binary
  with exit 0.
- Sequence operators `:>` (append), `--` (remove elements of the right from the
  left), and `in` (membership on seq/set, key test on dict) now parse and
  compile. `:>` already type-checked; it was missing codegen (`seq_snoc`). `--`
  and `in` were lexed but dropped by the Pratt parser (E0301 inside parens; bare
  expressions silently kept only the left operand).
- Top-level print of nested sequences (and other heap elements in a Seq) no
  longer dumps raw addresses: `[[1, 2], [3]]` prints as `[[1, 2], [3]]`. The seq
  printer consults `heap_flag` and dispatches on the RC type tag.
- Set/dict print no longer dumps nested heap values as integers: `{[1, 2]}` and
  `{1: [10, 20]}` print the inner sequences. HAMT-backed sets keep an `IS_SET`
  tag bit so `[{1}, {2}]` prints as sets, not `{k: 1}` dicts.
- Printing a tuple that contains a Seq (or other pointer-typed slot) is no
  longer an LLVM verifier failure: i64 tuple slots are unboxed to the type the
  `YonaRuntimePrint...` helpers expect (`print_tuple_int_seq`).

### Added

- Applying a function whose body performs an effect with no covering `handle` is
  a compile error **E0202** (points at the introducing `perform`, note at the
  call). Direct unhandled `perform` is still `-Wunhandled-effect`.
- Function arrows unify effect rows (closed sets + open rest). Higher-order
  `apply f x = f x` propagates the argument's effects; wrapping
  `let g = \() -> f ()` unions into `g`; a `handle` subtracts covered ops from
  the enclosing row. Types pretty-print as `(a -> !{Fs.read} b)`. Exported
  functions write closed rows on `.yonai` `FN` lines (`effects Fs.read` or
  `effects Fs.read,Net.post`); importers restore them so unhandled apply is
  **E0202** and a covering `handle` is not. FN lines without `effects` stay
  fresh type vars (stdlib unchanged). Open HOF rows write `effects | hof` so
  `apply f x = f x` still propagates the argument's effects after import. Module
  compile typechecks siblings as a unit so `wrap = \() -> readSecret ()` records
  `readSecret`'s row on `wrap`.

### Documentation

- Public Yona docs live in-repo at `site/` (Astro Starlight) and deploy to
  Cloudflare Pages via `.github/workflows/docs-site.yml`. Preview locally with
  `cd site && pnpm dev`.
- Docs site traffic is measured with cookieless Plausible at
  `plausible.kiket.dev` (`data-domain` `yona-lang.org`).
- Docs examples drop dummy trailing `0` and single-expression `do` wrappers.
  `let … in do` is valid: `let` binds (and may parallelize), `do` sequences.
- Style page: `let` and `do` have different semantics; combining them is
  idiomatic when you need both. The anti-pattern is `let _ = effect`.
- Stdlib reference no longer shows the “generated, do not edit” maintainer note
  on the public pages.
- Stdlib API pages put the full `name : T1 -> T2 -> R` signature in the heading
  (from source annotations or `.yonai` types), not a bare name or a parameter
  list.
- Function docs use `name pats = body` and juxtaposition application; the
  invented `name(x, y) ->` form and `f(x, y)` as a two-arg call are gone.
- `Std\Parallel.pmap` example is a fenced code block on the stdlib page.

### GPU / Std\Gpu

- `mapGpu` / `reduceGpu` (Int `Buffer`) and `mapFloatGpu` / `reduceFloatGpu`
  (`FloatArray`) via fixed kernel ADTs (`IntMapOp`, `FloatMapOp`, …).
- `mapReduceGraphGpu`: one Vulkan submit for map→…→reduce with
  `VK_KHR_synchronization2` barriers and timeline wait when enabled
  (`YONA_GPU_VULKAN_GRAPH` / `YONA_GPU_VULKAN_COMPUTE`). Stages are Add, Mul,
  and Square (each a real compute shader; unknown tags refuse the GPU path).
- Async float fence waiter: short-timeout poll; task-group cancel completes the
  promise with **-887** early and discards host writeback while GPU resources
  drain.
- `PinnedFloats`: prefers Vulkan host-visible mapped memory; malloc fallback;
  `YONA_GPU_PINNED_HOST_MALLOC=1` forces malloc. `pinnedBackend`,
  `mapFloatPinnedGpu`. CPU↔GPU pipelines: `gpuFloatChannel` /
  `drainMapFloatGpu`.
- Typed GPU failures: `GpuIssue`, `checkGpu`, `withGpuIssue` from `Std\Gpu`
  (Result-style). `raiseGpu` / `withGpuFallback` `perform Gpu.*`; GENFN
  remonomorphization inside a user `handle` binds the caller's clauses (effect
  rows on `.yonai`; not a C++ name list). Module-export compile of those helpers
  emits `:UnhandledEffect` if invoked with no handler. Direct use-site
  `perform Gpu.oom` / `deviceLost` / `fail` still works.
- Transparent lowering: inline `Std\IntArray` / `Std\FloatArray` `map` /
  `filter` / `foldl` in the fixed kernel library compile to the Std\Gpu ABI
  (`x + k`, `x - k`, `0 - x`, `x * k`, `x * x`, `x > k`, `x < k`, sum, float
  scale/sum). `filterLessThan` and `mapSquare` use real Vulkan shaders when
  compute is enabled (same env as filter / mapMul).
  `yonac --no-accelerator-lowering` keeps host closures.
  `yonac --strict-accelerator` errors (**E0700**) on unlowerable lambdas (full
  arbitrary-lambda SPIR-V still deferred).
- Bench: `gpu_filter_lt_hot`, `gpu_map_square_hot`, `gpu_pinned_scale_hot` in
  `run_gpu_compare.py`.
- `reduceFloatGpu` uses a real GPU block-reduce
  (`src/Runtime/Generated/Float64Reduce.comp` /
  `src/Runtime/Generated/Float32Reduce.comp`) when the Vulkan device is ready,
  else CPU.
- Bench: `bench/accelerators/gpu_pinned_scale_hot.yona` in `run_gpu_compare.py`.

### Type system

- **Effect-row inference (GitHub #8):** function arrows carry latent
  `!{Effect.op}` rows; `handle` subtracts covered ops; `.yonai` emits/parses
  `effects Op1,Op2` and open HOF tails (`effects |r0 0:|r0`). Applying an
  effectful function without a covering handler is **E0202**, pointed at the
  introducing `perform` with a note at the call. Higher-order functions keep an
  open `|r` rest (`\f x -> f x`); recursive functions take the least fixed point
  of `r ~ !{L | r}` instead of an infinite type. See `docs/effects.md`.
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

Packaging-only release so the GitHub Release workflow can finish Homebrew and
Copr.

- Homebrew: tap push uses `HOMEBREW_TAP_TOKEN` or `HOMEBREW_TAP_SSH_KEY` (v0.1.2
  had neither).
- Copr/RPM: install `libyona_lib.so` and strip the mock BUILD-dir RUNPATH
  (Fedora `check-rpaths`).
- Linux packaging/CI: `-DYONA_LINK_STATIC_CLI=ON -DCMAKE_SKIP_BUILD_RPATH=ON`.

## v0.1.2 (2026-08-18)

Packaging and in-process LLD so distro and CI builds link without FetchContent
or clang-driver flags passed to raw `ld.lld`.

### Distribution

- Prefer system CLI11, doctest, libxml2, and LLD headers
  (`-DYONA_FETCH_DEPS=OFF` in packaging).
- Debian: `libcli11-dev`, `liblld-dev`, `libpolly-dev`.
- Homebrew source formula in `akovari/homebrew-tap`.

### Linker

- Ubuntu: Polly package plus every `LLVM_TARGETS_TO_BUILD` for embedded LLD.
- macOS: `ld64.lld` `-arch` / `-platform_version` / `-syslibroot` before inputs.
- Linux: ELF in-process args from the C compiler (CRT, `-L`,
  `--export-dynamic`).

## v0.1.1 (2026-08-18)

First tagged release of the current tree (`VERSION` 0.1.1). Includes work landed
after the April changelog draft.

### Type system

- Added `docs/type-system-status.md` (GitHub #3 audit): effects, rows,
  linear/refinement checkers, `@borrow`, GENFN borrow masks, exhaustiveness.

### Runtime / platforms

- Linux: shared io_uring, File `auto_await`, GCC-safe SJLJ `longjmp` attributes.
- macOS: kqueue runtime, MoltenVK, Metal i32/f32 GPU kernels.
- Windows: UDP ABI and CI/DIA fixes (see recent `master` history).

### Distribution

- Fedora Copr, AUR `yona-bin`, and Launchpad PPA publish jobs after GitHub
  Release.
- Maintainer setup: `dist/RELEASING.md`.
- Windows WiX MSI + ZIP on release.

## v0.1.1-draft (2026-04-24)

Kept for history of the April packaging/linker notes.

- Added Fedora Copr, AUR `yona-bin`, and Launchpad PPA publish jobs after GitHub
  Release.
- Documented one-time maintainer setup in `dist/RELEASING.md`.
- Added Windows WiX v4 installer scaffold (`packaging/windows/`) with MSI build
  script.
- Extended release workflow with a Windows release job that publishes both ZIP
  and MSI artifacts.
- Kept Linux/macOS packaging aligned with runtime archive shipping.

### Toolchain and Linker

- Modularized embedded linker configuration into `cmake/YonaInProcessLld.cmake`.
- Fixed Windows embedded-LLD libxml resolution by retargeting
  `LLVMWindowsManifest` to fetched `LibXml2`, avoiding mixed `.a`/`.lib`
  linkage.
- Added stricter release checks for linker-mode/runtime artifact smoke
  validation on Windows.

### Documentation

- Updated roadmap/todo status to reflect Windows installer scaffolding and
  release CI progress.
- Updated installation and architecture docs for current linker/runtime
  packaging flow.

## v0.1.0 (2025-04-06)

Initial release of the Yona compiler targeting LLVM.

### Language Features

- Newline-aware lexer with juxtaposition-based function application
- Pattern matching: integer, symbol, wildcard, head-tail, tuple, constructor,
  or-pattern, guards
- Algebraic data types (non-recursive flat, recursive heap-allocated)
- Traits with concrete/constrained instances, default methods, superclass
  constraints
- Module system with FQN-based imports, interface files (.yonai), cross-module
  generics
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

- Persistent Seq: flat array (<=32) + radix-balanced trie with head chain + tail
  buffer
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

- **Pure Yona (12)**: Option, Result, List, Tuple, Range, Math, Pair, Bool,
  Test, Collection, Function, Http
- **C runtime (15)**: String, Encoding, Types, Io, File, Process, Random, Json,
  Crypto, Log, Net, Bytes, Time, Path, Format, Dict, Set, Regex

### I/O Architecture

- io_uring backend (Linux): raw syscalls, submit-and-return, async file/network
  I/O
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
