//
// Codegen — Collection code generation
//
// Tuples, sequences, sets, dicts, cons, join.
//

#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/BorrowEscapeAnalysis.h"
#include "yona/Semantics/TypeChecker.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

#include <algorithm>
#include <iostream>
#include <vector>

namespace yona::compiler::codegen {
using llvm::ArrayType;
using llvm::BasicBlock;
using llvm::Constant;
using llvm::ConstantInt;
using llvm::Function;
using llvm::FunctionType;
using llvm::IntegerType;
using llvm::IRBuilder;
using llvm::PointerType;
using llvm::StructType;
using llvm::Value;
using LType = llvm::Type;

static std::string ctype_to_string(CType ct) {
  switch (ct) {
  case CType::INT:
    return "INT";
  case CType::FLOAT:
    return "FLOAT";
  case CType::BOOL:
    return "BOOL";
  case CType::STRING:
    return "STRING";
  case CType::SEQ:
    return "SEQ";
  case CType::TUPLE:
    return "TUPLE";
  case CType::UNIT:
    return "UNIT";
  case CType::FUNCTION:
    return "FUNCTION";
  case CType::SYMBOL:
    return "SYMBOL";
  case CType::PROMISE:
    return "PROMISE";
  case CType::SET:
    return "SET";
  case CType::DICT:
    return "DICT";
  case CType::ADT:
    return "ADT";
  case CType::BYTE_ARRAY:
    return "BYTE_ARRAY";
  case CType::INT_ARRAY:
    return "INT_ARRAY";
  case CType::FLOAT_ARRAY:
    return "FLOAT_ARRAY";
  case CType::CHANNEL:
    return "CHANNEL";
  case CType::SUM:
    return "SUM";
  case CType::RECORD:
    return "RECORD";
  }
  return "INT";
}

// Persistent collection slots use one uniform i64 carrier irrespective of
// the source scalar's LLVM representation. Keep this conversion centralized
// so cons and snoc cannot accidentally pass `double` or `i1` to the runtime.
static Value *collection_carrier(IRBuilder<> &builder, Value *value,
                                 IntegerType *i64_type) {
  if (value->getType() == i64_type)
    return value;
  if (value->getType()->isPointerTy())
    return builder.CreatePtrToInt(value, i64_type);
  if (value->getType()->isDoubleTy())
    return builder.CreateBitCast(value, i64_type);
  if (value->getType()->isIntegerTy())
    return builder.CreateZExtOrTrunc(value, i64_type);
  return value;
}

static SemanticTypeIdentity semantic_identity(const TypedValue &value) {
  SemanticTypeIdentity identity;
  identity.type = value.type;
  identity.adt_name = value.adt_type_name;
  identity.arguments = !value.semantic_subtypes.empty()
                           ? value.semantic_subtypes
                           : value.adt_semantic_arguments;
  return identity;
}

// Empty nested collections carry their outer representation but cannot name
// an element type on their own.  Refine that incomplete identity from any
// later homogeneous element instead of permanently choosing the first value
// (for example the leading `[]` in `[[], [1], [1, 2]]`).
static void refine_semantic_identity(SemanticTypeIdentity &target,
                                     const SemanticTypeIdentity &candidate) {
  if (target.type != candidate.type)
    return;
  if (target.adt_name.empty())
    target.adt_name = candidate.adt_name;
  if (target.arguments.empty()) {
    target.arguments = candidate.arguments;
    return;
  }
  if (target.arguments.size() != candidate.arguments.size())
    return;
  for (size_t i = 0; i < target.arguments.size(); ++i)
    refine_semantic_identity(target.arguments[i], candidate.arguments[i]);
}

TypedValue Codegen::codegen_tuple(TupleExpr *node) {
  set_debug_loc(node->Range);
  auto i64_ty = LType::getInt64Ty(*context_);
  std::vector<Value *> elems;
  std::vector<TypedValue> element_values;
  std::vector<CType> subtypes;
  std::vector<SemanticTypeIdentity> semantic_subtypes;

  for (auto *v : node->values) {
    auto tv = codegen(v);
    if (!tv)
      return {};
    // Convert all elements to i64 for uniform layout
    Value *i64_val = tv.val;
    if (i64_val->getType() == i64_ty) {
      // already i64
    } else if (i64_val->getType()->isPointerTy()) {
      i64_val = builder_->CreatePtrToInt(i64_val, i64_ty);
    } else if (i64_val->getType()->isDoubleTy()) {
      i64_val = builder_->CreateBitCast(i64_val, i64_ty);
    } else if (i64_val->getType()->isIntegerTy()) {
      i64_val = builder_->CreateZExtOrTrunc(i64_val, i64_ty);
    } else if (i64_val->getType()->isStructTy() && tv.type == CType::ADT) {
      // Non-recursive ADT struct {tag, fields...} — box to heap so it
      // fits in an i64 tuple slot. The boxed pointer keeps the same
      // adt_type_name, so pattern matching downstream uses heap layout.
      auto *sty = llvm::cast<llvm::StructType>(i64_val->getType());
      unsigned num_fields = sty->getNumElements();
      auto *tag_val = builder_->CreateExtractValue(i64_val, {0});
      auto *adt_ptr = builder_->CreateCall(
          rt_.adt_alloc_, {tag_val, ConstantInt::get(i64_ty, num_fields - 1)});
      for (unsigned fi = 1; fi < num_fields; fi++) {
        auto *field_val = builder_->CreateExtractValue(i64_val, {fi});
        builder_->CreateCall(
            rt_.adt_set_field_,
            {adt_ptr, ConstantInt::get(i64_ty, fi - 1), field_val});
      }
      // Carry inner field heap mask so the boxed ADT frees its heap
      // fields on destruction.
      int64_t inner_mask = 0;
      for (size_t fi = 0; fi < tv.subtypes.size() && fi < 64; fi++)
        if (is_heap_type(tv.subtypes[fi]))
          inner_mask |= ((int64_t)1 << fi);
      if (inner_mask != 0)
        builder_->CreateCall(rt_.adt_set_heap_mask_,
                             {adt_ptr, ConstantInt::get(i64_ty, inner_mask)});
      i64_val = builder_->CreatePtrToInt(adt_ptr, i64_ty, "tuple_adt_box");
    }
    elems.push_back(i64_val);
    element_values.push_back(tv);
    subtypes.push_back(tv.type);
    SemanticTypeIdentity identity;
    identity.type = tv.type;
    identity.adt_name = tv.adt_type_name;
    identity.arguments = !tv.semantic_subtypes.empty()
                             ? tv.semantic_subtypes
                             : tv.adt_semantic_arguments;
    if (identity.arguments.empty())
      for (const auto subtype : tv.subtypes)
        identity.arguments.push_back({subtype, {}, {}});
    semantic_subtypes.push_back(std::move(identity));
  }

  // Heap-allocate tuple with metadata for recursive destruction
  auto *tuple_ptr = builder_->CreateCall(
      rt_.tuple_alloc_, {ConstantInt::get(i64_ty, elems.size())}, "tuple");
  int64_t tuple_heap_mask = 0;
  for (size_t i = 0; i < elems.size(); i++) {
    // A heap tuple owns every heap-marked child it stores. Borrowed values
    // keep their existing owner, and named owned values remain owned by their
    // lexical binding, so both require an independent tuple reference. Only a
    // truly anonymous owned temporary transfers its existing reference.
    const auto &element = element_values[i];
    const auto named_owner = named_binding_for_value(element.val);
    const bool has_lexical_owner =
        named_owner && !imports_.extern_functions.contains(*named_owner);
    const bool needs_independent_owner =
        element.heap_ownership == HeapOwnership::Borrowed || has_lexical_owner;
    if (needs_independent_owner && is_heap_type(subtypes[i]) && element.val &&
        !element.val->getType()->isStructTy())
      emit_rc_inc(element_values[i].val, element_values[i].type);
    builder_->CreateCall(rt_.tuple_set_,
                         {tuple_ptr, ConstantInt::get(i64_ty, i), elems[i]});
    if (is_heap_type(subtypes[i]) && i < 64)
      tuple_heap_mask |= ((int64_t)1 << i);
  }
  if (tuple_heap_mask != 0)
    builder_->CreateCall(
        rt_.tuple_set_heap_mask_,
        {tuple_ptr, ConstantInt::get(i64_ty, tuple_heap_mask)});
  auto *tuple_i64 = builder_->CreatePtrToInt(tuple_ptr, i64_ty, "tuple_i64");
  TypedValue result{tuple_i64, CType::TUPLE, subtypes};
  result.semantic_subtypes = std::move(semantic_subtypes);
  return result;
}

TypedValue Codegen::codegen_seq(ValuesSequenceExpr *node) {
  set_debug_loc(node->Range);
  size_t n = node->values.size();
  auto i64_ty = LType::getInt64Ty(*context_);
  auto count = ConstantInt::get(i64_ty, n);

  Value *seq;
  if (current_arena_) {
    // Arena allocation: allocate payload and set length manually
    auto payload_bytes = ConstantInt::get(
        i64_ty, (n + 2) * sizeof(int64_t)); // +2 for count+heap_flag
    seq = emit_arena_alloc(1 /* RC_TYPE_SEQ */, payload_bytes);
    // Set length at seq[0]
    builder_->CreateStore(count, seq);
  } else {
    seq = builder_->CreateCall(rt_.seq_alloc_, {count}, "seq");
  }

  CType elem_type = CType::INT;
  SemanticTypeIdentity element_identity;
  for (size_t i = 0; i < n; i++) {
    last_lambda_name_.clear();
    auto tv = codegen(node->values[i]);
    if (tv.type == CType::FUNCTION && !tv.val && !last_lambda_name_.empty()) {
      const auto deferred = deferred_functions_.find(last_lambda_name_);
      if (deferred != deferred_functions_.end()) {
        std::vector<TypedValue> lambda_args;
        lambda_args.reserve(deferred->second.param_names.size());
        for (size_t pi = 0; pi < deferred->second.param_names.size(); ++pi)
          lambda_args.push_back(dummy_typed_value(CType::INT));
        auto compiled =
            compile_function(last_lambda_name_, deferred->second, lambda_args);
        if (compiled.fn) {
          auto *closure = wrap_in_closure(compiled.fn, compiled.return_type);
          tv = {closure, CType::FUNCTION, {compiled.return_type}};
        }
      }
    }
    if (!tv)
      return {};
    if (i == 0) {
      elem_type = tv.type;
      element_identity = semantic_identity(tv);
    } else if (tv.type != elem_type)
      report_error(node->values[i]->Range,
                   "type error: heterogeneous sequence — expected " +
                       ctype_to_string(elem_type) + " but got " +
                       ctype_to_string(tv.type));
    else
      refine_semantic_identity(element_identity, semantic_identity(tv));
    auto idx = ConstantInt::get(i64_ty, i);
    Value *store_val = tv.val;
    if (store_val->getType()->isStructTy()) {
      // Box: heap-allocate the struct, store pointer as i64
      auto *alloca = builder_->CreateAlloca(store_val->getType());
      builder_->CreateStore(store_val, alloca);
      uint64_t sz =
          module_->getDataLayout().getTypeAllocSize(store_val->getType());
      store_val = builder_->CreateCall(rt_.box_,
                                       {alloca, ConstantInt::get(i64_ty, sz)});
      store_val = builder_->CreatePtrToInt(store_val, i64_ty);
    } else if (store_val->getType()->isPointerTy()) {
      store_val = builder_->CreatePtrToInt(store_val, i64_ty);
    } else if (store_val->getType()->isDoubleTy()) {
      store_val = builder_->CreateBitCast(store_val, i64_ty);
    } else if (store_val->getType() != i64_ty) {
      store_val = builder_->CreateZExtOrTrunc(store_val, i64_ty);
    }
    // Sequence storage follows the same ownership rule as tuples: borrowed
    // and lexically named values keep their current owner, so the sequence
    // takes an independent reference. A truly anonymous owned temporary
    // transfers its existing reference directly to the sequence.
    const auto named_owner = named_binding_for_value(tv.val);
    const bool has_lexical_owner =
        named_owner && !imports_.extern_functions.contains(*named_owner);
    const bool needs_independent_owner =
        tv.heap_ownership == HeapOwnership::Borrowed || has_lexical_owner;
    if (needs_independent_owner && is_heap_type(tv.type) && tv.val &&
        !isa<Constant>(tv.val) && !tv.val->getType()->isStructTy())
      emit_rc_inc(tv.val, tv.type);
    builder_->CreateCall(rt_.seq_set_, {seq, idx, store_val});
  }
  // Tell the seq destructor to walk elements when they're heap-typed.
  if (n > 0 && is_heap_type(elem_type))
    builder_->CreateCall(rt_.seq_set_heap_, {seq, ConstantInt::get(i64_ty, 1)});
  TypedValue result{seq, CType::SEQ, {elem_type}};
  if (n > 0)
    result.semantic_subtypes = {std::move(element_identity)};
  return result;
}

TypedValue Codegen::codegen_set(SetExpr *node) {
  set_debug_loc(node->Range);
  size_t n = node->values.size();
  auto i64_ty = LType::getInt64Ty(*context_);

  if (n == 0) {
    // `{}` has no payload from which to infer its collection kind.  The
    // type checker can nevertheless resolve it contextually (notably the
    // `Dict key value` result of Monoid.emptyLike), so lower the inferred
    // kind instead of hard-coding every empty literal as Set.
    auto *zero = ConstantInt::get(i64_ty, 0);
    bool inferred_dict = false;
    if (contextual_expected_node_ == node &&
        contextual_expected_type_ == CType::DICT)
      inferred_dict = true;
    // See CodegenExpr::codegen_comparison: only ASTs from the checked
    // importing module may consult the type checker's pointer-keyed map.
    if (type_checker_ && genfn_isolation_depth_ == 0) {
      auto *inferred = type_checker_->zonk(type_checker_->type_of(node));
      inferred_dict =
          inferred_dict ||
          (inferred && ((inferred->tag == typechecker::MonoType::App &&
                         inferred->type_name == "Dict") ||
                        (inferred->tag == typechecker::MonoType::Con &&
                         inferred->con == typechecker::TyCon::Dict)));
    }
    if (inferred_dict) {
      auto *dict = builder_->CreateCall(rt_.dict_alloc_, {zero}, "dict");
      return {dict, CType::DICT};
    }
    auto *set = builder_->CreateCall(rt_.set_alloc_, {zero}, "set");
    return {set, CType::SET};
  }

  /* Non-empty set: build via persistent insert (HAMT-backed) */
  auto *zero = ConstantInt::get(i64_ty, 0);
  Value *set = builder_->CreateCall(rt_.set_alloc_, {zero}, "set");

  CType elem_type = CType::INT;
  SemanticTypeIdentity element_identity;
  for (size_t i = 0; i < n; i++) {
    auto tv = codegen(node->values[i]);
    if (!tv)
      return {};
    if (i == 0) {
      elem_type = tv.type;
      element_identity = semantic_identity(tv);
    } else if (tv.type == elem_type) {
      refine_semantic_identity(element_identity, semantic_identity(tv));
    }
    Value *val = tv.val;
    if (val->getType()->isPointerTy())
      val = builder_->CreatePtrToInt(val, i64_ty);
    set = builder_->CreateCall(rt_.set_insert_, {set, val}, "set");
  }
  if (n > 0 && is_heap_type(elem_type))
    builder_->CreateCall(rt_.set_set_heap_, {set, ConstantInt::get(i64_ty, 1)});
  TypedValue result{set, CType::SET, {elem_type}};
  result.semantic_subtypes = {std::move(element_identity)};
  return result;
}

TypedValue Codegen::codegen_dict(DictExpr *node) {
  set_debug_loc(node->Range);
  size_t n = node->values.size();
  auto i64_ty = LType::getInt64Ty(*context_);
  auto zero = ConstantInt::get(i64_ty, 0);
  Value *dict = builder_->CreateCall(rt_.dict_alloc_, {zero}, "dict");

  CType key_type = CType::INT, val_type = CType::INT;
  SemanticTypeIdentity key_identity, value_identity;
  for (size_t i = 0; i < n; i++) {
    auto key_tv = codegen(node->values[i].first);
    auto val_tv = codegen(node->values[i].second);
    if (!key_tv || !val_tv)
      return {};
    if (i == 0) {
      key_type = key_tv.type;
      val_type = val_tv.type;
      key_identity.type = key_tv.type;
      key_identity.adt_name = key_tv.adt_type_name;
      key_identity.arguments = !key_tv.semantic_subtypes.empty()
                                   ? key_tv.semantic_subtypes
                                   : key_tv.adt_semantic_arguments;
      value_identity.type = val_tv.type;
      value_identity.adt_name = val_tv.adt_type_name;
      value_identity.arguments = !val_tv.semantic_subtypes.empty()
                                     ? val_tv.semantic_subtypes
                                     : val_tv.adt_semantic_arguments;
    }
    // Persistent put: dict = dict_put(dict, key, val)
    Value *key_val = key_tv.val;
    Value *val_val = val_tv.val;
    if (key_val->getType()->isPointerTy())
      key_val = builder_->CreatePtrToInt(key_val, i64_ty);
    if (val_val->getType()->isPointerTy())
      val_val = builder_->CreatePtrToInt(val_val, i64_ty);
    dict =
        builder_->CreateCall(rt_.dict_put_, {dict, key_val, val_val}, "dict");
  }
  if (n > 0 && (is_heap_type(key_type) || is_heap_type(val_type))) {
    builder_->CreateCall(
        rt_.dict_set_heap_,
        {dict, ConstantInt::get(i64_ty, is_heap_type(key_type) ? 1 : 0),
         ConstantInt::get(i64_ty, is_heap_type(val_type) ? 1 : 0)});
  }
  TypedValue result{dict, CType::DICT, {key_type, val_type}};
  if (n > 0)
    result.semantic_subtypes = {std::move(key_identity),
                                std::move(value_identity)};
  return result;
}

TypedValue Codegen::codegen_cons(ConsLeftExpr *node) {
  set_debug_loc(node->Range);
  auto elem = codegen(node->left);
  auto seq = codegen(node->right);
  if (!elem || !seq)
    return {};
  auto i64_ty = LType::getInt64Ty(*context_);
  Value *seq_ptr = seq.val;
  Value *elem_val = elem.val;
  // Storing a heap-typed element into a seq makes the seq a co-owner.
  // rc_inc the original value before stripping its type. The seq's
  // destructor uses the heap_flag we set below to free elements.
  if (is_heap_type(elem.type) && elem_val && !isa<Constant>(elem_val) &&
      !elem_val->getType()->isStructTy())
    emit_rc_inc(elem_val, elem.type);
  // Ensure correct types for rt_seq_cons(i64, ptr)
  if (elem_val->getType()->isStructTy()) {
    // Box struct elements (e.g. a non-recursive ADT) so they fit in i64.
    auto *alloca = builder_->CreateAlloca(elem_val->getType());
    builder_->CreateStore(elem_val, alloca);
    uint64_t sz =
        module_->getDataLayout().getTypeAllocSize(elem_val->getType());
    auto *boxed =
        builder_->CreateCall(rt_.box_, {alloca, ConstantInt::get(i64_ty, sz)});
    elem_val = builder_->CreatePtrToInt(boxed, i64_ty);
  } else
    elem_val = collection_carrier(*builder_, elem_val, i64_ty);
  if (seq_ptr->getType()->isIntegerTy())
    seq_ptr = builder_->CreateIntToPtr(seq_ptr, PointerType::get(*context_, 0));
  bool seq_is_named = false;
  bool single_use_named = false;
  bool has_arm_protected_ref = false;
  if (seq.type == CType::SEQ && seq.val && !isa<Constant>(seq.val) &&
      !seq.val->getType()->isStructTy()) {
    for (auto &[_, value] : named_values_)
      if (value.val == seq.val) {
        seq_is_named = true;
        break;
      }
    if (seq_is_named && node->right->get_type() == ast::AST_IDENTIFIER_EXPR &&
        current_fn_body_) {
      const auto &name =
          static_cast<IdentifierExpr *>(node->right)->name->value;
      single_use_named = compiler::analysis::max_identifier_refs_on_path(
                             current_fn_body_, name) == 1;
    }
    if (seq_is_named && !single_use_named && !arm_drop_stack_.empty())
      has_arm_protected_ref = std::any_of(
          arm_drop_stack_.back().begin(), arm_drop_stack_.back().end(),
          [&](const auto &entry) {
            return entry.first == seq.val && entry.second == CType::SEQ;
          });
    // A multi-use named sequence keeps its binding's reference. Give the
    // consuming runtime primitive a separate ownership reference unless
    // a borrowed head-tail pattern already created one for this arm.
    if (seq_is_named && !single_use_named && !has_arm_protected_ref)
      emit_rc_inc(seq_ptr, CType::SEQ);
  }
  auto *result =
      builder_->CreateCall(rt_.seq_cons_, {elem_val, seq_ptr}, "cons");
  // `cons` consumes one ownership reference from its right operand and
  // returns one reference to the result.  The runtime may return the same
  // pointer for a unique sequence or path-copy a shared one, so consume the
  // input only on the copy path.  A single-use named operand is transferred
  // out of its enclosing scope; a multi-use binding retains its protected
  // reference and is still released by normal scope cleanup.
  if (seq.type == CType::SEQ && seq.val && !isa<Constant>(seq.val) &&
      !seq.val->getType()->isStructTy()) {
    auto *is_same = builder_->CreateICmpEQ(result, seq_ptr, "cons_inplace");
    auto *dec_bb = BasicBlock::Create(*context_, "cons.dec",
                                      builder_->GetInsertBlock()->getParent());
    auto *cont_bb = BasicBlock::Create(*context_, "cons.cont",
                                       builder_->GetInsertBlock()->getParent());
    builder_->CreateCondBr(is_same, cont_bb, dec_bb);
    builder_->SetInsertPoint(dec_bb);
    emit_rc_dec(seq_ptr, CType::SEQ);
    builder_->CreateBr(cont_bb);
    builder_->SetInsertPoint(cont_bb);

    if (single_use_named) {
      mark_transferred(seq.val, TransferDomain::Seq);
      emit_frame_transfer(seq.val);
    } else if (has_arm_protected_ref) {
      // A borrowed head-tail pattern schedules its protected scrutinee
      // reference for arm-exit cleanup. Consuming that same binding as
      // the tail of `::` transfers the protected reference into the
      // result (or releases it on the copy path above); only the
      // function/local binding's original reference remains for normal
      // scope cleanup. Leaving both drops scheduled double-releases
      // recursive algorithms such as insertion sort.
      auto &drops = arm_drop_stack_.back();
      const auto drop =
          std::find_if(drops.begin(), drops.end(), [&](const auto &entry) {
            return entry.first == seq.val && entry.second == CType::SEQ;
          });
      if (drop != drops.end())
        drops.erase(drop);
    }
  }
  // Mark the new seq as containing heap elements so the destructor walks
  // and frees them. YonaRuntimeSequencePrepend inherits the source seq's
  // heap_flag, but on `x :: []` the right side is empty with heap_flag=0, so we
  // need to set it explicitly when the cons-ed element is heap-typed.
  if (is_heap_type(elem.type))
    builder_->CreateCall(rt_.seq_set_heap_,
                         {result, ConstantInt::get(i64_ty, 1)});
  TypedValue sequence{result, CType::SEQ, {elem.type}};
  SemanticTypeIdentity element_identity;
  element_identity.type = elem.type;
  element_identity.adt_name = elem.adt_type_name;
  element_identity.arguments = !elem.semantic_subtypes.empty()
                                   ? elem.semantic_subtypes
                                   : elem.adt_semantic_arguments;
  sequence.semantic_subtypes = {std::move(element_identity)};
  return sequence;
}

TypedValue Codegen::codegen_join(JoinExpr *node) {
  // The parser makes ++ chains left-associative. Lower the left spine
  // iteratively: recursively lowering a long sequence of joins can exhaust
  // the comparatively small Windows Debug stack.
  if (dynamic_cast<JoinExpr *>(node->left)) {
    std::vector<ExprNode *> operands;
    ExprNode *current = node;
    while (auto *join = dynamic_cast<JoinExpr *>(current)) {
      operands.push_back(join->right);
      current = join->left;
    }
    operands.push_back(current);
    std::reverse(operands.begin(), operands.end());

    auto joined = codegen(operands.front());
    if (!joined)
      return {};
    for (size_t i = 1; i < operands.size(); ++i) {
      set_debug_loc(operands[i]->Range);
      auto right = codegen(operands[i]);
      if (!right)
        return {};
      if (joined.type == CType::STRING || right.type == CType::STRING) {
        auto as_str = [&](const TypedValue &tv) -> Value * {
          if (tv.val->getType()->isPointerTy())
            return tv.val;
          return builder_->CreateIntToPtr(tv.val,
                                          PointerType::get(*context_, 0));
        };
        joined = {builder_->CreateCall(rt_.string_concat_,
                                       {as_str(joined), as_str(right)}),
                  CType::STRING};
      } else {
        auto as_seq = [&](const TypedValue &tv) -> Value * {
          if (tv.val->getType()->isPointerTy())
            return tv.val;
          return builder_->CreateIntToPtr(tv.val,
                                          PointerType::get(*context_, 0));
        };
        auto previous = joined;
        joined = {builder_->CreateCall(rt_.seq_join_,
                                       {as_seq(previous), as_seq(right)}),
                  CType::SEQ};
        if (previous.heap_ownership == HeapOwnership::Owned &&
            !named_binding_for_value(previous.val))
          emit_rc_dec(previous.val, CType::SEQ);
        if (right.heap_ownership == HeapOwnership::Owned &&
            !named_binding_for_value(right.val))
          emit_rc_dec(right.val, CType::SEQ);
      }
    }
    return joined;
  }

  set_debug_loc(node->Range);
  auto left = codegen(node->left);
  auto right = codegen(node->right);
  if (!left || !right)
    return {};

  if (left.type == CType::STRING || right.type == CType::STRING) {
    auto as_str = [&](const TypedValue &tv) -> Value * {
      if (tv.val->getType()->isPointerTy())
        return tv.val;
      return builder_->CreateIntToPtr(tv.val, PointerType::get(*context_, 0));
    };
    return {
        builder_->CreateCall(rt_.string_concat_, {as_str(left), as_str(right)}),
        CType::STRING};
  }
  // Join (++) always produces a sequence. Both operands must be sequences.
  // If an operand is typed as INT (element type not propagated from sequence
  // destructuring), the i64 value is actually a pointer to a sequence —
  // cast to ptr. This is semantically correct: ++ only operates on sequences.
  auto as_seq = [&](const TypedValue &tv) -> Value * {
    if (tv.val->getType()->isPointerTy())
      return tv.val;
    return builder_->CreateIntToPtr(tv.val, PointerType::get(*context_, 0));
  };
  TypedValue result{
      builder_->CreateCall(rt_.seq_join_, {as_seq(left), as_seq(right)}),
      CType::SEQ};
  if (left.heap_ownership == HeapOwnership::Owned &&
      !named_binding_for_value(left.val))
    emit_rc_dec(left.val, CType::SEQ);
  if (right.heap_ownership == HeapOwnership::Owned &&
      !named_binding_for_value(right.val))
    emit_rc_dec(right.val, CType::SEQ);
  return result;
}

TypedValue Codegen::codegen_cons_right(ConsRightExpr *node) {
  set_debug_loc(node->Range);
  auto seq = codegen(node->left);
  auto elem = codegen(node->right);
  if (!elem || !seq)
    return {};
  auto i64_ty = LType::getInt64Ty(*context_);
  Value *seq_ptr = seq.val;
  Value *elem_val = elem.val;
  if (is_heap_type(elem.type) && elem_val && !isa<Constant>(elem_val) &&
      !elem_val->getType()->isStructTy())
    emit_rc_inc(elem_val, elem.type);
  if (elem_val->getType()->isStructTy()) {
    auto *alloca = builder_->CreateAlloca(elem_val->getType());
    builder_->CreateStore(elem_val, alloca);
    uint64_t sz =
        module_->getDataLayout().getTypeAllocSize(elem_val->getType());
    auto *boxed =
        builder_->CreateCall(rt_.box_, {alloca, ConstantInt::get(i64_ty, sz)});
    elem_val = builder_->CreatePtrToInt(boxed, i64_ty);
  } else
    elem_val = collection_carrier(*builder_, elem_val, i64_ty);
  if (seq_ptr->getType()->isIntegerTy())
    seq_ptr = builder_->CreateIntToPtr(seq_ptr, PointerType::get(*context_, 0));
  auto *result =
      builder_->CreateCall(rt_.seq_snoc_, {seq_ptr, elem_val}, "snoc");
  if (seq.type == CType::SEQ && seq.val && !isa<Constant>(seq.val) &&
      !seq.val->getType()->isStructTy()) {
    bool is_named = false;
    for (auto &[k, v] : named_values_)
      if (v.val == seq.val) {
        is_named = true;
        break;
      }
    if (!is_named) {
      auto *is_same = builder_->CreateICmpEQ(result, seq_ptr, "snoc_inplace");
      auto *dec_bb = BasicBlock::Create(
          *context_, "snoc.dec", builder_->GetInsertBlock()->getParent());
      auto *cont_bb = BasicBlock::Create(
          *context_, "snoc.cont", builder_->GetInsertBlock()->getParent());
      builder_->CreateCondBr(is_same, cont_bb, dec_bb);
      builder_->SetInsertPoint(dec_bb);
      emit_rc_dec(seq_ptr, CType::SEQ);
      builder_->CreateBr(cont_bb);
      builder_->SetInsertPoint(cont_bb);
    }
  }
  if (is_heap_type(elem.type))
    builder_->CreateCall(rt_.seq_set_heap_,
                         {result, ConstantInt::get(i64_ty, 1)});
  return {result, CType::SEQ, {elem.type}};
}

TypedValue Codegen::codegen_in(InExpr *node) {
  set_debug_loc(node->Range);
  auto elem = codegen(node->left);
  auto coll = codegen(node->right);
  if (!elem || !coll)
    return {};
  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);
  Value *elem_val = elem.val;
  if (elem_val->getType()->isPointerTy())
    elem_val = builder_->CreatePtrToInt(elem_val, i64_ty);
  else if (elem_val->getType()->isIntegerTy() && elem_val->getType() != i64_ty)
    elem_val = builder_->CreateZExtOrTrunc(elem_val, i64_ty);
  Value *coll_ptr = coll.val;
  if (!coll_ptr->getType()->isPointerTy())
    coll_ptr = builder_->CreateIntToPtr(coll_ptr, ptr_ty);
  Value *found = nullptr;
  if (coll.type == CType::SET)
    found =
        builder_->CreateCall(rt_.set_contains_, {coll_ptr, elem_val}, "in_set");
  else if (coll.type == CType::DICT)
    found = builder_->CreateCall(rt_.dict_contains_, {coll_ptr, elem_val},
                                 "in_dict");
  else
    found =
        builder_->CreateCall(rt_.seq_contains_, {coll_ptr, elem_val}, "in_seq");
  auto *zero = ConstantInt::get(i64_ty, 0);
  return {builder_->CreateICmpNE(found, zero, "in"), CType::BOOL};
}

TypedValue Codegen::codegen_remove(RemoveExpr *node) {
  set_debug_loc(node->Range);
  auto left = codegen(node->left);
  auto right = codegen(node->right);
  if (!left || !right)
    return {};
  auto ptr_ty = PointerType::get(*context_, 0);
  auto as_ptr = [&](const TypedValue &tv) -> Value * {
    if (tv.val->getType()->isPointerTy())
      return tv.val;
    return builder_->CreateIntToPtr(tv.val, ptr_ty);
  };
  if (left.type == CType::SET || right.type == CType::SET) {
    auto *result = builder_->CreateCall(
        rt_.set_difference_, {as_ptr(left), as_ptr(right)}, "set_diff");
    mark_transferred(left.val, TransferDomain::Map);
    emit_frame_transfer(left.val);
    return {result, CType::SET, left.subtypes};
  }
  return {builder_->CreateCall(rt_.seq_difference_,
                               {as_ptr(left), as_ptr(right)}, "seq_diff"),
          CType::SEQ, left.subtypes};
}

// ===== Generator / Comprehension codegen =====
//
// Generators compile to counted loops over the source collection.
// [expr | x = src]       → alloc result[len], loop i=0..len, x=src[i],
// result[i]=expr [expr | x = src, if g] → two-pass: count matches, then fill
// {expr | x = src}       → alloc set[len], loop with set_put
// {k:v | x = src}        → alloc dict[len], loop with dict_set

// Helper: extract the binding variable name from a collection extractor
static std::string extractor_var_name(CollectionExtractorExpr *ext) {
  if (ext->get_type() == ast::AST_VALUE_COLLECTION_EXTRACTOR_EXPR) {
    auto *ve = static_cast<ValueCollectionExtractorExpr *>(ext);
    if (auto *id = std::get_if<IdentifierExpr *>(&ve->expr))
      return (*id)->name->value;
  }
  return "_";
}

TypedValue Codegen::codegen_seq_generator(SeqGeneratorExpr *node) {
  set_debug_loc(node->Range);
  auto *ext =
      static_cast<ValueCollectionExtractorExpr *>(node->collectionExtractor);
  if (!ext || !ext->collection) {
    report_error(node->Range, "sequence generator missing source collection");
    return {};
  }

  // Parallel comprehension: [| expr for var <- source |]
  // Spawns each iteration as a task in a group, collects promises, awaits all.
  if (node->is_parallel) {
    auto src = codegen(ext->collection);
    if (!src)
      return {};
    auto i64_ty = LType::getInt64Ty(*context_);
    auto ptr_ty = PointerType::get(*context_, 0);
    Value *src_ptr = src.val;
    if (!src_ptr->getType()->isPointerTy())
      src_ptr = builder_->CreateIntToPtr(src_ptr, ptr_ty);

    auto *src_len =
        builder_->CreateCall(rt_.seq_length_, {src_ptr}, "par_src_len");
    auto *result =
        builder_->CreateCall(rt_.seq_alloc_, {src_len}, "par_result");
    auto *group = builder_->CreateCall(rt_.group_begin_, {}, "par_group");
    auto saved_group = current_group_;
    current_group_ = group;

    std::string var_name = extractor_var_name(node->collectionExtractor);
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *loop_bb = BasicBlock::Create(*context_, "par.loop", func);
    auto *body_bb = BasicBlock::Create(*context_, "par.body", func);
    auto *done_bb = BasicBlock::Create(*context_, "par.done", func);
    auto *zero = ConstantInt::get(i64_ty, 0);

    builder_->CreateBr(loop_bb);
    builder_->SetInsertPoint(loop_bb);
    auto *idx_phi = builder_->CreatePHI(i64_ty, 2, "par_idx");
    idx_phi->addIncoming(
        zero, builder_->GetInsertBlock()->getSinglePredecessor()
                  ? builder_->GetInsertBlock()->getSinglePredecessor()
                  : loop_bb);
    auto *cmp = builder_->CreateICmpSLT(idx_phi, src_len, "par_cmp");
    builder_->CreateCondBr(cmp, body_bb, done_bb);

    builder_->SetInsertPoint(body_bb);
    auto *elem =
        builder_->CreateCall(rt_.seq_get_, {src_ptr, idx_phi}, "par_elem");
    auto saved_nv = named_values_;
    ActiveNamedValueSnapshot saved_nv_snapshot(*this, saved_nv);
    CType elem_type = (!src.subtypes.empty()) ? src.subtypes[0] : CType::INT;
    named_values_[var_name] = {elem, elem_type};

    auto body_val = codegen(node->reducerExpr);
    Value *body_i64 = body_val.val;
    if (body_i64->getType()->isPointerTy())
      body_i64 = builder_->CreatePtrToInt(body_i64, i64_ty);
    else if (body_i64->getType()->isDoubleTy())
      body_i64 = builder_->CreateBitCast(body_i64, i64_ty);
    else if (body_i64->getType()->isIntegerTy() &&
             body_i64->getType() != i64_ty)
      body_i64 = builder_->CreateZExtOrTrunc(body_i64, i64_ty);

    builder_->CreateCall(rt_.seq_set_, {result, idx_phi, body_i64});
    named_values_ = saved_nv;

    auto *next_idx =
        builder_->CreateAdd(idx_phi, ConstantInt::get(i64_ty, 1), "par_next");
    idx_phi->addIncoming(next_idx, builder_->GetInsertBlock());
    builder_->CreateBr(loop_bb);

    builder_->SetInsertPoint(done_bb);
    builder_->CreateCall(rt_.group_await_all_, {group});
    builder_->CreateCall(rt_.group_end_, {group});
    current_group_ = saved_group;

    return {builder_->CreateBitCast(result, ptr_ty), CType::SEQ,
            body_val ? std::vector<CType>{body_val.type}
                     : std::vector<CType>{}};
  }

  // Stream fusion: if source is a deferred single-use generator, fuse.
  if (ext->collection->get_type() == ast::AST_IDENTIFIER_EXPR) {
    auto *src_id = static_cast<IdentifierExpr *>(ext->collection);
    auto it = deferred_generators_.find(src_id->name->value);
    if (it != deferred_generators_.end()) {
      auto *inner = it->second;
      deferred_generators_.erase(it);
      return codegen_fused_seq_generator(node, inner);
    }
  }

  auto src = codegen(ext->collection);
  if (!src)
    return {};

  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);

  // Iterator source: call next() in a loop until None.
  // An Iterator is an ADT wrapping a closure. Detect by CType::ADT
  // (seq sources have CType::SEQ, so there's no ambiguity).
  if (src.type == CType::ADT) {
    {
      std::string var_name = extractor_var_name(node->collectionExtractor);
      auto *func = builder_->GetInsertBlock()->getParent();

      // Extract the next function from the Iterator ADT
      // Iterator layout: {tag, closure_ptr} — field 0 is the () -> Option a
      // closure
      Value *iter_val = src.val;
      if (iter_val->getType()->isIntegerTy())
        iter_val = builder_->CreateIntToPtr(iter_val, ptr_ty);

      // The Iterator constructor wraps a closure. Extract it.
      // For non-recursive ADTs: extractvalue {tag, closure}
      Value *next_fn;
      if (iter_val->getType()->isPointerTy()) {
        // Heap-allocated ADT: [tag, num_fields, heap_mask, closure_ptr]
        auto *gep =
            builder_->CreateGEP(i64_ty, iter_val, {ConstantInt::get(i64_ty, 3)},
                                "iter_next_gep"); // ADT_HDR_SIZE=3
        next_fn = builder_->CreateLoad(i64_ty, gep, "iter_next_fn");
        next_fn = builder_->CreateIntToPtr(next_fn, ptr_ty);
      } else {
        next_fn = builder_->CreateExtractValue(iter_val, {1});
        if (next_fn->getType()->isIntegerTy())
          next_fn = builder_->CreateIntToPtr(next_fn, ptr_ty);
      }

      // Build result seq incrementally using seq_snoc (append).
      // Starts empty, grows dynamically — no 32-element limit.
      auto *empty_seq = builder_->CreateCall(
          rt_.seq_alloc_, {ConstantInt::get(i64_ty, 0)}, "iter_empty");

      auto *loop_bb = BasicBlock::Create(*context_, "iter.loop", func);
      auto *body_bb = BasicBlock::Create(*context_, "iter.body", func);
      auto *done_bb = BasicBlock::Create(*context_, "iter.done", func);

      builder_->CreateBr(loop_bb);

      builder_->SetInsertPoint(loop_bb);
      // PHI for the growing seq (starts empty, grows via snoc)
      auto *seq_phi = builder_->CreatePHI(ptr_ty, 2, "iter_seq");
      seq_phi->addIncoming(empty_seq, loop_bb->getSinglePredecessor()
                                          ? loop_bb->getSinglePredecessor()
                                          : loop_bb);

      // Call next_fn() via closure indirect call
      auto *closure_fn_gep = builder_->CreateGEP(
          i64_ty, next_fn, {ConstantInt::get(i64_ty, 0)}, "closure_fn_gep");
      auto *closure_fn_raw =
          builder_->CreateLoad(i64_ty, closure_fn_gep, "closure_fn_raw");
      auto *closure_fn = builder_->CreateIntToPtr(closure_fn_raw, ptr_ty);
      auto *option_val =
          builder_->CreateCall(llvm::FunctionType::get(i64_ty, {ptr_ty}, false),
                               closure_fn, {next_fn}, "iter_next_result");

      // Check Some (tag 0) or None (tag 1)
      auto *opt_ptr = builder_->CreateIntToPtr(option_val, ptr_ty);
      auto *tag_gep = builder_->CreateGEP(
          i64_ty, opt_ptr, {ConstantInt::get(i64_ty, 0)}, "opt_tag_gep");
      auto *tag = builder_->CreateLoad(i64_ty, tag_gep, "opt_tag");
      auto *is_some =
          builder_->CreateICmpEQ(tag, ConstantInt::get(i64_ty, 0), "is_some");
      builder_->CreateCondBr(is_some, body_bb, done_bb);

      builder_->SetInsertPoint(body_bb);
      auto *val_gep =
          builder_->CreateGEP(i64_ty, opt_ptr, {ConstantInt::get(i64_ty, 3)},
                              "opt_val_gep"); // ADT_HDR_SIZE=3
      auto *elem = builder_->CreateLoad(i64_ty, val_gep, "iter_elem");

      // Bind variable and evaluate reducer
      auto saved_nv = named_values_;
      ActiveNamedValueSnapshot saved_nv_snapshot(*this, saved_nv);
      named_values_[var_name] = {elem, CType::INT};
      auto body_val = codegen(node->reducerExpr);
      Value *body_i64 = body_val.val;
      if (body_i64->getType()->isPointerTy())
        body_i64 = builder_->CreatePtrToInt(body_i64, i64_ty);
      else if (body_i64->getType()->isDoubleTy())
        body_i64 = builder_->CreateBitCast(body_i64, i64_ty);
      else if (body_i64->getType()->isIntegerTy() &&
               body_i64->getType() != i64_ty)
        body_i64 = builder_->CreateZExtOrTrunc(body_i64, i64_ty);

      // Append element to result seq via snoc (O(1) amortized)
      auto *new_seq =
          builder_->CreateCall(rt_.seq_snoc_, {seq_phi, body_i64}, "iter_snoc");
      named_values_ = saved_nv;

      seq_phi->addIncoming(new_seq, builder_->GetInsertBlock());
      builder_->CreateBr(loop_bb);

      builder_->SetInsertPoint(done_bb);
      // Result is the final seq from the PHI
      auto *result_phi = builder_->CreatePHI(ptr_ty, 2, "iter_result");
      for (auto it = llvm::pred_begin(done_bb); it != llvm::pred_end(done_bb);
           ++it)
        result_phi->addIncoming(seq_phi, *it);

      return {result_phi, CType::SEQ,
              body_val ? std::vector<CType>{body_val.type}
                       : std::vector<CType>{}};
    }
  }

  // Ensure source is a pointer (seq)
  Value *src_ptr = src.val;
  if (!src_ptr->getType()->isPointerTy())
    src_ptr = builder_->CreateIntToPtr(src_ptr, ptr_ty);

  auto *src_len = builder_->CreateCall(rt_.seq_length_, {src_ptr}, "src_len");

  bool has_guard = ext->condition != nullptr;
  std::string var_name = extractor_var_name(node->collectionExtractor);

  auto *func = builder_->GetInsertBlock()->getParent();

  if (!has_guard) {
    // Simple case: no guard, result has same length as source.
    // Use head/tail iteration instead of indexed get for O(1) per
    // element (indexed get is O(n/32) for chunked seqs).
    auto *result =
        builder_->CreateCall(rt_.seq_alloc_, {src_len}, "gen_result");
    auto ptr_ty = PointerType::get(*context_, 0);

    auto *loop_bb = BasicBlock::Create(*context_, "gen.loop", func);
    auto *body_bb = BasicBlock::Create(*context_, "gen.body", func);
    auto *done_bb = BasicBlock::Create(*context_, "gen.done", func);

    auto *zero = ConstantInt::get(i64_ty, 0);
    builder_->CreateBr(loop_bb);

    // Loop header: phi for index (i) and current seq cursor
    builder_->SetInsertPoint(loop_bb);
    auto *i_phi = builder_->CreatePHI(i64_ty, 2, "i");
    i_phi->addIncoming(zero, loop_bb->getSinglePredecessor());
    auto *cur_phi = builder_->CreatePHI(ptr_ty, 2, "cur");
    cur_phi->addIncoming(src_ptr, loop_bb->getSinglePredecessor());

    auto *is_empty =
        builder_->CreateCall(rt_.seq_is_empty_, {cur_phi}, "gen.empty");
    auto *cond = builder_->CreateICmpEQ(is_empty, zero, "gen.cond");
    builder_->CreateCondBr(cond, body_bb, done_bb);

    // Body: x = head(cur); cur = tail(cur); result[i] = reducer(x)
    builder_->SetInsertPoint(body_bb);
    auto *elem = builder_->CreateCall(rt_.seq_head_, {cur_phi}, "elem");
    auto *next_cur = builder_->CreateCall(rt_.seq_tail_, {cur_phi}, "cur.next");

    auto saved = named_values_;
    ActiveNamedValueSnapshot saved_snapshot(*this, saved);
    named_values_[var_name] = {elem, CType::INT};

    auto body_val = codegen(node->reducerExpr);

    Value *store_val = body_val.val;
    if (store_val->getType()->isPointerTy())
      store_val = builder_->CreatePtrToInt(store_val, i64_ty);
    else if (store_val->getType()->isDoubleTy())
      store_val = builder_->CreateBitCast(store_val, i64_ty);

    builder_->CreateCall(rt_.seq_set_, {result, i_phi, store_val});

    auto *i_next =
        builder_->CreateAdd(i_phi, ConstantInt::get(i64_ty, 1), "i.next");
    i_phi->addIncoming(i_next, builder_->GetInsertBlock());
    cur_phi->addIncoming(next_cur, builder_->GetInsertBlock());
    builder_->CreateBr(loop_bb);

    named_values_ = saved;

    builder_->SetInsertPoint(done_bb);
    return {result, CType::SEQ};
  } else {
    // Guard case: single-pass indexed with over-allocation.
    // Indexed get is O(1) for flat seqs, O(n/32) for chunked — much
    // better than head/tail which is O(n) per call on flat seqs due
    // to memmove. Over-allocate at source length, fill matching
    // elements, then adjust count.
    auto *zero = ConstantInt::get(i64_ty, 0);
    auto *one = ConstantInt::get(i64_ty, 1);

    auto *result =
        builder_->CreateCall(rt_.seq_alloc_, {src_len}, "gen_result");

    auto *loop_bb = BasicBlock::Create(*context_, "gen.loop", func);
    auto *body_bb = BasicBlock::Create(*context_, "gen.body", func);
    auto *guard_bb = BasicBlock::Create(*context_, "gen.guard", func);
    auto *next_bb = BasicBlock::Create(*context_, "gen.next", func);
    auto *done_bb = BasicBlock::Create(*context_, "gen.done", func);

    builder_->CreateBr(loop_bb);

    builder_->SetInsertPoint(loop_bb);
    auto *i_phi = builder_->CreatePHI(i64_ty, 2, "i");
    auto *wi_phi = builder_->CreatePHI(i64_ty, 2, "wi");
    i_phi->addIncoming(zero, loop_bb->getSinglePredecessor());
    wi_phi->addIncoming(zero, loop_bb->getSinglePredecessor());

    auto *cond = builder_->CreateICmpSLT(i_phi, src_len);
    builder_->CreateCondBr(cond, body_bb, done_bb);

    builder_->SetInsertPoint(body_bb);
    auto *elem = builder_->CreateCall(rt_.seq_get_, {src_ptr, i_phi}, "elem");

    auto saved = named_values_;
    ActiveNamedValueSnapshot saved_snapshot(*this, saved);
    named_values_[var_name] = {elem, CType::INT};
    auto guard_val = codegen(ext->condition);
    Value *guard_bool = guard_val.val;
    if (guard_bool->getType() == i64_ty)
      guard_bool = builder_->CreateICmpNE(guard_bool, zero);
    else if (guard_bool->getType() != LType::getInt1Ty(*context_))
      guard_bool = builder_->CreateICmpNE(
          builder_->CreateZExtOrTrunc(guard_bool, i64_ty), zero);
    builder_->CreateCondBr(guard_bool, guard_bb, next_bb);

    builder_->SetInsertPoint(guard_bb);
    named_values_[var_name] = {elem, CType::INT};
    auto body_val = codegen(node->reducerExpr);
    Value *store_val = body_val.val;
    if (store_val->getType()->isPointerTy())
      store_val = builder_->CreatePtrToInt(store_val, i64_ty);
    else if (store_val->getType()->isDoubleTy())
      store_val = builder_->CreateBitCast(store_val, i64_ty);
    builder_->CreateCall(rt_.seq_set_, {result, wi_phi, store_val});
    auto *wi_inc = builder_->CreateAdd(wi_phi, one, "wi.inc");
    builder_->CreateBr(next_bb);

    builder_->SetInsertPoint(next_bb);
    auto *wi_merged = builder_->CreatePHI(i64_ty, 2, "wi.m");
    wi_merged->addIncoming(wi_phi, body_bb);
    wi_merged->addIncoming(wi_inc, guard_bb);
    auto *i_next = builder_->CreateAdd(i_phi, one);
    i_phi->addIncoming(i_next, next_bb);
    wi_phi->addIncoming(wi_merged, next_bb);
    builder_->CreateBr(loop_bb);

    named_values_ = saved;

    // Adjust count to actual number of matches (seq[0] = count)
    builder_->SetInsertPoint(done_bb);
    builder_->CreateStore(wi_phi, result);
    return {result, CType::SEQ};
  }
}

TypedValue Codegen::codegen_set_generator(SetGeneratorExpr *node) {
  set_debug_loc(node->Range);
  auto *ext =
      static_cast<ValueCollectionExtractorExpr *>(node->collectionExtractor);
  if (!ext || !ext->collection) {
    report_error(node->Range, "set generator missing source collection");
    return {};
  }

  auto src = codegen(ext->collection);
  if (!src)
    return {};

  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);

  Value *src_ptr = src.val;
  if (!src_ptr->getType()->isPointerTy())
    src_ptr = builder_->CreateIntToPtr(src_ptr, ptr_ty);

  auto *src_len = builder_->CreateCall(rt_.seq_length_, {src_ptr}, "src_len");

  std::string var_name = extractor_var_name(node->collectionExtractor);
  auto *func = builder_->GetInsertBlock()->getParent();
  auto *zero = ConstantInt::get(i64_ty, 0);
  auto *one = ConstantInt::get(i64_ty, 1);

  /* Build set via persistent insert (HAMT-backed). */
  auto *empty_set = builder_->CreateCall(rt_.set_alloc_, {zero}, "gen_set");

  auto *loop_bb = BasicBlock::Create(*context_, "setgen.loop", func);
  auto *body_bb = BasicBlock::Create(*context_, "setgen.body", func);
  auto *done_bb = BasicBlock::Create(*context_, "setgen.done", func);

  builder_->CreateBr(loop_bb);

  builder_->SetInsertPoint(loop_bb);
  auto *i_phi = builder_->CreatePHI(i64_ty, 2, "i");
  auto *set_phi = builder_->CreatePHI(ptr_ty, 2, "set");
  i_phi->addIncoming(zero, loop_bb->getSinglePredecessor());
  set_phi->addIncoming(empty_set, loop_bb->getSinglePredecessor());
  auto *cond = builder_->CreateICmpSLT(i_phi, src_len);
  builder_->CreateCondBr(cond, body_bb, done_bb);

  builder_->SetInsertPoint(body_bb);
  auto *elem = builder_->CreateCall(rt_.seq_get_, {src_ptr, i_phi}, "elem");

  auto saved = named_values_;
  ActiveNamedValueSnapshot saved_snapshot(*this, saved);
  named_values_[var_name] = {elem, CType::INT};

  auto body_val = codegen(node->reducerExpr);
  Value *store_val = body_val.val;
  if (store_val->getType()->isPointerTy())
    store_val = builder_->CreatePtrToInt(store_val, i64_ty);

  auto *new_set =
      builder_->CreateCall(rt_.set_insert_, {set_phi, store_val}, "set.ins");

  auto *i_next = builder_->CreateAdd(i_phi, one);
  i_phi->addIncoming(i_next, builder_->GetInsertBlock());
  set_phi->addIncoming(new_set, builder_->GetInsertBlock());
  builder_->CreateBr(loop_bb);

  named_values_ = saved;

  builder_->SetInsertPoint(done_bb);
  if (is_heap_type(body_val.type))
    builder_->CreateCall(rt_.set_set_heap_,
                         {set_phi, ConstantInt::get(i64_ty, 1)});
  return {set_phi, CType::SET};
}

TypedValue Codegen::codegen_dict_generator(DictGeneratorExpr *node) {
  set_debug_loc(node->Range);
  auto *ext =
      static_cast<ValueCollectionExtractorExpr *>(node->collectionExtractor);
  if (!ext || !ext->collection) {
    report_error(node->Range, "dict generator missing source collection");
    return {};
  }

  auto src = codegen(ext->collection);
  if (!src)
    return {};

  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);

  Value *src_ptr = src.val;
  if (!src_ptr->getType()->isPointerTy())
    src_ptr = builder_->CreateIntToPtr(src_ptr, ptr_ty);

  auto *src_len = builder_->CreateCall(rt_.seq_length_, {src_ptr}, "src_len");

  std::string var_name = extractor_var_name(node->collectionExtractor);
  auto *func = builder_->GetInsertBlock()->getParent();
  auto *zero = ConstantInt::get(i64_ty, 0);

  // Dict generator uses HAMT dict_put (persistent insert).
  // Indexed iteration avoids O(n²) memmove cost of head/tail on flat seqs.
  auto *dict = builder_->CreateCall(rt_.dict_alloc_, {zero}, "gen_dict");
  auto *one = ConstantInt::get(i64_ty, 1);

  auto *loop_bb = BasicBlock::Create(*context_, "dictgen.loop", func);
  auto *body_bb = BasicBlock::Create(*context_, "dictgen.body", func);
  auto *done_bb = BasicBlock::Create(*context_, "dictgen.done", func);

  builder_->CreateBr(loop_bb);

  builder_->SetInsertPoint(loop_bb);
  auto *i_phi = builder_->CreatePHI(i64_ty, 2, "i");
  auto *dict_phi = builder_->CreatePHI(ptr_ty, 2, "dict");
  i_phi->addIncoming(zero, loop_bb->getSinglePredecessor());
  dict_phi->addIncoming(dict, loop_bb->getSinglePredecessor());

  auto *cond = builder_->CreateICmpSLT(i_phi, src_len);
  builder_->CreateCondBr(cond, body_bb, done_bb);

  builder_->SetInsertPoint(body_bb);
  auto *elem = builder_->CreateCall(rt_.seq_get_, {src_ptr, i_phi}, "elem");

  auto saved = named_values_;
  ActiveNamedValueSnapshot saved_snapshot(*this, saved);
  named_values_[var_name] = {elem, CType::INT};

  auto key_val = codegen(node->reducerExpr->key);
  auto val_val = codegen(node->reducerExpr->value);

  Value *key_i64 = key_val.val;
  if (key_i64->getType()->isPointerTy())
    key_i64 = builder_->CreatePtrToInt(key_i64, i64_ty);
  Value *val_i64 = val_val.val;
  if (val_i64->getType()->isPointerTy())
    val_i64 = builder_->CreatePtrToInt(val_i64, i64_ty);

  auto *new_dict = builder_->CreateCall(
      rt_.dict_put_, {dict_phi, key_i64, val_i64}, "dict.put");

  auto *i_next = builder_->CreateAdd(i_phi, one);
  i_phi->addIncoming(i_next, builder_->GetInsertBlock());
  dict_phi->addIncoming(new_dict, builder_->GetInsertBlock());
  builder_->CreateBr(loop_bb);

  named_values_ = saved;

  builder_->SetInsertPoint(done_bb);
  if (is_heap_type(key_val.type) || is_heap_type(val_val.type)) {
    builder_->CreateCall(
        rt_.dict_set_heap_,
        {dict_phi, ConstantInt::get(i64_ty, is_heap_type(key_val.type) ? 1 : 0),
         ConstantInt::get(i64_ty, is_heap_type(val_val.type) ? 1 : 0)});
  }
  return {dict_phi, CType::DICT};
}

// ===== Stream fusion: fused generator codegen =====
//
// Emits a single loop that combines an inner (deferred) generator with an
// outer generator. Eliminates the intermediate sequence allocation.
//
// Given: outer = [outer_reducer | outer_var = <inner>, if outer_guard]
//        inner = [inner_reducer | inner_var = src, if inner_guard]
//
// Produces a single loop over src that applies inner_reducer → outer_reducer
// with both guards, storing into a single result array.

TypedValue Codegen::codegen_fused_seq_generator(SeqGeneratorExpr *outer,
                                                SeqGeneratorExpr *inner) {
  set_debug_loc(outer->Range);

  auto *outer_ext =
      static_cast<ValueCollectionExtractorExpr *>(outer->collectionExtractor);
  auto *inner_ext =
      static_cast<ValueCollectionExtractorExpr *>(inner->collectionExtractor);

  bool inner_guarded = (inner_ext->condition != nullptr);
  bool outer_guarded = (outer_ext->condition != nullptr);
  bool any_guard = inner_guarded || outer_guarded;

  std::string inner_var = extractor_var_name(inner->collectionExtractor);
  std::string outer_var = extractor_var_name(outer->collectionExtractor);

  // Codegen the original source (the inner generator's input)
  auto src = codegen(inner_ext->collection);
  if (!src)
    return {};

  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);

  Value *src_ptr = src.val;
  if (!src_ptr->getType()->isPointerTy())
    src_ptr = builder_->CreateIntToPtr(src_ptr, ptr_ty);

  auto *src_len = builder_->CreateCall(rt_.seq_length_, {src_ptr}, "fuse_len");
  auto *result = builder_->CreateCall(rt_.seq_alloc_, {src_len}, "fuse_result");

  auto *func = builder_->GetInsertBlock()->getParent();
  auto *zero = ConstantInt::get(i64_ty, 0);
  auto *one = ConstantInt::get(i64_ty, 1);

  auto *loop_bb = BasicBlock::Create(*context_, "fuse.loop", func);
  auto *body_bb = BasicBlock::Create(*context_, "fuse.body", func);
  auto *done_bb = BasicBlock::Create(*context_, "fuse.done", func);

  BasicBlock *next_bb =
      any_guard ? BasicBlock::Create(*context_, "fuse.next", func) : nullptr;

  builder_->CreateBr(loop_bb);

  // Loop header: iterate source via head/tail (O(1) per element on rbt)
  builder_->SetInsertPoint(loop_bb);
  auto *cur_phi = builder_->CreatePHI(ptr_ty, 2, "fuse.cur");
  cur_phi->addIncoming(src_ptr, loop_bb->getSinglePredecessor());

  // Write index (for non-guard: same as element count)
  auto *wi_phi = builder_->CreatePHI(i64_ty, 2, "fuse.wi");
  wi_phi->addIncoming(zero, loop_bb->getSinglePredecessor());

  auto *is_empty =
      builder_->CreateCall(rt_.seq_is_empty_, {cur_phi}, "fuse.empty");
  auto *not_empty = builder_->CreateICmpEQ(is_empty, zero, "fuse.nempty");
  builder_->CreateCondBr(not_empty, body_bb, done_bb);

  // Body: head/tail on source cursor
  builder_->SetInsertPoint(body_bb);
  auto *elem = builder_->CreateCall(rt_.seq_head_, {cur_phi}, "fuse.elem");
  auto *next_cur =
      builder_->CreateCall(rt_.seq_tail_, {cur_phi}, "fuse.next_cur");

  auto saved = named_values_;
  ActiveNamedValueSnapshot saved_snapshot(*this, saved);
  named_values_[inner_var] = {elem, CType::INT};

  // Track blocks that branch to next_bb with unchanged wi
  std::vector<BasicBlock *> skip_blocks;

  // Inner guard (if present)
  if (inner_guarded) {
    auto guard_val = codegen(inner_ext->condition);
    Value *gb = guard_val.val;
    if (gb->getType() == i64_ty)
      gb = builder_->CreateICmpNE(gb, zero);
    else if (gb->getType() != LType::getInt1Ty(*context_))
      gb =
          builder_->CreateICmpNE(builder_->CreateZExtOrTrunc(gb, i64_ty), zero);
    auto *pass_bb = BasicBlock::Create(*context_, "fuse.ipass", func);
    skip_blocks.push_back(builder_->GetInsertBlock());
    builder_->CreateCondBr(gb, pass_bb, next_bb);
    builder_->SetInsertPoint(pass_bb);
    // Re-bind inner var (codegen of guard may have changed insert point)
    named_values_[inner_var] = {elem, CType::INT};
  }

  // Evaluate inner reducer → produces the element the outer sees
  auto inner_result = codegen(inner->reducerExpr);
  Value *ir_val = inner_result.val;

  // Bind outer variable to inner's result
  named_values_[outer_var] = {ir_val, inner_result.type};

  // Outer guard (if present)
  if (outer_guarded) {
    auto guard_val = codegen(outer_ext->condition);
    Value *gb = guard_val.val;
    if (gb->getType() == i64_ty)
      gb = builder_->CreateICmpNE(gb, zero);
    else if (gb->getType() != LType::getInt1Ty(*context_))
      gb =
          builder_->CreateICmpNE(builder_->CreateZExtOrTrunc(gb, i64_ty), zero);
    auto *store_bb = BasicBlock::Create(*context_, "fuse.store", func);
    skip_blocks.push_back(builder_->GetInsertBlock());
    builder_->CreateCondBr(gb, store_bb, next_bb);
    builder_->SetInsertPoint(store_bb);
    named_values_[outer_var] = {ir_val, inner_result.type};
  }

  // Evaluate outer reducer → final value to store
  auto final_val = codegen(outer->reducerExpr);
  Value *store_val = final_val.val;
  if (store_val->getType()->isPointerTy())
    store_val = builder_->CreatePtrToInt(store_val, i64_ty);
  else if (store_val->getType()->isDoubleTy())
    store_val = builder_->CreateBitCast(store_val, i64_ty);

  builder_->CreateCall(rt_.seq_set_, {result, wi_phi, store_val});

  if (any_guard) {
    auto *wi_inc = builder_->CreateAdd(wi_phi, one, "fuse.wi.inc");
    auto *store_end_bb = builder_->GetInsertBlock();
    builder_->CreateBr(next_bb);

    // Merge write index and cursor
    builder_->SetInsertPoint(next_bb);
    auto *wi_merged =
        builder_->CreatePHI(i64_ty, (int)(skip_blocks.size() + 1), "fuse.wi.m");
    for (auto *sb : skip_blocks)
      wi_merged->addIncoming(wi_phi, sb);
    wi_merged->addIncoming(wi_inc, store_end_bb);

    cur_phi->addIncoming(next_cur, next_bb);
    wi_phi->addIncoming(wi_merged, next_bb);
    builder_->CreateBr(loop_bb);
  } else {
    auto *wi_inc = builder_->CreateAdd(wi_phi, one, "fuse.wi.inc");
    cur_phi->addIncoming(next_cur, builder_->GetInsertBlock());
    wi_phi->addIncoming(wi_inc, builder_->GetInsertBlock());
    builder_->CreateBr(loop_bb);
  }

  named_values_ = saved;

  builder_->SetInsertPoint(done_bb);
  // Always adjust count: wi_phi tracks actual elements stored
  builder_->CreateStore(wi_phi, result);
  return {result, CType::SEQ};
}

} // namespace yona::compiler::codegen
