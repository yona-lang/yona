#ifndef YONA_SEMANTICS_GENERICFUNCTIONSOURCE_H
#define YONA_SEMANTICS_GENERICFUNCTIONSOURCE_H

#include "yona/Support/SourceManager.h"
#include "yona/Syntax/Ast.h"

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace yona::semantics {

/// Constructor information required to parse an interface GENFN body.
/// The semantic source service owns parsing; consumers never construct a
/// parser or retain parser-owned state.
struct GenericConstructorMetadata final {
  std::string Name;
  std::string TypeName;
  int Tag = 0;
  int Arity = 0;
  std::vector<std::string> FieldNames;
};

/// A parsed generic module and the source buffer that backs its ranges.
/// The unique root owns all descendants and Sources shares ownership of their
/// range storage. Borrowed node pointers are invalidated by move or destruction
/// of this object.
struct ParsedGenericModule final {
  std::shared_ptr<SourceManager> Sources;
  std::unique_ptr<ast::ModuleDecl> Module;
};

/// Parses canonical interface GENFN source with explicit constructor
/// metadata. Inputs are borrowed for the call and the service retains no state.
/// Failures contain owned parser diagnostics and expose no partial AST.
/// Distinct calls are thread-safe.
class GenericFunctionSourceService final {
public:
  [[nodiscard]] std::expected<ParsedGenericModule, std::vector<std::string>>
  parseGenericModule(
      const std::string &LocalName, const std::string &SourceText,
      const std::vector<GenericConstructorMetadata> &Constructors) const;
};

} // namespace yona::semantics

#endif /* YONA_SEMANTICS_GENERICFUNCTIONSOURCE_H */
