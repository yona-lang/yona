# CI Runtime Link Stabilization Design

## Goal

Make generated Yona programs link reliably on every supported CI platform,
preserve sanitizer instrumentation through the generated-program link step,
and eliminate the currently observed channel ownership leak and Linux release
test hang.

## Evidence

The 2026-09-01 CI matrix builds the compiler successfully but generated
program tests fail outside the CMake target graph. On macOS, the final link
reports unresolved PCRE2 symbols. Windows has the same class of generated
program test failures. The runtime archive records PCRE2 as a CMake usage
requirement, but `yonac` and test helper link commands run the compiler
directly and therefore do not receive package-specific library locations.

The sanitizer quality build compiles the runtime with ASan/UBSan but generated
program links omit those runtime options, yielding unresolved sanitizer
symbols. Linux ARM independently reports that the raw channel native ownership
test leaks four ADTs and one channel. Linux x64 Release times out while running
the monolithic doctest executable, so the blocking test must be identified
before changing timeout policy or runtime behaviour.

## Design

### Runtime link manifest

The build will produce compile-time configuration for the compiler and test
link helper that describes resolved native runtime dependencies as concrete
link arguments. This data is derived from the CMake target or resolved package
location, not reconstructed with platform-specific guesses such as
`-lpcre2-8`. `yona_runtime` remains the canonical archive; its native
dependencies are supplied after it at every external link boundary.

For source and installed builds, a static PCRE2 archive is copied beside the
runtime when necessary. When the selected package is shared, the configured
absolute link path and runtime search path are supplied instead. This avoids
assuming that Homebrew, Windows package managers, or the system linker search
path expose PCRE2 globally.

### Instrumented generated programs

When an instrumented CMake configuration builds `yonac`, its generated-program
link invocation will inherit the configuration's executable-link options.
This keeps ASan/UBSan/TSan runtimes present when a generated program links the
instrumented static runtime. The normal configuration receives no additional
flags.

### Regression coverage

Focused tests will exercise an emitted program that uses `Std\\Regex` and
verify that test helper and CLI links include the configured PCRE2 dependency.
The sanitizer quality command remains the end-to-end regression for sanitizer
runtime propagation. Tests will be written before the production changes.

### Independent runtime failures

The raw-channel test will remain the ownership regression oracle. The release
hang will be narrowed to one doctest case with filters or bisection before any
production change. If it is platform timing rather than a liveness defect,
the test registration will receive a precise, documented timeout; otherwise
the responsible runtime path will be corrected.

### Quality gates

Run the canonical CMake formatter on affected CMake files. Coverage thresholds
remain 80%; missing coverage is addressed with meaningful tests rather than
lowering the configured gates.

## Non-goals

- Do not add CI-only PCRE2 flags that leave local or installed compilers broken.
- Do not increase the global doctest timeout to mask a deadlock.
- Do not lower coverage thresholds to make the gate pass.
- Do not change public language semantics or retain backward compatibility for
  superseded internal linker plumbing.
