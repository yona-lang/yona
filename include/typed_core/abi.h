#ifndef YONA_TYPED_CORE_ABI_H
#define YONA_TYPED_CORE_ABI_H

/// Versioned C ABI for typed-core query/analysis results (GitHub #7 / #78).
///
/// A non-LLVM backend may include this header only. It does not include LLVM
/// headers, parser-private types, or C++ standard-library types.
///
/// Compatibility (v1):
/// - `YONA_TYPED_CORE_ABI_VERSION` is the struct/function contract.
/// - Adding, removing, or reordering fields is a breaking change and requires
///   a version bump. New entry points may be added in a later version.
/// - The producer (`yona_tc_analyze`) owns all pointers in a module. The
///   consumer must call `yona_tc_module_free` exactly once. Strings and
///   child arrays are valid until that free.
/// - Positions are 0-based UTF-8 code-unit offsets within the source line
///   (not LSP UTF-16). Ranges are half-open [start, end).

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YONA_TC_API
#if defined(YONA_STATIC_BUILD)
#define YONA_TC_API
#elif defined(_WIN32)
#ifdef yona_lib_EXPORTS
#define YONA_TC_API __declspec(dllexport)
#else
#define YONA_TC_API __declspec(dllimport)
#endif
#else
#define YONA_TC_API
#endif
#endif

#define YONA_TYPED_CORE_ABI_VERSION 1u
#define YONA_TYPED_CORE_ABI_VERSION_STRING "1"

typedef enum YonaTcKind {
    YONA_TC_KIND_MODULE = 1,
    YONA_TC_KIND_FUNCTION = 2,
    YONA_TC_KIND_ADT = 3,
    YONA_TC_KIND_CONSTRUCTOR = 4,
    YONA_TC_KIND_BINDING = 5,
    YONA_TC_KIND_IMPORT = 6,
    YONA_TC_KIND_CASE = 7,
    YONA_TC_KIND_PATTERN = 8,
    YONA_TC_KIND_EFFECT = 9,
    YONA_TC_KIND_UNSUPPORTED = 10
} YonaTcKind;

typedef struct YonaTcPosition {
    uint32_t line;
    uint32_t character;
} YonaTcPosition;

typedef struct YonaTcRange {
    YonaTcPosition start;
    YonaTcPosition end;
} YonaTcRange;

typedef struct YonaTcNode {
    YonaTcKind kind;
    YonaTcRange span;
    const char* name;
    const char* module;
    const char* type;
    const char* effects;
    const char* linearity;
    const char* detail;
    uint32_t child_count;
    const struct YonaTcNode* children;
} YonaTcNode;

typedef struct YonaTcDiagnostic {
    YonaTcRange range;
    int32_t severity;
    const char* code;
    const char* message;
} YonaTcDiagnostic;

typedef struct YonaTcModule {
    uint32_t abi_version;
    const char* filename;
    const char* module_name;
    uint32_t node_count;
    const YonaTcNode* nodes;
    uint32_t diagnostic_count;
    const YonaTcDiagnostic* diagnostics;
} YonaTcModule;

/// Current ABI version. Equals `YONA_TYPED_CORE_ABI_VERSION`.
YONA_TC_API uint32_t yona_tc_abi_version(void);

/// Analyze `source` as a Yona module or expression program.
/// `filename` may be NULL (treated as "-"). `include_paths` may be NULL when
/// `include_path_count` is 0. Returns NULL only when `source` is NULL.
YONA_TC_API YonaTcModule* yona_tc_analyze(const char* source, const char* filename,
                                          const char* const* include_paths,
                                          size_t include_path_count);

/// Release a module returned by `yona_tc_analyze`. NULL is a no-op.
YONA_TC_API void yona_tc_module_free(YonaTcModule* module);

/// Example non-LLVM backend: deterministic textual dump. Caller frees with
/// `yona_tc_string_free`. Returns NULL when `module` is NULL.
YONA_TC_API char* yona_tc_pretty_print(const YonaTcModule* module);

YONA_TC_API void yona_tc_string_free(char* text);

#ifdef __cplusplus
}
#endif

#endif /* YONA_TYPED_CORE_ABI_H */
