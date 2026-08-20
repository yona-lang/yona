#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include "SourceLocation.h"
#include "yona_export.h"

namespace yona::compiler {

enum class DiagLevel { Error, Warning, Note };

enum class WarningFlag {
    UnusedVariable,
    UnusedImport,
    Shadow,
    MissingSignature,
    IncompletePatterns,
    OverlappingPatterns,
    UnhandledEffect,
    UnmatchedAdt,
};

/// Structured error codes for --explain support.
/// Each code has a detailed explanation accessible via `yonac --explain E0001`.
enum class ErrorCode {
    // Type errors (E01xx)
    E0100,  ///< Type mismatch
    E0101,  ///< Infinite type (occurs check)
    E0102,  ///< Tuple size mismatch
    E0103,  ///< Undefined variable
    E0104,  ///< Undefined function
    E0105,  ///< No trait instance
    E0106,  ///< Missing trait instances

    // Effect errors (E02xx)
    E0200,  ///< Unhandled effect operation (codegen)
    E0201,  ///< Effect argument count mismatch
    E0202,  ///< Effect escapes at call site without a covering handler

    // Parse errors (E03xx)
    E0300,  ///< Unexpected token
    E0301,  ///< Invalid syntax
    E0302,  ///< Invalid pattern

    // Codegen errors (E04xx)
    E0400,  ///< Failed to emit object file
    E0401,  ///< Linking failed
    E0402,  ///< Unsupported expression
    E0403,  ///< Unknown field access
    E0404,  ///< Pipe requires function

    // Refinement errors (E05xx)
    E0500,  ///< Refinement predicate not satisfied

    // Linearity errors (E06xx)
    E0600,  ///< Use after consume (linear value already consumed)
    E0601,  ///< Branch inconsistency (consumed in one branch but not the other)
    E0602,  ///< Resource leak (linear value not consumed at scope exit)
    E0603,  ///< Invalid `@borrow` (pattern or escape rules violated)

    // Accelerator errors (E07xx)
    E0700,  ///< Columnar map/filter/foldl lambda not in the fixed GPU kernel library
};

/// Return the string form of an error code (e.g., "E0100").
YONA_API std::string error_code_str(ErrorCode code);

/// Return the detailed explanation for an error code, or "" if unknown.
/// Used by `yonac --explain E0100`.
YONA_API std::string error_explanation(ErrorCode code);

/// Parse an error code string (e.g., "E0100") back to enum. Returns nullopt on failure.
YONA_API std::optional<ErrorCode> parse_error_code(const std::string& str);

class YONA_API DiagnosticEngine {
public:
    DiagnosticEngine();

    // Configuration
    void enable_wall();                  // -Wall: common warnings
    void enable_wextra();                // -Wextra: all warnings
    void set_warnings_as_errors(bool v); // -Werror
    void suppress_all_warnings();        // -w
    void enable_warning(WarningFlag f);
    void disable_warning(WarningFlag f);

    // Source context for line display
    void set_source(std::string_view source, std::string_view filename);

    // Emission
    void error(const SourceLocation& loc, const std::string& message);
    void error(const SourceLocation& loc, ErrorCode code, const std::string& message);
    void warning(const SourceLocation& loc, const std::string& message, WarningFlag flag);
    void note(const SourceLocation& loc, const std::string& message);

    /// One emitted diagnostic (errors, warnings, notes) for tests and tooling.
    struct Record {
        DiagLevel level = DiagLevel::Error;
        SourceLocation loc;
        std::optional<ErrorCode> code;
        std::string message;
    };
    const std::vector<Record>& records() const { return records_; }

    // Counters
    int error_count() const { return error_count_; }
    int warning_count() const { return warning_count_; }
    bool has_errors() const { return error_count_ > 0; }

    // Warning flag name for display (e.g., "unused-variable")
    static std::string flag_name(WarningFlag f);

private:
    void emit(DiagLevel level, const SourceLocation& loc, const std::string& message,
              const std::string& flag_str = "");
    std::string get_source_line(int line) const;

    std::unordered_set<WarningFlag> enabled_warnings_;
    bool warnings_as_errors_ = false;
    bool suppress_warnings_ = false;
    int error_count_ = 0;
    int warning_count_ = 0;

    std::string source_;
    std::string filename_;
    std::vector<size_t> line_offsets_; // byte offset of each line start
    std::vector<Record> records_;
};

} // namespace yona::compiler
