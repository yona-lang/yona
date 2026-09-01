/// JSON report of explicit Std\Gpu call sites and transparent kernel rewrites.
/// See docs/gpu-transparent-lowering.md.

#include "yona/Semantics/AcceleratorDiag.h"

#include "yona/Codegen/AcceleratorLowering.h"
#include "yona/Model/InferType.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Syntax/Ast.h"
#include "yona/Syntax/Utils.h"

#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace yona::compiler {
using ast::ApplyExpr;
using ast::AST_APPLY_EXPR;
using ast::AST_CASE_EXPR;
using ast::AST_CONS_LEFT_EXPR;
using ast::AST_CONS_RIGHT_EXPR;
using ast::AST_DICT_GENERATOR_EXPR;
using ast::AST_DO_EXPR;
using ast::AST_EXTERN_DECL;
using ast::AST_FIELD_ACCESS_EXPR;
using ast::AST_FIELD_UPDATE_EXPR;
using ast::AST_FUNCTION_EXPR;
using ast::AST_HANDLE_EXPR;
using ast::AST_IF_EXPR;
using ast::AST_IMPORT_EXPR;
using ast::AST_IN_EXPR;
using ast::AST_LET_EXPR;
using ast::AST_MAIN;
using ast::AST_PERFORM_EXPR;
using ast::AST_RAISE_EXPR;
using ast::AST_RECORD_INSTANCE_EXPR;
using ast::AST_RECORD_LITERAL_EXPR;
using ast::AST_SEQ_GENERATOR_EXPR;
using ast::AST_SET_GENERATOR_EXPR;
using ast::AST_TRY_CATCH_EXPR;
using ast::AST_TUPLE_EXPR;
using ast::AST_VALUES_SEQUENCE_EXPR;
using ast::AST_WITH_EXPR;
using ast::AstNode;
using ast::BinaryNotOpExpr;
using ast::BinaryOpExpr;
using ast::BodyWithGuards;
using ast::BodyWithoutGuards;
using ast::CaseExpr;
using ast::CollectionExtractorExpr;
using ast::ConsLeftExpr;
using ast::ConsRightExpr;
using ast::DictGeneratorExpr;
using ast::DoExpr;
using ast::ExprCall;
using ast::ExprNode;
using ast::ExternDeclExpr;
using ast::FieldAccessExpr;
using ast::FieldUpdateExpr;
using ast::FqnExpr;
using ast::FunctionExpr;
using ast::HandleExpr;
using ast::IdentifierExpr;
using ast::IfExpr;
using ast::ImportExpr;
using ast::InExpr;
using ast::IntegerExpr;
using ast::KeyValueCollectionExtractorExpr;
using ast::LambdaAlias;
using ast::LetExpr;
using ast::LogicalNotOpExpr;
using ast::MainNode;
using ast::ModuleCall;
using ast::ModuleDecl;
using ast::NameCall;
using ast::PatternAlias;
using ast::PatternWithoutGuards;
using ast::PerformExpr;
using ast::RaiseExpr;
using ast::RecordInstanceExpr;
using ast::RecordLiteralExpr;
using ast::SeqGeneratorExpr;
using ast::SetGeneratorExpr;
using ast::TryCatchExpr;
using ast::TupleExpr;
using ast::ValueAlias;
using ast::ValueCollectionExtractorExpr;
using ast::ValueExpr;
using ast::ValuesSequenceExpr;
using ast::WithExpr;

namespace {

static void walk_ast(AstNode *node,
                     const std::function<void(ApplyExpr *)> &on_apply);

std::string gpu_std_module_fqn() {
  return std::string("Std") + PACKAGE_DELIMITER + "Gpu";
}

const std::unordered_map<std::string, std::string> &
gpu_export_api_signatures() {
  static const std::unordered_map<std::string, std::string> k = {
      {"backendName", "String"},
      {"vulkanStatus", "String"},
      {"vulkanLastNote", "String"},
      {"hasGpu", "Bool"},
      {"hasSimd", "Bool"},
      {"vulkanAvailable", "Bool"},
      {"vulkanTimelineSemaphore", "Bool"},
      {"available", "() -> Bool"},
      {"physicalDeviceCount", "() -> Int"},
      {"upload", "IntArray -> Buffer"},
      {"materialize", "Buffer -> IntArray"},
      {"length", "Buffer -> Int"},
      {"mapAdd", "Int -> Buffer -> Buffer"},
      {"mapMul", "Int -> Buffer -> Buffer"},
      {"mapSquare", "Buffer -> Buffer"},
      {"filterGreaterThan", "Int -> Buffer -> Buffer"},
      {"filterLessThan", "Int -> Buffer -> Buffer"},
      {"reduceSum", "Buffer -> Int"},
      {"mapGpu", "IntMapOp -> Buffer -> Buffer"},
      {"reduceGpu", "IntReduceOp -> Buffer -> Int"},
      {"mapFloatGpu", "FloatMapOp -> FloatArray -> FloatArray"},
      {"reduceFloatGpu", "FloatReduceOp -> FloatArray -> Float"},
      {"mapReduceGraphGpu", "Seq IntMapOp -> Buffer -> Int"},
      {"allocPinnedFloats", "Int -> PinnedFloats"},
      {"closePinnedFloats", "PinnedFloats -> Int"},
      {"floatArrayMul2Async", "FloatArray -> Int"},
      {"floatArrayScaleAsync", "Float -> FloatArray -> Int"},
  };
  return k;
}

bool inferred_mono_informative(typechecker::MonoTypePtr t) {
  if (!t)
    return false;
  if (t->tag == typechecker::MonoType::Var)
    return false;
  return true;
}

bool inferred_pretty_informative(const std::string &pp) {
  if (pp.empty())
    return false;
  if (pp.size() == 1 && pp[0] >= 'a' && pp[0] <= 'z')
    return false;
  return true;
}

bool is_gpu_kernel_name(const std::string &n) {
  static const std::unordered_set<std::string> kOps = {
      "backendName",
      "vulkanStatus",
      "vulkanLastNote",
      "hasGpu",
      "hasSimd",
      "vulkanAvailable",
      "vulkanTimelineSemaphore",
      "available",
      "physicalDeviceCount",
      "upload",
      "materialize",
      "length",
      "mapAdd",
      "mapMul",
      "mapSquare",
      "filterGreaterThan",
      "filterLessThan",
      "reduceSum",
      "mapGpu",
      "reduceGpu",
      "mapFloatGpu",
      "reduceFloatGpu",
      "mapReduceGraphGpu",
      "allocPinnedFloats",
      "closePinnedFloats",
      "pinnedLength",
      "pinnedGet",
      "pinnedSet",
      "pinnedToFloatArray",
      "copyFloatArrayToPinned",
      "floatArrayMul2Async",
      "floatArrayScaleAsync",
  };
  return kOps.count(n) > 0;
}

bool is_curried_inner_apply_segment(ApplyExpr *a) {
  if (!a || !a->parent)
    return false;
  auto *p = dynamic_cast<ExprCall *>(a->parent);
  if (!p || !p->parent)
    return false;
  auto *outer = dynamic_cast<ApplyExpr *>(p->parent);
  return outer && outer->call == p;
}

/// Walk curried Apply chain to the leaf Apply whose `call` is NameCall or
/// ModuleCall, and classify as Std\Gpu kernel if applicable.
std::optional<std::pair<std::string, std::string>>
resolve_std_gpu_apply(ApplyExpr *start) {
  ApplyExpr *cur = start;
  for (;;) {
    if (auto *nc = dynamic_cast<NameCall *>(cur->call)) {
      const std::string &n = nc->name->value;
      if (!is_gpu_kernel_name(n))
        return std::nullopt;
      return std::make_pair(n, std::string("import"));
    }
    if (auto *mc = dynamic_cast<ModuleCall *>(cur->call)) {
      if (auto *fe = std::get_if<FqnExpr *>(&mc->fqn)) {
        if (*fe && (*fe)->to_string() == gpu_std_module_fqn()) {
          const std::string &n = mc->funName->value;
          if (!is_gpu_kernel_name(n))
            return std::nullopt;
          return std::make_pair(n, (*fe)->to_string());
        }
      }
      return std::nullopt;
    }
    if (auto *ec = dynamic_cast<ExprCall *>(cur->call)) {
      if (auto *inner = dynamic_cast<ApplyExpr *>(ec->expr)) {
        cur = inner;
        continue;
      }
    }
    return std::nullopt;
  }
}

struct AccelSite {
  std::string op;
  std::string binding;
  SourceRange loc;
  std::string api_signature;
  std::string inferred_pp;
  std::string kind;   // "explicit" or "transparent"
  std::string kernel; // Std\Gpu ABI name when kind is transparent
};

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

static void walk_ast(AstNode *node,
                     const std::function<void(ApplyExpr *)> &on_apply) {
  if (!node)
    return;

  switch (node->get_type()) {
  case AST_MAIN:
    walk_ast(static_cast<MainNode *>(node)->node, on_apply);
    return;
  case AST_LET_EXPR: {
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
  case AST_IMPORT_EXPR: {
    auto *imp = static_cast<ImportExpr *>(node);
    walk_ast(imp->expr, on_apply);
    return;
  }
  case AST_IF_EXPR: {
    auto *ie = static_cast<IfExpr *>(node);
    walk_ast(ie->condition, on_apply);
    walk_ast(ie->thenExpr, on_apply);
    walk_ast(ie->elseExpr, on_apply);
    return;
  }
  case AST_CASE_EXPR: {
    auto *ce = static_cast<CaseExpr *>(node);
    walk_ast(ce->expr, on_apply);
    for (auto *cl : ce->clauses) {
      if (cl->guard)
        walk_ast(cl->guard, on_apply);
      walk_ast(cl->body, on_apply);
    }
    return;
  }
  case AST_DO_EXPR: {
    auto *de = static_cast<DoExpr *>(node);
    for (auto *s : de->steps)
      walk_ast(s, on_apply);
    return;
  }
  case AST_FUNCTION_EXPR: {
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
  case AST_EXTERN_DECL: {
    auto *ex = static_cast<ExternDeclExpr *>(node);
    walk_ast(ex->body, on_apply);
    return;
  }
  case AST_TRY_CATCH_EXPR: {
    auto *tce = static_cast<TryCatchExpr *>(node);
    walk_ast(tce->tryExpr, on_apply);
    if (tce->catchExpr) {
      for (auto *cp : tce->catchExpr->patterns) {
        if (auto *pwog = std::get_if<PatternWithoutGuards *>(&cp->pattern)) {
          if (*pwog && (*pwog)->expr)
            walk_ast((*pwog)->expr, on_apply);
        }
      }
    }
    return;
  }
  case AST_WITH_EXPR: {
    auto *we = static_cast<WithExpr *>(node);
    walk_ast(we->contextExpr, on_apply);
    walk_ast(we->bodyExpr, on_apply);
    return;
  }
  case AST_PERFORM_EXPR: {
    auto *pe = static_cast<PerformExpr *>(node);
    for (auto *a : pe->args)
      walk_ast(a, on_apply);
    return;
  }
  case AST_HANDLE_EXPR: {
    auto *he = static_cast<HandleExpr *>(node);
    walk_ast(he->body, on_apply);
    for (auto *hc : he->clauses)
      walk_ast(hc->body, on_apply);
    return;
  }
  case AST_VALUES_SEQUENCE_EXPR: {
    auto *se = static_cast<ValuesSequenceExpr *>(node);
    for (auto *v : se->values)
      walk_ast(v, on_apply);
    return;
  }
  case AST_TUPLE_EXPR: {
    auto *te = static_cast<TupleExpr *>(node);
    for (auto *v : te->values)
      walk_ast(v, on_apply);
    return;
  }
  case AST_CONS_LEFT_EXPR: {
    auto *c = static_cast<ConsLeftExpr *>(node);
    walk_ast(c->left, on_apply);
    walk_ast(c->right, on_apply);
    return;
  }
  case AST_CONS_RIGHT_EXPR: {
    auto *c = static_cast<ConsRightExpr *>(node);
    walk_ast(c->left, on_apply);
    walk_ast(c->right, on_apply);
    return;
  }
  case AST_RECORD_LITERAL_EXPR: {
    auto *re = static_cast<RecordLiteralExpr *>(node);
    for (auto &[_, e] : re->fields)
      walk_ast(e, on_apply);
    return;
  }
  case AST_RECORD_INSTANCE_EXPR: {
    auto *ri = static_cast<RecordInstanceExpr *>(node);
    for (auto &[_, e] : ri->items)
      walk_ast(e, on_apply);
    return;
  }
  case AST_FIELD_ACCESS_EXPR: {
    walk_ast(static_cast<FieldAccessExpr *>(node)->identifier, on_apply);
    return;
  }
  case AST_FIELD_UPDATE_EXPR: {
    auto *fu = static_cast<FieldUpdateExpr *>(node);
    walk_ast(fu->identifier, on_apply);
    for (auto &[_, e] : fu->updates)
      walk_ast(e, on_apply);
    return;
  }
  case AST_SEQ_GENERATOR_EXPR: {
    auto *g = static_cast<SeqGeneratorExpr *>(node);
    walk_ast(g->reducerExpr, on_apply);
    walk_ast(g->stepExpression, on_apply);
    walk_collection_extractor(g->collectionExtractor, on_apply);
    return;
  }
  case AST_SET_GENERATOR_EXPR: {
    auto *g = static_cast<SetGeneratorExpr *>(node);
    walk_ast(g->reducerExpr, on_apply);
    walk_ast(g->stepExpression, on_apply);
    walk_collection_extractor(g->collectionExtractor, on_apply);
    return;
  }
  case AST_DICT_GENERATOR_EXPR: {
    auto *g = static_cast<DictGeneratorExpr *>(node);
    walk_ast(g->reducerExpr->key, on_apply);
    walk_ast(g->reducerExpr->value, on_apply);
    walk_ast(g->stepExpression, on_apply);
    walk_collection_extractor(g->collectionExtractor, on_apply);
    return;
  }
  case AST_RAISE_EXPR:
    walk_ast(static_cast<RaiseExpr *>(node)->value, on_apply);
    return;
  case AST_IN_EXPR: {
    auto *ie = static_cast<InExpr *>(node);
    walk_ast(ie->left, on_apply);
    walk_ast(ie->right, on_apply);
    return;
  }
  case AST_APPLY_EXPR:
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
  if (auto *u = dynamic_cast<LogicalNotOpExpr *>(node)) {
    walk_ast(u->expr, on_apply);
    return;
  }
  if (auto *u = dynamic_cast<BinaryNotOpExpr *>(node)) {
    walk_ast(u->expr, on_apply);
    return;
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

std::string json_escape(std::string_view s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':
      o += "\\\"";
      break;
    case '\\':
      o += "\\\\";
      break;
    case '\n':
      o += "\\n";
      break;
    case '\r':
      o += "\\r";
      break;
    case '\t':
      o += "\\t";
      break;
    default:
      o += c;
      break;
    }
  }
  return o;
}

static std::function<void(ApplyExpr *)>
make_accel_site_collector(std::vector<AccelSite> &sites,
                          const typechecker::TypeChecker *tc) {
  // The closure must not capture a reference to the local std::function; that
  // object is destroyed when this helper returns. Hold the functor on the heap
  // so recursive walk_apply_children always invokes a live std::function.
  auto self = std::make_shared<std::function<void(ApplyExpr *)>>();
  *self = [self, &sites, tc](ApplyExpr *ae) {
    if (!is_curried_inner_apply_segment(ae)) {
      if (auto got = resolve_std_gpu_apply(ae)) {
        AccelSite s;
        s.op = std::move(got->first);
        s.binding = std::move(got->second);
        s.loc = ae->Range;
        s.kind = "explicit";
        if (auto it = gpu_export_api_signatures().find(s.op);
            it != gpu_export_api_signatures().end())
          s.api_signature = it->second;
        if (tc) {
          if (auto *ty = tc->type_of(ae)) {
            auto *z = const_cast<typechecker::TypeChecker *>(tc)->zonk(ty);
            if (inferred_mono_informative(z)) {
              std::string pp = pretty_print(z);
              if (inferred_pretty_informative(pp))
                s.inferred_pp = std::move(pp);
            }
          }
        }
        sites.push_back(std::move(s));
      } else if (auto tm = match_transparent_apply(ae)) {
        AccelSite s;
        s.op = tm->kernel_name ? tm->kernel_name : "map";
        s.binding = tm->binding.empty() ? std::string("import") : tm->binding;
        s.loc = ae->Range;
        s.kind = "transparent";
        s.kernel = tm->kernel_name ? tm->kernel_name : "";
        s.api_signature = "columnar Std\\Gpu ABI";
        if (tc) {
          if (auto *ty = tc->type_of(ae)) {
            auto *z = const_cast<typechecker::TypeChecker *>(tc)->zonk(ty);
            if (inferred_mono_informative(z)) {
              std::string pp = pretty_print(z);
              if (inferred_pretty_informative(pp))
                s.inferred_pp = std::move(pp);
            }
          }
        }
        sites.push_back(std::move(s));
      }
    }
    walk_apply_children(ae, *self);
  };
  return *self;
}

static void write_accel_json(std::ostream &out,
                             const std::vector<AccelSite> &sites,
                             std::string_view input_filename,
                             std::string_view report_kind) {
  out << "{\"schema\":\"yona.accelerator_diag\"";
  if (!input_filename.empty())
    out << ",\"file\":\"" << json_escape(input_filename) << "\"";
  if (!report_kind.empty())
    out << ",\"report_kind\":\"" << json_escape(report_kind) << "\"";
  out << ",\"sites\":[";
  for (size_t i = 0; i < sites.size(); ++i) {
    if (i)
      out << ',';
    const AccelSite &s = sites[i];
    out << "{\"op\":\"" << json_escape(s.op) << "\",\"binding\":\""
        << json_escape(s.binding) << "\",\"api_signature\":\""
        << json_escape(s.api_signature) << "\",\"line\":" << s.loc.Line
        << ",\"column\":" << s.loc.Column;
    if (!s.kind.empty())
      out << ",\"kind\":\"" << json_escape(s.kind) << "\"";
    if (!s.kernel.empty())
      out << ",\"kernel\":\"" << json_escape(s.kernel) << "\"";
    if (!s.inferred_pp.empty())
      out << ",\"inferred_type\":\"" << json_escape(s.inferred_pp) << "\"";
    out << '}';
  }
  out << "]}\n";
}

} // namespace

void emit_accelerator_diagnostic_report(std::ostream &out, AstNode *root,
                                        const typechecker::TypeChecker *tc,
                                        std::string_view input_filename) {
  std::vector<AccelSite> sites;
  walk_ast(root, make_accel_site_collector(sites, tc));
  write_accel_json(out, sites, input_filename, "program");
}

bool typecheck_module_for_accelerator_report(ModuleDecl *mod,
                                             typechecker::TypeChecker &tc) {
  if (!mod)
    return true;
  for (auto *fe : mod->functions) {
    if (fe)
      tc.check(fe);
  }
  for (auto *ex : mod->extern_declarations) {
    if (ex && ex->body)
      tc.check(ex->body);
  }
  for (auto *inst : mod->instance_declarations) {
    for (auto *m : inst->methods) {
      if (m)
        tc.check(m);
    }
  }
  for (auto *tr : mod->trait_declarations) {
    for (const auto &ms : tr->methods) {
      if (ms.default_impl)
        tc.check(ms.default_impl);
    }
  }
  if (tc.has_direct_errors())
    return false;
  if (!tc.solve_constraints())
    return false;
  if (tc.has_errors())
    return false;
  return true;
}

void emit_accelerator_diagnostic_report_for_module(
    std::ostream &out, ast::ModuleDecl *mod, std::string_view input_filename,
    const typechecker::TypeChecker *tc) {
  std::vector<AccelSite> sites;
  walk_module_decl(mod, make_accel_site_collector(sites, tc));
  write_accel_json(out, sites, input_filename, tc ? "module" : "module_ast");
}

} // namespace yona::compiler
