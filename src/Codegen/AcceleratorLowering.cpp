/// Transparent lowering: match IntArray / FloatArray map, filter, and foldl
/// whose lambdas are in the Std\Gpu fixed kernel library, then let codegen
/// emit the same C ABI as explicit mapAdd / mapMul / reduceSum / â€¦
/// See docs/gpu-transparent-lowering.md.

#include "yona/Codegen/AcceleratorLowering.h"

#include "yona/Model/ModuleIdentity.h"
#include "yona/Syntax/Utils.h"

#include <functional>
#include <memory>
#include <utility>
#include <variant>

namespace yona::compiler {
using ast::AddExpr;
using ast::ApplyExpr;
using ast::AST_APPLY_EXPR;
using ast::AST_CASE_EXPR;
using ast::AST_CONS_LEFT_EXPR;
using ast::AST_CONS_RIGHT_EXPR;
using ast::AST_DO_EXPR;
using ast::AST_FUNCTION_EXPR;
using ast::AST_HANDLE_EXPR;
using ast::AST_IF_EXPR;
using ast::AST_IMPORT_EXPR;
using ast::AST_LET_EXPR;
using ast::AST_MAIN;
using ast::AST_PERFORM_EXPR;
using ast::AST_SEQ_GENERATOR_EXPR;
using ast::AST_TUPLE_EXPR;
using ast::AST_VALUES_SEQUENCE_EXPR;
using ast::AST_WITH_EXPR;
using ast::AstNode;
using ast::BinaryOpExpr;
using ast::BodyWithGuards;
using ast::BodyWithoutGuards;
using ast::CaseExpr;
using ast::CollectionExtractorExpr;
using ast::ConsLeftExpr;
using ast::ConsRightExpr;
using ast::DoExpr;
using ast::ExprCall;
using ast::ExprNode;
using ast::FloatExpr;
using ast::FqnExpr;
using ast::FunctionAlias;
using ast::FunctionExpr;
using ast::FunctionsImport;
using ast::GtExpr;
using ast::HandleExpr;
using ast::IdentifierExpr;
using ast::IfExpr;
using ast::ImportExpr;
using ast::IntegerExpr;
using ast::KeyValueCollectionExtractorExpr;
using ast::LambdaAlias;
using ast::LetExpr;
using ast::LtExpr;
using ast::MainNode;
using ast::ModuleCall;
using ast::ModuleDecl;
using ast::MultiplyExpr;
using ast::NameCall;
using ast::PatternAlias;
using ast::PatternNode;
using ast::PatternValue;
using ast::PerformExpr;
using ast::SeqGeneratorExpr;
using ast::SubtractExpr;
using ast::TupleExpr;
using ast::ValueAlias;
using ast::ValueCollectionExtractorExpr;
using ast::ValueExpr;
using ast::ValuesSequenceExpr;
using ast::WithExpr;

namespace {

const char *kernel_name_tbl(AccelKernel k) {
  switch (k) {
  case AccelKernel::IntMapAdd:
    return "mapAdd";
  case AccelKernel::IntMapMul:
    return "mapMul";
  case AccelKernel::IntMapSquare:
    return "mapSquare";
  case AccelKernel::IntFilterGt:
    return "filterGreaterThan";
  case AccelKernel::IntFilterLt:
    return "filterLessThan";
  case AccelKernel::IntReduceSum:
    return "reduceSum";
  case AccelKernel::FloatScale:
    return "mapFloatScale";
  case AccelKernel::FloatReduceSum:
    return "reduceFloatGpu";
  case AccelKernel::None:
    return nullptr;
  }
  return nullptr;
}

const char *kernel_abi_tbl(AccelKernel k) {
  static const std::string IntMapAdd =
      model::mangleExport("Std\\Gpu", "rawMapAdd");
  static const std::string IntMapMul =
      model::mangleExport("Std\\Gpu", "rawMapMul");
  static const std::string IntMapSquare =
      model::mangleExport("Std\\Gpu", "rawMapSquare");
  static const std::string IntFilterGreaterThan =
      model::mangleExport("Std\\Gpu", "rawFilterGreaterThan");
  static const std::string IntFilterLessThan =
      model::mangleExport("Std\\Gpu", "rawFilterLessThan");
  static const std::string IntReduceSum =
      model::mangleExport("Std\\Gpu", "rawReduceSum");
  static const std::string FloatScale =
      model::mangleExport("Std\\Gpu", "rawMapFloatScale");
  static const std::string FloatReduceSum =
      model::mangleExport("Std\\Gpu", "rawReduceFloatGpu");

  switch (k) {
  case AccelKernel::IntMapAdd:
    return IntMapAdd.c_str();
  case AccelKernel::IntMapMul:
    return IntMapMul.c_str();
  case AccelKernel::IntMapSquare:
    return IntMapSquare.c_str();
  case AccelKernel::IntFilterGt:
    return IntFilterGreaterThan.c_str();
  case AccelKernel::IntFilterLt:
    return IntFilterLessThan.c_str();
  case AccelKernel::IntReduceSum:
    return IntReduceSum.c_str();
  case AccelKernel::FloatScale:
    return FloatScale.c_str();
  case AccelKernel::FloatReduceSum:
    return FloatReduceSum.c_str();
  case AccelKernel::None:
    return nullptr;
  }
  return nullptr;
}

ExprNode *arg_as_expr(const std::variant<ExprNode *, ValueExpr *> &a) {
  if (std::holds_alternative<ExprNode *>(a))
    return std::get<ExprNode *>(a);
  return std::get<ValueExpr *>(a);
}

const std::string *ident_name(ExprNode *e) {
  if (auto *id = dynamic_cast<IdentifierExpr *>(e)) {
    if (id->name)
      return &id->name->value;
  }
  return nullptr;
}

bool is_param_ref(ExprNode *e, const std::string &param) {
  auto *n = ident_name(e);
  return n && *n == param;
}

bool is_simple_scalar(ExprNode *e, const std::string &param) {
  if (!e)
    return false;
  if (ident_name(e))
    return !is_param_ref(e, param);
  if (dynamic_cast<IntegerExpr *>(e) || dynamic_cast<FloatExpr *>(e))
    return true;
  if (auto *b = dynamic_cast<BinaryOpExpr *>(e))
    return is_simple_scalar(b->left, param) &&
           is_simple_scalar(b->right, param);
  return false;
}

bool is_int_zero(ExprNode *e) {
  if (auto *i = dynamic_cast<IntegerExpr *>(e))
    return i->value == 0;
  return false;
}

bool is_float_zero(ExprNode *e) {
  if (auto *f = dynamic_cast<FloatExpr *>(e))
    return f->value == 0.0;
  if (auto *i = dynamic_cast<IntegerExpr *>(e))
    return i->value == 0;
  return false;
}

std::optional<std::string> pattern_ident(PatternNode *p) {
  auto *pv = dynamic_cast<PatternValue *>(p);
  if (!pv)
    return std::nullopt;
  if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr)) {
    if (*id && (*id)->name)
      return (*id)->name->value;
  }
  return std::nullopt;
}

FunctionExpr *as_lambda(ExprNode *e) { return dynamic_cast<FunctionExpr *>(e); }

ExprNode *lambda_unguarded_body(FunctionExpr *fn) {
  if (!fn || fn->bodies.size() != 1)
    return nullptr;
  if (auto *wog = dynamic_cast<BodyWithoutGuards *>(fn->bodies[0]))
    return wog->expr;
  return nullptr;
}

struct BinSplit {
  ExprNode *param_side = nullptr;
  ExprNode *other = nullptr;
  bool param_on_left = false;
};

std::optional<BinSplit> split_param_binop(BinaryOpExpr *bin,
                                          const std::string &param) {
  const bool l = is_param_ref(bin->left, param);
  const bool r = is_param_ref(bin->right, param);
  if (l && !r) {
    if (!is_simple_scalar(bin->right, param))
      return std::nullopt;
    return BinSplit{bin->left, bin->right, true};
  }
  if (r && !l) {
    if (!is_simple_scalar(bin->left, param))
      return std::nullopt;
    return BinSplit{bin->right, bin->left, false};
  }
  return std::nullopt;
}

void fill_match(AccelMatch &m, AccelKernel k, ApplyExpr *site,
                const std::string &binding) {
  m.kernel = k;
  m.site = site;
  m.abi_symbol = kernel_abi_tbl(k);
  m.kernel_name = kernel_name_tbl(k);
  m.binding = binding;
}

std::optional<AccelMatch> classify_int_map(FunctionExpr *fn, ExprNode *array,
                                           ApplyExpr *site,
                                           const std::string &binding) {
  if (!fn || fn->patterns.size() != 1)
    return std::nullopt;
  auto pname = pattern_ident(fn->patterns[0]);
  if (!pname)
    return std::nullopt;
  ExprNode *body = lambda_unguarded_body(fn);
  if (!body)
    return std::nullopt;

  if (auto *add = dynamic_cast<AddExpr *>(body)) {
    if (is_param_ref(add->left, *pname) && is_param_ref(add->right, *pname)) {
      AccelMatch m;
      fill_match(m, AccelKernel::IntMapMul, site, binding);
      m.array = array;
      m.scalar_is_literal = true;
      m.lit_i64 = 2;
      return m;
    }
    auto sp = split_param_binop(add, *pname);
    if (!sp)
      return std::nullopt;
    AccelMatch m;
    fill_match(m, AccelKernel::IntMapAdd, site, binding);
    m.array = array;
    m.scalar = sp->other;
    return m;
  }
  if (auto *sub = dynamic_cast<SubtractExpr *>(body)) {
    // `\x -> 0 - x` â†’ mapMul(-1); `\x -> x - k` â†’ mapAdd(-k).
    if (is_int_zero(sub->left) && is_param_ref(sub->right, *pname)) {
      AccelMatch m;
      fill_match(m, AccelKernel::IntMapMul, site, binding);
      m.array = array;
      m.scalar_is_literal = true;
      m.lit_i64 = -1;
      return m;
    }
    auto sp = split_param_binop(sub, *pname);
    if (!sp || !sp->param_on_left)
      return std::nullopt;
    AccelMatch m;
    fill_match(m, AccelKernel::IntMapAdd, site, binding);
    m.array = array;
    m.scalar = sp->other;
    m.negate_scalar = true;
    if (auto *lit = dynamic_cast<IntegerExpr *>(sp->other)) {
      m.scalar_is_literal = true;
      m.lit_i64 = -lit->value;
      m.negate_scalar = false;
    }
    return m;
  }
  if (auto *mul = dynamic_cast<MultiplyExpr *>(body)) {
    if (is_param_ref(mul->left, *pname) && is_param_ref(mul->right, *pname)) {
      AccelMatch m;
      fill_match(m, AccelKernel::IntMapSquare, site, binding);
      m.array = array;
      return m;
    }
    auto sp = split_param_binop(mul, *pname);
    if (!sp)
      return std::nullopt;
    AccelMatch m;
    fill_match(m, AccelKernel::IntMapMul, site, binding);
    m.array = array;
    m.scalar = sp->other;
    return m;
  }
  return std::nullopt;
}

std::optional<AccelMatch> classify_int_filter(FunctionExpr *fn, ExprNode *array,
                                              ApplyExpr *site,
                                              const std::string &binding) {
  if (!fn || fn->patterns.size() != 1)
    return std::nullopt;
  auto pname = pattern_ident(fn->patterns[0]);
  if (!pname)
    return std::nullopt;
  ExprNode *body = lambda_unguarded_body(fn);
  if (!body)
    return std::nullopt;

  if (auto *gt = dynamic_cast<GtExpr *>(body)) {
    auto sp = split_param_binop(gt, *pname);
    if (!sp || !sp->param_on_left)
      return std::nullopt;
    AccelMatch m;
    fill_match(m, AccelKernel::IntFilterGt, site, binding);
    m.array = array;
    m.scalar = sp->other;
    return m;
  }
  if (auto *lt = dynamic_cast<LtExpr *>(body)) {
    auto sp = split_param_binop(lt, *pname);
    if (!sp)
      return std::nullopt;
    // `\x -> x < k` â†’ filterLessThan; `\x -> k < x` â†’ filterGreaterThan.
    AccelMatch m;
    if (sp->param_on_left)
      fill_match(m, AccelKernel::IntFilterLt, site, binding);
    else
      fill_match(m, AccelKernel::IntFilterGt, site, binding);
    m.array = array;
    m.scalar = sp->other;
    return m;
  }
  return std::nullopt;
}

std::optional<AccelMatch> classify_int_foldl(FunctionExpr *fn, ExprNode *init,
                                             ExprNode *array, ApplyExpr *site,
                                             const std::string &binding) {
  if (!is_int_zero(init))
    return std::nullopt;
  if (!fn || fn->patterns.size() != 2)
    return std::nullopt;
  auto a = pattern_ident(fn->patterns[0]);
  auto b = pattern_ident(fn->patterns[1]);
  if (!a || !b)
    return std::nullopt;
  ExprNode *body = lambda_unguarded_body(fn);
  auto *add = dynamic_cast<AddExpr *>(body);
  if (!add)
    return std::nullopt;
  const bool left_a = is_param_ref(add->left, *a);
  const bool left_b = is_param_ref(add->left, *b);
  const bool right_a = is_param_ref(add->right, *a);
  const bool right_b = is_param_ref(add->right, *b);
  if ((left_a && right_b) || (left_b && right_a)) {
    AccelMatch m;
    fill_match(m, AccelKernel::IntReduceSum, site, binding);
    m.array = array;
    return m;
  }
  return std::nullopt;
}

std::optional<AccelMatch> classify_float_map(FunctionExpr *fn, ExprNode *array,
                                             ApplyExpr *site,
                                             const std::string &binding) {
  if (!fn || fn->patterns.size() != 1)
    return std::nullopt;
  auto pname = pattern_ident(fn->patterns[0]);
  if (!pname)
    return std::nullopt;
  ExprNode *body = lambda_unguarded_body(fn);
  auto *mul = dynamic_cast<MultiplyExpr *>(body);
  if (!mul)
    return std::nullopt;
  auto sp = split_param_binop(mul, *pname);
  if (!sp)
    return std::nullopt;
  AccelMatch m;
  fill_match(m, AccelKernel::FloatScale, site, binding);
  m.array = array;
  m.scalar = sp->other;
  return m;
}

std::optional<AccelMatch> classify_float_foldl(FunctionExpr *fn, ExprNode *init,
                                               ExprNode *array, ApplyExpr *site,
                                               const std::string &binding) {
  if (!is_float_zero(init))
    return std::nullopt;
  if (!fn || fn->patterns.size() != 2)
    return std::nullopt;
  auto a = pattern_ident(fn->patterns[0]);
  auto b = pattern_ident(fn->patterns[1]);
  if (!a || !b)
    return std::nullopt;
  ExprNode *body = lambda_unguarded_body(fn);
  auto *add = dynamic_cast<AddExpr *>(body);
  if (!add)
    return std::nullopt;
  const bool left_a = is_param_ref(add->left, *a);
  const bool left_b = is_param_ref(add->left, *b);
  const bool right_a = is_param_ref(add->right, *a);
  const bool right_b = is_param_ref(add->right, *b);
  if ((left_a && right_b) || (left_b && right_a)) {
    AccelMatch m;
    fill_match(m, AccelKernel::FloatReduceSum, site, binding);
    m.array = array;
    return m;
  }
  return std::nullopt;
}

struct FlatApply {
  std::string fn_name;
  std::string module_fqn;
  std::vector<ApplyExpr *> chain;
};

FlatApply flatten_apply(ApplyExpr *node) {
  FlatApply result;
  ApplyExpr *cur = node;
  while (cur) {
    result.chain.push_back(cur);
    if (auto *nc = dynamic_cast<NameCall *>(cur->call)) {
      result.fn_name = nc->name->value;
      break;
    }
    if (auto *mc = dynamic_cast<ModuleCall *>(cur->call)) {
      result.fn_name = mc->funName->value;
      if (auto *fe = std::get_if<FqnExpr *>(&mc->fqn)) {
        if (*fe)
          result.module_fqn = (*fe)->to_string();
      }
      break;
    }
    if (auto *ec = dynamic_cast<ExprCall *>(cur->call)) {
      if (auto *inner = dynamic_cast<ApplyExpr *>(ec->expr))
        cur = inner;
      else
        break;
    } else {
      break;
    }
  }
  return result;
}

std::vector<ExprNode *>
collect_arg_exprs(const std::vector<ApplyExpr *> &chain) {
  std::vector<ExprNode *> args;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (auto &a : (*it)->args)
      args.push_back(arg_as_expr(a));
  }
  return args;
}

bool is_curried_inner_apply(ApplyExpr *a) {
  if (!a || !a->parent)
    return false;
  auto *p = dynamic_cast<ExprCall *>(a->parent);
  if (!p || !p->parent)
    return false;
  auto *outer = dynamic_cast<ApplyExpr *>(p->parent);
  return outer && outer->call == p;
}

struct ResolvedFunction {
  std::string ModuleFqn;
  std::string LocalName;
};

std::optional<ResolvedFunction>
resolve_imported_symbol(ApplyExpr *site, const std::string &local_name) {
  for (AstNode *p = site ? site->parent : nullptr; p; p = p->parent) {
    auto *imp = dynamic_cast<ImportExpr *>(p);
    if (!imp)
      continue;
    for (auto cit = imp->clauses.rbegin(); cit != imp->clauses.rend(); ++cit) {
      auto *fi = dynamic_cast<FunctionsImport *>(*cit);
      if (!fi || !fi->fromFqn)
        continue;
      for (auto ait = fi->aliases.rbegin(); ait != fi->aliases.rend(); ++ait) {
        FunctionAlias *al = *ait;
        if (!al || !al->name)
          continue;
        const std::string &original = al->name->value;
        const std::string &local = (al->alias) ? al->alias->value : original;
        if (local != local_name)
          continue;
        return ResolvedFunction{fi->fromFqn->to_string(), original};
      }
    }
  }
  return std::nullopt;
}

static void walk_ast(AstNode *node,
                     const std::function<void(ApplyExpr *)> &on_apply);

static void
walk_collection_extractor(CollectionExtractorExpr *ce,
                          const std::function<void(ApplyExpr *)> &on_apply) {
  if (!ce)
    return;
  if (auto *v = dynamic_cast<ValueCollectionExtractorExpr *>(ce)) {
    if (v->collection)
      walk_ast(v->collection, on_apply);
    if (v->condition)
      walk_ast(v->condition, on_apply);
    return;
  }
  if (auto *kv = dynamic_cast<KeyValueCollectionExtractorExpr *>(ce)) {
    if (kv->collection)
      walk_ast(kv->collection, on_apply);
    if (kv->condition)
      walk_ast(kv->condition, on_apply);
  }
}

static void
walk_apply_children(ApplyExpr *ae,
                    const std::function<void(ApplyExpr *)> &on_apply) {
  if (auto *ec = dynamic_cast<ExprCall *>(ae->call))
    walk_ast(ec->expr, on_apply);
  for (auto &arg : ae->args) {
    AstNode *arg_node =
        std::holds_alternative<ExprNode *>(arg)
            ? static_cast<AstNode *>(std::get<ExprNode *>(arg))
            : static_cast<AstNode *>(std::get<ValueExpr *>(arg));
    walk_ast(arg_node, on_apply);
  }
}

static void walk_ast(AstNode *node,
                     const std::function<void(ApplyExpr *)> &on_apply) {
  if (!node)
    return;

  switch (node->get_type()) {
  case ast::AST_MAIN:
    walk_ast(static_cast<MainNode *>(node)->node, on_apply);
    return;
  case ast::AST_LET_EXPR: {
    auto *le = static_cast<LetExpr *>(node);
    for (auto *alias : le->aliases) {
      if (auto *va = dynamic_cast<ValueAlias *>(alias))
        walk_ast(va->expr, on_apply);
      else if (auto *la = dynamic_cast<LambdaAlias *>(alias))
        walk_ast(la->lambda, on_apply);
      else if (auto *pa = dynamic_cast<PatternAlias *>(alias))
        walk_ast(pa->expr, on_apply);
    }
    walk_ast(le->expr, on_apply);
    return;
  }
  case ast::AST_IMPORT_EXPR:
    walk_ast(static_cast<ImportExpr *>(node)->expr, on_apply);
    return;
  case ast::AST_IF_EXPR: {
    auto *ie = static_cast<IfExpr *>(node);
    walk_ast(ie->condition, on_apply);
    walk_ast(ie->thenExpr, on_apply);
    walk_ast(ie->elseExpr, on_apply);
    return;
  }
  case ast::AST_CASE_EXPR: {
    auto *ce = static_cast<CaseExpr *>(node);
    walk_ast(ce->expr, on_apply);
    for (auto *cl : ce->clauses) {
      if (cl->guard)
        walk_ast(cl->guard, on_apply);
      walk_ast(cl->body, on_apply);
    }
    return;
  }
  case ast::AST_DO_EXPR:
    for (auto *s : static_cast<DoExpr *>(node)->steps)
      walk_ast(s, on_apply);
    return;
  case ast::AST_FUNCTION_EXPR: {
    auto *fe = static_cast<FunctionExpr *>(node);
    for (auto *b : fe->bodies) {
      if (auto *wg = dynamic_cast<BodyWithGuards *>(b)) {
        walk_ast(wg->guard, on_apply);
        walk_ast(wg->expr, on_apply);
      } else if (auto *wog = dynamic_cast<BodyWithoutGuards *>(b)) {
        walk_ast(wog->expr, on_apply);
      }
    }
    return;
  }
  case ast::AST_WITH_EXPR: {
    auto *we = static_cast<WithExpr *>(node);
    walk_ast(we->contextExpr, on_apply);
    walk_ast(we->bodyExpr, on_apply);
    return;
  }
  case ast::AST_PERFORM_EXPR:
    for (auto *a : static_cast<PerformExpr *>(node)->args)
      walk_ast(a, on_apply);
    return;
  case ast::AST_HANDLE_EXPR: {
    auto *he = static_cast<HandleExpr *>(node);
    walk_ast(he->body, on_apply);
    for (auto *hc : he->clauses)
      walk_ast(hc->body, on_apply);
    return;
  }
  case ast::AST_VALUES_SEQUENCE_EXPR:
    for (auto *v : static_cast<ValuesSequenceExpr *>(node)->values)
      walk_ast(v, on_apply);
    return;
  case ast::AST_TUPLE_EXPR:
    for (auto *v : static_cast<TupleExpr *>(node)->values)
      walk_ast(v, on_apply);
    return;
  case ast::AST_CONS_LEFT_EXPR: {
    auto *c = static_cast<ConsLeftExpr *>(node);
    walk_ast(c->left, on_apply);
    walk_ast(c->right, on_apply);
    return;
  }
  case ast::AST_CONS_RIGHT_EXPR: {
    auto *c = static_cast<ConsRightExpr *>(node);
    walk_ast(c->left, on_apply);
    walk_ast(c->right, on_apply);
    return;
  }
  case ast::AST_SEQ_GENERATOR_EXPR: {
    auto *g = static_cast<SeqGeneratorExpr *>(node);
    walk_ast(g->reducerExpr, on_apply);
    walk_ast(g->stepExpression, on_apply);
    walk_collection_extractor(g->collectionExtractor, on_apply);
    return;
  }
  case ast::AST_APPLY_EXPR:
    on_apply(static_cast<ApplyExpr *>(node));
    return;
  default:
    break;
  }

  if (auto *bin = dynamic_cast<BinaryOpExpr *>(node)) {
    walk_ast(bin->left, on_apply);
    walk_ast(bin->right, on_apply);
    return;
  }
}

static void walk_module_decl(ModuleDecl *mod,
                             const std::function<void(ApplyExpr *)> &on_apply) {
  if (!mod)
    return;
  for (auto *fe : mod->functions)
    walk_ast(fe, on_apply);
  for (auto *ex : mod->extern_declarations) {
    if (ex->body)
      walk_ast(ex->body, on_apply);
  }
  for (auto *inst : mod->instance_declarations) {
    for (auto *m : inst->methods)
      walk_ast(m, on_apply);
  }
  for (auto *tr : mod->trait_declarations) {
    for (const auto &ms : tr->methods) {
      if (ms.default_impl)
        walk_ast(ms.default_impl, on_apply);
    }
  }
}

std::vector<AccelMatch> collect_from_walk(
    const std::function<void(const std::function<void(ApplyExpr *)> &)> &run) {
  std::vector<AccelMatch> out;
  auto self = std::make_shared<std::function<void(ApplyExpr *)>>();
  *self = [self, &out](ApplyExpr *ae) {
    if (!is_curried_inner_apply(ae)) {
      if (auto m = match_transparent_apply(ae))
        out.push_back(std::move(*m));
    }
    walk_apply_children(ae, *self);
  };
  run(*self);
  return out;
}

} // namespace

const char *accel_kernel_name(AccelKernel k) { return kernel_name_tbl(k); }
const char *accel_kernel_abi_symbol(AccelKernel k) { return kernel_abi_tbl(k); }

std::optional<AccelMatch>
match_column_kernel(std::string_view module_fqn, std::string_view local_name,
                    const std::vector<ExprNode *> &args, ApplyExpr *site) {
  const std::string Binding(module_fqn);
  if (module_fqn == "Std\\IntArray" && local_name == "map") {
    if (args.size() != 2)
      return std::nullopt;
    return classify_int_map(as_lambda(args[0]), args[1], site, Binding);
  }
  if (module_fqn == "Std\\IntArray" && local_name == "filter") {
    if (args.size() != 2)
      return std::nullopt;
    return classify_int_filter(as_lambda(args[0]), args[1], site, Binding);
  }
  if (module_fqn == "Std\\IntArray" && local_name == "foldl") {
    if (args.size() != 3)
      return std::nullopt;
    return classify_int_foldl(as_lambda(args[0]), args[1], args[2], site,
                              Binding);
  }
  if (module_fqn == "Std\\FloatArray" && local_name == "map") {
    if (args.size() != 2)
      return std::nullopt;
    return classify_float_map(as_lambda(args[0]), args[1], site, Binding);
  }
  if (module_fqn == "Std\\FloatArray" && local_name == "foldl") {
    if (args.size() != 3)
      return std::nullopt;
    return classify_float_foldl(as_lambda(args[0]), args[1], args[2], site,
                                Binding);
  }
  return std::nullopt;
}

std::optional<AccelMatch> match_transparent_apply(ApplyExpr *site) {
  if (!site)
    return std::nullopt;
  auto flat = flatten_apply(site);
  if (flat.fn_name.empty())
    return std::nullopt;
  auto args = collect_arg_exprs(flat.chain);

  std::string ModuleFqn = flat.module_fqn;
  std::string LocalName = flat.fn_name;
  if (!flat.module_fqn.empty()) {
  } else {
    auto got = resolve_imported_symbol(site, flat.fn_name);
    if (!got)
      return std::nullopt;
    ModuleFqn = got->ModuleFqn;
    LocalName = got->LocalName;
  }
  return match_column_kernel(ModuleFqn, LocalName, args, site);
}

static bool is_column_stdlib_function(std::string_view ModuleFqn,
                                      std::string_view LocalName) {
  const bool IsIntArray = ModuleFqn == "Std\\IntArray";
  const bool IsFloatArray = ModuleFqn == "Std\\FloatArray";
  return (IsIntArray && (LocalName == "map" || LocalName == "filter" ||
                         LocalName == "foldl")) ||
         (IsFloatArray && (LocalName == "map" || LocalName == "foldl"));
}

bool is_unlowerable_column_apply(ApplyExpr *site) {
  if (!site || is_curried_inner_apply(site))
    return false;
  auto flat = flatten_apply(site);
  if (flat.fn_name.empty())
    return false;
  auto args = collect_arg_exprs(flat.chain);
  std::string ModuleFqn = flat.module_fqn;
  std::string LocalName = flat.fn_name;
  if (!flat.module_fqn.empty()) {
  } else {
    auto got = resolve_imported_symbol(site, flat.fn_name);
    if (!got)
      return false;
    ModuleFqn = got->ModuleFqn;
    LocalName = got->LocalName;
  }
  if (!is_column_stdlib_function(ModuleFqn, LocalName))
    return false;
  // Must have an inline lambda in the first argument position for map/filter,
  // or foldl's combiner.
  ExprNode *fn_arg = nullptr;
  if (LocalName == "foldl") {
    if (args.size() != 3)
      return false;
    fn_arg = args[0];
  } else {
    if (args.size() != 2)
      return false;
    fn_arg = args[0];
  }
  if (!as_lambda(fn_arg))
    return false;
  return !match_column_kernel(ModuleFqn, LocalName, args, site).has_value();
}

std::vector<AccelMatch> collect_transparent_matches(AstNode *root) {
  return collect_from_walk(
      [&](const std::function<void(ApplyExpr *)> &on) { walk_ast(root, on); });
}

std::vector<AccelMatch> collect_transparent_matches_module(ModuleDecl *mod) {
  return collect_from_walk([&](const std::function<void(ApplyExpr *)> &on) {
    walk_module_decl(mod, on);
  });
}

} // namespace yona::compiler
