#ifndef YONA_SYNTAX_YONASTYLE_H
#define YONA_SYNTAX_YONASTYLE_H
#include "yona/Support/SourceManager.h"
#include "yona/Syntax/Lexer.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace yona::syntax {

struct YonaStyleDiagnostic {
  SourceRange Location;
  std::string Message;
  std::shared_ptr<const SourceManager> Sources;
};

/// Checks identifier spelling using the compiler lexer. Callers must parse the
/// source first, so style diagnostics are never a substitute for syntax
/// diagnostics and never attempt to format comments or trivia. Input views are
/// borrowed only for the call. Returned diagnostics and lexer errors share
/// ownership of a fresh SourceManager, so their ranges remain resolvable after
/// return. The checker has no mutable global state and supports concurrent
/// calls. Lexical failures return no partial style-diagnostic vector.
std::expected<std::vector<YonaStyleDiagnostic>, std::vector<lexer::LexError>>
checkYonaStyle(std::string_view Source, std::string_view Filename = "<input>");

} // namespace yona::syntax

#endif /* YONA_SYNTAX_YONASTYLE_H */
