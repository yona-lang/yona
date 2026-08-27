#include "typechecker/ModuleFunctionDependencies.h"

#include "ast.h"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace yona::compiler::typechecker {
namespace {

using ShadowedNames = std::unordered_set<std::string>;

ast::AstNode *argument_node(const std::variant<ast::ExprNode *, ast::ValueExpr *> &argument) {
  return std::visit([](auto *node) -> ast::AstNode * { return node; }, argument);
}

void shadow_pattern(ast::PatternNode *pattern, ShadowedNames &shadowed) {
  if (!pattern)
    return;
  if (auto *value = dynamic_cast<ast::PatternValue *>(pattern)) {
    if (auto *identifier = std::get_if<ast::IdentifierExpr *>(&value->expr); identifier && *identifier && (*identifier)->name)
      shadowed.insert((*identifier)->name->value);
  } else if (auto *constructor = dynamic_cast<ast::ConstructorPattern *>(pattern)) {
    for (auto *child : constructor->sub_patterns)
      shadow_pattern(child, shadowed);
  } else if (auto *alias = dynamic_cast<ast::AsDataStructurePattern *>(pattern)) {
    if (alias->identifier && alias->identifier->name)
      shadowed.insert(alias->identifier->name->value);
    shadow_pattern(alias->pattern, shadowed);
  } else if (auto *sequence = dynamic_cast<ast::SeqPattern *>(pattern)) {
    for (auto *child : sequence->patterns)
      shadow_pattern(child, shadowed);
  } else if (auto *head_tail = dynamic_cast<ast::HeadTailsPattern *>(pattern)) {
    for (auto *child : head_tail->heads)
      shadow_pattern(child, shadowed);
    shadow_pattern(head_tail->tail, shadowed);
  } else if (auto *tail_head = dynamic_cast<ast::TailsHeadPattern *>(pattern)) {
    shadow_pattern(tail_head->tail, shadowed);
    for (auto *child : tail_head->heads)
      shadow_pattern(child, shadowed);
  } else if (auto *split = dynamic_cast<ast::HeadTailsHeadPattern *>(pattern)) {
    for (auto *child : split->left)
      shadow_pattern(child, shadowed);
    shadow_pattern(split->tail, shadowed);
    for (auto *child : split->right)
      shadow_pattern(child, shadowed);
  } else if (auto *tuple = dynamic_cast<ast::TuplePattern *>(pattern)) {
    for (auto *child : tuple->patterns)
      shadow_pattern(child, shadowed);
  } else if (auto *record = dynamic_cast<ast::RecordPattern *>(pattern)) {
    for (auto &[_, child] : record->items)
      shadow_pattern(child, shadowed);
  } else if (auto *dict = dynamic_cast<ast::DictPattern *>(pattern)) {
    for (auto &[_, child] : dict->keyValuePairs)
      shadow_pattern(child, shadowed);
  } else if (auto *typed = dynamic_cast<ast::TypedPattern *>(pattern)) {
    shadowed.insert(typed->binding_name);
  } else if (auto *alternatives = dynamic_cast<ast::OrPattern *>(pattern)) {
    for (const auto &child : alternatives->patterns)
      shadow_pattern(child.get(), shadowed);
  }
}

void shadow_generator_binding(const ast::IdentifierOrUnderscore &binding, ShadowedNames &shadowed) {
  if (const auto *identifier = std::get_if<ast::IdentifierExpr *>(&binding); identifier && *identifier && (*identifier)->name)
    shadowed.insert((*identifier)->name->value);
}

class DependencyCollector {
public:
  DependencyCollector(ast::ModuleDecl *module, ModuleExportResolver resolve_exports) : module_(module), resolve_exports_(std::move(resolve_exports)) {
    for (size_t index = 0; index < module_->functions.size(); ++index) {
      auto *function = module_->functions[index];
      if (function && !function->name.empty())
        indices_.emplace(function->name, index);
    }
    edges_.resize(module_->functions.size());
    self_edges_.resize(module_->functions.size());
  }

  std::vector<ModuleFunctionComponent> run() {
    for (size_t index = 0; index < module_->functions.size(); ++index) {
      auto *function = module_->functions[index];
      if (!function)
        continue;
      ShadowedNames shadowed;
      for (auto *pattern : function->patterns)
        shadow_pattern(pattern, shadowed);
      for (auto *body : function->bodies) {
        if (auto *plain = dynamic_cast<ast::BodyWithoutGuards *>(body))
          walk(plain->expr, shadowed, index);
        else if (auto *guarded = dynamic_cast<ast::BodyWithGuards *>(body)) {
          walk(guarded->guard, shadowed, index);
          walk(guarded->expr, shadowed, index);
        }
      }
    }
    return strongly_connected_components();
  }

private:
  ast::ModuleDecl *module_;
  ModuleExportResolver resolve_exports_;
  std::unordered_map<std::string, size_t> indices_;
  std::vector<std::unordered_set<size_t>> edges_;
  std::vector<bool> self_edges_;

  void record(const std::string &name, const ShadowedNames &shadowed, size_t caller) {
    if (shadowed.contains(name))
      return;
    const auto found = indices_.find(name);
    if (found == indices_.end())
      return;
    edges_[caller].insert(found->second);
    if (found->second == caller)
      self_edges_[caller] = true;
  }

  void walk_generator(ast::CollectionExtractorExpr *extractor, ast::AstNode *reducer, ast::AstNode *step, const ShadowedNames &shadowed,
                      size_t caller) {
    auto body_shadowed = shadowed;
    if (auto *value = dynamic_cast<ast::ValueCollectionExtractorExpr *>(extractor)) {
      walk(value->collection, shadowed, caller);
      shadow_generator_binding(value->expr, body_shadowed);
      walk(value->condition, body_shadowed, caller);
    } else if (auto *pair = dynamic_cast<ast::KeyValueCollectionExtractorExpr *>(extractor)) {
      walk(pair->collection, shadowed, caller);
      shadow_generator_binding(pair->keyExpr, body_shadowed);
      shadow_generator_binding(pair->valueExpr, body_shadowed);
      walk(pair->condition, body_shadowed, caller);
    }
    walk(reducer, body_shadowed, caller);
    walk(step, body_shadowed, caller);
  }

  void walk(ast::AstNode *node, const ShadowedNames &shadowed, size_t caller) {
    if (!node)
      return;
    if (auto *identifier = dynamic_cast<ast::IdentifierExpr *>(node)) {
      if (identifier->name)
        record(identifier->name->value, shadowed, caller);
    } else if (auto *function = dynamic_cast<ast::FunctionExpr *>(node)) {
      auto body_shadowed = shadowed;
      if (!function->name.empty())
        body_shadowed.insert(function->name);
      for (auto *pattern : function->patterns)
        shadow_pattern(pattern, body_shadowed);
      for (auto *body : function->bodies) {
        if (auto *plain = dynamic_cast<ast::BodyWithoutGuards *>(body))
          walk(plain->expr, body_shadowed, caller);
        else if (auto *guarded = dynamic_cast<ast::BodyWithGuards *>(body)) {
          walk(guarded->guard, body_shadowed, caller);
          walk(guarded->expr, body_shadowed, caller);
        }
      }
    } else if (auto *apply = dynamic_cast<ast::ApplyExpr *>(node)) {
      if (auto *named = dynamic_cast<ast::NameCall *>(apply->call)) {
        if (named->name)
          record(named->name->value, shadowed, caller);
      } else if (auto *expression = dynamic_cast<ast::ExprCall *>(apply->call)) {
        walk(expression->expr, shadowed, caller);
      } else if (auto *module_call = dynamic_cast<ast::ModuleCall *>(apply->call)) {
        if (auto *expression = std::get_if<ast::ExprNode *>(&module_call->fqn))
          walk(*expression, shadowed, caller);
      }
      for (const auto &argument : apply->args)
        walk(argument_node(argument), shadowed, caller);
      if (apply->named_args)
        for (const auto &[_, argument] : *apply->named_args)
          walk(argument_node(argument), shadowed, caller);
    } else if (auto *let = dynamic_cast<ast::LetExpr *>(node)) {
      auto body_shadowed = shadowed;
      for (auto *alias : let->aliases)
        if (auto *lambda = dynamic_cast<ast::LambdaAlias *>(alias); lambda && lambda->name)
          body_shadowed.insert(lambda->name->value);
      for (auto *alias : let->aliases) {
        if (auto *value = dynamic_cast<ast::ValueAlias *>(alias)) {
          walk(value->expr, body_shadowed, caller);
          if (value->identifier && value->identifier->name)
            body_shadowed.insert(value->identifier->name->value);
        } else if (auto *pattern = dynamic_cast<ast::PatternAlias *>(alias)) {
          walk(pattern->expr, body_shadowed, caller);
          shadow_pattern(pattern->pattern, body_shadowed);
        } else if (auto *lambda = dynamic_cast<ast::LambdaAlias *>(alias)) {
          walk(lambda->lambda, body_shadowed, caller);
        }
      }
      walk(let->expr, body_shadowed, caller);
    } else if (auto *case_expr = dynamic_cast<ast::CaseExpr *>(node)) {
      walk(case_expr->expr, shadowed, caller);
      for (auto *clause : case_expr->clauses) {
        if (!clause)
          continue;
        auto arm_shadowed = shadowed;
        shadow_pattern(clause->pattern, arm_shadowed);
        walk(clause->guard, arm_shadowed, caller);
        walk(clause->body, arm_shadowed, caller);
      }
    } else if (auto *binary = dynamic_cast<ast::BinaryOpExpr *>(node)) {
      walk(binary->left, shadowed, caller);
      walk(binary->right, shadowed, caller);
    } else if (auto *conditional = dynamic_cast<ast::IfExpr *>(node)) {
      walk(conditional->condition, shadowed, caller);
      walk(conditional->thenExpr, shadowed, caller);
      walk(conditional->elseExpr, shadowed, caller);
    } else if (auto *sequence = dynamic_cast<ast::DoExpr *>(node)) {
      for (auto *step : sequence->steps)
        walk(step, shadowed, caller);
    } else if (auto *import = dynamic_cast<ast::ImportExpr *>(node)) {
      auto body_shadowed = shadowed;
      for (auto *clause : import->clauses) {
        if (auto *functions = dynamic_cast<ast::FunctionsImport *>(clause)) {
          for (auto *alias : functions->aliases)
            if (alias && alias->name)
              body_shadowed.insert(alias->alias ? alias->alias->value : alias->name->value);
        } else if (auto *module = dynamic_cast<ast::ModuleImport *>(clause); module && module->fqn && resolve_exports_) {
          for (const auto &name : resolve_exports_(module->fqn->to_string()))
            body_shadowed.insert(name);
        }
      }
      walk(import->expr, body_shadowed, caller);
    } else if (auto *external = dynamic_cast<ast::ExternDeclExpr *>(node)) {
      auto body_shadowed = shadowed;
      body_shadowed.insert(external->name);
      walk(external->body, body_shadowed, caller);
    } else if (auto *with = dynamic_cast<ast::WithExpr *>(node)) {
      walk(with->contextExpr, shadowed, caller);
      auto body_shadowed = shadowed;
      if (with->name)
        body_shadowed.insert(with->name->value);
      walk(with->bodyExpr, body_shadowed, caller);
    } else if (auto *handle = dynamic_cast<ast::HandleExpr *>(node)) {
      walk(handle->body, shadowed, caller);
      for (auto *clause : handle->clauses) {
        if (!clause)
          continue;
        auto body_shadowed = shadowed;
        for (const auto &name : clause->arg_names)
          if (!name.empty())
            body_shadowed.insert(name);
        if (!clause->resume_name.empty())
          body_shadowed.insert(clause->resume_name);
        if (!clause->return_binding.empty())
          body_shadowed.insert(clause->return_binding);
        walk(clause->body, body_shadowed, caller);
      }
    } else if (auto *try_catch = dynamic_cast<ast::TryCatchExpr *>(node)) {
      walk(try_catch->tryExpr, shadowed, caller);
      if (try_catch->catchExpr)
        for (auto *item : try_catch->catchExpr->patterns) {
          if (!item)
            continue;
          auto catch_shadowed = shadowed;
          shadow_pattern(item->matchPattern, catch_shadowed);
          std::visit(
              [&](auto &body) {
                using Body = std::remove_cvref_t<decltype(body)>;
                if constexpr (std::is_same_v<Body, ast::PatternWithoutGuards *>) {
                  if (body)
                    walk(body->expr, catch_shadowed, caller);
                } else {
                  for (auto *guarded : body)
                    if (guarded) {
                      walk(guarded->guard, catch_shadowed, caller);
                      walk(guarded->expr, catch_shadowed, caller);
                    }
                }
              },
              item->pattern);
        }
    } else if (auto *raise = dynamic_cast<ast::RaiseExpr *>(node)) {
      walk(raise->value, shadowed, caller);
    } else if (auto *logical_not = dynamic_cast<ast::LogicalNotOpExpr *>(node)) {
      walk(logical_not->expr, shadowed, caller);
    } else if (auto *binary_not = dynamic_cast<ast::BinaryNotOpExpr *>(node)) {
      walk(binary_not->expr, shadowed, caller);
    } else if (auto *tuple = dynamic_cast<ast::TupleExpr *>(node)) {
      for (auto *value : tuple->values)
        walk(value, shadowed, caller);
    } else if (auto *sequence = dynamic_cast<ast::ValuesSequenceExpr *>(node)) {
      for (auto *value : sequence->values)
        walk(value, shadowed, caller);
    } else if (auto *range = dynamic_cast<ast::RangeSequenceExpr *>(node)) {
      walk(range->start, shadowed, caller);
      walk(range->end, shadowed, caller);
      walk(range->step, shadowed, caller);
    } else if (auto *set = dynamic_cast<ast::SetExpr *>(node)) {
      for (auto *value : set->values)
        walk(value, shadowed, caller);
    } else if (auto *dict = dynamic_cast<ast::DictExpr *>(node)) {
      for (const auto &[key, value] : dict->values) {
        walk(key, shadowed, caller);
        walk(value, shadowed, caller);
      }
    } else if (auto *record = dynamic_cast<ast::RecordInstanceExpr *>(node)) {
      for (const auto &[_, value] : record->items)
        walk(value, shadowed, caller);
    } else if (auto *record = dynamic_cast<ast::RecordLiteralExpr *>(node)) {
      for (const auto &[_, value] : record->fields)
        walk(value, shadowed, caller);
    } else if (auto *update = dynamic_cast<ast::FieldUpdateExpr *>(node)) {
      walk(update->identifier, shadowed, caller);
      for (const auto &[_, value] : update->updates)
        walk(value, shadowed, caller);
    } else if (auto *access = dynamic_cast<ast::FieldAccessExpr *>(node)) {
      walk(access->identifier, shadowed, caller);
    } else if (auto *sequence = dynamic_cast<ast::SeqGeneratorExpr *>(node)) {
      walk_generator(sequence->collectionExtractor, sequence->reducerExpr, sequence->stepExpression, shadowed, caller);
    } else if (auto *set = dynamic_cast<ast::SetGeneratorExpr *>(node)) {
      walk_generator(set->collectionExtractor, set->reducerExpr, set->stepExpression, shadowed, caller);
    } else if (auto *dict = dynamic_cast<ast::DictGeneratorExpr *>(node)) {
      walk_generator(dict->collectionExtractor, dict->reducerExpr, dict->stepExpression, shadowed, caller);
    } else if (auto *reducer = dynamic_cast<ast::DictGeneratorReducer *>(node)) {
      walk(reducer->key, shadowed, caller);
      walk(reducer->value, shadowed, caller);
    } else if (auto *perform = dynamic_cast<ast::PerformExpr *>(node)) {
      for (auto *argument : perform->args)
        walk(argument, shadowed, caller);
    } else if (auto *instance = dynamic_cast<ast::TypeInstance *>(node)) {
      for (auto *expression : instance->exprs)
        walk(expression, shadowed, caller);
    }
  }

  std::vector<ModuleFunctionComponent> strongly_connected_components() {
    const size_t count = module_->functions.size();
    std::vector<int> index(count, -1), lowlink(count, -1);
    std::vector<size_t> stack;
    std::vector<bool> on_stack(count, false);
    std::vector<ModuleFunctionComponent> result;
    int next_index = 0;

    std::function<void(size_t)> visit = [&](size_t node) {
      index[node] = lowlink[node] = next_index++;
      stack.push_back(node);
      on_stack[node] = true;
      for (size_t dependency : edges_[node]) {
        if (!module_->functions[dependency])
          continue;
        if (index[dependency] < 0) {
          visit(dependency);
          lowlink[node] = std::min(lowlink[node], lowlink[dependency]);
        } else if (on_stack[dependency]) {
          lowlink[node] = std::min(lowlink[node], index[dependency]);
        }
      }
      if (lowlink[node] != index[node])
        return;

      ModuleFunctionComponent component;
      while (true) {
        const size_t member = stack.back();
        stack.pop_back();
        on_stack[member] = false;
        component.functions.push_back(module_->functions[member]);
        if (member == node)
          break;
      }
      std::sort(component.functions.begin(), component.functions.end(), [&](const auto *left, const auto *right) {
        return std::find(module_->functions.begin(), module_->functions.end(), left) <
               std::find(module_->functions.begin(), module_->functions.end(), right);
      });
      component.recursive = component.functions.size() > 1 || self_edges_[node];
      result.push_back(std::move(component));
    };

    for (size_t node = 0; node < count; ++node)
      if (module_->functions[node] && index[node] < 0)
        visit(node);
    return result;
  }
};

} // namespace

std::vector<ModuleFunctionComponent> module_function_components(ast::ModuleDecl *module, ModuleExportResolver resolve_exports) {
  if (!module)
    return {};
  return DependencyCollector(module, std::move(resolve_exports)).run();
}

} // namespace yona::compiler::typechecker
