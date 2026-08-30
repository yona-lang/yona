#include "yona/Semantics/TerminationAnalysis.h"

#include "yona/Syntax/Ast.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace yona::compiler::termination_analysis {
namespace {

using ast::AstNode;
using ast::FunctionExpr;
using Relations = std::vector<Relation>;
using Facts = std::unordered_map<std::string, Relations>;
// A present optional identifies a statically known local function. nullopt is a
// lexical binding that shadows outer functions but is not a known local target.
using LexicalBindings = std::unordered_map<std::string, std::optional<size_t>>;
using FunctionDefinitions = std::vector<std::pair<std::string, FunctionExpr *>>;

struct FunctionInfo;

struct Edge {
  FunctionInfo *caller = nullptr;
  FunctionInfo *callee = nullptr;
  SourceRange location;
  Relations relations;
};

struct FunctionInfo {
  FunctionExpr *node = nullptr;
  size_t index = 0;
  std::vector<Edge> outgoing;
  LexicalBindings bindings;
  std::string name;
};

struct DirectCall {
  ast::NameCall *target = nullptr;
  std::vector<AstNode *> arguments;
};

AstNode *
argument_node(const std::variant<ast::ExprNode *, ast::ValueExpr *> &argument) {
  return std::visit([](auto *node) -> AstNode * { return node; }, argument);
}

std::optional<DirectCall> direct_call(ast::ApplyExpr *outer) {
  std::vector<std::vector<AstNode *>> argument_groups;
  auto *current = outer;
  while (current) {
    std::vector<AstNode *> group;
    group.reserve(current->args.size());
    for (const auto &argument : current->args)
      group.push_back(argument_node(argument));
    argument_groups.push_back(std::move(group));

    if (auto *target = dynamic_cast<ast::NameCall *>(current->call)) {
      DirectCall call;
      call.target = target;
      for (auto group_it = argument_groups.rbegin();
           group_it != argument_groups.rend(); ++group_it)
        call.arguments.insert(call.arguments.end(), group_it->begin(),
                              group_it->end());
      return call;
    }
    auto *expression_call = dynamic_cast<ast::ExprCall *>(current->call);
    current = expression_call
                  ? dynamic_cast<ast::ApplyExpr *>(expression_call->expr)
                  : nullptr;
  }
  return std::nullopt;
}

std::optional<std::string> identifier_name(AstNode *node) {
  auto *identifier = dynamic_cast<ast::IdentifierExpr *>(node);
  if (!identifier || !identifier->name)
    return std::nullopt;
  return identifier->name->value;
}

std::optional<std::string> pattern_identifier(ast::PatternNode *pattern) {
  auto *value = dynamic_cast<ast::PatternValue *>(pattern);
  if (!value)
    return std::nullopt;
  auto *identifier = std::get_if<ast::IdentifierExpr *>(&value->expr);
  if (!identifier || !*identifier || !(*identifier)->name)
    return std::nullopt;
  return (*identifier)->name->value;
}

Relations unknown_relations(size_t arity) {
  return Relations(arity, Relation::Unknown);
}

Relations strict_descendant(Relations relations) {
  for (auto &relation : relations)
    if (relation != Relation::Unknown)
      relation = Relation::Strict;
  return relations;
}

class Analyzer {
public:
  Result run(AstNode &root) {
    discover(&root);
    for (auto &function : functions_)
      collect_edges(function);
    prove_components();
    return std::move(result_);
  }

private:
  std::vector<FunctionInfo> functions_;
  std::unordered_map<FunctionExpr *, size_t> function_indices_;
  Result result_;

  size_t register_function(FunctionExpr *function,
                           const std::string &binding_name) {
    if (const auto found = function_indices_.find(function);
        found != function_indices_.end()) {
      if (functions_[found->second].name.empty())
        functions_[found->second].name = binding_name;
      return found->second;
    }
    const size_t index = functions_.size();
    function_indices_.emplace(function, index);
    functions_.push_back({function, index, {}, {}, binding_name});
    return index;
  }

  LexicalBindings bind_function_group(const FunctionDefinitions &functions,
                                      const LexicalBindings &outer_bindings) {
    LexicalBindings bindings = outer_bindings;
    for (const auto &[name, function] : functions) {
      if (!function)
        continue;
      bindings[name] = register_function(function, name);
    }

    return bindings;
  }

  void discover_function_group(const FunctionDefinitions &functions,
                               const LexicalBindings &bindings) {
    for (const auto &[_, function] : functions) {
      if (!function)
        continue;
      functions_[function_indices_.at(function)].bindings = bindings;
      LexicalBindings body_bindings = bindings;
      for (auto *pattern : function->patterns)
        shadow_pattern(pattern, body_bindings);
      body_bindings[functions_[function_indices_.at(function)].name] =
          function_indices_.at(function);
      for (auto *body : function->bodies) {
        if (auto *plain = dynamic_cast<ast::BodyWithoutGuards *>(body))
          discover(plain->expr, body_bindings);
        else if (auto *guarded = dynamic_cast<ast::BodyWithGuards *>(body)) {
          discover(guarded->guard, body_bindings);
          discover(guarded->expr, body_bindings);
        }
      }
    }
  }

  LexicalBindings
  register_function_group(const FunctionDefinitions &functions,
                          const LexicalBindings &outer_bindings) {
    auto bindings = bind_function_group(functions, outer_bindings);
    discover_function_group(functions, bindings);
    return bindings;
  }

  static void shadow_pattern(ast::PatternNode *pattern,
                             LexicalBindings &bindings) {
    if (!pattern)
      return;
    if (auto name = pattern_identifier(pattern)) {
      bindings[*name] = std::nullopt;
    } else if (auto *constructor =
                   dynamic_cast<ast::ConstructorPattern *>(pattern)) {
      for (auto *sub_pattern : constructor->sub_patterns)
        shadow_pattern(sub_pattern, bindings);
    } else if (auto *alias =
                   dynamic_cast<ast::AsDataStructurePattern *>(pattern)) {
      if (alias->identifier && alias->identifier->name)
        bindings[alias->identifier->name->value] = std::nullopt;
      shadow_pattern(alias->pattern, bindings);
    } else if (auto *head_tail =
                   dynamic_cast<ast::HeadTailsPattern *>(pattern)) {
      for (auto *head : head_tail->heads)
        shadow_pattern(head, bindings);
      shadow_pattern(head_tail->tail, bindings);
    } else if (auto *tail_head =
                   dynamic_cast<ast::TailsHeadPattern *>(pattern)) {
      shadow_pattern(tail_head->tail, bindings);
      for (auto *head : tail_head->heads)
        shadow_pattern(head, bindings);
    } else if (auto *split =
                   dynamic_cast<ast::HeadTailsHeadPattern *>(pattern)) {
      for (auto *item : split->left)
        shadow_pattern(item, bindings);
      shadow_pattern(split->tail, bindings);
      for (auto *item : split->right)
        shadow_pattern(item, bindings);
    } else if (auto *tuple = dynamic_cast<ast::TuplePattern *>(pattern)) {
      for (auto *item : tuple->patterns)
        shadow_pattern(item, bindings);
    } else if (auto *sequence = dynamic_cast<ast::SeqPattern *>(pattern)) {
      for (auto *item : sequence->patterns)
        shadow_pattern(item, bindings);
    } else if (auto *record = dynamic_cast<ast::RecordPattern *>(pattern)) {
      for (auto &[_, item] : record->items)
        shadow_pattern(item, bindings);
    } else if (auto *dict = dynamic_cast<ast::DictPattern *>(pattern)) {
      for (auto &[_, item] : dict->keyValuePairs)
        shadow_pattern(item, bindings);
    } else if (auto *typed = dynamic_cast<ast::TypedPattern *>(pattern)) {
      bindings[typed->binding_name] = std::nullopt;
    } else if (auto *alternatives = dynamic_cast<ast::OrPattern *>(pattern)) {
      for (const auto &alternative : alternatives->patterns)
        shadow_pattern(alternative.get(), bindings);
    }
  }

  static std::optional<size_t> callable_alias(AstNode *expression,
                                              const LexicalBindings &bindings) {
    const auto name = identifier_name(expression);
    if (!name)
      return std::nullopt;
    const auto found = bindings.find(*name);
    return found == bindings.end() ? std::nullopt : found->second;
  }

  static void
  shadow_generator_binding(const ast::IdentifierOrUnderscore &binding,
                           LexicalBindings &bindings) {
    if (const auto *identifier = std::get_if<ast::IdentifierExpr *>(&binding);
        identifier && *identifier && (*identifier)->name)
      bindings[(*identifier)->name->value] = std::nullopt;
  }

  void discover_generator(ast::CollectionExtractorExpr *extractor,
                          AstNode *reducer, AstNode *step,
                          const LexicalBindings &bindings) {
    auto body_bindings = bindings;
    if (auto *value =
            dynamic_cast<ast::ValueCollectionExtractorExpr *>(extractor)) {
      discover(value->collection, bindings);
      shadow_generator_binding(value->expr, body_bindings);
      discover(value->condition, body_bindings);
    } else if (auto *key_value =
                   dynamic_cast<ast::KeyValueCollectionExtractorExpr *>(
                       extractor)) {
      discover(key_value->collection, bindings);
      shadow_generator_binding(key_value->keyExpr, body_bindings);
      shadow_generator_binding(key_value->valueExpr, body_bindings);
      discover(key_value->condition, body_bindings);
    }
    discover(reducer, body_bindings);
    discover(step, body_bindings);
  }

  void discover(AstNode *node, const LexicalBindings &bindings = {}) {
    if (!node)
      return;
    if (auto *main = dynamic_cast<ast::MainNode *>(node)) {
      discover(main->node, bindings);
    } else if (auto *module = dynamic_cast<ast::ModuleDecl *>(node)) {
      FunctionDefinitions module_functions;
      for (auto *function : module->functions)
        if (function)
          module_functions.emplace_back(function->name, function);
      const auto module_bindings =
          register_function_group(module_functions, bindings);
      for (auto *trait : module->trait_declarations) {
        if (!trait)
          continue;
        FunctionDefinitions defaults;
        for (const auto &method : trait->methods)
          if (method.default_impl)
            defaults.emplace_back(method.name, method.default_impl);
        register_function_group(defaults, module_bindings);
      }
      for (auto *instance : module->instance_declarations)
        if (instance) {
          FunctionDefinitions methods;
          for (auto *method : instance->methods)
            if (method)
              methods.emplace_back(method->name, method);
          register_function_group(methods, module_bindings);
        }
      for (auto *external : module->extern_declarations)
        if (external)
          discover(external, module_bindings);
    } else if (auto *function = dynamic_cast<FunctionExpr *>(node)) {
      register_function_group({{function->name, function}}, bindings);
    } else if (auto *let = dynamic_cast<ast::LetExpr *>(node)) {
      FunctionDefinitions lambdas;
      for (auto *alias : let->aliases)
        if (auto *lambda = dynamic_cast<ast::LambdaAlias *>(alias))
          if (lambda->name)
            lambdas.emplace_back(lambda->name->value, lambda->lambda);
      auto body_bindings = bind_function_group(lambdas, bindings);
      for (auto *alias : let->aliases) {
        if (auto *value = dynamic_cast<ast::ValueAlias *>(alias)) {
          discover(value->expr, body_bindings);
          if (value->identifier && value->identifier->name)
            body_bindings[value->identifier->name->value] =
                callable_alias(value->expr, body_bindings);
        } else if (auto *pattern = dynamic_cast<ast::PatternAlias *>(alias)) {
          discover(pattern->expr, body_bindings);
          shadow_pattern(pattern->pattern, body_bindings);
        }
      }
      discover_function_group(lambdas, body_bindings);
      discover(let->expr, body_bindings);
    } else if (auto *apply = dynamic_cast<ast::ApplyExpr *>(node)) {
      if (auto *call = dynamic_cast<ast::ExprCall *>(apply->call))
        discover(call->expr, bindings);
      else if (auto *call = dynamic_cast<ast::ModuleCall *>(apply->call))
        if (auto *expression = std::get_if<ast::ExprNode *>(&call->fqn))
          discover(*expression, bindings);
      for (const auto &argument : apply->args)
        discover(argument_node(argument), bindings);
      if (apply->named_args)
        for (const auto &[_, argument] : *apply->named_args)
          discover(argument_node(argument), bindings);
    } else if (auto *case_expr = dynamic_cast<ast::CaseExpr *>(node)) {
      discover(case_expr->expr, bindings);
      for (auto *clause : case_expr->clauses)
        if (clause) {
          auto arm_bindings = bindings;
          shadow_pattern(clause->pattern, arm_bindings);
          discover(clause->guard, arm_bindings);
          discover(clause->body, arm_bindings);
        }
    } else if (auto *binary = dynamic_cast<ast::BinaryOpExpr *>(node)) {
      discover(binary->left, bindings);
      discover(binary->right, bindings);
    } else if (auto *if_expr = dynamic_cast<ast::IfExpr *>(node)) {
      discover(if_expr->condition, bindings);
      discover(if_expr->thenExpr, bindings);
      discover(if_expr->elseExpr, bindings);
    } else if (auto *do_expr = dynamic_cast<ast::DoExpr *>(node)) {
      for (auto *step : do_expr->steps)
        discover(step, bindings);
    } else if (auto *import = dynamic_cast<ast::ImportExpr *>(node)) {
      auto imported_bindings = bindings;
      for (auto *clause : import->clauses)
        if (auto *functions = dynamic_cast<ast::FunctionsImport *>(clause))
          for (auto *alias : functions->aliases)
            if (alias && alias->name)
              imported_bindings[alias->alias ? alias->alias->value
                                             : alias->name->value] =
                  std::nullopt;
      discover(import->expr, imported_bindings);
    } else if (auto *external = dynamic_cast<ast::ExternDeclExpr *>(node)) {
      auto body_bindings = bindings;
      body_bindings[external->name] = std::nullopt;
      discover(external->body, body_bindings);
    } else if (auto *with = dynamic_cast<ast::WithExpr *>(node)) {
      discover(with->contextExpr, bindings);
      auto body_bindings = bindings;
      if (with->name)
        body_bindings[with->name->value] = std::nullopt;
      discover(with->bodyExpr, body_bindings);
    } else if (auto *handle = dynamic_cast<ast::HandleExpr *>(node)) {
      discover(handle->body, bindings);
      for (auto *clause : handle->clauses)
        if (clause) {
          auto clause_bindings = bindings;
          for (const auto &name : clause->arg_names)
            if (!name.empty())
              clause_bindings[name] = std::nullopt;
          if (!clause->resume_name.empty())
            clause_bindings[clause->resume_name] = std::nullopt;
          if (!clause->return_binding.empty())
            clause_bindings[clause->return_binding] = std::nullopt;
          discover(clause->body, clause_bindings);
        }
    } else if (auto *try_catch = dynamic_cast<ast::TryCatchExpr *>(node)) {
      discover(try_catch->tryExpr, bindings);
      if (try_catch->catchExpr)
        for (auto *item : try_catch->catchExpr->patterns)
          if (item) {
            auto catch_bindings = bindings;
            shadow_pattern(item->matchPattern, catch_bindings);
            std::visit(
                [&](auto &body) {
                  using Body = std::remove_cvref_t<decltype(body)>;
                  if constexpr (std::is_same_v<Body,
                                               ast::PatternWithoutGuards *>) {
                    if (body)
                      discover(body->expr, catch_bindings);
                  } else {
                    for (auto *guarded : body)
                      if (guarded) {
                        discover(guarded->guard, catch_bindings);
                        discover(guarded->expr, catch_bindings);
                      }
                  }
                },
                item->pattern);
          }
    } else if (auto *raise = dynamic_cast<ast::RaiseExpr *>(node)) {
      discover(raise->value, bindings);
    } else if (auto *logical_not =
                   dynamic_cast<ast::LogicalNotOpExpr *>(node)) {
      discover(logical_not->expr, bindings);
    } else if (auto *binary_not = dynamic_cast<ast::BinaryNotOpExpr *>(node)) {
      discover(binary_not->expr, bindings);
    } else if (auto *tuple = dynamic_cast<ast::TupleExpr *>(node)) {
      for (auto *value : tuple->values)
        discover(value, bindings);
    } else if (auto *sequence = dynamic_cast<ast::ValuesSequenceExpr *>(node)) {
      for (auto *value : sequence->values)
        discover(value, bindings);
    } else if (auto *range = dynamic_cast<ast::RangeSequenceExpr *>(node)) {
      discover(range->start, bindings);
      discover(range->end, bindings);
      discover(range->step, bindings);
    } else if (auto *set = dynamic_cast<ast::SetExpr *>(node)) {
      for (auto *value : set->values)
        discover(value, bindings);
    } else if (auto *dict = dynamic_cast<ast::DictExpr *>(node)) {
      for (const auto &[key, value] : dict->values) {
        discover(key, bindings);
        discover(value, bindings);
      }
    } else if (auto *record = dynamic_cast<ast::RecordInstanceExpr *>(node)) {
      for (const auto &[_, value] : record->items)
        discover(value, bindings);
    } else if (auto *record = dynamic_cast<ast::RecordLiteralExpr *>(node)) {
      for (const auto &[_, value] : record->fields)
        discover(value, bindings);
    } else if (auto *update = dynamic_cast<ast::FieldUpdateExpr *>(node)) {
      for (const auto &[_, value] : update->updates)
        discover(value, bindings);
    } else if (auto *sequence_generator =
                   dynamic_cast<ast::SeqGeneratorExpr *>(node)) {
      discover_generator(sequence_generator->collectionExtractor,
                         sequence_generator->reducerExpr,
                         sequence_generator->stepExpression, bindings);
    } else if (auto *set_generator =
                   dynamic_cast<ast::SetGeneratorExpr *>(node)) {
      discover_generator(set_generator->collectionExtractor,
                         set_generator->reducerExpr,
                         set_generator->stepExpression, bindings);
    } else if (auto *dict_generator =
                   dynamic_cast<ast::DictGeneratorExpr *>(node)) {
      discover_generator(dict_generator->collectionExtractor,
                         dict_generator->reducerExpr,
                         dict_generator->stepExpression, bindings);
    } else if (auto *reducer =
                   dynamic_cast<ast::DictGeneratorReducer *>(node)) {
      discover(reducer->key, bindings);
      discover(reducer->value, bindings);
    } else if (auto *extractor =
                   dynamic_cast<ast::ValueCollectionExtractorExpr *>(node)) {
      discover(extractor->collection, bindings);
      discover(extractor->condition, bindings);
    } else if (auto *extractor =
                   dynamic_cast<ast::KeyValueCollectionExtractorExpr *>(node)) {
      discover(extractor->collection, bindings);
      discover(extractor->condition, bindings);
    } else if (auto *perform = dynamic_cast<ast::PerformExpr *>(node)) {
      for (auto *argument : perform->args)
        discover(argument, bindings);
    } else if (auto *type_instance = dynamic_cast<ast::TypeInstance *>(node)) {
      for (auto *expression : type_instance->exprs)
        discover(expression, bindings);
    } else if (auto *pattern = dynamic_cast<ast::PatternWithGuards *>(node)) {
      discover(pattern->guard, bindings);
      discover(pattern->expr, bindings);
    } else if (auto *pattern =
                   dynamic_cast<ast::PatternWithoutGuards *>(node)) {
      discover(pattern->expr, bindings);
    } else if (auto *pattern = dynamic_cast<ast::PatternExpr *>(node)) {
      std::visit(
          [&](auto &body) {
            using Body = std::remove_cvref_t<decltype(body)>;
            if constexpr (std::is_same_v<Body, ast::PatternWithoutGuards *>) {
              discover(body, bindings);
            } else if constexpr (std::is_same_v<
                                     Body,
                                     std::vector<ast::PatternWithGuards *>>) {
              for (auto *guarded : body)
                discover(guarded, bindings);
            }
          },
          pattern->patternExpr);
    } else if (auto *clause = dynamic_cast<ast::CaseClause *>(node)) {
      auto clause_bindings = bindings;
      shadow_pattern(clause->pattern, clause_bindings);
      discover(clause->guard, clause_bindings);
      discover(clause->body, clause_bindings);
    } else if (auto *pattern = dynamic_cast<ast::CatchPatternExpr *>(node)) {
      auto catch_bindings = bindings;
      shadow_pattern(pattern->matchPattern, catch_bindings);
      std::visit(
          [&](auto &body) {
            using Body = std::remove_cvref_t<decltype(body)>;
            if constexpr (std::is_same_v<Body, ast::PatternWithoutGuards *>) {
              discover(body, catch_bindings);
            } else {
              for (auto *guarded : body)
                discover(guarded, catch_bindings);
            }
          },
          pattern->pattern);
    } else if (auto *catch_expr = dynamic_cast<ast::CatchExpr *>(node)) {
      for (auto *pattern : catch_expr->patterns)
        discover(pattern, bindings);
    } else if (auto *value = dynamic_cast<ast::ValueAlias *>(node)) {
      discover(value->expr, bindings);
    } else if (auto *pattern = dynamic_cast<ast::PatternAlias *>(node)) {
      discover(pattern->expr, bindings);
    } else if (auto *lambda = dynamic_cast<ast::LambdaAlias *>(node)) {
      if (lambda->name)
        register_function_group({{lambda->name->value, lambda->lambda}},
                                bindings);
    } else if (auto *body = dynamic_cast<ast::BodyWithGuards *>(node)) {
      discover(body->guard, bindings);
      discover(body->expr, bindings);
    } else if (auto *body = dynamic_cast<ast::BodyWithoutGuards *>(node)) {
      discover(body->expr, bindings);
    }
  }

  void bind_unknown(ast::PatternNode *pattern, Facts &facts,
                    LexicalBindings &bindings, size_t arity) {
    bind_pattern(pattern, unknown_relations(arity), facts, bindings, false);
  }

  void bind_pattern(ast::PatternNode *pattern, const Relations &source,
                    Facts &facts, LexicalBindings &bindings,
                    bool structural_field) {
    if (!pattern)
      return;
    const Relations relation =
        structural_field ? strict_descendant(source) : source;
    if (auto name = pattern_identifier(pattern)) {
      facts[*name] = relation;
      bindings[*name] = std::nullopt;
    } else if (auto *constructor =
                   dynamic_cast<ast::ConstructorPattern *>(pattern)) {
      for (auto *sub_pattern : constructor->sub_patterns)
        bind_pattern(sub_pattern, relation, facts, bindings, true);
    } else if (auto *alias =
                   dynamic_cast<ast::AsDataStructurePattern *>(pattern)) {
      if (alias->identifier && alias->identifier->name) {
        facts[alias->identifier->name->value] = relation;
        bindings[alias->identifier->name->value] = std::nullopt;
      }
      bind_pattern(alias->pattern, relation, facts, bindings, false);
    } else if (auto *head_tail =
                   dynamic_cast<ast::HeadTailsPattern *>(pattern)) {
      for (auto *head : head_tail->heads)
        bind_unknown(head, facts, bindings, relation.size());
      bind_pattern(head_tail->tail, relation, facts, bindings, true);
    } else if (auto *tail_head =
                   dynamic_cast<ast::TailsHeadPattern *>(pattern)) {
      bind_pattern(tail_head->tail, relation, facts, bindings, true);
      for (auto *head : tail_head->heads)
        bind_unknown(head, facts, bindings, relation.size());
    } else if (auto *split =
                   dynamic_cast<ast::HeadTailsHeadPattern *>(pattern)) {
      for (auto *item : split->left)
        bind_unknown(item, facts, bindings, relation.size());
      bind_pattern(split->tail, relation, facts, bindings, true);
      for (auto *item : split->right)
        bind_unknown(item, facts, bindings, relation.size());
    } else if (auto *tuple = dynamic_cast<ast::TuplePattern *>(pattern)) {
      for (auto *item : tuple->patterns)
        bind_unknown(item, facts, bindings, relation.size());
    } else if (auto *sequence = dynamic_cast<ast::SeqPattern *>(pattern)) {
      for (auto *item : sequence->patterns)
        bind_unknown(item, facts, bindings, relation.size());
    } else if (auto *record = dynamic_cast<ast::RecordPattern *>(pattern)) {
      for (auto &[_, item] : record->items)
        bind_unknown(item, facts, bindings, relation.size());
    } else if (auto *dict = dynamic_cast<ast::DictPattern *>(pattern)) {
      for (auto &[_, item] : dict->keyValuePairs)
        bind_unknown(item, facts, bindings, relation.size());
    } else if (auto *typed = dynamic_cast<ast::TypedPattern *>(pattern)) {
      facts[typed->binding_name] = relation;
      bindings[typed->binding_name] = std::nullopt;
    } else if (auto *alternatives = dynamic_cast<ast::OrPattern *>(pattern)) {
      for (const auto &alternative : alternatives->patterns)
        bind_unknown(alternative.get(), facts, bindings, relation.size());
    }
  }

  Relations expression_relations(AstNode *expression, const Facts &facts,
                                 size_t arity) const {
    auto name = identifier_name(expression);
    if (!name)
      return unknown_relations(arity);
    auto found = facts.find(*name);
    return found == facts.end() ? unknown_relations(arity) : found->second;
  }

  void collect_edges(FunctionInfo &function) {
    const size_t arity = function.node->patterns.size();
    for (auto *body : function.node->bodies) {
      Facts facts;
      LexicalBindings bindings = function.bindings;
      const bool guarded = dynamic_cast<ast::BodyWithGuards *>(body) != nullptr;
      for (size_t index = 0; index < arity; ++index) {
        Relations parameter = unknown_relations(arity);
        parameter[index] = Relation::Weak;
        if (guarded)
          bind_unknown(function.node->patterns[index], facts, bindings, arity);
        else
          bind_pattern(function.node->patterns[index], parameter, facts,
                       bindings, false);
      }
      // A function name remains callable even though parameter/value bindings
      // shadow other local function names.
      bindings[function.name] = function.index;

      if (auto *plain = dynamic_cast<ast::BodyWithoutGuards *>(body)) {
        walk(plain->expr, facts, bindings, function);
      } else if (auto *guarded = dynamic_cast<ast::BodyWithGuards *>(body)) {
        walk(guarded->guard, facts, bindings, function);
        walk(guarded->expr, facts, bindings, function);
      }
    }
  }

  void add_call_edges(ast::ApplyExpr *apply, const DirectCall &call,
                      const Facts &facts, const LexicalBindings &bindings,
                      FunctionInfo &caller) {
    if (!call.target || !call.target->name)
      return;
    const auto target = bindings.find(call.target->name->value);
    if (target == bindings.end() || !target->second)
      return;

    auto &callee = functions_[*target->second];
    Relations relations(callee.node->patterns.size(), Relation::Unknown);
    for (size_t position = 0;
         position < relations.size() && position < call.arguments.size();
         ++position) {
      const auto argument = expression_relations(
          call.arguments[position], facts, caller.node->patterns.size());
      if (position < argument.size())
        relations[position] = argument[position];
    }
    caller.outgoing.push_back(
        {&caller, &callee, apply->Range, std::move(relations)});
  }

  static Facts without_descent_facts(Facts facts) {
    for (auto &[_, relations] : facts)
      for (auto &relation : relations)
        if (relation == Relation::Strict)
          relation = Relation::Unknown;
    return facts;
  }

  void bind_generator_name(const ast::IdentifierOrUnderscore &binding,
                           Facts &facts, LexicalBindings &bindings,
                           size_t arity) {
    if (const auto *identifier = std::get_if<ast::IdentifierExpr *>(&binding);
        identifier && *identifier && (*identifier)->name) {
      const auto &name = (*identifier)->name->value;
      facts[name] = unknown_relations(arity);
      bindings[name] = std::nullopt;
    }
  }

  void walk_generator(ast::CollectionExtractorExpr *extractor, AstNode *reducer,
                      AstNode *step, const Facts &facts,
                      const LexicalBindings &bindings, FunctionInfo &function) {
    Facts body_facts = facts;
    LexicalBindings body_bindings = bindings;
    if (auto *value =
            dynamic_cast<ast::ValueCollectionExtractorExpr *>(extractor)) {
      walk(value->collection, facts, bindings, function);
      bind_generator_name(value->expr, body_facts, body_bindings,
                          function.node->patterns.size());
      walk(value->condition, body_facts, body_bindings, function);
    } else if (auto *key_value =
                   dynamic_cast<ast::KeyValueCollectionExtractorExpr *>(
                       extractor)) {
      walk(key_value->collection, facts, bindings, function);
      bind_generator_name(key_value->keyExpr, body_facts, body_bindings,
                          function.node->patterns.size());
      bind_generator_name(key_value->valueExpr, body_facts, body_bindings,
                          function.node->patterns.size());
      walk(key_value->condition, body_facts, body_bindings, function);
    }
    walk(reducer, body_facts, body_bindings, function);
    walk(step, body_facts, body_bindings, function);
  }

  void walk(AstNode *node, const Facts &facts, const LexicalBindings &bindings,
            FunctionInfo &function) {
    if (!node)
      return;
    if (auto *apply = dynamic_cast<ast::ApplyExpr *>(node)) {
      if (auto call = direct_call(apply)) {
        add_call_edges(apply, *call, facts, bindings, function);
        for (auto *argument : call->arguments)
          walk(argument, facts, bindings, function);
        if (apply->named_args)
          for (const auto &[_, argument] : *apply->named_args)
            walk(argument_node(argument), facts, bindings, function);
        return;
      }
      if (auto *call = dynamic_cast<ast::ExprCall *>(apply->call))
        walk(call->expr, facts, bindings, function);
      else if (auto *call = dynamic_cast<ast::ModuleCall *>(apply->call))
        if (auto *expression = std::get_if<ast::ExprNode *>(&call->fqn))
          walk(*expression, facts, bindings, function);
      for (const auto &argument : apply->args)
        walk(argument_node(argument), facts, bindings, function);
      if (apply->named_args)
        for (const auto &[_, argument] : *apply->named_args)
          walk(argument_node(argument), facts, bindings, function);
    } else if (auto *case_expr = dynamic_cast<ast::CaseExpr *>(node)) {
      walk(case_expr->expr, facts, bindings, function);
      const Relations scrutinee = expression_relations(
          case_expr->expr, facts, function.node->patterns.size());
      for (auto *clause : case_expr->clauses)
        if (clause) {
          Facts arm_facts = facts;
          LexicalBindings arm_bindings = bindings;
          if (clause->guard)
            bind_unknown(clause->pattern, arm_facts, arm_bindings,
                         function.node->patterns.size());
          else
            bind_pattern(clause->pattern, scrutinee, arm_facts, arm_bindings,
                         false);
          walk(clause->guard, arm_facts, arm_bindings, function);
          walk(clause->body, arm_facts, arm_bindings, function);
        }
    } else if (auto *binary = dynamic_cast<ast::BinaryOpExpr *>(node)) {
      walk(binary->left, facts, bindings, function);
      walk(binary->right, facts, bindings, function);
    } else if (auto *if_expr = dynamic_cast<ast::IfExpr *>(node)) {
      const Facts guarded_facts = without_descent_facts(facts);
      walk(if_expr->condition, guarded_facts, bindings, function);
      walk(if_expr->thenExpr, guarded_facts, bindings, function);
      walk(if_expr->elseExpr, guarded_facts, bindings, function);
    } else if (auto *let = dynamic_cast<ast::LetExpr *>(node)) {
      Facts body_facts = facts;
      LexicalBindings body_bindings = bindings;
      for (auto *alias : let->aliases) {
        auto *lambda = dynamic_cast<ast::LambdaAlias *>(alias);
        if (!lambda || !lambda->name || !lambda->lambda)
          continue;
        const auto found = function_indices_.find(lambda->lambda);
        if (found != function_indices_.end())
          body_bindings[lambda->name->value] = found->second;
      }
      for (auto *alias : let->aliases) {
        if (auto *value = dynamic_cast<ast::ValueAlias *>(alias)) {
          walk(value->expr, facts, body_bindings, function);
          if (value->identifier && value->identifier->name) {
            const auto &name = value->identifier->name->value;
            body_facts[name] = expression_relations(
                value->expr, facts, function.node->patterns.size());
            body_bindings[name] = callable_alias(value->expr, body_bindings);
          }
        } else if (auto *pattern = dynamic_cast<ast::PatternAlias *>(alias)) {
          walk(pattern->expr, facts, body_bindings, function);
          bind_unknown(pattern->pattern, body_facts, body_bindings,
                       function.node->patterns.size());
        }
      }
      walk(let->expr, body_facts, body_bindings, function);
    } else if (dynamic_cast<FunctionExpr *>(node)) {
      // Nested functions are collected and analysed independently. Walking
      // their bodies here would misattribute captured calls to the enclosing
      // function.
    } else if (auto *import = dynamic_cast<ast::ImportExpr *>(node)) {
      LexicalBindings imported = bindings;
      for (auto *clause : import->clauses) {
        if (auto *functions = dynamic_cast<ast::FunctionsImport *>(clause))
          for (auto *alias : functions->aliases)
            if (alias && alias->name)
              imported[alias->alias ? alias->alias->value
                                    : alias->name->value] = std::nullopt;
      }
      walk(import->expr, facts, imported, function);
    } else if (auto *external = dynamic_cast<ast::ExternDeclExpr *>(node)) {
      Facts body_facts = facts;
      LexicalBindings body_bindings = bindings;
      body_facts[external->name] =
          unknown_relations(function.node->patterns.size());
      body_bindings[external->name] = std::nullopt;
      walk(external->body, body_facts, body_bindings, function);
    } else if (auto *do_expr = dynamic_cast<ast::DoExpr *>(node)) {
      for (auto *step : do_expr->steps)
        walk(step, facts, bindings, function);
    } else if (auto *with = dynamic_cast<ast::WithExpr *>(node)) {
      walk(with->contextExpr, facts, bindings, function);
      Facts body_facts = facts;
      LexicalBindings body_bindings = bindings;
      if (with->name) {
        body_facts[with->name->value] =
            unknown_relations(function.node->patterns.size());
        body_bindings[with->name->value] = std::nullopt;
      }
      walk(with->bodyExpr, body_facts, body_bindings, function);
    } else if (auto *handle = dynamic_cast<ast::HandleExpr *>(node)) {
      walk(handle->body, facts, bindings, function);
      for (auto *clause : handle->clauses)
        if (clause) {
          Facts clause_facts = facts;
          LexicalBindings clause_bindings = bindings;
          const auto bind_clause_name = [&](const std::string &name) {
            if (name.empty())
              return;
            clause_facts[name] =
                unknown_relations(function.node->patterns.size());
            clause_bindings[name] = std::nullopt;
          };
          for (const auto &name : clause->arg_names)
            bind_clause_name(name);
          bind_clause_name(clause->resume_name);
          bind_clause_name(clause->return_binding);
          walk(clause->body, clause_facts, clause_bindings, function);
        }
    } else if (auto *try_catch = dynamic_cast<ast::TryCatchExpr *>(node)) {
      walk(try_catch->tryExpr, facts, bindings, function);
      if (try_catch->catchExpr)
        for (auto *item : try_catch->catchExpr->patterns)
          if (item) {
            Facts catch_facts = facts;
            LexicalBindings catch_bindings = bindings;
            bind_unknown(item->matchPattern, catch_facts, catch_bindings,
                         function.node->patterns.size());
            std::visit(
                [&](auto &body) {
                  using Body = std::remove_cvref_t<decltype(body)>;
                  if constexpr (std::is_same_v<Body,
                                               ast::PatternWithoutGuards *>) {
                    if (body)
                      walk(body->expr, catch_facts, catch_bindings, function);
                  } else {
                    for (auto *guarded : body)
                      if (guarded) {
                        walk(guarded->guard, catch_facts, catch_bindings,
                             function);
                        walk(guarded->expr, catch_facts, catch_bindings,
                             function);
                      }
                  }
                },
                item->pattern);
          }
    } else if (auto *raise = dynamic_cast<ast::RaiseExpr *>(node)) {
      walk(raise->value, facts, bindings, function);
    } else if (auto *logical_not =
                   dynamic_cast<ast::LogicalNotOpExpr *>(node)) {
      walk(logical_not->expr, facts, bindings, function);
    } else if (auto *binary_not = dynamic_cast<ast::BinaryNotOpExpr *>(node)) {
      walk(binary_not->expr, facts, bindings, function);
    } else if (auto *tuple = dynamic_cast<ast::TupleExpr *>(node)) {
      for (auto *value : tuple->values)
        walk(value, facts, bindings, function);
    } else if (auto *sequence = dynamic_cast<ast::ValuesSequenceExpr *>(node)) {
      for (auto *value : sequence->values)
        walk(value, facts, bindings, function);
    } else if (auto *range = dynamic_cast<ast::RangeSequenceExpr *>(node)) {
      walk(range->start, facts, bindings, function);
      walk(range->end, facts, bindings, function);
      walk(range->step, facts, bindings, function);
    } else if (auto *set = dynamic_cast<ast::SetExpr *>(node)) {
      for (auto *value : set->values)
        walk(value, facts, bindings, function);
    } else if (auto *dict = dynamic_cast<ast::DictExpr *>(node)) {
      for (const auto &[key, value] : dict->values) {
        walk(key, facts, bindings, function);
        walk(value, facts, bindings, function);
      }
    } else if (auto *record = dynamic_cast<ast::RecordInstanceExpr *>(node)) {
      for (const auto &[_, value] : record->items)
        walk(value, facts, bindings, function);
    } else if (auto *record = dynamic_cast<ast::RecordLiteralExpr *>(node)) {
      for (const auto &[_, value] : record->fields)
        walk(value, facts, bindings, function);
    } else if (auto *update = dynamic_cast<ast::FieldUpdateExpr *>(node)) {
      for (const auto &[_, value] : update->updates)
        walk(value, facts, bindings, function);
    } else if (auto *sequence_generator =
                   dynamic_cast<ast::SeqGeneratorExpr *>(node)) {
      walk_generator(sequence_generator->collectionExtractor,
                     sequence_generator->reducerExpr,
                     sequence_generator->stepExpression, facts, bindings,
                     function);
    } else if (auto *set_generator =
                   dynamic_cast<ast::SetGeneratorExpr *>(node)) {
      walk_generator(set_generator->collectionExtractor,
                     set_generator->reducerExpr, set_generator->stepExpression,
                     facts, bindings, function);
    } else if (auto *dict_generator =
                   dynamic_cast<ast::DictGeneratorExpr *>(node)) {
      walk_generator(dict_generator->collectionExtractor,
                     dict_generator->reducerExpr,
                     dict_generator->stepExpression, facts, bindings, function);
    } else if (auto *reducer =
                   dynamic_cast<ast::DictGeneratorReducer *>(node)) {
      walk(reducer->key, facts, bindings, function);
      walk(reducer->value, facts, bindings, function);
    } else if (auto *extractor =
                   dynamic_cast<ast::ValueCollectionExtractorExpr *>(node)) {
      walk(extractor->collection, facts, bindings, function);
      walk(extractor->condition, facts, bindings, function);
    } else if (auto *extractor =
                   dynamic_cast<ast::KeyValueCollectionExtractorExpr *>(node)) {
      walk(extractor->collection, facts, bindings, function);
      walk(extractor->condition, facts, bindings, function);
    } else if (auto *perform = dynamic_cast<ast::PerformExpr *>(node)) {
      for (auto *argument : perform->args)
        walk(argument, facts, bindings, function);
    } else if (auto *type_instance = dynamic_cast<ast::TypeInstance *>(node)) {
      for (auto *expression : type_instance->exprs)
        walk(expression, facts, bindings, function);
    } else if (auto *pattern = dynamic_cast<ast::PatternWithGuards *>(node)) {
      const Facts guarded_facts = without_descent_facts(facts);
      walk(pattern->guard, guarded_facts, bindings, function);
      walk(pattern->expr, guarded_facts, bindings, function);
    } else if (auto *pattern =
                   dynamic_cast<ast::PatternWithoutGuards *>(node)) {
      walk(pattern->expr, facts, bindings, function);
    } else if (auto *pattern = dynamic_cast<ast::PatternExpr *>(node)) {
      std::visit(
          [&](auto &body) {
            using Body = std::remove_cvref_t<decltype(body)>;
            if constexpr (std::is_same_v<Body, ast::PatternWithoutGuards *>) {
              walk(body, facts, bindings, function);
            } else if constexpr (std::is_same_v<
                                     Body,
                                     std::vector<ast::PatternWithGuards *>>) {
              for (auto *guarded : body)
                walk(guarded, facts, bindings, function);
            }
          },
          pattern->patternExpr);
    } else if (auto *clause = dynamic_cast<ast::CaseClause *>(node)) {
      Facts clause_facts = facts;
      LexicalBindings clause_bindings = bindings;
      bind_unknown(clause->pattern, clause_facts, clause_bindings,
                   function.node->patterns.size());
      walk(clause->guard, clause_facts, clause_bindings, function);
      walk(clause->body, clause_facts, clause_bindings, function);
    } else if (auto *pattern = dynamic_cast<ast::CatchPatternExpr *>(node)) {
      Facts catch_facts = facts;
      LexicalBindings catch_bindings = bindings;
      bind_unknown(pattern->matchPattern, catch_facts, catch_bindings,
                   function.node->patterns.size());
      std::visit(
          [&](auto &body) {
            using Body = std::remove_cvref_t<decltype(body)>;
            if constexpr (std::is_same_v<Body, ast::PatternWithoutGuards *>) {
              walk(body, catch_facts, catch_bindings, function);
            } else {
              for (auto *guarded : body)
                walk(guarded, catch_facts, catch_bindings, function);
            }
          },
          pattern->pattern);
    } else if (auto *catch_expr = dynamic_cast<ast::CatchExpr *>(node)) {
      for (auto *pattern : catch_expr->patterns)
        walk(pattern, facts, bindings, function);
    } else if (auto *value = dynamic_cast<ast::ValueAlias *>(node)) {
      walk(value->expr, facts, bindings, function);
    } else if (auto *pattern = dynamic_cast<ast::PatternAlias *>(node)) {
      walk(pattern->expr, facts, bindings, function);
    } else if (auto *body = dynamic_cast<ast::BodyWithGuards *>(node)) {
      const Facts guarded_facts = without_descent_facts(facts);
      walk(body->guard, guarded_facts, bindings, function);
      walk(body->expr, guarded_facts, bindings, function);
    } else if (auto *body = dynamic_cast<ast::BodyWithoutGuards *>(node)) {
      walk(body->expr, facts, bindings, function);
    } else if (auto *call = dynamic_cast<ast::ExprCall *>(node)) {
      walk(call->expr, facts, bindings, function);
    } else if (auto *call = dynamic_cast<ast::ModuleCall *>(node)) {
      if (auto *expression = std::get_if<ast::ExprNode *>(&call->fqn))
        walk(*expression, facts, bindings, function);
    }
  }

  static Relation compose(Relation earlier, Relation later) {
    if (earlier == Relation::Unknown || later == Relation::Unknown)
      return Relation::Unknown;
    if (earlier == Relation::Strict || later == Relation::Strict)
      return Relation::Strict;
    return Relation::Weak;
  }

  static Relations compose(const Relations &prefix, const Relations &edge) {
    Relations result(std::max(prefix.size(), edge.size()), Relation::Unknown);
    for (size_t i = 0; i < result.size(); ++i) {
      const Relation left = i < prefix.size() ? prefix[i] : Relation::Unknown;
      const Relation right = i < edge.size() ? edge[i] : Relation::Unknown;
      result[i] = compose(left, right);
    }
    return result;
  }

  static bool proves_lexicographic_descent(const Relations &relations) {
    for (Relation relation : relations) {
      if (relation == Relation::Strict)
        return true;
      if (relation != Relation::Weak)
        return false;
    }
    return false;
  }

  std::string component_name(const std::vector<size_t> &component) const {
    std::vector<std::string> names;
    names.reserve(component.size());
    for (size_t index : component)
      names.push_back(functions_[index].name);
    std::sort(names.begin(), names.end());
    std::ostringstream stream;
    for (size_t i = 0; i < names.size(); ++i) {
      if (i)
        stream << ", ";
      stream << names[i];
    }
    return stream.str();
  }

  void emit_failure(const Edge &edge, const std::string &component,
                    const std::string &reason,
                    std::unordered_set<const Edge *> &emitted) {
    if (!emitted.insert(&edge).second)
      return;
    result_.failures.push_back({edge.location, edge.caller->name,
                                edge.callee->name, component, reason});
  }

  void prove_component(const std::vector<size_t> &component) {
    std::unordered_set<size_t> members(component.begin(), component.end());
    bool recursive = component.size() > 1;
    if (!recursive) {
      const size_t only = component.front();
      recursive = std::any_of(
          functions_[only].outgoing.begin(), functions_[only].outgoing.end(),
          [&](const Edge &edge) { return edge.callee->index == only; });
    }
    if (!recursive)
      return;

    const std::string component_text = component_name(component);
    const size_t arity = functions_[component.front()].node->patterns.size();
    const bool compatible =
        std::all_of(component.begin(), component.end(), [&](size_t index) {
          return functions_[index].node->patterns.size() == arity;
        });
    std::unordered_set<const Edge *> emitted;
    if (!compatible) {
      for (size_t index : component)
        for (const auto &edge : functions_[index].outgoing)
          if (members.contains(edge.callee->index))
            emit_failure(edge, component_text,
                         "recursive component members have incompatible arity",
                         emitted);
      return;
    }

    const std::string reason =
        component.size() > 1
            ? "mutual recursion has no provable lexicographic structural "
              "descent"
            : "recursive call has no provable lexicographic structural descent";

    for (size_t start : component) {
      Relations identity(arity, Relation::Weak);
      std::unordered_set<size_t> visited{start};
      std::function<void(size_t, const Relations &)> enumerate =
          [&](size_t current, const Relations &prefix) {
            for (const auto &edge : functions_[current].outgoing) {
              if (!members.contains(edge.callee->index))
                continue;
              Relations path = compose(prefix, edge.relations);
              const size_t next = edge.callee->index;
              if (next == start) {
                if (!proves_lexicographic_descent(path))
                  emit_failure(edge, component_text, reason, emitted);
              } else if (!visited.contains(next)) {
                visited.insert(next);
                enumerate(next, path);
                visited.erase(next);
              }
            }
          };
      enumerate(start, identity);
    }
  }

  void prove_components() {
    const size_t count = functions_.size();
    std::vector<int> index(count, -1);
    std::vector<int> lowlink(count, -1);
    std::vector<size_t> stack;
    std::vector<bool> on_stack(count, false);
    int next_index = 0;

    std::function<void(size_t)> strong_connect = [&](size_t vertex) {
      index[vertex] = lowlink[vertex] = next_index++;
      stack.push_back(vertex);
      on_stack[vertex] = true;

      for (const auto &edge : functions_[vertex].outgoing) {
        const size_t target = edge.callee->index;
        if (index[target] < 0) {
          strong_connect(target);
          lowlink[vertex] = std::min(lowlink[vertex], lowlink[target]);
        } else if (on_stack[target]) {
          lowlink[vertex] = std::min(lowlink[vertex], index[target]);
        }
      }

      if (lowlink[vertex] != index[vertex])
        return;
      std::vector<size_t> component;
      while (true) {
        const size_t member = stack.back();
        stack.pop_back();
        on_stack[member] = false;
        component.push_back(member);
        if (member == vertex)
          break;
      }
      prove_component(component);
    };

    for (size_t vertex = 0; vertex < count; ++vertex)
      if (index[vertex] < 0)
        strong_connect(vertex);
  }
};

} // namespace

Result analyze(ast::AstNode &root) { return Analyzer{}.run(root); }

} // namespace yona::compiler::termination_analysis
