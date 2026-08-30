#ifndef YONA_TYPEDCORE_ABI_H
#define YONA_TYPEDCORE_ABI_H
/// Canonical C adapter for typed-core query and analysis results.
///
/// This header intentionally exposes no LLVM headers, parser-private types,
/// or C++ standard-library types.
///
/// Ownership:
/// - `YonaTypedCoreAnalyze` owns every pointer reachable from its result.
/// - The caller must pass a successful result to
///   `YonaTypedCoreDisposeModule` exactly once.
/// - Strings and child arrays remain valid until the module is disposed.
/// - `YonaTypedCorePrettyPrint` returns an independent allocation that must be
///   passed to `YonaTypedCoreDisposeString` exactly once.
///
/// Failure:
/// - `YonaTypedCoreAnalyze` returns NULL when Source is NULL or allocation
///   fails. Syntax and semantic failures are returned as diagnostics.
/// - `YonaTypedCorePrettyPrint` returns NULL for a NULL module or allocation
///   failure.
/// - Dispose functions accept NULL.
///
/// Thread safety:
/// - Separate analysis calls may run concurrently.
/// - A completed module is immutable and may be read concurrently.
/// - Disposal must not overlap any access to the same module or string.
///
/// Positions are zero-based UTF-8 code-unit offsets within the source line.
/// Ranges are half-open [Start, End).

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YONA_TYPED_CORE_API
#if defined(YONA_STATIC_BUILD)
#define YONA_TYPED_CORE_API
#elif defined(_WIN32)
#ifdef YONA_LIB_EXPORTS
#define YONA_TYPED_CORE_API __declspec(dllexport)
#else
#define YONA_TYPED_CORE_API __declspec(dllimport)
#endif
#else
#define YONA_TYPED_CORE_API
#endif
#endif

typedef enum YonaTypedCoreNodeKind {
  YonaTypedCoreNodeKindModule = 1,
  YonaTypedCoreNodeKindFunction = 2,
  YonaTypedCoreNodeKindAdt = 3,
  YonaTypedCoreNodeKindConstructor = 4,
  YonaTypedCoreNodeKindBinding = 5,
  YonaTypedCoreNodeKindImport = 6,
  YonaTypedCoreNodeKindCase = 7,
  YonaTypedCoreNodeKindPattern = 8,
  YonaTypedCoreNodeKindEffect = 9,
  YonaTypedCoreNodeKindUnsupported = 10
} YonaTypedCoreNodeKind;

typedef struct YonaTypedCorePosition {
  uint32_t Line;
  uint32_t Character;
} YonaTypedCorePosition;

typedef struct YonaTypedCoreSourceRange {
  YonaTypedCorePosition Start;
  YonaTypedCorePosition End;
} YonaTypedCoreSourceRange;

typedef struct YonaTypedCoreNode {
  YonaTypedCoreNodeKind Kind;
  YonaTypedCoreSourceRange SourceRange;
  const char *Name;
  const char *Module;
  const char *Type;
  const char *Effects;
  const char *Linearity;
  const char *Detail;
  uint32_t ChildCount;
  const struct YonaTypedCoreNode *Children;
} YonaTypedCoreNode;

typedef struct YonaTypedCoreDiagnostic {
  YonaTypedCoreSourceRange SourceRange;
  int32_t Severity;
  const char *Code;
  const char *Message;
} YonaTypedCoreDiagnostic;

typedef struct YonaTypedCoreModule {
  const char *Filename;
  const char *ModuleName;
  uint32_t NodeCount;
  const YonaTypedCoreNode *Nodes;
  uint32_t DiagnosticCount;
  const YonaTypedCoreDiagnostic *Diagnostics;
} YonaTypedCoreModule;

/// Analyze Source as a Yona module or expression program.
/// Filename may be NULL (treated as "-"). IncludePaths may be NULL when
/// IncludePathCount is zero.
YONA_TYPED_CORE_API YonaTypedCoreModule *
YonaTypedCoreAnalyze(const char *Source, const char *Filename,
                     const char *const *IncludePaths, size_t IncludePathCount);

/// Dispose a module returned by `YonaTypedCoreAnalyze`.
YONA_TYPED_CORE_API void
YonaTypedCoreDisposeModule(YonaTypedCoreModule *Module);

/// Produce a deterministic textual dump without exposing LLVM types.
YONA_TYPED_CORE_API char *
YonaTypedCorePrettyPrint(const YonaTypedCoreModule *Module);

/// Dispose a string returned by `YonaTypedCorePrettyPrint`.
YONA_TYPED_CORE_API void YonaTypedCoreDisposeString(char *Text);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* YONA_TYPEDCORE_ABI_H */
