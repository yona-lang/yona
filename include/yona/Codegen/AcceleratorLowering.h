#ifndef YONA_CODEGEN_ACCELERATORLOWERING_H
#define YONA_CODEGEN_ACCELERATORLOWERING_H

#include "yona/Syntax/Ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yona::compiler {

/// Fixed Std\Gpu columnar kernels that transparent lowering may select.
/// Driven by IntArray / FloatArray stdlib ops + lambda shape, not a GPU
/// export-name allowlist.
enum class AccelKernel : uint8_t {
  None = 0,
  IntMapAdd,
  IntMapMul,
  IntMapSquare,
  IntFilterGt,
  IntFilterLt,
  IntReduceSum,
  FloatScale,
  FloatReduceSum,
};

struct AccelMatch {
  AccelKernel kernel = AccelKernel::None;
  ast::ApplyExpr *site = nullptr;
  ast::ExprNode *scalar = nullptr;
  ast::ExprNode *array = nullptr;
  bool scalar_is_literal = false;
  /// When true, IntMapAdd emits `-scalar` (for `\x -> x - k` → mapAdd).
  bool negate_scalar = false;
  int64_t lit_i64 = 0;
  double lit_f64 = 0.0;
  const char *abi_symbol = nullptr;
  const char *kernel_name = nullptr;
  std::string binding;
};

const char *accel_kernel_name(AccelKernel k);
const char *accel_kernel_abi_symbol(AccelKernel k);

/// Classify a function identified by its defining module and local Yona name.
/// `args` are juxtaposition arguments in application order (function first).
std::optional<AccelMatch>
match_column_kernel(std::string_view module_fqn, std::string_view local_name,
                    const std::vector<ast::ExprNode *> &args,
                    ast::ApplyExpr *site);

/// Flatten juxtaposition, resolve the stdlib symbol from a ModuleCall or an
/// enclosing `import … from Std\IntArray` / `Std\FloatArray`, then classify.
std::optional<AccelMatch> match_transparent_apply(ast::ApplyExpr *site);

/// True when `site` is an IntArray/FloatArray map/filter/foldl with an inline
/// lambda that is **not** in the fixed kernel library (e.g. `\x -> x + x * x`).
/// Used by `--strict-accelerator` (E0700) so unsupported shapes are rejected
/// instead of silently staying on the host path.
bool is_unlowerable_column_apply(ast::ApplyExpr *site);

std::vector<AccelMatch> collect_transparent_matches(ast::AstNode *root);
std::vector<AccelMatch>
collect_transparent_matches_module(ast::ModuleDecl *mod);

} // namespace yona::compiler

#endif /* YONA_CODEGEN_ACCELERATORLOWERING_H */
