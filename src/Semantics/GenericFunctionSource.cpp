#include "yona/Semantics/GenericFunctionSource.h"

#include "yona/Syntax/Parser.h"

#include <string>
#include <utility>
#include <vector>

namespace yona::semantics {

std::expected<ParsedGenericModule, std::vector<std::string>>
GenericFunctionSourceService::parseGenericModule(
    const std::string &LocalName, const std::string &SourceText,
    const std::vector<GenericConstructorMetadata> &Constructors) const {
  parser::Parser Parser;
  for (const auto &Constructor : Constructors) {
    Parser.register_constructor(Constructor.Name, Constructor.TypeName,
                                Constructor.Tag, Constructor.Arity,
                                Constructor.FieldNames);
  }

  const std::string ModuleSource =
      "module __Import\nexport " + LocalName + "\n" + SourceText + "\n";
  auto Parsed = Parser.parseModule(ModuleSource, "<imported>");
  if (!Parsed) {
    std::vector<std::string> Diagnostics;
    Diagnostics.reserve(Parsed.error().size());
    for (const auto &Error : Parsed.error())
      Diagnostics.push_back(Error.Message);
    return std::unexpected(std::move(Diagnostics));
  }

  ParsedGenericModule Result;
  Result.Sources = std::move(Parsed->Sources);
  Result.Module = std::move(Parsed->Module);
  return Result;
}

} // namespace yona::semantics
