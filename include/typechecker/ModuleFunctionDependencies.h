#ifndef YONA_TYPECHECKER_MODULE_FUNCTION_DEPENDENCIES_H
#define YONA_TYPECHECKER_MODULE_FUNCTION_DEPENDENCIES_H

#include <functional>
#include <string>
#include <vector>

namespace yona::ast {
class FunctionExpr;
class ModuleDecl;
} // namespace yona::ast

namespace yona::compiler::typechecker {

struct ModuleFunctionComponent {
  std::vector<ast::FunctionExpr *> functions;
  bool recursive = false;
};

using ModuleExportResolver = std::function<std::vector<std::string>(const std::string &module_fqn)>;

/// Return module-function dependency components in callee-first order.
/// Lexical binders shadow module names, and function-valued references count
/// as dependencies even when they are not immediately applied.
std::vector<ModuleFunctionComponent> module_function_components(ast::ModuleDecl *module, ModuleExportResolver resolve_exports = {});

} // namespace yona::compiler::typechecker

#endif
