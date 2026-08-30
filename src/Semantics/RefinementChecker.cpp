/// RefinementChecker â€” flow-sensitive refinement type verification.
///
/// Tracks integer intervals and collection non-emptiness through
/// let bindings, pattern matches, guards, and conditionals.
/// Verifies built-in and user-registered refinement predicates at call sites.

#include "yona/Semantics/RefinementChecker.h"

#include "yona/Semantics/TypeChecker.h"

namespace yona::compiler::typechecker {
using ast::AstNode;
using ast::ExprNode;
using ast::IdentifierExpr;
using ast::IntegerExpr;
using ast::LetExpr;
using ast::LiteralExpr;
using ast::AddExpr;
using ast::ApplyExpr;
using ast::AST_APPLY_EXPR;
using ast::AST_CASE_EXPR;
using ast::AST_CONS_LEFT_EXPR;
using ast::AST_DIVIDE_EXPR;
using ast::AST_DO_EXPR;
using ast::AST_FUNCTION_EXPR;
using ast::AST_HEAD_TAILS_PATTERN;
using ast::AST_IDENTIFIER_EXPR;
using ast::AST_IF_EXPR;
using ast::AST_IMPORT_EXPR;
using ast::AST_INTEGER_EXPR;
using ast::AST_LET_EXPR;
using ast::AST_MAIN;
using ast::AST_MODULE_DECL;
using ast::AST_PATTERN_VALUE;
using ast::AST_SEQ_PATTERN;
using ast::AST_UNDERSCORE_PATTERN;
using ast::AST_VALUES_SEQUENCE_EXPR;
using ast::BinaryOpExpr;
using ast::BodyWithGuards;
using ast::BodyWithoutGuards;
using ast::CaseExpr;
using ast::DivideExpr;
using ast::DoExpr;
using ast::EqExpr;
using ast::FunctionExpr;
using ast::GteExpr;
using ast::GtExpr;
using ast::IfExpr;
using ast::ImportExpr;
using ast::LambdaAlias;
using ast::LogicalAndExpr;
using ast::LteExpr;
using ast::LtExpr;
using ast::MainNode;
using ast::ModuleDecl;
using ast::NameCall;
using ast::NeqExpr;
using ast::PatternValue;
using ast::SeqPattern;
using ast::SubtractExpr;
using ast::ValueAlias;
using ast::ValueExpr;
using ast::ValuesSequenceExpr;

namespace {

/// Named type applications that are collections, not user ADT sums.
bool is_collection_app(const MonoType *t) {
  if (!t || t->tag != MonoType::App)
    return false;
  const std::string &n = t->type_name;
  return n == "Seq" || n == "Set" || n == "Dict";
}

/// Algebraic (sum) type in the type checker: App("Option", ...) etc., excluding
/// collections.
bool is_tracked_adt_shape(MonoTypePtr t) {
  if (!t || t->tag != MonoType::App)
    return false;
  return !is_collection_app(t);
}

} // namespace

// ===== Interval Arithmetic =====

Interval Interval::add(int64_t c) const {
  int64_t new_lo = (lo == std::numeric_limits<int64_t>::min()) ? lo : lo + c;
  int64_t new_hi = (hi == std::numeric_limits<int64_t>::max()) ? hi : hi + c;
  return {new_lo, new_hi};
}

Interval Interval::sub(int64_t c) const {
  int64_t new_lo = (lo == std::numeric_limits<int64_t>::min()) ? lo : lo - c;
  int64_t new_hi = (hi == std::numeric_limits<int64_t>::max()) ? hi : hi - c;
  return {new_lo, new_hi};
}

// ===== FactEnv =====

bool FactEnv::satisfies(const types::RefinePredicate &pred) const {
  return satisfies(pred, pred.var_name);
}

bool FactEnv::satisfies(const types::RefinePredicate &pred,
                        const std::string &actual_var) const {
  switch (pred.op) {
  case types::RefinePredicate::Gt: {
    auto it = int_bounds.find(actual_var);
    if (it != int_bounds.end() && it->second.lo > pred.literal)
      return true;
    // Check excluded: if var excludes all values <= literal, it's > literal
    // (only practical for Ne 0 style checks)
    return false;
  }
  case types::RefinePredicate::Lt: {
    auto it = int_bounds.find(actual_var);
    return it != int_bounds.end() && it->second.hi < pred.literal;
  }
  case types::RefinePredicate::Ge: {
    auto it = int_bounds.find(actual_var);
    return it != int_bounds.end() && it->second.lo >= pred.literal;
  }
  case types::RefinePredicate::Le: {
    auto it = int_bounds.find(actual_var);
    return it != int_bounds.end() && it->second.hi <= pred.literal;
  }
  case types::RefinePredicate::Eq: {
    auto it = int_bounds.find(actual_var);
    return it != int_bounds.end() && it->second.lo == pred.literal &&
           it->second.hi == pred.literal;
  }
  case types::RefinePredicate::Ne: {
    // Satisfied if the interval excludes the value entirely
    auto it = int_bounds.find(actual_var);
    if (it != int_bounds.end() && it->second.excludes(pred.literal))
      return true;
    // Or if the value is in the excluded set
    auto ex = excluded_values.find(actual_var);
    if (ex != excluded_values.end() && ex->second.count(pred.literal))
      return true;
    return false;
  }
  case types::RefinePredicate::LengthGt: {
    if (pred.literal == 0)
      return non_empty_seqs.count(actual_var) > 0;
    return false;
  }
  case types::RefinePredicate::And: {
    for (auto &child : pred.children) {
      auto cv = actual_var.empty() ? child->var_name : actual_var;
      if (!satisfies(*child, cv))
        return false;
    }
    return true;
  }
  case types::RefinePredicate::Or: {
    for (auto &child : pred.children) {
      auto cv = actual_var.empty() ? child->var_name : actual_var;
      if (satisfies(*child, cv))
        return true;
    }
    return false;
  }
  case types::RefinePredicate::Not: {
    if (pred.children.empty())
      return false;
    auto cv = actual_var.empty() ? pred.children[0]->var_name : actual_var;
    return !satisfies(*pred.children[0], cv);
  }
  }
  return false;
}

FactEnv FactEnv::with_int_bound(const std::string &var,
                                Interval interval) const {
  FactEnv copy = *this;
  auto it = copy.int_bounds.find(var);
  if (it != copy.int_bounds.end()) {
    it->second.lo = std::max(it->second.lo, interval.lo);
    it->second.hi = std::min(it->second.hi, interval.hi);
  } else {
    copy.int_bounds[var] = interval;
  }
  return copy;
}

FactEnv FactEnv::with_non_empty(const std::string &var) const {
  FactEnv copy = *this;
  copy.non_empty_seqs.insert(var);
  return copy;
}

FactEnv FactEnv::with_excluded(const std::string &var, int64_t value) const {
  FactEnv copy = *this;
  copy.excluded_values[var].insert(value);
  return copy;
}

// ===== RefinementChecker =====

RefinementChecker::RefinementChecker(DiagnosticEngine &diag, TypeChecker *tc)
    : diag_(diag), tc_(tc) {
  // Register built-in function refinement requirements
  builtin_refinements_["head"] = {0, BuiltinRefinement::NonEmpty};
  builtin_refinements_["tail"] = {0, BuiltinRefinement::NonEmpty};
  builtin_refinements_["seq_first"] = {0, BuiltinRefinement::NonEmpty};
  builtin_refinements_["seq_last"] = {0, BuiltinRefinement::NonEmpty};
}

void RefinementChecker::register_refined_type(
    const std::string &name, const types::Type &base_type,
    std::shared_ptr<types::RefinePredicate> predicate) {
  refined_types_[name] = {name, base_type, std::move(predicate)};
}

const RefinedTypeInfo *
RefinementChecker::lookup(const std::string &name) const {
  auto it = refined_types_.find(name);
  return (it != refined_types_.end()) ? &it->second : nullptr;
}

void RefinementChecker::check(AstNode *node) {
  FactEnv facts;
  check_node(node, facts);
}

void RefinementChecker::warn_if_discarded_adt(AstNode *expr) {
  if (!tc_ || !expr)
    return;
  MonoTypePtr raw = tc_->type_of(expr);
  if (!raw)
    return;
  MonoTypePtr t = tc_->zonk(raw);
  if (!t || t->tag == MonoType::Var)
    return;
  if (!is_tracked_adt_shape(t))
    return;
  diag_.warning(expr->Range,
                "ADT value of type " + pretty_print(t) +
                    " not handled at call site",
                WarningFlag::UnmatchedAdt);
}

std::string RefinementChecker::arg_var_name(AstNode *node) {
  if (!node)
    return "";
  if (node->get_type() == AST_IDENTIFIER_EXPR)
    return static_cast<IdentifierExpr *>(node)->name->value;
  return "";
}

void RefinementChecker::check_node(AstNode *node, FactEnv &facts) {
  if (!node)
    return;

  switch (node->get_type()) {
  case AST_MAIN:
    check_node(static_cast<MainNode *>(node)->node, facts);
    break;
  case AST_LET_EXPR:
    check_let(static_cast<LetExpr *>(node), facts);
    break;
  case AST_CASE_EXPR:
    check_case(static_cast<CaseExpr *>(node), facts);
    break;
  case AST_IF_EXPR:
    check_if(static_cast<IfExpr *>(node), facts);
    break;
  case AST_APPLY_EXPR:
    check_apply(static_cast<ApplyExpr *>(node), facts);
    break;
  case AST_FUNCTION_EXPR:
    check_function(static_cast<FunctionExpr *>(node), facts);
    break;
  case AST_MODULE_DECL:
    check_module(static_cast<ModuleDecl *>(node), facts);
    break;
  case AST_IMPORT_EXPR:
    check_node(static_cast<ImportExpr *>(node)->expr, facts);
    break;
  case AST_DO_EXPR: {
    auto *doex = static_cast<DoExpr *>(node);
    for (size_t i = 0; i < doex->steps.size(); ++i) {
      check_node(doex->steps[i], facts);
      if (i + 1 < doex->steps.size())
        warn_if_discarded_adt(doex->steps[i]);
    }
    break;
  }
  case AST_DIVIDE_EXPR: {
    auto *div = static_cast<DivideExpr *>(node);
    check_node(div->left, facts);
    check_node(div->right, facts);
    // Check divisor is non-zero
    std::string divisor = arg_var_name(div->right);
    if (!divisor.empty()) {
      auto pred = types::RefinePredicate::make_cmp(types::RefinePredicate::Ne,
                                                   divisor, 0);
      if (!facts.satisfies(*pred)) {
        diag_.error(node->Range, ErrorCode::E0500,
                    "division requires a non-zero divisor, but '" + divisor +
                        "' may be zero");
        error_count_++;
      }
    }
    break;
  }
  default:
    if (auto *binop = dynamic_cast<BinaryOpExpr *>(node)) {
      check_node(binop->left, facts);
      check_node(binop->right, facts);
    }
    break;
  }
}

// ===== Let Binding Facts =====

void RefinementChecker::establish_let_facts(const std::string &name,
                                            AstNode *expr, FactEnv &facts) {
  if (!expr)
    return;

  // Integer literal â†’ exact interval
  if (expr->get_type() == AST_INTEGER_EXPR) {
    facts.int_bounds[name] =
        Interval::exact(static_cast<IntegerExpr *>(expr)->value);
    return;
  }

  // Cons (x :: xs) â†’ non-empty
  if (expr->get_type() == AST_CONS_LEFT_EXPR) {
    facts.non_empty_seqs.insert(name);
    return;
  }

  // Non-empty sequence literal [1, 2, 3]
  if (expr->get_type() == AST_VALUES_SEQUENCE_EXPR) {
    auto *seq = static_cast<ValuesSequenceExpr *>(expr);
    if (!seq->values.empty())
      facts.non_empty_seqs.insert(name);
    return;
  }

  // Arithmetic: let y = x + 1 â†’ propagate interval
  if (auto *add = dynamic_cast<AddExpr *>(expr)) {
    std::string lvar = arg_var_name(add->left);
    if (!lvar.empty() && add->right->get_type() == AST_INTEGER_EXPR) {
      auto it = facts.int_bounds.find(lvar);
      if (it != facts.int_bounds.end()) {
        int64_t c = static_cast<IntegerExpr *>(add->right)->value;
        facts.int_bounds[name] = it->second.add(c);
        return;
      }
    }
    std::string rvar = arg_var_name(add->right);
    if (!rvar.empty() && add->left->get_type() == AST_INTEGER_EXPR) {
      auto it = facts.int_bounds.find(rvar);
      if (it != facts.int_bounds.end()) {
        int64_t c = static_cast<IntegerExpr *>(add->left)->value;
        facts.int_bounds[name] = it->second.add(c);
        return;
      }
    }
  }

  if (auto *sub = dynamic_cast<SubtractExpr *>(expr)) {
    std::string lvar = arg_var_name(sub->left);
    if (!lvar.empty() && sub->right->get_type() == AST_INTEGER_EXPR) {
      auto it = facts.int_bounds.find(lvar);
      if (it != facts.int_bounds.end()) {
        int64_t c = static_cast<IntegerExpr *>(sub->right)->value;
        facts.int_bounds[name] = it->second.sub(c);
        return;
      }
    }
  }

  // Identifier alias: let y = x â†’ copy facts
  if (expr->get_type() == AST_IDENTIFIER_EXPR) {
    std::string src = static_cast<IdentifierExpr *>(expr)->name->value;
    auto ib = facts.int_bounds.find(src);
    if (ib != facts.int_bounds.end())
      facts.int_bounds[name] = ib->second;
    if (facts.non_empty_seqs.count(src))
      facts.non_empty_seqs.insert(name);
    auto ex = facts.excluded_values.find(src);
    if (ex != facts.excluded_values.end())
      facts.excluded_values[name] = ex->second;
  }
}

void RefinementChecker::check_let(LetExpr *node, FactEnv &facts) {
  for (auto *alias : node->aliases) {
    if (auto *va = dynamic_cast<ValueAlias *>(alias)) {
      check_node(va->expr, facts);
      if (va->identifier->name->value == "_")
        warn_if_discarded_adt(va->expr);
      establish_let_facts(va->identifier->name->value, va->expr, facts);
    } else if (auto *la = dynamic_cast<LambdaAlias *>(alias)) {
      check_node(la->lambda, facts);
    }
  }
  check_node(node->expr, facts);
}

// ===== Case Expression =====

void RefinementChecker::check_case(CaseExpr *node, FactEnv &facts) {
  check_node(node->expr, facts);

  std::string scrut_name;
  if (node->expr->get_type() == AST_IDENTIFIER_EXPR)
    scrut_name = static_cast<IdentifierExpr *>(node->expr)->name->value;

  // Collect integer values matched by prior clauses for wildcard complement
  std::unordered_set<int64_t> matched_ints;

  for (size_t ci = 0; ci < node->clauses.size(); ci++) {
    auto *clause = node->clauses[ci];
    FactEnv branch_facts = facts;
    auto *pat = clause->pattern;

    if (!scrut_name.empty()) {
      // Head-tail pattern â†’ non-empty
      if (pat->get_type() == AST_HEAD_TAILS_PATTERN)
        branch_facts.non_empty_seqs.insert(scrut_name);

      // Empty seq pattern â†’ empty (remove non-empty)
      if (pat->get_type() == AST_SEQ_PATTERN) {
        auto *sp = static_cast<SeqPattern *>(pat);
        if (sp->patterns.empty())
          branch_facts.non_empty_seqs.erase(scrut_name);
      }

      if (pat->get_type() == AST_PATTERN_VALUE) {
        auto *pv = static_cast<PatternValue *>(pat);

        // Integer literal â†’ exact bound + record for wildcard complement
        if (auto *lit = std::get_if<LiteralExpr<void *> *>(&pv->expr)) {
          auto *an = static_cast<AstNode *>(*lit);
          if (an->get_type() == AST_INTEGER_EXPR) {
            int64_t val = static_cast<IntegerExpr *>(an)->value;
            branch_facts.int_bounds[scrut_name] = Interval::exact(val);
            matched_ints.insert(val);
          }
        }

        // Identifier binding â†’ transfer facts
        if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr)) {
          std::string bound_name = (*id)->name->value;
          auto ib = facts.int_bounds.find(scrut_name);
          if (ib != facts.int_bounds.end())
            branch_facts.int_bounds[bound_name] = ib->second;
          if (facts.non_empty_seqs.count(scrut_name))
            branch_facts.non_empty_seqs.insert(bound_name);
          auto ex = facts.excluded_values.find(scrut_name);
          if (ex != facts.excluded_values.end())
            branch_facts.excluded_values[bound_name] = ex->second;
        }
      }

      // Wildcard/underscore â†’ complement of all prior matched integer values
      if (pat->get_type() == AST_UNDERSCORE_PATTERN && !matched_ints.empty()) {
        for (int64_t v : matched_ints)
          branch_facts.excluded_values[scrut_name].insert(v);
      }
      // Identifier in last position also gets complement
      if (pat->get_type() == AST_PATTERN_VALUE && !matched_ints.empty()) {
        auto *pv = static_cast<PatternValue *>(pat);
        if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr)) {
          std::string bound_name = (*id)->name->value;
          for (int64_t v : matched_ints)
            branch_facts.excluded_values[bound_name].insert(v);
          // Also apply to scrutinee
          for (int64_t v : matched_ints)
            branch_facts.excluded_values[scrut_name].insert(v);
        }
      }
    }

    // Guard narrowing: case x of n | n > 0 -> ...
    if (clause->guard) {
      narrow_from_condition(clause->guard, branch_facts, branch_facts);
    }

    check_node(clause->body, branch_facts);
  }
}

// ===== Function Application =====

void RefinementChecker::check_apply(ApplyExpr *node, const FactEnv &facts) {
  // Get function name
  std::string fn_name;
  if (auto *nc = dynamic_cast<NameCall *>(node->call))
    fn_name = nc->name->value;

  // Check built-in refinement requirements
  if (!fn_name.empty()) {
    auto it = builtin_refinements_.find(fn_name);
    if (it != builtin_refinements_.end()) {
      auto &req = it->second;
      if (req.arg_index < (int)node->args.size()) {
        AstNode *arg_node =
            std::holds_alternative<ExprNode *>(node->args[req.arg_index])
                ? static_cast<AstNode *>(
                      std::get<ExprNode *>(node->args[req.arg_index]))
                : static_cast<AstNode *>(
                      std::get<ValueExpr *>(node->args[req.arg_index]));
        std::string arg_name = arg_var_name(arg_node);

        if (req.kind == BuiltinRefinement::NonEmpty) {
          if (!arg_name.empty() && facts.non_empty_seqs.count(arg_name) == 0) {
            diag_.error(node->Range, ErrorCode::E0500,
                        "'" + fn_name +
                            "' requires a non-empty sequence, but '" +
                            arg_name +
                            "' may be empty; use a [h|t] pattern match to "
                            "prove non-emptiness");
            error_count_++;
          }
        } else if (req.kind == BuiltinRefinement::NonZero) {
          auto pred = types::RefinePredicate::make_cmp(
              types::RefinePredicate::Ne, arg_name, 0);
          if (!arg_name.empty() && !facts.satisfies(*pred)) {
            diag_.error(
                node->Range, ErrorCode::E0500,
                "division requires a non-zero divisor, but '" + arg_name +
                    "' may be zero; check with a guard or if expression");
            error_count_++;
          }
        }
      }
    }
  }

  // Check user-registered refined type requirements
  // (requires type annotation integration â€” checked against registered refined
  // types)

  // Recurse into arguments
  for (auto &arg_variant : node->args) {
    AstNode *arg_node =
        std::holds_alternative<ExprNode *>(arg_variant)
            ? static_cast<AstNode *>(std::get<ExprNode *>(arg_variant))
            : static_cast<AstNode *>(std::get<ValueExpr *>(arg_variant));
    FactEnv mutable_facts = facts;
    check_node(arg_node, mutable_facts);
  }
}

// ===== If Expression =====

void RefinementChecker::narrow_from_condition(AstNode *cond,
                                              FactEnv &then_facts,
                                              FactEnv &else_facts) {
  if (!cond)
    return;

  // x > N
  if (auto *cmp = dynamic_cast<GtExpr *>(cond)) {
    std::string var = arg_var_name(cmp->left);
    if (!var.empty() && cmp->right->get_type() == AST_INTEGER_EXPR) {
      int64_t val = static_cast<IntegerExpr *>(cmp->right)->value;
      then_facts = then_facts.with_int_bound(var, Interval::above(val + 1));
      else_facts = else_facts.with_int_bound(var, Interval::below(val));
    }
  }
  // x < N
  if (auto *cmp = dynamic_cast<LtExpr *>(cond)) {
    std::string var = arg_var_name(cmp->left);
    if (!var.empty() && cmp->right->get_type() == AST_INTEGER_EXPR) {
      int64_t val = static_cast<IntegerExpr *>(cmp->right)->value;
      then_facts = then_facts.with_int_bound(var, Interval::below(val - 1));
      else_facts = else_facts.with_int_bound(var, Interval::above(val));
    }
  }
  // x >= N
  if (auto *cmp = dynamic_cast<GteExpr *>(cond)) {
    std::string var = arg_var_name(cmp->left);
    if (!var.empty() && cmp->right->get_type() == AST_INTEGER_EXPR) {
      int64_t val = static_cast<IntegerExpr *>(cmp->right)->value;
      then_facts = then_facts.with_int_bound(var, Interval::above(val));
      else_facts = else_facts.with_int_bound(var, Interval::below(val - 1));
    }
  }
  // x <= N
  if (auto *cmp = dynamic_cast<LteExpr *>(cond)) {
    std::string var = arg_var_name(cmp->left);
    if (!var.empty() && cmp->right->get_type() == AST_INTEGER_EXPR) {
      int64_t val = static_cast<IntegerExpr *>(cmp->right)->value;
      then_facts = then_facts.with_int_bound(var, Interval::below(val));
      else_facts = else_facts.with_int_bound(var, Interval::above(val + 1));
    }
  }
  // x == N
  if (auto *cmp = dynamic_cast<EqExpr *>(cond)) {
    std::string var = arg_var_name(cmp->left);
    if (!var.empty() && cmp->right->get_type() == AST_INTEGER_EXPR) {
      int64_t val = static_cast<IntegerExpr *>(cmp->right)->value;
      then_facts = then_facts.with_int_bound(var, Interval::exact(val));
      else_facts = else_facts.with_excluded(var, val);
    }
  }
  // x != N
  if (auto *cmp = dynamic_cast<NeqExpr *>(cond)) {
    std::string var = arg_var_name(cmp->left);
    if (!var.empty() && cmp->right->get_type() == AST_INTEGER_EXPR) {
      int64_t val = static_cast<IntegerExpr *>(cmp->right)->value;
      then_facts = then_facts.with_excluded(var, val);
      else_facts = else_facts.with_int_bound(var, Interval::exact(val));
    }
  }
  // &&: narrow both sides
  if (auto *land = dynamic_cast<LogicalAndExpr *>(cond)) {
    narrow_from_condition(land->left, then_facts, else_facts);
    narrow_from_condition(land->right, then_facts, else_facts);
  }
}

void RefinementChecker::check_if(IfExpr *node, FactEnv &facts) {
  check_node(node->condition, facts);

  FactEnv then_facts = facts;
  FactEnv else_facts = facts;
  narrow_from_condition(node->condition, then_facts, else_facts);

  check_node(node->thenExpr, then_facts);
  check_node(node->elseExpr, else_facts);
}

void RefinementChecker::check_function(FunctionExpr *node,
                                       FactEnv & /*outer*/) {
  if (!node)
    return;
  FactEnv fn_facts;
  for (auto *body : node->bodies) {
    if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body)) {
      check_node(bwg->expr, fn_facts);
    } else if (auto *g = dynamic_cast<BodyWithGuards *>(body)) {
      check_node(g->guard, fn_facts);
      check_node(g->expr, fn_facts);
    }
  }
}

void RefinementChecker::check_module(ModuleDecl *node, FactEnv &facts) {
  if (!node)
    return;
  for (auto *fn : node->functions)
    check_function(fn, facts);
  for (auto *inst : node->instance_declarations) {
    if (!inst)
      continue;
    for (auto *method : inst->methods)
      check_function(method, facts);
  }
}

} // namespace yona::compiler::typechecker
