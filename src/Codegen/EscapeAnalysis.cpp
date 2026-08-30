//
// EscapeAnalysis.cpp â€” Determines which let-bound values don't escape their
// scope.
//

#include "yona/Codegen/EscapeAnalysis.h"

#include <functional>

namespace yona::compiler::codegen {
using ast::ApplyExpr;
using ast::AstNode;
using ast::CaseExpr;
using ast::ExprCall;
using ast::FunctionExpr;
using ast::IdentifierExpr;
using ast::LetExpr;
using ast::ScopedNode;
using ast::AST_APPLY_EXPR;
using ast::AST_CASE_EXPR;
using ast::AST_DO_EXPR;
using ast::AST_FUNCTION_EXPR;
using ast::AST_IDENTIFIER_EXPR;
using ast::AST_IF_EXPR;
using ast::AST_LAMBDA_ALIAS;
using ast::AST_LET_EXPR;
using ast::AST_PATTERN_VALUE;
using ast::AST_TUPLE_EXPR;
using ast::AST_VALUE_ALIAS;
using ast::AST_VALUES_SEQUENCE_EXPR;
using ast::BodyWithGuards;
using ast::BodyWithoutGuards;
using ast::DoExpr;
using ast::ExprNode;
using ast::IfExpr;
using ast::LambdaAlias;
using ast::NameCall;
using ast::PatternValue;
using ast::TupleExpr;
using ast::ValueAlias;
using ast::ValueExpr;
using ast::ValuesSequenceExpr;

// Forward declaration
static void
collect_lambda_captures(FunctionExpr *func,
                        std::unordered_set<std::string> &escaping,
                        const std::unordered_set<std::string> &let_bound);

// Collect variable names that appear in escaping positions.
// A variable escapes if it's in return position, captured by a closure,
// or passed to an opaque (extern/unknown) function.
void EscapeAnalysis::collect_escaping(
    AstNode *node, std::unordered_set<std::string> &escaping,
    const std::unordered_set<std::string> &let_bound,
    const std::unordered_set<std::string> &known_local_functions,
    bool in_return_position) {
  if (!node)
    return;
  auto ty = node->get_type();

  // Identifier in return position â†’ escapes
  if (ty == ast::AST_IDENTIFIER_EXPR) {
    auto *id = static_cast<IdentifierExpr *>(node);
    std::string name = id->name->value;
    if (in_return_position && let_bound.count(name))
      escaping.insert(name);
    return;
  }

  // Let expression: the body is in return position if the let is
  if (ty == ast::AST_LET_EXPR) {
    auto *le = static_cast<LetExpr *>(node);
    // Binding values are NOT in return position
    for (auto *alias : le->aliases) {
      if (alias->get_type() == ast::AST_VALUE_ALIAS) {
        auto *va = static_cast<ValueAlias *>(alias);
        collect_escaping(va->expr, escaping, let_bound, known_local_functions,
                         false);
      } else if (alias->get_type() == ast::AST_LAMBDA_ALIAS) {
        auto *la = static_cast<LambdaAlias *>(alias);
        // Lambda body captures â€” mark free vars as escaping
        collect_lambda_captures(la->lambda, escaping, let_bound);
      }
    }
    // Body is in return position if the let is
    collect_escaping(le->expr, escaping, let_bound, known_local_functions,
                     in_return_position);
    return;
  }

  // If expression: both branches are in return position
  if (ty == ast::AST_IF_EXPR) {
    auto *ie = static_cast<IfExpr *>(node);
    collect_escaping(ie->condition, escaping, let_bound, known_local_functions,
                     false);
    collect_escaping(ie->thenExpr, escaping, let_bound, known_local_functions,
                     in_return_position);
    collect_escaping(ie->elseExpr, escaping, let_bound, known_local_functions,
                     in_return_position);
    return;
  }

  // Case expression: all clause bodies are in return position
  if (ty == ast::AST_CASE_EXPR) {
    auto *ce = static_cast<CaseExpr *>(node);
    collect_escaping(ce->expr, escaping, let_bound, known_local_functions,
                     false);
    for (auto *clause : ce->clauses)
      collect_escaping(clause->body, escaping, let_bound, known_local_functions,
                       in_return_position);
    return;
  }

  // Do expression: last expression is in return position
  if (ty == ast::AST_DO_EXPR) {
    auto *de = static_cast<DoExpr *>(node);
    for (size_t i = 0; i < de->steps.size(); i++) {
      bool is_last = (i == de->steps.size() - 1);
      collect_escaping(de->steps[i], escaping, let_bound, known_local_functions,
                       is_last && in_return_position);
    }
    return;
  }

  // Function/lambda expression: variables used inside are captured â†’ escape
  if (ty == ast::AST_FUNCTION_EXPR) {
    auto *fe = static_cast<FunctionExpr *>(node);
    collect_lambda_captures(fe, escaping, let_bound);
    return;
  }

  // Apply expression: arguments to opaque functions escape
  if (ty == ast::AST_APPLY_EXPR) {
    auto *ae = static_cast<ApplyExpr *>(node);
    // Determine if the callee is a known local function
    std::string callee_name;
    if (auto *nc = dynamic_cast<NameCall *>(ae->call))
      callee_name = nc->name->value;

    bool callee_is_local = known_local_functions.count(callee_name) > 0;

    // Arguments to opaque functions escape
    for (auto &arg : ae->args) {
      AstNode *arg_node =
          std::holds_alternative<ExprNode *>(arg)
              ? static_cast<AstNode *>(std::get<ExprNode *>(arg))
              : static_cast<AstNode *>(std::get<ValueExpr *>(arg));
      if (!callee_is_local) {
        // Opaque call â€” arguments escape
        collect_escaping(arg_node, escaping, let_bound, known_local_functions,
                         true);
      } else {
        // Local call â€” arguments don't escape (they're borrowed)
        collect_escaping(arg_node, escaping, let_bound, known_local_functions,
                         false);
      }
    }

    // The apply itself might be in return position (result escapes),
    // but that's about the RESULT, not the arguments
    // Recurse into nested applies
    if (auto *ec = dynamic_cast<ExprCall *>(ae->call))
      collect_escaping(ec->expr, escaping, let_bound, known_local_functions,
                       false);

    return;
  }

  // Collection constructions: elements don't escape by themselves
  // (the collection escapes, not the elements stored in it)
  if (ty == ast::AST_VALUES_SEQUENCE_EXPR) {
    auto *se = static_cast<ValuesSequenceExpr *>(node);
    for (auto *elem : se->values)
      collect_escaping(elem, escaping, let_bound, known_local_functions, false);
    return;
  }
  if (ty == ast::AST_TUPLE_EXPR) {
    auto *te = static_cast<TupleExpr *>(node);
    for (auto *elem : te->values)
      collect_escaping(elem, escaping, let_bound, known_local_functions, false);
    return;
  }

  // Binary ops, comparisons â€” operands don't escape
  // (result is a new value, not the operands)
}

// Helper: collect variables from let_bound that appear free in a lambda
static void
collect_lambda_captures(FunctionExpr *func,
                        std::unordered_set<std::string> &escaping,
                        const std::unordered_set<std::string> &let_bound) {
  // Build the set of parameters (bound inside the lambda)
  std::unordered_set<std::string> lambda_bound;
  for (auto *pat : func->patterns) {
    if (pat->get_type() == ast::AST_PATTERN_VALUE) {
      auto *pv = static_cast<PatternValue *>(pat);
      if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr))
        lambda_bound.insert((*id)->name->value);
    }
  }

  // Walk the body looking for identifiers from let_bound that aren't lambda
  // params
  std::function<void(AstNode *)> walk = [&](AstNode *node) {
    if (!node)
      return;
    if (node->get_type() == ast::AST_IDENTIFIER_EXPR) {
      auto *id = static_cast<IdentifierExpr *>(node);
      std::string name = id->name->value;
      if (let_bound.count(name) && !lambda_bound.count(name))
        escaping.insert(name);
      return;
    }
    if (node->get_type() == ast::AST_LET_EXPR) {
      auto *le = static_cast<LetExpr *>(node);
      for (auto *alias : le->aliases) {
        if (alias->get_type() == ast::AST_VALUE_ALIAS) {
          walk(static_cast<ValueAlias *>(alias)->expr);
        } else if (alias->get_type() == ast::AST_LAMBDA_ALIAS) {
          walk(static_cast<LambdaAlias *>(alias)->lambda);
        }
      }
      walk(le->expr);
      return;
    }
    if (node->get_type() == ast::AST_IF_EXPR) {
      auto *ie = static_cast<IfExpr *>(node);
      walk(ie->condition);
      walk(ie->thenExpr);
      walk(ie->elseExpr);
      return;
    }
    if (node->get_type() == ast::AST_CASE_EXPR) {
      auto *ce = static_cast<CaseExpr *>(node);
      walk(ce->expr);
      for (auto *clause : ce->clauses)
        walk(clause->body);
      return;
    }
    if (node->get_type() == ast::AST_APPLY_EXPR) {
      auto *ae = static_cast<ApplyExpr *>(node);
      if (auto *nc = dynamic_cast<NameCall *>(ae->call))
        walk(nc->name);
      if (auto *ec = dynamic_cast<ExprCall *>(ae->call))
        walk(ec->expr);
      for (auto &arg : ae->args) {
        if (std::holds_alternative<ExprNode *>(arg))
          walk(std::get<ExprNode *>(arg));
        else
          walk(std::get<ValueExpr *>(arg));
      }
      return;
    }
    if (node->get_type() == ast::AST_DO_EXPR) {
      auto *de = static_cast<DoExpr *>(node);
      for (auto *e : de->steps)
        walk(e);
      return;
    }
    if (node->get_type() == ast::AST_FUNCTION_EXPR) {
      // Nested lambda â€” recursively check
      auto *fe = static_cast<FunctionExpr *>(node);
      collect_lambda_captures(fe, escaping, let_bound);
      return;
    }
  };

  for (auto *body : func->bodies) {
    if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body))
      walk(bwg->expr);
    else if (auto *bwg2 = dynamic_cast<BodyWithGuards *>(body)) {
      walk(bwg2->guard);
      walk(bwg2->expr);
    }
  }
}

std::unordered_set<std::string> EscapeAnalysis::analyze(
    FunctionExpr *func,
    const std::unordered_set<std::string> &known_local_functions) {
  // Collect all let-bound variable names in the function body
  std::unordered_set<std::string> let_bound;
  std::function<void(AstNode *)> collect_lets = [&](AstNode *node) {
    if (!node)
      return;
    if (node->get_type() == ast::AST_LET_EXPR) {
      auto *le = static_cast<LetExpr *>(node);
      for (auto *alias : le->aliases) {
        if (alias->get_type() == ast::AST_VALUE_ALIAS)
          let_bound.insert(
              static_cast<ValueAlias *>(alias)->identifier->name->value);
        else if (alias->get_type() == ast::AST_LAMBDA_ALIAS)
          let_bound.insert(static_cast<LambdaAlias *>(alias)->name->value);
      }
      collect_lets(le->expr);
    }
    if (node->get_type() == ast::AST_IF_EXPR) {
      auto *ie = static_cast<IfExpr *>(node);
      collect_lets(ie->thenExpr);
      collect_lets(ie->elseExpr);
    }
    if (node->get_type() == ast::AST_CASE_EXPR) {
      auto *ce = static_cast<CaseExpr *>(node);
      for (auto *clause : ce->clauses)
        collect_lets(clause->body);
    }
    if (node->get_type() == ast::AST_DO_EXPR) {
      auto *de = static_cast<DoExpr *>(node);
      for (auto *e : de->steps)
        collect_lets(e);
    }
  };

  for (auto *body : func->bodies) {
    if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body))
      collect_lets(bwg->expr);
  }

  if (let_bound.empty())
    return let_bound; // nothing to analyze

  // Collect escaping variables
  std::unordered_set<std::string> escaping;
  for (auto *body : func->bodies) {
    if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body))
      collect_escaping(bwg->expr, escaping, let_bound, known_local_functions,
                       true);
  }

  // Non-escaping = let_bound - escaping
  std::unordered_set<std::string> non_escaping;
  for (auto &name : let_bound) {
    if (!escaping.count(name))
      non_escaping.insert(name);
  }

  return non_escaping;
}

std::unordered_set<std::string> EscapeAnalysis::analyze_expr(
    ExprNode *expr,
    const std::unordered_set<std::string> &known_local_functions) {
  // For standalone expressions, wrap in a synthetic analysis
  std::unordered_set<std::string> let_bound;
  std::function<void(AstNode *)> collect_lets = [&](AstNode *node) {
    if (!node)
      return;
    if (node->get_type() == ast::AST_LET_EXPR) {
      auto *le = static_cast<LetExpr *>(node);
      for (auto *alias : le->aliases) {
        if (alias->get_type() == ast::AST_VALUE_ALIAS)
          let_bound.insert(
              static_cast<ValueAlias *>(alias)->identifier->name->value);
        else if (alias->get_type() == ast::AST_LAMBDA_ALIAS)
          let_bound.insert(static_cast<LambdaAlias *>(alias)->name->value);
      }
      collect_lets(le->expr);
    }
  };
  collect_lets(expr);

  if (let_bound.empty())
    return {};

  std::unordered_set<std::string> escaping;
  collect_escaping(expr, escaping, let_bound, known_local_functions, true);

  std::unordered_set<std::string> non_escaping;
  for (auto &name : let_bound) {
    if (!escaping.count(name))
      non_escaping.insert(name);
  }
  return non_escaping;
}

} // namespace yona::compiler::codegen
