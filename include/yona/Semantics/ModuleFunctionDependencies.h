#ifndef YONA_SEMANTICS_MODULEFUNCTIONDEPENDENCIES_H
#define YONA_SEMANTICS_MODULEFUNCTIONDEPENDENCIES_H
#include <functional>
#include <string>
#include <vector>

namespace yona::ast {
class FunctionExpr;
class ModuleDecl;
} // namespace yona::ast

namespace yona::compiler::typechecker {

struct ModuleFunctionComponent {
  /// Borrowed function nodes owned by the analyzed module AST.
  std::vector<ast::FunctionExpr *> functions;
  bool recursive = false;
};

using ModuleExportResolver =
    std::function<std::vector<std::string>(const std::string &module_fqn)>;

/// Return module-function dependency components in callee-first order.
/// Lexical binders shadow module names, and function-valued references count
/// as dependencies even when they are not immediately applied. The returned
/// containers are owned, but their function pointers remain valid only for the
/// module AST lifetime. A null module returns an empty vector. The resolver is
/// invoked synchronously; its exceptions and thread-safety policy propagate.
/// Concurrent calls require an immutable module and thread-safe resolver.
std::vector<ModuleFunctionComponent>
module_function_components(ast::ModuleDecl *module,
                           ModuleExportResolver resolve_exports = {});

} // namespace yona::compiler::typechecker

#endif /* YONA_SEMANTICS_MODULEFUNCTIONDEPENDENCIES_H */
