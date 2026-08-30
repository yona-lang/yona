#ifndef YONA_SEMANTICS_LINEARITYCHECKER_H
#define YONA_SEMANTICS_LINEARITYCHECKER_H

/// Linearity checker for Yona.
///
/// Tracks values of type `Linear a` (a built-in ADT) through the program.
/// A Linear value must be pattern-matched exactly once:
///   - Error if used after consumption (use-after-consume)
///   - Warning [E0602] (`-Wlinear-leak`) if it goes out of scope without being
///   consumed
///   - Error if branches disagree on consumption (branch inconsistency)
///
/// The `Linear` ADT is the mechanism — wrapping a resource handle in `Linear`
/// creates a compile-time obligation to unwrap it via pattern matching.
///
/// Producers are discovered from types: a let-bound expression whose inferred
/// type is `Linear _` (or a product containing `Linear _`) is tracked. The
/// `Linear` constructor is also recognized when types are unavailable.
/// The only way to extract the inner value is `case x of Linear fd -> ...`.
/// This pattern match is the consumption point.

#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Ast.h"

#include <string>
#include <unordered_map>

namespace yona::compiler::typechecker {

class TypeChecker;
struct MonoType;

/// Tracks the lifecycle status of a linear variable.
enum class LinearStatus {
  Live,     ///< Created but not yet consumed
  Consumed, ///< Consumed by pattern match or transfer
};

/// Environment tracking linear variable obligations.
struct LinearEnv {
  /// var_name → status
  std::unordered_map<std::string, LinearStatus> vars;

  /// var_name → source location where it was created
  std::unordered_map<std::string, SourceRange> created_at;

  /// var_name → source location where it was consumed
  std::unordered_map<std::string, SourceRange> consumed_at;

  /// Mark a variable as a live linear value.
  void create(const std::string &name, const SourceRange &loc);

  /// Mark a variable as consumed. Returns false if already consumed.
  bool consume(const std::string &name, const SourceRange &loc);

  /// Check if a variable is live (created but not yet consumed).
  bool is_live(const std::string &name) const;

  /// Check if a variable is consumed.
  bool is_consumed(const std::string &name) const;

  /// Check if a variable is tracked at all.
  bool is_tracked(const std::string &name) const;

  /// Get all live (unconsumed) variables.
  std::vector<std::string> live_vars() const;
};

/// Stateful analysis borrowing its DiagnosticEngine and optional TypeChecker.
///
/// Both dependencies must outlive the checker. check() borrows the AST for the
/// call, mutates analysis counters, and emits into the diagnostic engine;
/// callers must serialize it with every access to the same checker, diagnostic
/// engine, or TypeChecker. Violations are reported as diagnostics rather than
/// by returning a partial result.
class LinearityChecker {
public:
  /// \p tc optional; when set, producers are those whose inferred type is
  /// `Linear _` or a product of `Linear` values.
  explicit LinearityChecker(DiagnosticEngine &diag, TypeChecker *tc = nullptr);

  /// Check an AST tree for linearity violations. The tree must stay alive and
  /// immutable for the duration of the call.
  void check(ast::AstNode *node);

  bool has_errors() const { return error_count_ > 0; }

private:
  void check_node(ast::AstNode *node, LinearEnv &env);
  void check_let(ast::LetExpr *node, LinearEnv &env);
  void check_case(ast::CaseExpr *node, LinearEnv &env);
  void check_if(ast::IfExpr *node, LinearEnv &env);
  void check_apply(ast::ApplyExpr *node, LinearEnv &env);
  void check_with(ast::WithExpr *node, LinearEnv &env);
  void check_function(ast::FunctionExpr *node, LinearEnv &env);
  void check_module(ast::ModuleDecl *node, LinearEnv &env);

  /// Warn about any live linear variables going out of scope (E0602).
  void warn_unconsumed(const LinearEnv &env);
  void warn_leak(const std::string &name, const SourceRange &loc);

  /// Zonked inferred type of \p expr, or nullptr.
  const MonoType *type_of_expr(ast::AstNode *expr);

  /// True if \p expr produces a `Linear _` value (type-directed, with
  /// `Linear` constructor fallback when types are absent/unresolved).
  bool expr_produces_linear(ast::AstNode *expr);

  /// Mark pattern identifiers whose corresponding type is `Linear _`.
  void track_linear_pattern(ast::PatternNode *pat, const MonoType *ty,
                            LinearEnv &env, const SourceRange &loc);

  /// Check if a constructor name indicates a Linear wrapper.
  static bool is_linear_constructor(const std::string &name) {
    return name == "Linear";
  }

  DiagnosticEngine &diag_;
  TypeChecker *tc_ = nullptr;
  int error_count_ = 0;
};

} // namespace yona::compiler::typechecker

#endif /* YONA_SEMANTICS_LINEARITYCHECKER_H */
