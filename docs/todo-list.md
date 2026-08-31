# Yona-LLVM — Outstanding Work

This file contains only actionable, unfinished work. Historical milestones and
shipped-feature status live in `CHANGELOG.md` and the corresponding plans under
`docs/superpowers/plans/`.

## Bugs

- [ ] **The Time fixture calls canonical zero-arity values as functions.**
  Repro: run the `stdlib_time` codegen fixture; `now ()` reports an expected
  `Int` versus `Unit -> ...` mismatch now that imported zero-arity declarations
  correctly behave as values. Update the fixture to use `now` directly.

- [ ] **Pure-contract `sortBy` corrupts the sequence allocator freelist.**
  Repro: run the `stdlib_pure_contract_values` codegen fixture; the generated
  program aborts in `poolAlloc` while `sortBy`/`filter` prepends an element,
  reporting an unaligned freelist link instead of printing the result tuple.

- [ ] **Cross-module generic tests use invalid underscore identifiers.**
  Repro: run `./out/build/x64-debug-linux/tests -tc="*cross-module*"`; the
  `double_it` and `unwrap_or` modules fail interface validation before testing
  generic reuse because local Yona symbols must use canonical camelCase names.

- [ ] **The opaque-constructor negative codegen regression expects a failed
  compile to return a module.** Repro: run `tests -tc="*interface*"`; the
  `Opaque exported ADTs omit constructors from their interface` case correctly
  rejects hidden `MkToken` with an undefined-function diagnostic, then fails
  because it requires `compile(...) != nullptr` before checking the error.

- [ ] **Character escape parsing accepts values that are not Unicode scalar
  values.** Repro: compile `'\uD800'` or `'\UFFFFFFFF'`; parsing and typing
  accept the surrogate/out-of-range value and codegen emits it as an integer
  instead of diagnosing an invalid character literal.

- [ ] **String escapes encode non-scalar Unicode values as invalid UTF-8.**
  Repro: compile `"\uD800"` or `"\U00110000"`; the shared escape parser
  accepts the invalid value and the string scanner emits an illegal UTF-8 byte
  sequence instead of a lexer diagnostic.

- [ ] **Non-ASCII character patterns are truncated during parsing.** Repro:
  parse `case 'λ' of 'λ' -> 1; _ -> 0 end`; `ParserPattern` casts the
  lexer `char32_t` token through `char`, losing the Unicode code point before
  semantic analysis and codegen.

- [ ] **Printed non-ASCII character ASTs use an unsupported escape form.**
  Repro: print a `CharacterExpr` containing U+03BB; `Ast.cpp` emits
  `'\x3bb'`, but the lexer supports only `\u`/`\U` Unicode escapes, so the
  printed AST cannot be parsed back.

- [ ] **Malformed or non-scalar raw UTF-8 can escape lexer validation or crash
  `yonac`.** Repro: pipe the overlong bytes `C0 80` inside a character literal
  to `yonac --emit-ir`; cursor accounting reaches `string_view::substr` out of
  range. Raw surrogate `ED A0 80` and above-U+10FFFF `F4 90 80 80` sequences
  are also accepted as character values instead of rejected.

- [ ] **Invalid UTF-8 inside a character literal is diagnosed one column
  late.** Repro: place an invalid raw byte at column 2 between single quotes;
  the lexer reports column 3 because literal rescanning rewinds `Current`
  without restoring `Column`.

- [ ] **Byte and character literals are accepted by parsing and typing but
  rejected by codegen.** Repro: compile `case [2b] of [1b] -> 1; _ -> 2 end`
  (or the analogous character sequence); codegen reports `unsupported
  expression type` before pattern matching.

- [ ] **Literal-pattern lowering is inconsistent outside integer and symbol
  controls.** Repro: match a string, float, or boolean literal against a
  different value of the same type as a direct value, or-pattern alternative,
  tuple/constructor field, or sequence head; codegen enters the literal arm
  because these paths either emit only integer/symbol predicates or only bind
  identifiers.

- [ ] **Exact sequence patterns ignore string-literal elements when case arms
  have the same length.** Repro: compile
  `case ["ordinary"] of ["explicit"] -> 1 ["ordinary"] -> 2 end`; codegen
  enters the first one-element arm without comparing its string literal.

- [ ] **The Yona runner leaks temporary source files when stdin or `-e`
  compilation fails.** Repro: run `printf 'bad syntax' |
  ./out/build/x64-debug-linux/yona` with an isolated `TMPDIR`; the runner exits
  from `compileToTemp` before its caller removes `yona-src*.yona`.

- [ ] **Canonical zero-arity interface functions import as values instead of
  `Unit -> T`.** Repro: run `./out/build/x64-debug-linux/tests
  -tc="Interface files preserve sibling-wrapped FN effect rows"`; `wrap ()`
  reports an expected `Int` versus `(() -> ...)` mismatch before the effect
  row can be checked.

- [ ] **The semantic generic-source service cannot reparse its owned source.**
  Repro: run `./out/build/x64-debug-linux/tests -tc="Semantics generic source
  service retains GENFN source ownership"`; `GenericFunctionSourceService`
  returns no parsed function before native dependency registration begins.

- [ ] **The Yona runner exits 109 for ordinary Linux programs.** Repro: run
  `./out/build/x64-debug-linux/tests -tc="yona runs a file"`; the built
  runner produces no output and returns status 109 instead of compiling and
  printing `3`. File, shebang, stdin, and `-e` modes fail the same way.

- [ ] **Canonical interface reading rejects emitted `LINEAR` and `TUPLE`
  types.** Repro: run `./out/build/x64-debug-linux/tests
  -tc="Stdlib conformance fixtures"`; `stdlib_process_pid` reports `unknown
  canonical interface type: LINEAR`, while `pure_Core_test` reports the same
  error for `TUPLE`.

- [ ] **Exported function effect rows do not survive interface round-trips.**
  Repro: run `./out/build/x64-debug-linux/tests -ts="Codegen Modules"`;
  exported function, higher-order open-rest, and sibling-wrapped effect-row
  tests fail, including `Interface effect schemes preserve two independent
  callback rows`.

- [ ] **Whole-module imports corrupt module dependency typing.** Repro: run
  `./out/build/x64-debug-linux/tests -tc="yonac module dependencies respect
  whole-module import bindings"`; compilation exits 1 and emits an unexpected
  `E0100` diagnostic.

- [ ] **Cross-module trait dispatch and structural derives regress together.**
  Repro: run `./out/build/x64-debug-linux/tests -ts="Trait"`; constrained and
  cross-module generic instances return codegen/interface errors, while
  derived Show/Eq/Ord/Hash cases return run or type errors (13 failing cases
  in the current Linux debug preset).

- [ ] **The missing-clang-format regression does not report the missing tool.**
  Repro: run `./out/build/x64-debug-linux/tests -tc="format script fails
  clearly when clang-format is unavailable"`; the subprocess output contains
  no `clang-format` diagnostic.

- [ ] **Annotated ADT case functions fail module compilation after heap
  boxing.** Repro: run `./out/build/x64-debug-linux/tests -tc="Annotated ADT
  case functions heap-box non-recursive results"`; expression module creation
  returns null.

- [ ] **Parameterized ADT equality loses floating-point field ABI.** Repro:
  run `./out/build/x64-debug-linux/tests -tc="Parameterized ADT case fields
  retain their floating-point ABI"`; LLVM rejects the derived `Result`
  equality call because codegen passes a `double` to a `ptr` parameter.

- [x] **Generated fixture links discard a configured MoltenVK library name.**
  Repro: configure macOS with `YONA_VULKAN_LIBRARY` resolving directly to
  `libMoltenVK.dylib` and no `libvulkan.dylib`; the fixture linker retains only
  the directory and emits `-lvulkan`, so the supported direct-MoltenVK link
  cannot resolve. Fixed by preserving the exact configured loader path in the
  generated link contract; the isolated direct-MoltenVK configuration probe
  retains `libMoltenVK.dylib`, and the Linux GPU conformance CTest passes.

- [x] **Linux file-descriptor I/O fallbacks leak their initially allocated
  io_uring context.** Repro: force `YonaRuntimeIoUringSubmit` to return zero
  in `YonaRuntimePlatformSubmitFileDescriptorByteRead` or
  `YonaRuntimePlatformSubmitFileDescriptorByteWrite`; each path registers a
  separate direct-result context without freeing its original `Ctx`. Fixed by
  allocating asynchronous contexts only after successful submission; the
  seccomp/Valgrind fallback regression reports no definite leaks.

- [x] **Installed Yona executable targets collide with their output file.**
  Repro: run `ctest --preset unit-tests-linux -R
  installed_consumer_contract`; Ninja rejects `yona_language_consumer`
  because the phony custom target names itself as an input and multiple rules
  generate the same path. Fixed by emitting installed-helper executables below
  a dedicated `bin/` directory; the installed-consumer CTest configures,
  builds, and runs without duplicate-rule diagnostics.

- [x] **Installed `yonac` cannot load the compiler library.** Repro: after
  fixing the installed-consumer target/output collision, run `ctest --preset
  unit-tests-linux -R installed_consumer_contract`; the installed `yonac`
  exits with an unresolved `yona::compiler::DiagnosticEngine` symbol. Fixed
  with a relocatable sibling-library install RPATH on shared Unix CLI tools;
  the contract passes and `ldd` resolves the packaged `libyona_lib`.

- [x] **Vulkan-enabled generated programs omit the loader link dependency.**
  Repro: run `ctest --preset unit-tests-linux -R doctest_stdlib_gpu`; linking
  the stdlib GPU conformance fixture reports unresolved `vk*` symbols from
  `runtime/libyona_runtime.a` and returns `LINK_ERROR`. Fixed by appending the
  exact configured loader after the runtime archive; the GPU conformance CTest
  passes with 4/4 assertions.

- [x] **Platform I/O registry declarations lack C++ linkage guards.** Repro:
  run `cmake --build --preset build-debug-linux`; linking `tests` reports
  undefined `YonaRuntimeIoContextPut`/`YonaRuntimeIoContextTake` references
  from `IoReadExactTest.cpp` while `runtime/libyona_runtime.a` exports the C
  symbols. Fixed with canonical `extern "C"` guards; the test executable links
  and the `IoReadExact` suite passes all 8 cases and 46 assertions.

- [x] **The Linux and macOS platform I/O headers cannot be included together
  by C++ consumers.** Repro: run `clang++ -std=gnu++23 -Iinclude -x c++
  -fsyntax-only` with both `IoUring.h` and `Kqueue.h`; the duplicate
  `YonaIoOperationKind` and `YonaIoContext` definitions are rejected. Share
  the common platform-I/O declarations or make their definitions mutually
  exclusive while retaining each platform API. Fixed by extracting the common
  ABI into `IoContext.h`; the dual-header syntax probe and test link both pass.

- [x] **The Linux io_uring implementation disagrees with its public cancel
  declaration.** Repro: build the debug `tests` target; `IoUringLinux.c`
  defines `YonaRuntimeIoUringCancelGroup(uint64_t *, int)` while
  `IoUring.h` declares `const uint64_t *`. Reconcile the ownership contract
  in the declaration and definition. Fixed by preserving `const` in the
  definition; the full Linux debug graph builds successfully.

- [x] **The modularized Linux platform I/O sources still use the pre-refactor
  `YonaIoContext` fields and inconsistent local identifier casing.** Repro:
  build the debug `tests` target; `FileLinux.c` and `NetLinux.c` reference
  removed `type`, `fd`, and `buf` fields, and names such as `hints`/`res`
  that are declared as `Hints`/`Res`. Port these consumers to the canonical
  context API and identifier spelling. Fixed by migrating both sources to the
  canonical context fields and local spellings; the component and full graph
  build, and the focused file/network suites pass.

- [x] **The modularized native stdlib sources contain inconsistent local
  identifier casing.** Repro: build the debug `tests` target; `Native.c`
  declares `Buf`, `Len`, `Cap`, `Fd`, `Got`, and `Off` but later uses their
  lowercase forms. Restore consistent identifiers in the read/write helpers.
  Fixed by completing the native POSIX identifier migration; the stdlib
  component and full Linux debug graph build successfully.

- [x] **The modularized AST header omitted its `nullptr_t` import.** Repro:
  build with Clang 22 and libstdc++ 16; `yona/Syntax/Ast.h` rejects its
  unqualified `nullptr_t` uses. Fixed by explicitly importing
  `std::nullptr_t` into `yona::ast` and qualifying semantic uses outside that
  namespace; the AST compile regression verifies the unit-expression literal
  type.

### Frontend correctness audit (2026-08-30)

- [ ] **Generic functions reconstructed from `.yonai` can lose their native
  Prelude dependency bindings.** Repro: run
  `ctest --test-dir out/build/release-gate-x64 -R doctest_tests`; generic
  String/Foldable bodies report `undefined function 'primitiveSizeString'` or
  `primitiveGetString` while recompiling their serialized source. The reader,
  generic isolation, and native-dependency registration must retain the
  `GENFN_DEP ... NATIVE YonaRuntime...` bindings through reparsing.

- [ ] **The stream module lowering path can leave an unterminated LLVM basic
  block.** Repro: run the `Lazy stream takeStream` doctest; LLVM rejects
  `takeStream__genfn_0_12_Stream` because `%case.impossible` has no
  terminator. Fix the exhaustive-case lowering path before code emission.

- [ ] **The current channel fixture suite does not satisfy its typed ownership
  contract.** Repro: run `tests.exe -tc="Fixture-based codegen tests"`; the
  channel fixtures return `RUN_ERROR` or reject inferred payloads as
  non-concrete. Reconcile channel creation, payload descriptors, and the
  canonical `Std\Channel` interfaces.

- [ ] **The checked-in GPU channel helper interface loses its endpoint and
  operation types.** Repro: run the `gpu_float_channel` fixture;
  `drainMapFloatGpu` imports as `ADT INT INT -> INT` instead of
  `FloatMapOp -> Receiver FloatArray -> Sender FloatArray -> Int`, producing
  a type error before the channel runtime is exercised.

- [ ] **Generated channel programs leak their endpoint object graph.** Repro:
  run a compiled `channel_basic` with `YONA_ALLOC_STATS=1`; it returns `42`
  but reports three leaked ADTs, one tuple, and one channel even though direct
  runtime release recursively frees the same tuple/Linear/endpoint/channel
  shape.

- [ ] **Generated binary File programs do not release ByteArray temporaries
  or read results.** Repro: run the `binary_write_read` fixture, then execute
  its generated program with `YONA_ALLOC_STATS=1`; output is correct (`12`)
  and all six File/Linear ADTs are freed, but both ByteArrays remain leaked.

- [ ] **Generated entry points do not release a heap-backed final result.**
  Repro: compile a program whose root expression returns `[1]` and run it with
  `YONA_ALLOC_STATS=1`; the value is printed successfully, but the sequence
  reports one allocation and zero frees when `main` exits.

- [ ] **Destructuring a named tuple can free it before its enclosing scope's
  final use.** Repro: compile and run
  `let t = ([1], [2]) in let (x, y) = t in let q = (3, 4) in t`; tuple-pattern
  lowering releases `t`, the unrelated allocation reuses its pool slot, and
  the final access to `t` exits with status 139.

- [ ] **A failed tuple pattern leaks heap-valued prefix bindings.** Repro: run
  `case ([1], :no) of (x, :yes) -> 1; _ -> 0 end` with
  `YONA_ALLOC_STATS=1`; it prints `0`, but the sequence bound before the
  symbol mismatch reports one allocation and zero frees.

- [ ] **Record-pattern fields can dangle after a temporary scrutinee is
  released.** Repro: construct an exported generic `Box { item : a }` and run
  `case make [1] of Box { item = x } -> x end`; record binding neither retains
  the heap field nor registers an arm drop, so releasing the temporary `Box`
  leaves `x` pointing at freed sequence storage and prints garbage.

- [ ] **A heap value returned from a nested `let` is misclassified as a
  borrowed ADT field.** Repro: run
  `case Some (let temporary = [1] in temporary) of Some values -> 0; None -> 1 end`
  with allocation stats; the stale `temporary` entry in `named_values_` makes
  constructor lowering add an unmatched retain, leaking the sequence.

- [ ] **The macOS file runtime uses undeclared lowercase async-I/O locals.**
  Repro: compile `src/Runtime/Platform/FileMacOs.c`; the read path references
  `fd`, `buf`, `count`, and `offset` although its parameters are `Fd`, `Buf`,
  `Count`, and `Offset`, the write path similarly references lowercase
  `fd`, `data`, `len`, and `offset`, and the fallback/seek/truncate blocks
  contain the same incomplete identifier-case migration.

- [ ] **The macOS network runtime uses stale context fields and lowercase
  locals.** Repro: compile `src/Runtime/Platform/NetMacOs.c`; async operations
  initialize removed `YonaIoContext` members `type`, `fd`, and `buf`, while
  connect/listen/HTTP/UDP paths reference undeclared identifiers such as
  `hints`, `res`, `addr`, `ai`, and `fd` instead of their declared canonical
  spellings.

- [ ] **Cancelling an async file write frees a reference-counted ByteArray
  payload directly.** Repro: submit `writeBytes` and cancel its I/O context
  before completion; the Linux and macOS cancellation cleanup calls `free`
  on the retained managed payload instead of `YonaRuntimeRelease`, risking an
  invalid free and leaving the internal pin's ownership contract unbalanced.

- [ ] **Failed async read/receive submission leaks its managed result buffer.**
  Repro: make `YonaRuntimeIoUringSubmit` return zero after
  `YonaRuntimePlatformSubmitFileRead` or `YonaStdNetRecv` allocates its managed
  string; the failure path frees the `YonaIoContext` but neither returns nor
  releases the buffer. ByteArray read submission has the same ownership gap.

- [ ] **Binary I/O fixtures infer a `FileHandle` as `Int` at native call
  boundaries.** Repro: run `tests.exe -tc="Fixture-based codegen tests"`;
  `binary_chunks`, `binary_seek`, and `binary_write_read` report an expected
  `Int` where their live `Linear FileHandle` is passed. Preserve the resource
  type through the native binary I/O interface and codegen call lowering.

- [x] **Exact-read `Err` results leak the discarded short string.** Repro:
  call `Std\Io.readExact` on a two-byte stream with a requested length of four
  under `YONA_ALLOC_STATS=1`; `readExactFromFd` replaces the short managed
  string with `Err "unexpected eof"` without releasing it. Fixed by releasing
  the short read before constructing `Err`; the isolated allocation regression
  now reports two STRING allocations and two frees.

- [x] **Length-tagged strings are absent from runtime allocation statistics.**
  Repro: allocate a string through `YonaRuntimeAllocateStringWithLength` under
  `YONA_ALLOC_STATS=1`; the STRING allocation counter does not advance even
  though release advances the free counter. Fixed by recording the STRING
  allocation on the length-tagged allocation path; the exact-read lifetime
  regression observes the balanced counter totals.

- [ ] **Direct ownership doctests cannot find Prelude without `YONA_PATH`.**
  Repro: unset `YONA_PATH` and run `tests -tc="dropping a set of seqs releases
  inner heap objects"`; its empty `Codegen.ModulePaths` makes `SemanticSetup`
  throw `unable to install Prelude interface in test` despite the configured
  deterministic test Prelude artifacts and repository library path.

- [ ] **Lifted dictionary trait code can crash during ownership lowering.**
  Repro: run `tests.exe -tc="Fixture-based codegen tests"`; the
  `dict_lifted_trait_lifetime` fixture terminates with `SIGSEGV`. Audit the
  lifted trait materialization and dictionary-value ownership path before
  enabling the fixture suite as a release gate.

- [ ] **Temporary constructor cases leak their scrutinee and heap fields.**
  Repro: `case Some 1 of Some x -> 0; None -> 0 end` leaks the ADT, while
  `case Some [1] of Some xs -> 0; None -> 0 end` leaks both the ADT and Seq.
  The IR retains the anonymous sequence before `ADTSetField` and retains the
  extracted field, but drops neither the temporary scrutinee nor field binding.

- [ ] **ABI refinement no longer emits the canonical refined call form.**
  Repro: run `tests.exe -tc="ABI refinement leaves one canonical function"`;
  the generated IR no longer contains `call fastcc i1 @f(i64 %x)`. Reconcile
  refined function declaration and call lowering, then update the regression
  only if the canonical ABI intentionally changed.

- [ ] **Effectful file fixtures lose their `Fs.read` handler context.**
  Repro: run `tests.exe -tc="Fixture-based codegen tests"`; file-oriented
  fixture expressions report unhandled `Fs.read` [E0202] despite being run
  under their expected handler path. Preserve the effect environment through
  imported/fixture compilation.

- [x] **A bundled-PCRE2 CMake install could fail before exporting the Yona
  package.** Repro: configure without a system PCRE2, build `yonac`, then run
  `cmake --install`; PCRE2's install script requires `pcre2-posixd.lib` even
  though the Yona target graph does not need it. Fixed by excluding all
  FetchContent dependency installation rules from the Yona package.

- [x] **Public semantic/source headers fail when Windows `min`/`max` macros
  are visible.** Repro: compile a translation unit that includes `Windows.h`
  before `yona/Semantics/RefinementChecker.h`; `numeric_limits::min()` and
  `max()` are expanded as function-like macros. Fixed by using the standard
  macro-safe parenthesized calls in RefinementChecker and SourceManager; the
  standalone Windows header audit now succeeds.

- [x] **`ThreadPool::submit_async<void>` does not compile.** Repro: call
  `ThreadPool Pool(1); Pool.submit_async<void>([] {});`; the template passes a
  void expression to `std::promise<void>::set_value`. Fixed with a compile-time
  void branch that invokes the task before fulfilling the promise; the focused
  Support regression waits on the returned `future<void>`.

- [x] **The public `first_defined_optional<T>` template is not defined for
  callers.** Repro: include `yona/Syntax/Utils.h`, call
  `first_defined_optional<int>({1, std::nullopt})`, and link; the definition is
  hidden in `Utils.cpp`, which explicitly instantiates only `std::any`. Fixed
  by removing the unused declaration, implementation, and lone instantiation
  from the clean-slate public surface.

- [x] **Terminal size probing returns indeterminate dimensions when the OS
  query fails.** Repro: redirect stdout to a file on Windows and call
  `terminal::getTerminalSize()`; `GetConsoleScreenBufferInfo` can fail while
  the function still reads the uninitialized `ScreenBufferInfo` fields. Fixed
  by checking the Windows handle and query result before reading it; the
  focused Windows regression verifies the invalid-handle result is `{0, 0}`.

- [x] **Native-promise interface generation erases a generic result
  descriptor.** Repro: compile `extern native spawn : (() -> result) -> result`
  and inspect the generated `.yonai`; the promise lowering records a concrete
  machine type instead of `VAR(result)`. Fixed by preserving the semantic
  return descriptor through interface emission; the checked-in `Std\\Task`
  interface now regenerates byte-for-byte with its generic result type.

- [x] **Grouped GPU failures can complete an unregistered task.** Repro: pass
  a task group to an unavailable GPU operation and end the group; the immediate
  failure path creates and completes a task without first registering it in
  the group. Fixed by registering every grouped task before either dispatch or
  immediate completion and by handling registration failure explicitly; the
  focused GPU group regression awaits the retained result before group teardown.

- [x] **The LSP JSON parser accepts trailing non-whitespace after a value.**
  Repro: `Json::parse("true trailing", &Error)` returns `true` without setting
  `Error` because the parser never verifies that the input was exhausted.
  Fixed by requiring input exhaustion after trailing whitespace and clearing
  stale caller error text; the focused LSP JSON regression covers both cases.

- [x] **The packaged `Std\\Json` interface contradicted its constructor
  metadata.** Repro: call `interface::readModule("lib/Std/Json.yonai")`; the
  canonical reader rejects maximum arity zero because six `Json` constructors
  have one field. Fixed by deriving the canonical maximum arity from the
  constructors; the packaged-interface regression validates every checked-in
  interface structurally and deterministically.

- [x] **SemanticModel facts extraction crashes on case patterns and linear
  bindings.** Repro: run `tests -ts=TypedCoreC`; constructing the shared model
  for `case value of Some item -> item end` or `let value = Linear 1 in value`
  throws `std::bad_function_call` while TypedCore is collecting node facts.
  Fixed by supplying total empty constructor-catalog callbacks when semantic
  indexing has no module catalog; direct SemanticModel and TypedCore
  regressions cover constructor and linear case patterns.

- [x] **Recursive `Stream a` nominal types fail to unify with themselves.**
  Repro: compile `lib/Std/Stream.yona -I lib --emit-obj`; the type checker
  reports repeated E0100 diagnostics such as `Stream a vs Stream a` for
  `singleton`, `fromSeq`, `range`, and the remaining stream combinators. Fixed
  by retaining named type arguments in recursive function fields; the focused
  type-checker regression covers `() -> Stream a`.

- [x] **`Std\\Stream.async` captures a non-shareable stream in `Task.spawn`.**
  Repro: compile `lib/Std/Stream.yona -I lib --emit-obj`; after recursive
  nominal field inference succeeds, the `spawn (\\_ -> drive s)` producer is
  rejected with E0105 because `Stream a` has no lawful `Shareable` instance.
  Fixed by removing `async` and `buffered`: the canonical closure-backed stream
  is intentionally task-local, and concurrent pipelines use typed channels.

- [x] **Case pattern inference forces every arm to the first payload shape.**
  Repro: compile `lib/Std/Collection.yona -I lib --emit-obj`; `unfold` accepts
  the `:some, (value, nextSeed)` arm but rejects the alternate `:none` symbol
  arm with E0100, claiming the pattern must also be a `(Symbol, (a, b))` tuple.
  Fixed by making the canonical producer contract
  `(state -> Option (value, state))`; `Some` and `None` now share one lawful
  nominal result type and the stdlib fixture covers the countdown case.

- [x] **Expression-scoped externs cannot separate a local name from the C
  contract symbol.** Repro: parse
  `extern async testSlowAdd : Int -> Int = "YonaTestSlowAdd" in testSlowAdd 1`;
  the parser requires `in` immediately after the type and rejects the canonical
  local-name/contract-name form already supported by module externs. Fixed by
  accepting the contractual string after `=` and retaining it in the extern
  AST; the parser regression verifies both names.

- [x] **Accelerator diagnostics retained a pre-normalization FQN segment.**
  Repro: emit an accelerator report for `Std\Gpu::mapAdd 1 buffer`; imported
  calls are recognized, but the direct FQN call is omitted because the module
  matcher still compares against the pre-normalization uppercase segment. Fixed
  by recognizing only the canonical `Std\Gpu` FQN; the accelerator-report
  regression covers the direct module-call AST.

- [x] **Package-qualified module calls are rejected as expressions.**
  Repro: parse `Std\Gpu::available ()`; `Parser::parseExpression` rejects the
  backslash-qualified module before it can construct the existing
  `FqnExpr`/`ModuleCall` AST. Fixed by parsing a backslash-qualified FQN before
  the shared `::` token can be interpreted as sequence cons and preserving the
  resulting `ModuleCall` when building an application.

- [x] **Pattern literals use an invalid type-erasure cast and identifier patterns
  leak their AST node.** Repro: parse `case 1 of 1 -> 1 end`; the parser
  `reinterpret_cast`s `IntegerExpr *` to the unrelated
  `LiteralExpr<void *> *` specialization, and `PatternValue` later deletes
  through that type while never deleting its `IdentifierExpr *` alternative.

- [x] **Invalid field-update targets release their AST before the downcast is
  validated.** Repro: parse `(identity value) { field = 1 }`; `parse_expr`
  calls `left.release()` before `dynamic_cast<IdentifierExpr *>`, leaking the
  expression when the cast fails.

- [x] **Borrow/last-use analysis skips pattern-alias RHSs and guards.** Repro:
  parse `let (head, tail) = pair in pair` or
  `case value of item if consume value -> item end`; reference counting omits
  the alias RHS or guard and can incorrectly classify the binding as borrowed
  or single-use.

- [x] **LSP local-symbol queries conflate shadowed lexical bindings.** Repro:
  on `let value = 1 in let value = 2 in value`, definition, references,
  highlights, and rename match both `value` declarations because identity is
  based only on the spelling and import origin.

- [x] **CLI and REPL compilation/execution pass user-controlled values through
  a shell command.** Repro: invoke `yonac` with an output or input path
  containing shell metacharacters, or enter an expression in `yona-repl` while
  its temporary path contains metacharacters; the tools build command strings
  for `system()`/`popen()` instead of passing exact argument vectors. The same
  issue affected `Std\Process.exec`, `spawn`, and the compiler test harness;
  all compiler, runtime, and test process paths now pass executable/argument
  vectors without an implicit shell.

- [x] **Every code-generation session leaks its LLVM target machine.** Repro:
  repeatedly construct and destroy `Codegen` under LeakSanitizer; each
  `Target::createTargetMachine` allocation remains live because `Codegen`
  stores the owning result in a raw pointer and uses a default destructor.
  Fixed by making the target machine a `unique_ptr`; the lifetime regression
  constructs and destroys a complete code-generation session under LSan.

- [x] **Multi-argument async calls share mutable LLVM globals between
  invocations.** Repro: launch the same generated multi-argument async function
  concurrently with distinct arguments; `CodegenApply.cpp` stores arguments in
  per-callsite globals before submitting a zero-argument thunk, so a later call
  can overwrite values before an earlier worker loads them. Fixed by allocating
  an invocation-owned argument context and submitting a context-aware wrapper;
  overlapping calls no longer share argument storage, and every completion,
  failure, and cancellation path destroys its context.

### Runtime correctness implementation

- [x] **`Std\Gpu.gpuFloatChannel` left its payload type unconstrained.**
  Repro: run `yonac lib/Std/Gpu.yona -I lib --emit-obj`; typed channel
  lowering rejects `gpuFloatChannel n = channel n` because it cannot construct
  the required concrete payload descriptor, despite the public API documenting
  a `FloatArray` channel. Fixed with the explicit
  `Int -> (Linear (Sender FloatArray), Linear (Receiver FloatArray))` contract
  and a concrete native constructor that always uses the RC payload
  descriptor; `gpu_float_channel.yona` covers the imported API.

- [x] **Linearity reports a consumed imported channel receiver as leaked.**
  Repro: compile `test/Fixtures/Codegen/gpu_float_channel.yona`; `rl` is immediately
  consumed by `case rl of Linear receiver -> ... end`, but E0602 still warns
  that the binding at `let (sl, rl) = gpuFloatChannel 4` was not consumed.
  Fixed by propagating every unanimously consumed live binding out of a case,
  including consumption performed by nested cases; divergent branches still
  report E0601.

- [x] **Channel buffers erase payload ownership.** Repro: send an RC-managed
  value into a channel, drop both channel endpoints without receiving it, and
  observe that `src/Runtime/Concurrency/ChannelPosix.c` and `ChannelWin32.c`
  free only the ring buffer and
  never release the buffered value. Repro for the corresponding codegen
  erasure: send and receive `"owned"`; the received `Some String` is lowered as
  `Int` and fails LLVM verification when printed.

- [x] **The POSIX I/O registry loses colliding requests after deletion.**
  Repro: store contexts for IDs `1` and `1 + YONA_IO_CONTEXT_TABLE_SIZE`, take ID `1`,
  then take the colliding ID; lookup stops at the newly empty first slot.

- [x] **Vulkan shutdown races active device work.** Repro: repeatedly call
  device/context shutdown on one thread while another thread submits compute or
  completes an asynchronous fence job; global device handles can be destroyed
  while the submit/cleanup path is still using them.

- [x] **JSON and regex error paths leak managed allocations.** Repro: stringify
  nested JSON strings/floats or parse malformed arrays/objects repeatedly, and
  force a second-pass PCRE2 substitution error; temporary/partial RC values are
  not released.

- [ ] **Windows `Std\Convert` rejects the Bool case expected by its conformance
  suite.** `foundation_Convert_test` reports one failure for “Parse Bool is
  explicit and case sensitive” (`16 passed, 1 failed`) only on Windows.
  Repro: GitHub Actions run `33167091803`, Windows x64 Debug job `98834979501`.
  Compare the runtime/parser result contract and make it platform-independent.

## Build quality

- [ ] **Normalize the checked-in C++ formatting baseline.** The current project
  formatter reports broad violations in untouched `include/yona/Codegen/Codegen.h` and
  `src/Codegen/CodegenModule.cpp`, so formatting a focused change creates
  unrelated whole-file churn. Repro: `clang-format --dry-run --Werror
  include/yona/Codegen/Codegen.h src/Codegen/CodegenModule.cpp`. Agree the pinned formatter
  version, then make one dedicated mechanical formatting change.

- [ ] **Relax stream-fusion gating only with benchmark evidence.** Run the full
  Linux and Windows benchmark matrix three times (`-n 10`), require a ≥5%
  median win on the targeted fusion rows, no correctness/test regression, and
  no >3% median regression elsewhere without a documented root cause.

## Distribution and toolchains

- [ ] **Productionize the Windows installer.** Finish upgrade behavior, code
  signing, and final UX polish.

- [ ] **Publish a versioned WinGet manifest for each tagged release.** After
  stable GitHub Release URLs and SHA-256 hashes exist for both x64 and ARM64
  MSIs, submit one multi-file manifest set to `microsoft/winget-pkgs`, validate
  with `winget validate` and a clean local or Windows Sandbox install, then
  monitor its validation PR. This is a registry manifest, not an in-repository
  package formula; do not submit it before release assets exist.

- [ ] **Complete the sysroot distribution pass.** Validate the installed
  CLI/REPL layout on every packaged platform without requiring an external C
  compiler for normal `yonac` use.

- [ ] **Enable embedded LLD by default across supported toolchains.** The
  remaining Windows gate is an MSVC-compatible LibXml2 dependency.

## Type system and language safety

- [ ] **[#5](https://github.com/yona-lang/yona/issues/76) Complete the totality
  story.** Extend `yonac --require-effect-free` beyond its existing closed-row,
  finite-domain, and local structural-recursion checks to general termination
  and arbitrary open-domain coverage before making a full totality claim.

- [ ] **[#4](https://github.com/yona-lang/yona/issues/75) Implement a
  deterministic compile-time evaluator.** After #5 and either #7 or a
  documented typechecked-AST subset, evaluate pure total expressions only;
  exclude macros and arbitrary native code at compile time. This is the shared
  prerequisite for multi-stage programming and user-defined derives.

- [ ] **Make `Linear FileHandle` and other resources real payload types.**
  Replace the raw-`Int` runtime-handle path with APIs such as
  `openFile : String -> FileMode -> Linear FileHandle` and
  `spawn : String -> Linear Process`; do the same for TCP/UDP and channel ends.
  Consume resources through `with` or `case Linear h -> …`, implement
  `Closeable` on the payload, and preserve both `LINEAR` and the inner ADT in
  `.yonai`. Promote E0602 leaks to errors. Update the linear/file/process/net/
  channel docs and site pages, with codegen regressions for valid use,
  use-after-consume, and leaks.

- [ ] **Add type-level borrows (`&T`) and interface carry-over.** Follow
  [design-borrow-types.md](./design-borrow-types.md): extend syntax,
  `MonoType`, unification, `.yonai`, and codegen so cross-module APIs state
  callee-reads-only directly and `borrowed_params` derives from zonked types.
  Version one excludes lifetimes and borrowed ADT fields.

### Formal specification (Rocq)

Program of record:
[2026-08-17-yona-rocq-formalization.md](./superpowers/plans/2026-08-17-yona-rocq-formalization.md).

- [ ] **Phase 0:** `formal/` dune/opam skeleton, Rocq 9.2 pin, CI job, and
  `docs/formal-spec.md`.
- [ ] **Phase 1:** Yona-Core syntax, semantics, and typing; progress and
  preservation.
- [ ] **Phase 2:** verified unification plus Algorithm W; soundness,
  completeness, and principal types.
- [ ] **Phase 3:** extracted checker, `yonac --emit-typed-core`, and a
  differential harness.
- [ ] **Phase 4:** Rows, Effects, Async, Traits, Refine, and Linear modules.
- [ ] **Phase 5:** Alectryon documentation and feature-status headers.
- [ ] **Phase 6:** optional Iris proofs for the Perceus RC runtime.

## Performance and runtime research

- [ ] Migrate to LLVM exception handling (`invoke` / `landingpad`) if
  correctness requires it.
- [ ] Add profile-guided optimization.
- [ ] Complete a JIT feasibility/design study (ORC, Cranelift, or alternatives).
- [ ] Investigate LLVM coroutine lowering for async after cancellation semantics
  are frozen.
- [ ] Design gradual typing with contracts.
- [ ] Design distributed Yona.
- [ ] Design a serialization system.
- [ ] Design STM.

## Language architecture

- [ ] **Supervisors as effect handlers.** Model Erlang-style supervision trees
  using `handle ... with`, where a supervisor catches child failures and
  chooses restart, escalation, or ignore. Depends on settled structured
  concurrency semantics.

- [ ] **Content-addressed code.** Research Unison-style AST hashes as function
  identities for caching, zero-conflict merges, and refactoring, including the
  package-manager, LSP, and VCS implications.

## GPU and heterogeneous compute

- [ ] **Compile arbitrary lambdas to SPIR-V.** Fixed kernels are supported;
  arbitrary closures such as `\x -> x + x * x` still stay on the host path or
  fail under `--strict-accelerator` with E0700.

- [ ] **Evaluate io_uring/reactor GPU integration.** Research interleaving GPU
  completion with the io_uring reap loop; the current fence waiter thread is
  the supported implementation.

- [ ] **Design CPU/GPU occupancy and scheduling hints.** Consider optional
  attributes or stdlib helpers for wave size, shared memory, and
  throughput-versus-latency after user demand and profiling data justify them.

- [ ] **Re-capture macOS and Windows GPU benchmarks.** Run
  `bench/run_gpu_compare.py`, including pinned-scale hot cases, on MoltenVK and
  Vulkan hosts; refresh the platform benchmark reports.

## Metaprogramming

- [ ] **Multi-stage programming.** Define hygienic staging, including a pure
  `static` computation model and its relationship to the deterministic
  evaluator.
- [ ] **User-defined derives.** Let traits provide `derive` templates over ADT
  structure after the evaluator or an enriched `.yonai` codegen interface
  exists.
- [ ] **Quasiquotes and template expressions.** Add AST `quote` / `splice`
  facilities for DSLs and generated code.

## Tooling and documentation

- [ ] **Package manager and build tool.** Define dependency resolution,
  reproducible builds, and project metadata around `.yonai` interfaces.

- [ ] **Rewrite `yls` in Yona as the editor-default server.** Keep the existing
  C++ `yls --stdio`, VS Code client, and Zed extension as maintained fallback
  paths until the Yona implementation has protocol and performance parity.

- [ ] **Build compiler-aware API documentation extraction.** Preserve
  handwritten Learn/Guides/Reference pages, but replace the regex-only
  `scripts/gendocs.py` path with `yonac --emit-docs` or a successor that
  understands `.yonai`, C modules, types, effects, and exports. Until then,
  update source comments and public site pages together.
