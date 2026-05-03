#include "Diagnostic.h"

#include <cstdio>
#include <iostream>
#include <string>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace yona::compiler {

namespace {

bool stderr_is_tty() {
#if defined(_WIN32)
    static const bool is_tty = _isatty(_fileno(stderr)) != 0;
#else
    static const bool is_tty = isatty(fileno(stderr)) != 0;
#endif
    return is_tty;
}

const char* color_reset()   { return stderr_is_tty() ? "\033[0m"  : ""; }
const char* color_bold()    { return stderr_is_tty() ? "\033[1m"  : ""; }
const char* color_red()     { return stderr_is_tty() ? "\033[1;31m" : ""; }
const char* color_yellow()  { return stderr_is_tty() ? "\033[1;35m" : ""; }
const char* color_cyan()    { return stderr_is_tty() ? "\033[1;36m" : ""; }
const char* color_green()   { return stderr_is_tty() ? "\033[1;32m" : ""; }

} // anonymous namespace

DiagnosticEngine::DiagnosticEngine() = default;

void DiagnosticEngine::enable_wall() {
    enable_warning(WarningFlag::UnusedVariable);
    enable_warning(WarningFlag::IncompletePatterns);
    enable_warning(WarningFlag::OverlappingPatterns);
    enable_warning(WarningFlag::UnhandledEffect);
    enable_warning(WarningFlag::UnmatchedAdt);
}

void DiagnosticEngine::enable_wextra() {
    enable_wall();
    enable_warning(WarningFlag::Shadow);
    enable_warning(WarningFlag::MissingSignature);
    enable_warning(WarningFlag::UnusedImport);
}

void DiagnosticEngine::set_warnings_as_errors(bool v) {
    warnings_as_errors_ = v;
}

void DiagnosticEngine::suppress_all_warnings() {
    suppress_warnings_ = true;
}

void DiagnosticEngine::enable_warning(WarningFlag f) {
    enabled_warnings_.insert(f);
}

void DiagnosticEngine::disable_warning(WarningFlag f) {
    enabled_warnings_.erase(f);
}

void DiagnosticEngine::set_source(std::string_view source, std::string_view filename) {
    source_ = std::string(source);
    filename_ = std::string(filename);

    // Build line offset index
    line_offsets_.clear();
    line_offsets_.push_back(0); // line 1 starts at offset 0
    for (size_t i = 0; i < source_.size(); ++i) {
        if (source_[i] == '\n') {
            line_offsets_.push_back(i + 1);
        }
    }
}

void DiagnosticEngine::error(const SourceLocation& loc, const std::string& message) {
    ++error_count_;
    emit(DiagLevel::Error, loc, message);
}

void DiagnosticEngine::error(const SourceLocation& loc, ErrorCode code, const std::string& message) {
    ++error_count_;
    emit(DiagLevel::Error, loc, message, error_code_str(code));
}

void DiagnosticEngine::warning(const SourceLocation& loc, const std::string& message, WarningFlag flag) {
    if (suppress_warnings_) return;
    if (enabled_warnings_.find(flag) == enabled_warnings_.end()) return;

    std::string flag_str = flag_name(flag);

    if (warnings_as_errors_) {
        ++error_count_;
        emit(DiagLevel::Error, loc, message, flag_str);
    } else {
        ++warning_count_;
        emit(DiagLevel::Warning, loc, message, flag_str);
    }
}

void DiagnosticEngine::note(const SourceLocation& loc, const std::string& message) {
    emit(DiagLevel::Note, loc, message);
}

std::string DiagnosticEngine::flag_name(WarningFlag f) {
    switch (f) {
        case WarningFlag::UnusedVariable:     return "unused-variable";
        case WarningFlag::UnusedImport:       return "unused-import";
        case WarningFlag::Shadow:             return "shadow";
        case WarningFlag::MissingSignature:   return "missing-signature";
        case WarningFlag::IncompletePatterns: return "incomplete-patterns";
        case WarningFlag::OverlappingPatterns:return "overlapping-patterns";
        case WarningFlag::UnhandledEffect:   return "unhandled-effect";
        case WarningFlag::UnmatchedAdt:    return "unmatched-adt";
    }
    return "unknown";
}

void DiagnosticEngine::emit(DiagLevel level, const SourceLocation& loc, const std::string& message,
                            const std::string& flag_str) {
    // Determine level string and color
    const char* level_color = "";
    const char* level_str = "";
    switch (level) {
        case DiagLevel::Error:
            level_color = color_red();
            level_str = "error";
            break;
        case DiagLevel::Warning:
            level_color = color_yellow();
            level_str = "warning";
            break;
        case DiagLevel::Note:
            level_color = color_cyan();
            level_str = "note";
            break;
    }

    // Use the filename from the diagnostic engine if the location doesn't have one
    std::string_view fname = loc.filename.empty() ? std::string_view(filename_) : loc.filename;

    // Print: filename:line:col: level: message [-Wflag]
    std::fprintf(stderr, "%s%s%s:%zu:%zu: %s%s%s: %s",
                 color_bold(), fname.data(), color_reset(),
                 loc.line, loc.column,
                 level_color, level_str, color_reset(),
                 message.c_str());

    if (!flag_str.empty()) {
        // Error codes start with 'E', warning flags get -W prefix
        if (flag_str[0] == 'E')
            std::fprintf(stderr, " %s[%s]%s", level_color, flag_str.c_str(), color_reset());
        else
            std::fprintf(stderr, " %s[-W%s]%s", level_color, flag_str.c_str(), color_reset());
    }
    std::fprintf(stderr, "\n");

    // Show source line with caret if we have source context
    if (!source_.empty() && loc.line > 0 && loc.line <= line_offsets_.size()) {
        std::string src_line = get_source_line(static_cast<int>(loc.line));
        if (!src_line.empty()) {
            // Print the source line with line number
            std::fprintf(stderr, " %4zu | %s\n", loc.line, src_line.c_str());

            // Print the caret line
            std::fprintf(stderr, "      | ");
            // Spaces up to the column, then caret
            for (size_t i = 1; i < loc.column; ++i) {
                // Preserve tabs for alignment
                if (i <= src_line.size() && src_line[i - 1] == '\t') {
                    std::fputc('\t', stderr);
                } else {
                    std::fputc(' ', stderr);
                }
            }
            std::fprintf(stderr, "%s^", color_green());
            // Underline the rest of the token length if available
            if (loc.length > 1) {
                for (size_t i = 1; i < loc.length; ++i) {
                    std::fputc('~', stderr);
                }
            }
            std::fprintf(stderr, "%s\n", color_reset());
        }
    }
}

std::string DiagnosticEngine::get_source_line(int line) const {
    if (line < 1 || static_cast<size_t>(line) > line_offsets_.size()) {
        return "";
    }

    size_t start = line_offsets_[line - 1];
    size_t end;
    if (static_cast<size_t>(line) < line_offsets_.size()) {
        end = line_offsets_[line] - 1; // exclude the newline
    } else {
        end = source_.size();
    }

    if (start >= source_.size()) return "";
    if (end > source_.size()) end = source_.size();

    return source_.substr(start, end - start);
}

// ===== Error Code System =====

std::string error_code_str(ErrorCode code) {
    switch (code) {
        case ErrorCode::E0100: return "E0100";
        case ErrorCode::E0101: return "E0101";
        case ErrorCode::E0102: return "E0102";
        case ErrorCode::E0103: return "E0103";
        case ErrorCode::E0104: return "E0104";
        case ErrorCode::E0105: return "E0105";
        case ErrorCode::E0106: return "E0106";
        case ErrorCode::E0200: return "E0200";
        case ErrorCode::E0201: return "E0201";
        case ErrorCode::E0300: return "E0300";
        case ErrorCode::E0301: return "E0301";
        case ErrorCode::E0302: return "E0302";
        case ErrorCode::E0400: return "E0400";
        case ErrorCode::E0401: return "E0401";
        case ErrorCode::E0402: return "E0402";
        case ErrorCode::E0403: return "E0403";
        case ErrorCode::E0404: return "E0404";
        case ErrorCode::E0500: return "E0500";
        case ErrorCode::E0600: return "E0600";
        case ErrorCode::E0601: return "E0601";
        case ErrorCode::E0602: return "E0602";
        case ErrorCode::E0603: return "E0603";
    }
    return "E????";
}

std::optional<ErrorCode> parse_error_code(const std::string& str) {
    if (str == "E0100") return ErrorCode::E0100;
    if (str == "E0101") return ErrorCode::E0101;
    if (str == "E0102") return ErrorCode::E0102;
    if (str == "E0103") return ErrorCode::E0103;
    if (str == "E0104") return ErrorCode::E0104;
    if (str == "E0105") return ErrorCode::E0105;
    if (str == "E0106") return ErrorCode::E0106;
    if (str == "E0200") return ErrorCode::E0200;
    if (str == "E0201") return ErrorCode::E0201;
    if (str == "E0300") return ErrorCode::E0300;
    if (str == "E0301") return ErrorCode::E0301;
    if (str == "E0302") return ErrorCode::E0302;
    if (str == "E0400") return ErrorCode::E0400;
    if (str == "E0401") return ErrorCode::E0401;
    if (str == "E0402") return ErrorCode::E0402;
    if (str == "E0403") return ErrorCode::E0403;
    if (str == "E0404") return ErrorCode::E0404;
    if (str == "E0500") return ErrorCode::E0500;
    if (str == "E0600") return ErrorCode::E0600;
    if (str == "E0601") return ErrorCode::E0601;
    if (str == "E0602") return ErrorCode::E0602;
    if (str == "E0603") return ErrorCode::E0603;
    return std::nullopt;
}

std::string error_explanation(ErrorCode code) {
    switch (code) {
        case ErrorCode::E0100:
            return
                "Type mismatch [E0100]\n"
                "\n"
                "Two types that should be compatible are not. This usually happens when:\n"
                "  - An operator is applied to incompatible types (e.g., Int + String)\n"
                "  - An if expression's branches return different types\n"
                "  - A function is called with the wrong argument type\n"
                "  - A sequence contains elements of different types\n"
                "\n"
                "Examples:\n"
                "\n"
                "  -- Error: Int and String cannot be unified\n"
                "  1 + \"hello\"\n"
                "\n"
                "  -- Fix: ensure both operands have the same type\n"
                "  1 + 2\n"
                "  \"hello\" ++ \" world\"\n"
                "\n"
                "  -- Error: if branches must have the same type\n"
                "  if true then 1 else \"no\"\n"
                "\n"
                "  -- Fix: return the same type from both branches\n"
                "  if true then 1 else 0\n";

        case ErrorCode::E0101:
            return
                "Infinite type [E0101]\n"
                "\n"
                "A type variable would need to contain itself, creating an infinite type.\n"
                "This happens when an expression's type depends on itself in a circular way.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Error: f is applied to itself\n"
                "  let f x = f in f\n"
                "\n"
                "  -- Fix: ensure recursive types use ADTs\n"
                "  type List a = Cons a (List a) | Nil\n";

        case ErrorCode::E0102:
            return
                "Tuple size mismatch [E0102]\n"
                "\n"
                "A tuple pattern or expression has a different number of elements than expected.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Error: 3-tuple matched against 2-tuple pattern\n"
                "  case (1, 2, 3) of (a, b) -> a end\n"
                "\n"
                "  -- Fix: match all elements\n"
                "  case (1, 2, 3) of (a, b, c) -> a end\n";

        case ErrorCode::E0103:
            return
                "Undefined variable [E0103]\n"
                "\n"
                "A variable is used but has not been defined in the current scope.\n"
                "Check for typos — the compiler will suggest similar names if found.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Error: 'lenght' is not defined\n"
                "  lenght [1, 2, 3]\n"
                "\n"
                "  -- Fix: use the correct name\n"
                "  length [1, 2, 3]\n"
                "\n"
                "Variables are only visible within their defining scope:\n"
                "\n"
                "  let x = 42 in x  -- OK: x is in scope\n"
                "  x                -- Error: x is not in scope here\n";

        case ErrorCode::E0104:
            return
                "Undefined function [E0104]\n"
                "\n"
                "A function is called but has not been defined or imported.\n"
                "Check for typos, or ensure the function is imported from the right module.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Error: 'prnt' is not defined\n"
                "  prnt \"hello\"\n"
                "\n"
                "  -- Fix: use the correct function name\n"
                "  print \"hello\"\n"
                "\n"
                "  -- Or import from a module\n"
                "  import print from Std\\IO in print \"hello\"\n";

        case ErrorCode::E0105:
            return
                "No trait instance [E0105]\n"
                "\n"
                "A trait method is called on a type that doesn't implement the trait.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Error: no instance for 'Num String'\n"
                "  abs \"hello\"\n"
                "\n"
                "  -- Fix: use a type that has a Num instance\n"
                "  abs (-42)    -- Int has Num\n"
                "  abs (-3.14)  -- Float has Num\n";

        case ErrorCode::E0106:
            return
                "Missing trait instances [E0106]\n"
                "\n"
                "A trait is used but no instances have been registered for it.\n"
                "This may mean the trait definition is missing or not imported.\n";

        case ErrorCode::E0200:
            return
                "Unhandled effect operation [E0200]\n"
                "\n"
                "A `perform` calls an effect operation, but no `handle...with` block\n"
                "in scope provides a handler for it.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Error: no handler for State.get\n"
                "  perform State.get ()\n"
                "\n"
                "  -- Fix: wrap in a handle block\n"
                "  handle\n"
                "      perform State.get ()\n"
                "  with\n"
                "      State.get () resume -> resume 42\n"
                "      return val -> val\n"
                "  end\n";

        case ErrorCode::E0201:
            return
                "Effect argument count mismatch [E0201]\n"
                "\n"
                "A `perform` call passes the wrong number of arguments to an effect operation.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Effect declares: put : s -> ()\n"
                "  -- Error: put expects 1 argument, got 0\n"
                "  perform State.put\n"
                "\n"
                "  -- Fix: pass the required argument\n"
                "  perform State.put 42\n";

        case ErrorCode::E0300:
            return
                "Unexpected token [E0300]\n"
                "\n"
                "The parser encountered a token that doesn't fit the expected syntax.\n"
                "Common causes:\n"
                "  - Missing closing bracket, paren, or `end` keyword\n"
                "  - Extra comma or semicolon\n"
                "  - Reserved word used as identifier\n";

        case ErrorCode::E0301:
            return
                "Invalid syntax [E0301]\n"
                "\n"
                "The source code doesn't match any valid Yona syntax.\n"
                "Check the expression structure and ensure all keywords are spelled correctly.\n";

        case ErrorCode::E0302:
            return
                "Invalid pattern [E0302]\n"
                "\n"
                "A pattern in a case expression or function parameter is malformed.\n"
                "\n"
                "Valid pattern types:\n"
                "  42              -- integer literal\n"
                "  \"hello\"         -- string literal\n"
                "  :ok             -- symbol\n"
                "  x               -- variable binding\n"
                "  _               -- wildcard\n"
                "  (a, b)          -- tuple\n"
                "  [h|t]           -- head-tail (list)\n"
                "  []              -- empty list\n"
                "  Some x          -- constructor\n"
                "  (n : Int)       -- typed (sum type)\n"
                "  p1 | p2         -- or-pattern\n";

        case ErrorCode::E0400:
            return
                "Failed to emit object file [E0400]\n"
                "\n"
                "LLVM could not produce an object file from the compiled IR.\n"
                "This is usually an internal compiler error. Please report it.\n";

        case ErrorCode::E0401:
            return
                "Linking failed [E0401]\n"
                "\n"
                "The system linker (cc) failed to produce an executable.\n"
                "Common causes:\n"
                "  - Missing runtime library (compiled_runtime.o)\n"
                "  - Undefined symbols from missing module imports\n"
                "  - System linker not installed\n";

        case ErrorCode::E0402:
            return
                "Unsupported expression [E0402]\n"
                "\n"
                "The codegen encountered an AST node type it cannot compile.\n"
                "This may indicate use of a language feature that is not yet implemented.\n";

        case ErrorCode::E0403:
            return
                "Unknown field [E0403]\n"
                "\n"
                "A field access or update refers to a field name that doesn't exist\n"
                "on the ADT constructor.\n"
                "\n"
                "Example:\n"
                "\n"
                "  type Person = Person { name : String, age : Int }\n"
                "\n"
                "  -- Error: 'email' is not a field of Person\n"
                "  p.email\n"
                "\n"
                "  -- Fix: use a field that exists\n"
                "  p.name\n";

        case ErrorCode::E0404:
            return
                "Pipe requires function [E0404]\n"
                "\n"
                "The pipe operator (|> or <|) requires a function on the receiving side.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- Error: 42 is not a function\n"
                "  \"hello\" |> 42\n"
                "\n"
                "  -- Fix: pipe into a function\n"
                "  \"hello\" |> length\n";
        case ErrorCode::E0500:
            return
                "Refinement predicate not satisfied [E0500]\n"
                "\n"
                "A function expects a refined type, but the compiler cannot prove\n"
                "that the argument satisfies the refinement predicate.\n"
                "\n"
                "Example:\n"
                "\n"
                "  type NonEmpty a = { xs : [a] | length xs > 0 }\n"
                "  head : NonEmpty a -> a\n"
                "\n"
                "  -- Error: cannot prove 'someList' is non-empty\n"
                "  head someList\n"
                "\n"
                "  -- Fix: establish the fact via pattern matching\n"
                "  case someList of\n"
                "      [h|t] -> head someList   -- OK: [h|t] proves non-empty\n"
                "      []    -> defaultValue\n"
                "  end\n"
                "\n"
                "  -- Or use a literal that is obviously non-empty\n"
                "  head [1, 2, 3]   -- OK: 3 elements\n"
                "\n"
                "For integer refinements:\n"
                "\n"
                "  type Port = { n : Int | n > 0 && n < 65536 }\n"
                "\n"
                "  -- Error: cannot prove x > 0 && x < 65536\n"
                "  connect host x\n"
                "\n"
                "  -- Fix: use a known-valid literal or check with if\n"
                "  connect host 8080\n"
                "  if x > 0 && x < 65536 then connect host x else error\n";
        case ErrorCode::E0600:
            return
                "Use after consume [E0600]\n"
                "\n"
                "A linear value (wrapped in the `Linear` ADT) was used after it\n"
                "was already consumed by a pattern match or function call.\n"
                "\n"
                "Example:\n"
                "\n"
                "  let conn = Linear (tcpConnect host port) in\n"
                "  case conn of Linear fd -> close fd end   -- conn consumed\n"
                "  send conn \"hello\"                        -- ERROR: already consumed\n"
                "\n"
                "  -- Fix: use the value before consuming it\n"
                "  let conn = Linear (tcpConnect host port) in\n"
                "  case conn of Linear fd ->\n"
                "      do; send fd \"hello\"; close fd; end\n"
                "  end\n";

        case ErrorCode::E0601:
            return
                "Branch inconsistency [E0601]\n"
                "\n"
                "A linear value is consumed in one branch of an if/case expression\n"
                "but not the other. Both branches must consume the same linear values.\n"
                "\n"
                "Example:\n"
                "\n"
                "  -- ERROR: conn consumed in then-branch but not else-branch\n"
                "  if ready then\n"
                "      case conn of Linear fd -> close fd end\n"
                "  else\n"
                "      0   -- conn still live here!\n"
                "\n"
                "  -- Fix: consume in both branches\n"
                "  if ready then\n"
                "      case conn of Linear fd -> do; send fd msg; close fd; end end\n"
                "  else\n"
                "      case conn of Linear fd -> close fd end\n";

        case ErrorCode::E0602:
            return
                "Resource leak [E0602]\n"
                "\n"
                "A linear value went out of scope without being consumed.\n"
                "This likely means a resource (file, socket, process) is leaked.\n"
                "\n"
                "Example:\n"
                "\n"
                "  let conn = Linear (tcpConnect host port) in\n"
                "  42   -- WARNING: conn never consumed\n"
                "\n"
                "  -- Fix: consume the value via pattern match\n"
                "  let conn = Linear (tcpConnect host port) in\n"
                "  case conn of Linear fd ->\n"
                "      let data = recv fd 1024 in\n"
                "      do; close fd; data; end\n"
                "  end\n";

        case ErrorCode::E0603:
            return
                "Invalid borrow parameter [E0603]\n"
                "\n"
                "`@borrow` marks a parameter as read-only for the function body: it must not\n"
                "be returned, stored in a collection literal, captured by a nested lambda, or\n"
                "used as a case scrutinee (head/tail consumes the seq). It is only supported\n"
                "on simple identifier parameters.\n"
                "\n"
                "Example (invalid — `s` is returned):\n"
                "\n"
                "  let f @borrow s = s in ...\n"
                "\n"
                "Fix: remove `@borrow`, or change the body so the parameter is only read.\n";
    }
    return "";
}

} // namespace yona::compiler
