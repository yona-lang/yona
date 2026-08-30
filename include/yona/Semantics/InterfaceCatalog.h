#ifndef YONA_SEMANTICS_INTERFACECATALOG_H
#define YONA_SEMANTICS_INTERFACECATALOG_H

#include "yona/Interface/Reader.h"
#include "yona/Semantics/TypeChecker.h"

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yona::parser {
class Parser;
}

namespace yona::semantics {

/// Semantics-owned view of canonical module interfaces.
///
/// The catalog is the import source used by analysis-only clients. It owns
/// every loaded interface and never constructs an LLVM/code-generation
/// session. Search paths and cached modules belong to one catalog instance;
/// loading and import callbacks mutate the cache, so callers must externally
/// synchronize all operations on one catalog that can overlap them. Returned
/// module pointers remain valid until any search-path update clears the cache.
class InterfaceCatalog final : public compiler::typechecker::ImportTypeSource {
public:
  using LoadResult = std::expected<const interface::InterfaceModule *,
                                   std::vector<interface::ParseError>>;
  using PreludeResult = std::expected<bool, std::vector<interface::ParseError>>;

  InterfaceCatalog() = default;
  explicit InterfaceCatalog(std::vector<std::string> SearchPaths);

  /// Replace search paths and invalidate all cached modules.
  void setSearchPaths(std::vector<std::string> SearchPaths);

  /// Append a search root if it is not already present.
  void addSearchPath(std::string SearchPath);

  /// Append existing directories from YONA_PATH. No network or shell is used.
  void appendEnvironmentSearchPaths();

  /// Borrow the roots container until catalog destruction. Path updates are
  /// visible through the same reference and may invalidate its iterators and
  /// element references.
  [[nodiscard]] const std::vector<std::string> &searchPaths() const noexcept {
    return SearchPaths;
  }

  /// Load one canonical interface. A null pointer means no matching file;
  /// malformed/read-error files are returned as errors.
  [[nodiscard]] LoadResult loadModule(std::string_view ModuleFqn);

  /// Install Prelude constructors, ADTs, functions, traits, instances, and
  /// compiler intrinsics into Parser and TypeChecker. Returns false when no
  /// Prelude interface exists in the configured roots. Interface read/parse
  /// failures are returned before installation; a structurally invalid type
  /// descriptor throws `std::invalid_argument`, after which callers must
  /// discard the Parser and TypeChecker passed to this operation.
  [[nodiscard]] PreludeResult
  installPrelude(parser::Parser &Parser,
                 compiler::typechecker::TypeChecker &TypeChecker);

  std::optional<compiler::typechecker::ImportedFnSig>
  imported_function_sig(const std::string &ModuleFqn,
                        const std::string &Name) override;
  std::vector<std::string>
  imported_module_exports(const std::string &ModuleFqn) override;
  std::vector<compiler::typechecker::ImportedInstanceSig>
  imported_instances(const std::string &ModuleFqn) override;

private:
  std::vector<std::string> SearchPaths;
  std::map<std::string, std::optional<interface::InterfaceModule>> Modules;
};

} // namespace yona::semantics

#endif /* YONA_SEMANTICS_INTERFACECATALOG_H */
