#ifndef YONA_TYPEDIR_PARSER_H
#define YONA_TYPEDIR_PARSER_H
#include "yona/TypedIr/TypedIr.h"

#include <expected>
#include <string_view>
namespace yona::typed_ir {
struct ParseError {
  SourceRange Range;
  std::string Message;
};
using ParseModuleResult = std::expected<Module, std::vector<ParseError>>;
[[nodiscard]] ParseModuleResult
parseModule(std::string_view Text,
            std::shared_ptr<const SourceManager> Sources);
} // namespace yona::typed_ir
#endif
