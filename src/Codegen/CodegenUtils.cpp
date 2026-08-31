//
// Codegen — Utility functions
//
// Type inference helpers: yona_type_to_ctype, uncurry_type_signature,
// infer_param_types, infer_type_from_pattern.
//

#include "yona/Codegen/Codegen.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

#include <algorithm>
#include <iostream>

namespace yona::compiler::codegen {
using llvm::BasicBlock;
using llvm::Constant;
using llvm::ConstantInt;
using llvm::Function;
using llvm::Instruction;
using llvm::PointerType;
using llvm::Value;
using LType = llvm::Type;

Codegen::AdtInfo::FieldShape Codegen::field_shape_from_semantic_identity(
    const SemanticTypeIdentity &identity) {
  AdtInfo::FieldShape shape;
  shape.type = identity.type;
  if (identity.type == CType::TUPLE) {
    for (const auto &element : identity.arguments)
      shape.tuple_elements.push_back(
          field_shape_from_semantic_identity(element));
  } else if (identity.type == CType::FUNCTION && !identity.arguments.empty()) {
    const auto &result = identity.arguments.back();
    shape.function_return_type = result.type;
    shape.function_return_adt_name = result.adt_name;
    shape.function_return_identity = result;
  }
  return shape;
}

namespace {
bool identifier_used_as_callee(AstNode *node, const std::string &name) {
  if (!node)
    return false;

  if (node->get_type() == ast::AST_APPLY_EXPR) {
    auto *ae = static_cast<ApplyExpr *>(node);
    if (auto *nc = dynamic_cast<NameCall *>(ae->call)) {
      if (nc->name->value == name)
        return true;
    } else if (auto *ec = dynamic_cast<ExprCall *>(ae->call)) {
      if (auto *id = dynamic_cast<IdentifierExpr *>(ec->expr)) {
        if (id->name->value == name)
          return true;
      }
      if (identifier_used_as_callee(ec->expr, name))
        return true;
    }
    for (auto &arg : ae->args) {
      AstNode *arg_node =
          std::holds_alternative<ExprNode *>(arg)
              ? static_cast<AstNode *>(std::get<ExprNode *>(arg))
              : static_cast<AstNode *>(std::get<ValueExpr *>(arg));
      if (identifier_used_as_callee(arg_node, name))
        return true;
    }
  } else if (auto *ce = dynamic_cast<CaseExpr *>(node)) {
    if (identifier_used_as_callee(ce->expr, name))
      return true;
    for (auto *clause : ce->clauses)
      if (identifier_used_as_callee(clause->body, name))
        return true;
  } else if (auto *le = dynamic_cast<LetExpr *>(node)) {
    if (identifier_used_as_callee(le->expr, name))
      return true;
    for (auto *alias : le->aliases) {
      if (auto *va = dynamic_cast<ValueAlias *>(alias)) {
        if (identifier_used_as_callee(va->expr, name))
          return true;
      } else if (auto *la = dynamic_cast<LambdaAlias *>(alias)) {
        if (identifier_used_as_callee(la->lambda, name))
          return true;
      } else if (auto *pa = dynamic_cast<PatternAlias *>(alias)) {
        if (identifier_used_as_callee(pa->expr, name))
          return true;
      } else if (identifier_used_as_callee(alias, name)) {
        return true;
      }
    }
  } else if (auto *ie = dynamic_cast<IfExpr *>(node)) {
    return identifier_used_as_callee(ie->condition, name) ||
           identifier_used_as_callee(ie->thenExpr, name) ||
           identifier_used_as_callee(ie->elseExpr, name);
  } else if (auto *de = dynamic_cast<DoExpr *>(node)) {
    for (auto *step : de->steps)
      if (identifier_used_as_callee(step, name))
        return true;
  } else if (auto *bop = dynamic_cast<BinaryOpExpr *>(node)) {
    return identifier_used_as_callee(bop->left, name) ||
           identifier_used_as_callee(bop->right, name);
  } else if (auto *te = dynamic_cast<TupleExpr *>(node)) {
    for (auto *value : te->values)
      if (identifier_used_as_callee(value, name))
        return true;
  } else if (auto *ve = dynamic_cast<ValuesSequenceExpr *>(node)) {
    for (auto *value : ve->values)
      if (identifier_used_as_callee(value, name))
        return true;
  } else if (auto *fn_expr = dynamic_cast<FunctionExpr *>(node)) {
    for (auto *body : fn_expr->bodies) {
      if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body)) {
        if (identifier_used_as_callee(bwg->expr, name))
          return true;
      }
    }
  } else if (auto *sg = dynamic_cast<SeqGeneratorExpr *>(node)) {
    if (identifier_used_as_callee(sg->reducerExpr, name))
      return true;
    if (auto *ext = dynamic_cast<ValueCollectionExtractorExpr *>(
            sg->collectionExtractor)) {
      if (identifier_used_as_callee(ext->collection, name))
        return true;
      if (identifier_used_as_callee(ext->condition, name))
        return true;
    }
  } else if (auto *setg = dynamic_cast<SetGeneratorExpr *>(node)) {
    if (identifier_used_as_callee(setg->reducerExpr, name))
      return true;
    if (auto *ext = dynamic_cast<ValueCollectionExtractorExpr *>(
            setg->collectionExtractor)) {
      if (identifier_used_as_callee(ext->collection, name))
        return true;
      if (identifier_used_as_callee(ext->condition, name))
        return true;
    }
  } else if (auto *dictg = dynamic_cast<DictGeneratorExpr *>(node)) {
    if (dictg->reducerExpr) {
      if (identifier_used_as_callee(dictg->reducerExpr->key, name))
        return true;
      if (identifier_used_as_callee(dictg->reducerExpr->value, name))
        return true;
    }
    if (auto *ext = dynamic_cast<ValueCollectionExtractorExpr *>(
            dictg->collectionExtractor)) {
      if (identifier_used_as_callee(ext->collection, name))
        return true;
      if (identifier_used_as_callee(ext->condition, name))
        return true;
    }
  }

  return false;
}

std::vector<CType> infer_seq_head_subtypes_from_pattern(PatternNode *pat,
                                                        ExprNode *body) {
  if (!pat)
    return {};

  if (pat->get_type() == ast::AST_HEAD_TAILS_PATTERN) {
    auto *htp = static_cast<HeadTailsPattern *>(pat);
    for (auto *head : htp->heads) {
      if (head->get_type() != ast::AST_PATTERN_VALUE)
        continue;
      auto *pv = static_cast<PatternValue *>(head);
      if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr)) {
        if (identifier_used_as_callee(body, (*id)->name->value)) {
          return {CType::FUNCTION};
        }
      }
    }
  }
  return {};
}
} // namespace

// Infer a CType from a single pattern node.
// Tuple patterns → TUPLE, sequence/head-tail patterns → SEQ,
// symbol patterns → SYMBOL, etc. Variable/wildcard defaults to INT.
CType Codegen::infer_type_from_pattern(PatternNode *pat) {
  if (!pat)
    return CType::INT;

  switch (pat->get_type()) {
  case ast::AST_TUPLE_PATTERN:
    return CType::TUPLE;

  case ast::AST_SEQ_PATTERN:
  case ast::AST_HEAD_TAILS_PATTERN:
  case ast::AST_TAILS_HEAD_PATTERN:
  case ast::AST_HEAD_TAILS_HEAD_PATTERN:
    return CType::SEQ;

  case ast::AST_PATTERN_VALUE: {
    auto *pv = static_cast<PatternValue *>(pat);
    // Check what kind of value it is
    if (std::holds_alternative<SymbolExpr *>(pv->expr))
      return CType::SYMBOL;
    if (std::holds_alternative<IdentifierExpr *>(pv->expr))
      return CType::INT; // variable — unknown, default to INT
    if (std::holds_alternative<ast::LiteralExpr<void *> *>(pv->expr)) {
      auto *lit = std::get<ast::LiteralExpr<void *> *>(pv->expr);
      // Check literal type from AST node type
      if (dynamic_cast<IntegerExpr *>(lit))
        return CType::INT;
      if (dynamic_cast<FloatExpr *>(lit))
        return CType::FLOAT;
      if (dynamic_cast<StringExpr *>(lit))
        return CType::STRING;
      if (dynamic_cast<TrueLiteralExpr *>(lit) ||
          dynamic_cast<FalseLiteralExpr *>(lit))
        return CType::BOOL;
    }
    return CType::INT;
  }

  case ast::AST_RECORD_PATTERN:
    return CType::ADT; // named field pattern

  case ast::AST_UNDERSCORE_PATTERN:
  case ast::AST_UNDERSCORE_NODE:
    return CType::INT; // wildcard — unknown

  case ast::AST_AS_DATA_STRUCTURE_PATTERN: {
    auto *as = static_cast<AsDataStructurePattern *>(pat);
    return infer_type_from_pattern(as->pattern);
  }

  case ast::AST_OR_PATTERN: {
    auto *op = static_cast<OrPattern *>(pat);
    if (!op->patterns.empty())
      return infer_type_from_pattern(op->patterns[0].get());
    return CType::INT;
  }

  case ast::AST_CONSTRUCTOR_PATTERN:
    return CType::ADT;

  default:
    return CType::INT;
  }
}

// Infer parameter types for a function by analyzing its patterns
// and body. Pattern-based inference handles tuple/seq/symbol patterns.
// Body-based inference handles variable parameters used as case scrutinees.
std::vector<Codegen::InferredParamType>
Codegen::infer_param_types(FunctionExpr *func) {
  std::vector<InferredParamType> result;
  result.resize(func->patterns.size());

  // First: infer from patterns directly
  std::unordered_map<std::string, size_t> param_index;
  for (size_t i = 0; i < func->patterns.size(); i++) {
    auto *pat = func->patterns[i];
    result[i].type = infer_type_from_pattern(pat);
    result[i].source_pattern = pat;

    if (pat->get_type() == ast::AST_PATTERN_VALUE) {
      auto *pv = static_cast<PatternValue *>(pat);
      if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr))
        param_index[(*id)->name->value] = i;
    }
  }

  // Second: analyze body for case expressions that reveal parameter types
  if (!func->bodies.empty()) {
    ExprNode *body_expr = nullptr;
    if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(func->bodies[0]))
      body_expr = bwg->expr;

    std::function<void(AstNode *)> analyze = [&](AstNode *node) {
      if (!node)
        return;

      // Case expressions: scrutinee type inference
      if (node->get_type() == ast::AST_CASE_EXPR) {
        auto *ce = static_cast<CaseExpr *>(node);
        analyze(ce->expr);
        if (ce->expr && ce->expr->get_type() == ast::AST_IDENTIFIER_EXPR) {
          auto *id = static_cast<IdentifierExpr *>(ce->expr);
          auto it = param_index.find(id->name->value);
          if (it != param_index.end() &&
              result[it->second].type == CType::INT) {
            for (auto *clause : ce->clauses) {
              CType pat_type = infer_type_from_pattern(clause->pattern);
              if (pat_type != CType::INT) {
                if (result[it->second].type == CType::INT) {
                  result[it->second].type = pat_type;
                  result[it->second].source_pattern = clause->pattern;
                }
                if (pat_type == CType::SEQ) {
                  auto subtypes = infer_seq_head_subtypes_from_pattern(
                      clause->pattern, clause->body);
                  if (!subtypes.empty())
                    result[it->second].subtypes = std::move(subtypes);
                }
                if (result[it->second].type != CType::SEQ ||
                    !result[it->second].subtypes.empty())
                  break;
              }
            }
          }
        }
        for (auto *clause : ce->clauses)
          analyze(clause->body);
      }

      // Apply expressions: if a parameter is in function position, it's
      // FUNCTION
      if (node->get_type() == ast::AST_APPLY_EXPR) {
        auto *ae = static_cast<ApplyExpr *>(node);
        // Check NameCall: fn x → fn is in function position
        if (auto *nc = dynamic_cast<NameCall *>(ae->call)) {
          auto it = param_index.find(nc->name->value);
          if (it != param_index.end() &&
              result[it->second].type == CType::INT) {
            result[it->second].type = CType::FUNCTION;
            result[it->second].source_pattern = nullptr;
          }
        }
        // Check ExprCall where expr is an identifier (curried application)
        if (auto *ec = dynamic_cast<ExprCall *>(ae->call)) {
          if (auto *id = dynamic_cast<IdentifierExpr *>(ec->expr)) {
            auto it = param_index.find(id->name->value);
            if (it != param_index.end() &&
                result[it->second].type == CType::INT) {
              result[it->second].type = CType::FUNCTION;
              result[it->second].source_pattern = nullptr;
            }
          }
          // Recurse into the call expression (nested apply chains)
          analyze(ec->expr);
        }
        // Recurse into arguments
        for (auto &a : ae->args) {
          if (std::holds_alternative<ExprNode *>(a))
            analyze(std::get<ExprNode *>(a));
          else
            analyze(std::get<ValueExpr *>(a));
        }
      }

      // Recurse into all expression types that may contain sub-expressions
      if (auto *le = dynamic_cast<LetExpr *>(node)) {
        analyze(le->expr);
        // Recurse into each alias's RHS so a parameter used in a let
        // binding ("let r = acq x, …") is still seen in function
        // position and gets inferred as CType::FUNCTION.
        for (auto *alias : le->aliases) {
          if (auto *va = dynamic_cast<ValueAlias *>(alias))
            analyze(va->expr);
          else if (auto *la = dynamic_cast<LambdaAlias *>(alias))
            analyze(la->lambda);
          else if (auto *pa = dynamic_cast<PatternAlias *>(alias))
            analyze(pa->expr);
          else
            analyze(alias);
        }
      } else if (auto *ie = dynamic_cast<IfExpr *>(node)) {
        analyze(ie->condition);
        analyze(ie->thenExpr);
        analyze(ie->elseExpr);
      } else if (auto *de = dynamic_cast<DoExpr *>(node)) {
        for (auto *s : de->steps)
          analyze(s);
      } else if (auto *bop = dynamic_cast<BinaryOpExpr *>(node)) {
        analyze(bop->left);
        analyze(bop->right);
      } else if (auto *te = dynamic_cast<TupleExpr *>(node)) {
        for (auto *v : te->values)
          analyze(v);
      } else if (auto *ve = dynamic_cast<ValuesSequenceExpr *>(node)) {
        for (auto *v : ve->values)
          analyze(v);
      } else if (auto *fn_expr = dynamic_cast<FunctionExpr *>(node)) {
        for (auto *body : fn_expr->bodies)
          if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body))
            analyze(bwg->expr);
      } else if (auto *fa = dynamic_cast<FieldAccessExpr *>(node)) {
        // Field access: if the object is a parameter, it must be an ADT
        if (auto *id = dynamic_cast<IdentifierExpr *>(fa->identifier)) {
          auto it = param_index.find(id->name->value);
          if (it != param_index.end()) {
            auto &inferred = result[it->second];
            if (inferred.type == CType::INT) {
              inferred.type = CType::ADT;
              inferred.source_pattern = nullptr;
            }
            if (inferred.type == CType::ADT) {
              auto &fields = inferred.accessed_fields;
              if (std::find(fields.begin(), fields.end(), fa->name->value) ==
                  fields.end())
                fields.push_back(fa->name->value);
            }
          }
        }
      } else if (auto *sg = dynamic_cast<SeqGeneratorExpr *>(node)) {
        analyze(sg->reducerExpr);
        if (auto *ext = dynamic_cast<ValueCollectionExtractorExpr *>(
                sg->collectionExtractor)) {
          analyze(ext->collection);
          analyze(ext->condition);
        }
      } else if (auto *setg = dynamic_cast<SetGeneratorExpr *>(node)) {
        analyze(setg->reducerExpr);
        if (auto *ext = dynamic_cast<ValueCollectionExtractorExpr *>(
                setg->collectionExtractor)) {
          analyze(ext->collection);
          analyze(ext->condition);
        }
      } else if (auto *dictg = dynamic_cast<DictGeneratorExpr *>(node)) {
        if (dictg->reducerExpr) {
          analyze(dictg->reducerExpr->key);
          analyze(dictg->reducerExpr->value);
        }
        if (auto *ext = dynamic_cast<ValueCollectionExtractorExpr *>(
                dictg->collectionExtractor)) {
          analyze(ext->collection);
          analyze(ext->condition);
        }
      }
    };
    if (body_expr)
      analyze(body_expr);
  }

  return result;
}

// ===== Reference Counting Helpers =====

bool Codegen::is_heap_type(CType ct) {
  return ct == CType::SEQ || ct == CType::SET || ct == CType::DICT ||
         ct == CType::ADT || ct == CType::FUNCTION || ct == CType::STRING ||
         ct == CType::BYTE_ARRAY || ct == CType::TUPLE || ct == CType::SUM ||
         ct == CType::RECORD || ct == CType::INT_ARRAY ||
         ct == CType::FLOAT_ARRAY || ct == CType::CHANNEL;
}

bool Codegen::is_unboxed_enum_adt(const TypedValue &value) const {
  if (value.type != CType::ADT || value.adt_type_name.empty())
    return false;
  bool saw_ctor = false;
  for (const auto &[_, info] : types_.adt_constructors) {
    if (info.type_name != value.adt_type_name)
      continue;
    saw_ctor = true;
    if (info.is_recursive || info.max_arity != 0)
      return false;
  }
  return saw_ctor;
}

bool Codegen::is_heap_value(const TypedValue &value) const {
  return is_heap_type(value.type) && !is_unboxed_enum_adt(value);
}

bool Codegen::block_has_terminator(const BasicBlock *block) const {
  // LLVM 23's BasicBlock::getTerminator() now asserts unless the last
  // instruction is already a terminator.  A non-empty block can still be a
  // fall-through block while a case arm or frame cleanup is being emitted,
  // so establish that precondition without calling getTerminator first.
  return block && !block->empty() && block->back().isTerminator();
}

Instruction *Codegen::block_terminator(BasicBlock *block) const {
  return block_has_terminator(block) ? block->getTerminator() : nullptr;
}

bool Codegen::current_block_terminated() const {
  return builder_ && block_has_terminator(builder_->GetInsertBlock());
}

void Codegen::emit_rc_inc(Value *val, CType type) {
  if (!val || !is_heap_type(type))
    return;
  if (isa<Constant>(val))
    return;
  // Non-recursive ADTs are struct-typed (no RC header) — skip
  if (val->getType()->isStructTy())
    return;
  Value *ptr_val = val;
  if (val->getType()->isIntegerTy())
    ptr_val = builder_->CreateIntToPtr(val, PointerType::get(*context_, 0));
  builder_->CreateCall(rt_.rc_inc_, {ptr_val});
}

void Codegen::emit_rc_dec(Value *val, CType type) {
  if (!val || !is_heap_type(type))
    return;
  if (isa<Constant>(val))
    return;
  if (val->getType()->isStructTy())
    return;
  Value *ptr_val = val;
  if (val->getType()->isIntegerTy())
    ptr_val = builder_->CreateIntToPtr(val, PointerType::get(*context_, 0));
  builder_->CreateCall(rt_.rc_dec_, {ptr_val});
}

// ===== Return type inference =====
// Infers the LLVM type and CType of an expression without compiling it.
// Used to determine function return types before creating the LLVM function.

std::pair<llvm::Type *, CType> Codegen::infer_return_type(AstNode *node) {
  using ast::AstNode;
  using ast::ExprNode;
  using ast::FunctionExpr;
  using ast::IdentifierExpr;
  using ast::PatternNode;
  auto i64_ty = LType::getInt64Ty(*context_);

  if (!node)
    return {i64_ty, CType::INT};
  auto ct = node->get_type();

  if (ct == ast::AST_VALUES_SEQUENCE_EXPR || ct == ast::AST_CONS_LEFT_EXPR ||
      ct == ast::AST_CONS_RIGHT_EXPR || ct == ast::AST_JOIN_EXPR ||
      ct == ast::AST_REMOVE_EXPR)
    return {llvm_type(CType::SEQ), CType::SEQ};
  if (ct == ast::AST_IN_EXPR)
    return {llvm_type(CType::BOOL), CType::BOOL};
  if (ct == ast::AST_SET_EXPR)
    return {llvm_type(CType::SET), CType::SET};
  if (ct == ast::AST_DICT_EXPR)
    return {llvm_type(CType::DICT), CType::DICT};
  if (ct == ast::AST_SYMBOL_EXPR)
    return {i64_ty, CType::SYMBOL};
  if (ct == ast::AST_STRING_EXPR)
    return {llvm_type(CType::STRING), CType::STRING};
  if (ct == ast::AST_INTEGER_EXPR)
    return {i64_ty, CType::INT};
  if (ct == ast::AST_BYTE_EXPR || ct == ast::AST_CHARACTER_EXPR)
    return {i64_ty, CType::INT};
  if (ct == ast::AST_FLOAT_EXPR)
    return {llvm_type(CType::FLOAT), CType::FLOAT};
  if (ct == ast::AST_TRUE_LITERAL_EXPR || ct == ast::AST_FALSE_LITERAL_EXPR)
    return {llvm_type(CType::BOOL), CType::BOOL};
  if (ct == ast::AST_FUNCTION_EXPR)
    return {PointerType::get(*context_, 0), CType::FUNCTION};
  if (ct == ast::AST_TUPLE_EXPR) {
    return {LType::getInt64Ty(*context_), CType::TUPLE};
  }
  if (ct == ast::AST_APPLY_EXPR) {
    auto *app = static_cast<ApplyExpr *>(node);
    ApplyExpr *a = app;
    std::string root_name;
    while (a) {
      if (auto *nc = dynamic_cast<NameCall *>(a->call)) {
        root_name = nc->name->value;
        break;
      } else if (auto *ec = dynamic_cast<ExprCall *>(a->call)) {
        if (auto *inner = dynamic_cast<ApplyExpr *>(ec->expr))
          a = inner;
        else
          break;
      } else
        break;
    }
    if (!root_name.empty()) {
      auto adt_it = types_.adt_constructors.find(root_name);
      if (adt_it != types_.adt_constructors.end())
        return {PointerType::get(*context_, 0), CType::ADT};
      if (auto compiled = compiled_functions_.find(root_name);
          compiled != compiled_functions_.end()) {
        const auto &function = compiled->second;
        auto *return_type = function.fn ? function.fn->getReturnType()
                                        : llvm_type(function.return_type);
        return {return_type, function.return_type};
      }
      if (auto external = imports_.extern_functions.find(root_name);
          external != imports_.extern_functions.end()) {
        if (auto metadata = imports_.meta.find(external->second);
            metadata != imports_.meta.end()) {
          const auto &function = metadata->second;
          auto *return_type = function.return_type == CType::ADT &&
                                      !function.return_adt_name.empty()
                                  ? adt_llvm_type(function.return_adt_name)
                                  : llvm_type(function.return_type);
          return {return_type, function.return_type};
        }
      }
    }
  }
  if (ct == ast::AST_CASE_EXPR) {
    auto *ce = static_cast<CaseExpr *>(node);
    if (!ce->clauses.empty())
      return infer_return_type(ce->clauses[0]->body);
  }
  if (ct == ast::AST_LET_EXPR)
    return infer_return_type(static_cast<LetExpr *>(node)->expr);
  if (ct == ast::AST_IF_EXPR)
    return infer_return_type(static_cast<IfExpr *>(node)->thenExpr);
  if (ct == ast::AST_DO_EXPR) {
    auto *de = static_cast<DoExpr *>(node);
    if (!de->steps.empty())
      return infer_return_type(de->steps.back());
  }
  return {i64_ty, CType::INT};
}

// ===== Arena allocation helpers =====

llvm::Value *Codegen::emit_arena_alloc(int64_t type_tag,
                                       llvm::Value *payload_bytes) {
  if (!current_arena_)
    return nullptr;
  auto i64_ty = LType::getInt64Ty(*context_);
  return builder_->CreateCall(
      rt_.arena_alloc_,
      {current_arena_, ConstantInt::get(i64_ty, type_tag), payload_bytes},
      "arena_obj");
}

} // namespace yona::compiler::codegen
