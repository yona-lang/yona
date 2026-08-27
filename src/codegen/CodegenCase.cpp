//
// Codegen — Case expression / pattern matching
//

#include "Codegen.h"
#include "PatternAnalysis.h"
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <algorithm>

namespace yona::compiler::codegen {
using namespace llvm;
using LType = llvm::Type;

// ===== Pattern match helpers (extracted from codegen_case) =====

bool Codegen::codegen_pattern_value(PatternValue* pv, const TypedValue& scrutinee,
                                     BasicBlock* body_bb, BasicBlock* next_bb) {
    if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
        named_values_[(*id)->name->value] = scrutinee;
        builder_->CreateBr(body_bb);
    } else if (auto* sym = std::get_if<SymbolExpr*>(&pv->expr)) {
        int64_t sym_id = intern_symbol((*sym)->value);
        auto sym_val = ConstantInt::get(LType::getInt64Ty(*context_), sym_id);
        auto cmp = builder_->CreateICmpEQ(scrutinee.val, sym_val);
        builder_->CreateCondBr(cmp, body_bb, next_bb);
    } else if (auto* lit = std::get_if<LiteralExpr<void*>*>(&pv->expr)) {
        auto* an = reinterpret_cast<AstNode*>(*lit);
        if (an->get_type() == AST_INTEGER_EXPR) {
            auto* ie = static_cast<IntegerExpr*>(an);
            auto mv = ConstantInt::get(LType::getInt64Ty(*context_), ie->value);
            auto cmp = builder_->CreateICmpEQ(scrutinee.val, mv);
            builder_->CreateCondBr(cmp, body_bb, next_bb);
        } else builder_->CreateBr(body_bb);
    } else builder_->CreateBr(body_bb);
    return false;
}

bool Codegen::codegen_pattern_headtail(HeadTailsPattern* htp, CaseExpr* node,
                                        CaseClause* clause, const TypedValue& scrutinee,
                                        Value* seq_ptr,
                                        BasicBlock* body_bb, BasicBlock* next_bb) {
    auto i64_ty = LType::getInt64Ty(*context_);

    if (htp->heads.size() == 1) {
        auto* count_ptr = builder_->CreateGEP(i64_ty, seq_ptr,
            {ConstantInt::get(i64_ty, 0)});
        auto* count = builder_->CreateLoad(i64_ty, count_ptr, "seq_count");
        builder_->CreateCondBr(
            builder_->CreateICmpSGT(count, ConstantInt::get(i64_ty, 0)),
            body_bb, next_bb);
    } else {
        auto len = builder_->CreateCall(rt_.seq_length_, {seq_ptr});
        auto min_len = ConstantInt::get(i64_ty, htp->heads.size());
        builder_->CreateCondBr(builder_->CreateICmpSGE(len, min_len), body_bb, next_bb);
    }

    // Recursive semantic identity is authoritative when the shallow ABI
    // subtype came from an untyped empty literal (`[]` defaults to INT).
    // Concrete annotations and call-site specialization retain the actual
    // element here, e.g. Seq String in Std\Test.runCases.
    CType elem_type = !scrutinee.semantic_subtypes.empty()
        ? scrutinee.semantic_subtypes.front().type
        : (!scrutinee.subtypes.empty() ? scrutinee.subtypes.front() : CType::INT);

    // A single-use scrutinee may be consumed in place when binding its tail.
    // Determine that before binding heads: heap-valued head bindings need an
    // independent reference because consuming the sequence releases the
    // removed element's ownership.
    bool owned = true;
    if (node->expr->get_type() == AST_IDENTIFIER_EXPR) {
        const auto scrut_name =
            static_cast<IdentifierExpr*>(node->expr)->name->value;
        owned = current_fn_body_
            ? count_identifier_refs(current_fn_body_, scrut_name) == 1
            : false;
    }
    const bool retain_bound_heads = owned && htp->tail != nullptr;

    builder_->SetInsertPoint(body_bb);
    for (size_t hi = 0; hi < htp->heads.size(); hi++) {
        Value* hv;
        if (hi == 0 && htp->heads.size() == 1)
            hv = builder_->CreateCall(rt_.seq_head_, {seq_ptr}, "head");
        else
            hv = builder_->CreateCall(rt_.seq_get_, {seq_ptr, ConstantInt::get(i64_ty, hi)});
        auto* hp = htp->heads[hi];
        CType head_type = (hp->get_type() == AST_TUPLE_PATTERN) ? CType::TUPLE : elem_type;
        Value* elem_val = hv;
        if (head_type == CType::SEQ || head_type == CType::STRING ||
            head_type == CType::FUNCTION || head_type == CType::ADT ||
            elem_type == CType::SET || elem_type == CType::DICT)
            elem_val = builder_->CreateIntToPtr(hv, PointerType::get(*context_, 0));
        else if (head_type == CType::FLOAT)
            elem_val = builder_->CreateBitCast(
                hv, LType::getDoubleTy(*context_), "head_float");
        else if (head_type == CType::BOOL)
            elem_val = builder_->CreateICmpNE(
                hv, ConstantInt::get(hv->getType(), 0), "head_bool");
        if (hp->get_type() == AST_PATTERN_VALUE) {
            auto* pv = static_cast<PatternValue*>(hp);
            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                TypedValue head{elem_val, head_type};
                if (!scrutinee.semantic_subtypes.empty()) {
                    const auto& identity = scrutinee.semantic_subtypes.front();
                    head.adt_type_name = identity.adt_name;
                    head.semantic_subtypes = identity.arguments;
                    head.adt_semantic_arguments = identity.arguments;
                    for (const auto& argument : identity.arguments) {
                        head.adt_type_arguments.push_back(argument.type);
                        head.adt_type_argument_names.push_back(argument.adt_name);
                    }
                }
                named_values_[(*id)->name->value] = std::move(head);
                if (retain_bound_heads && is_heap_type(head_type)) {
                    emit_rc_inc(elem_val, head_type);
                    if (!arm_drop_stack_.empty())
                        arm_drop_stack_.back().push_back({elem_val, head_type});
                }
            }
        } else if (hp->get_type() == AST_TUPLE_PATTERN) {
            auto* tp = static_cast<TuplePattern*>(hp);
            Value* tuple_ptr = elem_val;
            if (tuple_ptr->getType()->isIntegerTy())
                tuple_ptr = builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0));
            // A consuming head-tail match releases the sequence's ownership
            // of its removed heap element. Keep the tuple container alive
            // through the arm so its heap-marked fields remain valid while
            // their pattern bindings are used.
            if (retain_bound_heads) {
                emit_rc_inc(tuple_ptr, CType::TUPLE);
                if (!arm_drop_stack_.empty())
                    arm_drop_stack_.back().push_back({tuple_ptr, CType::TUPLE});
            }
            const SemanticTypeIdentity* tuple_identity = nullptr;
            if (!scrutinee.semantic_subtypes.empty() &&
                scrutinee.semantic_subtypes.front().type == CType::TUPLE)
                tuple_identity = &scrutinee.semantic_subtypes.front();
            for (size_t ti = 0; ti < tp->patterns.size(); ti++) {
                auto* sub = tp->patterns[ti];
                if (sub->get_type() != AST_PATTERN_VALUE) continue;
                auto* pv = static_cast<PatternValue*>(sub);
                auto* id = std::get_if<IdentifierExpr*>(&pv->expr);
                if (!id) continue;
                auto* gep = builder_->CreateGEP(i64_ty, tuple_ptr,
                    {ConstantInt::get(i64_ty, ti + 2)}, "tuple_head_gep");
                auto* elem = builder_->CreateLoad(i64_ty, gep, "tuple_head_elem");
                const SemanticTypeIdentity* element_identity =
                    tuple_identity && ti < tuple_identity->arguments.size()
                        ? &tuple_identity->arguments[ti] : nullptr;
                const CType element_type = element_identity
                    ? element_identity->type : CType::INT;
                Value* typed_elem = elem;
                if (is_heap_type(element_type))
                    typed_elem = builder_->CreateIntToPtr(
                        elem, PointerType::get(*context_, 0), "tuple_head_elem_ptr");
                else if (element_type == CType::FLOAT)
                    typed_elem = builder_->CreateBitCast(
                        elem, LType::getDoubleTy(*context_), "tuple_head_elem_float");
                else if (element_type == CType::BOOL)
                    typed_elem = builder_->CreateICmpNE(
                        elem, ConstantInt::get(elem->getType(), 0),
                        "tuple_head_elem_bool");
                TypedValue bound{typed_elem, element_type};
                if (element_identity) {
                    bound.adt_type_name = element_identity->adt_name;
                    bound.semantic_subtypes = element_identity->arguments;
                    bound.adt_semantic_arguments = element_identity->arguments;
                }
                named_values_[(*id)->name->value] = std::move(bound);
            }
        } else if (hp->get_type() == AST_RECORD_PATTERN) {
            // A sequence head may itself be a named-field ADT pattern, e.g.
            // `[TestCase { thunk = f } | rest]`. Preserve the declared field
            // shape exactly as direct record-pattern matching does.
            auto* record = static_cast<RecordPattern*>(hp);
            auto ctor_it = types_.adt_constructors.find(record->recordType);
            if (ctor_it == types_.adt_constructors.end()) continue;
            Value* adt_ptr = elem_val;
            if (!adt_ptr->getType()->isPointerTy())
                adt_ptr = builder_->CreateIntToPtr(adt_ptr, PointerType::get(*context_, 0));
            for (auto& [field_name, field_pattern] : record->items) {
                if (!field_name || field_pattern->get_type() != AST_PATTERN_VALUE) continue;
                auto* pattern_value = static_cast<PatternValue*>(field_pattern);
                auto* identifier = std::get_if<IdentifierExpr*>(&pattern_value->expr);
                if (!identifier) continue;
                for (size_t fi = 0; fi < ctor_it->second.field_names.size(); ++fi) {
                    if (ctor_it->second.field_names[fi] != field_name->value) continue;
                    auto* raw = builder_->CreateCall(rt_.adt_get_field_,
                        {adt_ptr, ConstantInt::get(i64_ty, fi)});
                    AdtInfo::FieldShape fallback;
                    if (fi < ctor_it->second.field_types.size())
                        fallback.type = ctor_it->second.field_types[fi];
                    if (fi < ctor_it->second.field_fn_return_types.size())
                        fallback.function_return_type = ctor_it->second.field_fn_return_types[fi];
                    if (fi < ctor_it->second.field_fn_return_adt_names.size())
                        fallback.function_return_adt_name = ctor_it->second.field_fn_return_adt_names[fi];
                    const auto& shape = fi < ctor_it->second.field_shapes.size()
                        ? ctor_it->second.field_shapes[fi] : fallback;
                    Value* typed = raw;
                    if (shape.type == CType::FLOAT)
                        typed = builder_->CreateBitCast(raw, LType::getDoubleTy(*context_));
                    else if (is_heap_type(shape.type))
                        typed = builder_->CreateIntToPtr(raw, PointerType::get(*context_, 0),
                                                         "seq_record_field_ptr");
                    TypedValue bound{typed, shape.type};
                    if (shape.type == CType::FUNCTION) {
                        bound.subtypes = {shape.function_return_type};
                        bound.adt_type_name = shape.function_return_adt_name;
                    }
                    named_values_[(*identifier)->name->value] = bound;
                    if (retain_bound_heads && is_heap_type(shape.type)) {
                        emit_rc_inc(typed, shape.type);
                        if (!arm_drop_stack_.empty())
                            arm_drop_stack_.back().push_back({typed, shape.type});
                    }
                    break;
                }
            }
        }
    }
    if (htp->tail) {
        // Ownership model — Perceus-linear (phase 1, single-use):
        //
        //   OWNED SCRUTINEE (scrut is a single-use identifier in the
        //   enclosing function body, or is a temporary): we hand ownership
        //   to seq_tail_consume, which returns either the same pointer
        //   (rc==1 in-place) or a fresh copy + rc_dec of the old. We then
        //   drop just `t` at arm exit. Keeps the hot foldl/scanl pattern
        //   on the in-place fast path and closes most of the list_* gap.
        //
        //   BORROWED SCRUTINEE (scrut is an identifier used more than
        //   once in the function body): in-place tail would mutate a seq
        //   that other uses expect intact, so we rc_inc first and let
        //   seq_tail force the copy path. Drop both t and the incremented
        //   scrut at arm exit.
        //
        // Scrut is "owned" when:
        //   - It's not an identifier (temporary expression result), or
        //   - It's an identifier with exactly one textual occurrence in
        //     the enclosing function body (so this case IS that occurrence).
        llvm::Value* tv;
        if (owned) {
            // Phase 3: mark the scrutinee transferred in any active frame
            // BEFORE the runtime consume so a raise during the consume
            // (shouldn't happen, but defensive) doesn't double-dec.
            emit_frame_transfer(seq_ptr);
            tv = builder_->CreateCall(rt_.seq_tail_consume_, {seq_ptr});
            // Mark the scrutinee Value as transferred so downstream scope
            // cleanups (let scope exit, function exit) skip its rc_dec.
            mark_transferred(seq_ptr, TransferDomain::Seq);
        } else {
            emit_rc_inc(seq_ptr, CType::SEQ);
            tv = builder_->CreateCall(rt_.seq_tail_, {seq_ptr});
        }
        if (htp->tail->get_type() == AST_PATTERN_VALUE) {
            auto* pv = static_cast<PatternValue*>(htp->tail);
            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                TypedValue tail{tv, CType::SEQ, scrutinee.subtypes};
                tail.semantic_subtypes = scrutinee.semantic_subtypes;
                named_values_[(*id)->name->value] = std::move(tail);
            }
        } else if (htp->tail->get_type() == AST_HEAD_TAILS_PATTERN) {
            auto* nested_tail_bb = BasicBlock::Create(*context_, "tail.pat.match",
                body_bb->getParent());
            auto* nested = static_cast<HeadTailsPattern*>(htp->tail);
            TypedValue tail_scrutinee{tv, CType::SEQ, scrutinee.subtypes};
            tail_scrutinee.semantic_subtypes = scrutinee.semantic_subtypes;
            codegen_pattern_headtail(nested, node, clause, tail_scrutinee, tv,
                                     nested_tail_bb, next_bb);
        }
        if (!arm_drop_stack_.empty()) {
            arm_drop_stack_.back().push_back({tv, CType::SEQ});
            if (!owned)
                arm_drop_stack_.back().push_back({seq_ptr, CType::SEQ});
        }
    }
    return true;
}

bool Codegen::codegen_pattern_seq(SeqPattern* sp, const TypedValue& scrutinee,
                                   BasicBlock* body_bb, BasicBlock* next_bb) {
    auto* i64_ty = LType::getInt64Ty(*context_);
    Value* seq_val = scrutinee.val;
    if (!seq_val->getType()->isPointerTy())
        seq_val = builder_->CreateIntToPtr(
            seq_val, PointerType::get(*context_, 0));
    if (sp->patterns.empty()) {
        auto* count_ptr = builder_->CreateGEP(i64_ty,
            seq_val, {ConstantInt::get(i64_ty, 0)});
        auto* count = builder_->CreateLoad(i64_ty, count_ptr, "seq_count");
        builder_->CreateCondBr(
            builder_->CreateICmpEQ(count, ConstantInt::get(i64_ty, 0)),
            body_bb, next_bb);
        // Perceus-linear: under callee-owns, an empty-seq arm doesn't
        // consume the scrutinee through any pattern-match call. Drop it
        // directly at the start of the body so the owned ref doesn't
        // leak. (This is not routed through arm_drop_stack because that
        // path's seq-transfer skip would also fire on this drop —
        // the scrut may have been marked transferred by a sibling
        // head-tail arm's consume, but at runtime only ONE arm runs,
        // and this arm didn't actually consume.)
        if (scrutinee.type == CType::SEQ && scrutinee.val &&
            !scrutinee.val->getType()->isStructTy()) {
            auto saved_ip = builder_->saveIP();
            builder_->SetInsertPoint(body_bb);
            emit_frame_transfer(scrutinee.val);
            emit_rc_dec(scrutinee.val, CType::SEQ);
            builder_->restoreIP(saved_ip);
            // Mark the scrutinee as already-drained on this path so the
            // outer case transfer_scope_exit doesn't emit a compensating
            // second rc_dec when a sibling head-tail arm transferred the
            // same scrutinee via seq_tail_consume.
            mark_transferred(scrutinee.val, TransferDomain::Seq);
        }
    } else {
        auto* count = builder_->CreateCall(rt_.seq_length_, {seq_val}, "seq_exact_count");
        auto* bind_bb = BasicBlock::Create(
            *context_, "seq.exact.bind", builder_->GetInsertBlock()->getParent());
        builder_->CreateCondBr(
            builder_->CreateICmpEQ(
                count, ConstantInt::get(i64_ty, sp->patterns.size())),
            bind_bb, next_bb);
        builder_->SetInsertPoint(bind_bb);

        const SemanticTypeIdentity* element_identity =
            scrutinee.semantic_subtypes.empty()
            ? nullptr : &scrutinee.semantic_subtypes.front();
        std::function<void(PatternNode*, Value*, const SemanticTypeIdentity*)>
            bind_pattern;
        bind_pattern = [&](PatternNode* pattern, Value* carrier,
                           const SemanticTypeIdentity* identity) {
            if (!pattern || pattern->get_type() == AST_UNDERSCORE_PATTERN) return;
            if (pattern->get_type() == AST_PATTERN_VALUE) {
                auto* value_pattern = static_cast<PatternValue*>(pattern);
                if (auto* id = std::get_if<IdentifierExpr*>(&value_pattern->expr)) {
                    const CType type = identity ? identity->type : CType::INT;
                    Value* value = carrier;
                    if (is_heap_type(type))
                        value = builder_->CreateIntToPtr(
                            carrier, PointerType::get(*context_, 0),
                            "seq_pattern_ptr");
                    else if (type == CType::FLOAT)
                        value = builder_->CreateBitCast(
                            carrier, LType::getDoubleTy(*context_),
                            "seq_pattern_float");
                    else if (type == CType::BOOL)
                        value = builder_->CreateICmpNE(
                            carrier, ConstantInt::get(carrier->getType(), 0),
                            "seq_pattern_bool");
                    TypedValue bound{value, type};
                    if (identity) {
                        bound.adt_type_name = identity->adt_name;
                        bound.semantic_subtypes = identity->arguments;
                        bound.adt_semantic_arguments = identity->arguments;
                    }
                    named_values_[(*id)->name->value] = std::move(bound);
                    return;
                }
                Value* expected = nullptr;
                if (auto* symbol = std::get_if<SymbolExpr*>(&value_pattern->expr))
                    expected = ConstantInt::get(i64_ty, intern_symbol((*symbol)->value));
                else if (auto* literal =
                             std::get_if<LiteralExpr<void*>*>(&value_pattern->expr)) {
                    auto* node = reinterpret_cast<AstNode*>(*literal);
                    if (node->get_type() == AST_INTEGER_EXPR)
                        expected = ConstantInt::get(
                            i64_ty, static_cast<IntegerExpr*>(node)->value);
                }
                if (expected) {
                    auto* matched = BasicBlock::Create(
                        *context_, "seq.exact.value", builder_->GetInsertBlock()->getParent());
                    builder_->CreateCondBr(
                        builder_->CreateICmpEQ(carrier, expected), matched, next_bb);
                    builder_->SetInsertPoint(matched);
                }
                return;
            }
            if (pattern->get_type() != AST_TUPLE_PATTERN) return;
            auto* tuple = static_cast<TuplePattern*>(pattern);
            Value* tuple_ptr = carrier;
            if (tuple_ptr->getType()->isIntegerTy())
                tuple_ptr = builder_->CreateIntToPtr(
                    tuple_ptr, PointerType::get(*context_, 0),
                    "seq_pattern_tuple");
            for (size_t i = 0; i < tuple->patterns.size(); ++i) {
                auto* slot = builder_->CreateGEP(
                    i64_ty, tuple_ptr, {ConstantInt::get(i64_ty, i + 2)},
                    "seq_pattern_tuple_gep");
                auto* field = builder_->CreateLoad(
                    i64_ty, slot, "seq_pattern_tuple_field");
                const SemanticTypeIdentity* field_identity =
                    identity && i < identity->arguments.size()
                    ? &identity->arguments[i] : nullptr;
                bind_pattern(tuple->patterns[i], field, field_identity);
            }
        };
        for (size_t i = 0; i < sp->patterns.size(); ++i) {
            auto* element = builder_->CreateCall(
                rt_.seq_get_, {seq_val, ConstantInt::get(i64_ty, i)},
                "seq_exact_element");
            bind_pattern(sp->patterns[i], element, element_identity);
        }
        builder_->CreateBr(body_bb);
    }
    return false;
}

bool Codegen::codegen_pattern_tuple(TuplePattern* tp, const TypedValue& scrutinee,
                                     BasicBlock* body_bb, BasicBlock* next_bb) {
    auto i64_ty = LType::getInt64Ty(*context_);
    auto* fn = builder_->GetInsertBlock()->getParent();
    Value* tuple_val = scrutinee.val;
    bool is_ptr = tuple_val->getType()->isPointerTy();

    if (tuple_val->getType()->isIntegerTy()) {
        tuple_val = builder_->CreateIntToPtr(tuple_val, PointerType::get(*context_, 0));
        is_ptr = true;
    }

    for (size_t ti = 0; ti < tp->patterns.size(); ti++) {
        Value* elem;
        if (is_ptr) {
            auto* gep = builder_->CreateGEP(i64_ty, tuple_val,
                {ConstantInt::get(i64_ty, ti + 2)}, "tuple_gep");
            elem = builder_->CreateLoad(i64_ty, gep, "tuple_elem");
        } else {
            elem = builder_->CreateExtractValue(tuple_val, {(unsigned)ti});
        }
        CType et = (ti < scrutinee.subtypes.size()) ? scrutinee.subtypes[ti] : CType::INT;
        auto* sub = tp->patterns[ti];
        if (sub->get_type() == AST_PATTERN_VALUE) {
            auto* pv = static_cast<PatternValue*>(sub);
            if (auto* sym = std::get_if<SymbolExpr*>(&pv->expr)) {
                int64_t sym_id = intern_symbol((*sym)->value);
                auto* sym_val = ConstantInt::get(i64_ty, sym_id);
                Value* cmp_val = elem;
                if (cmp_val->getType() != i64_ty) {
                    if (cmp_val->getType()->isPointerTy())
                        cmp_val = builder_->CreatePtrToInt(cmp_val, i64_ty);
                    else if (cmp_val->getType()->isIntegerTy())
                        cmp_val = builder_->CreateZExtOrTrunc(cmp_val, i64_ty);
                }
                auto* match_bb = BasicBlock::Create(*context_, "tuple.sym.match", fn);
                builder_->CreateCondBr(builder_->CreateICmpEQ(cmp_val, sym_val), match_bb, next_bb);
                builder_->SetInsertPoint(match_bb);
            } else if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                // Heap-typed elements were stored as i64-cast pointers; restore
                // the pointer so downstream pattern matching can use heap layout.
                Value* typed_elem = elem;
                if (et == CType::ADT || et == CType::STRING ||
                    et == CType::FUNCTION || et == CType::SET ||
                    et == CType::DICT || et == CType::CHANNEL)
                    typed_elem = builder_->CreateIntToPtr(elem,
                        PointerType::get(*context_, 0), "tuple_pat_elem_ptr");
                named_values_[(*id)->name->value] = {typed_elem, et};
            } else if (auto* lit = std::get_if<LiteralExpr<void*>*>(&pv->expr)) {
                auto* an = reinterpret_cast<AstNode*>(*lit);
                if (an->get_type() == AST_INTEGER_EXPR) {
                    auto* ie = static_cast<IntegerExpr*>(an);
                    Value* cmp_val = elem;
                    if (cmp_val->getType() != i64_ty)
                        cmp_val = builder_->CreateZExtOrTrunc(cmp_val, i64_ty);
                    auto* match_bb = BasicBlock::Create(*context_, "tuple.lit.match", fn);
                    builder_->CreateCondBr(
                        builder_->CreateICmpEQ(cmp_val, ConstantInt::get(i64_ty, ie->value)),
                        match_bb, next_bb);
                    builder_->SetInsertPoint(match_bb);
                }
            }
        } else if (sub->get_type() == AST_TUPLE_PATTERN) {
            auto* nested = static_cast<TuplePattern*>(sub);
            Value* nested_ptr = elem;
            if (nested_ptr->getType()->isIntegerTy())
                nested_ptr = builder_->CreateIntToPtr(nested_ptr,
                    PointerType::get(*context_, 0), "nested_tuple_ptr");
            for (size_t ni = 0; ni < nested->patterns.size(); ni++) {
                auto* nested_sub = nested->patterns[ni];
                if (nested_sub->get_type() != AST_PATTERN_VALUE) continue;
                auto* pv = static_cast<PatternValue*>(nested_sub);
                auto* id = std::get_if<IdentifierExpr*>(&pv->expr);
                if (!id) continue;
                auto* gep = builder_->CreateGEP(i64_ty, nested_ptr,
                    {ConstantInt::get(i64_ty, ni + 2)}, "nested_tuple_gep");
                auto* nested_elem = builder_->CreateLoad(i64_ty, gep, "nested_tuple_elem");
                named_values_[(*id)->name->value] = {nested_elem, CType::INT};
            }
        }
        // AST_UNDERSCORE_PATTERN: wildcard, no action needed
    }
    builder_->CreateBr(body_bb);
    return false;
}

bool Codegen::codegen_pattern_constructor(ConstructorPattern* cp, const TypedValue& scrutinee_in,
                                           BasicBlock* body_bb, BasicBlock* next_bb) {
    auto ctor_it = types_.adt_constructors.find(cp->constructor_name);
    if (ctor_it == types_.adt_constructors.end()) {
        builder_->CreateBr(body_bb);
        return false;
    }

    int8_t tag = static_cast<int8_t>(ctor_it->second.tag);
    auto tag_ty = LType::getInt64Ty(*context_);
    auto i64_ty = LType::getInt64Ty(*context_);

    using FieldShape = AdtInfo::FieldShape;
    std::function<void(PatternNode*, Value*, const FieldShape&)> bind_pattern;
    std::function<void(const std::vector<PatternNode*>&, Value*, const FieldShape&)> bind_tuple;
    bind_tuple = [&](const std::vector<PatternNode*>& patterns, Value* tuple_value,
                     const FieldShape& shape) {
        if (!tuple_value) return;
        Value* tuple_ptr = tuple_value;
        if (tuple_ptr->getType()->isIntegerTy())
            tuple_ptr = builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0),
                                                 "ctor_tuple_field_ptr");
        for (size_t ti = 0; ti < patterns.size() && ti < shape.tuple_elements.size(); ++ti) {
            auto* gep = builder_->CreateGEP(i64_ty, tuple_ptr,
                {ConstantInt::get(i64_ty, ti + 2)}, "ctor_tuple_gep");
            auto* elem = builder_->CreateLoad(i64_ty, gep, "ctor_tuple_elem");
            bind_pattern(patterns[ti], elem, shape.tuple_elements[ti]);
        }
    };
    bind_pattern = [&](PatternNode* pattern, Value* raw_value, const FieldShape& shape) {
        if (!pattern || !raw_value) return;
        if (shape.type == CType::TUPLE && pattern->get_type() == AST_TUPLE_PATTERN) {
            auto* tuple = static_cast<TuplePattern*>(pattern);
            std::vector<PatternNode*> elements(tuple->patterns.begin(), tuple->patterns.end());
            bind_tuple(elements, raw_value, shape);
            return;
        }
        if (pattern->get_type() != AST_PATTERN_VALUE) return;
        auto* value_pattern = static_cast<PatternValue*>(pattern);
        auto* identifier = std::get_if<IdentifierExpr*>(&value_pattern->expr);
        if (!identifier) return;

        Value* typed_value = raw_value;
        if (shape.type == CType::FLOAT && raw_value->getType()->isIntegerTy())
            typed_value = builder_->CreateBitCast(raw_value, LType::getDoubleTy(*context_));
        else if (is_heap_type(shape.type) && raw_value->getType()->isIntegerTy())
            typed_value = builder_->CreateIntToPtr(raw_value, PointerType::get(*context_, 0),
                                                   "ctor_tuple_element_ptr");
        TypedValue bound{typed_value, shape.type};
        if (shape.type == CType::FUNCTION) {
            bound.subtypes = {shape.function_return_type};
            bound.adt_type_name = shape.function_return_adt_name;
        } else if (shape.type == CType::TUPLE) {
            for (const auto& element : shape.tuple_elements)
                bound.subtypes.push_back(element.type);
        }
        named_values_[(*identifier)->name->value] = bound;
    };

    auto bind_field_pattern = [&](size_t field_index, Value* field_value) {
        FieldShape fallback;
        if (field_index < ctor_it->second.field_types.size())
            fallback.type = ctor_it->second.field_types[field_index];
        if (field_index < ctor_it->second.field_fn_return_types.size())
            fallback.function_return_type = ctor_it->second.field_fn_return_types[field_index];
        if (field_index < ctor_it->second.field_fn_return_adt_names.size())
            fallback.function_return_adt_name = ctor_it->second.field_fn_return_adt_names[field_index];
        const auto& shape = field_index < ctor_it->second.field_shapes.size()
            ? ctor_it->second.field_shapes[field_index] : fallback;
        if (field_index < cp->sub_patterns.size()) {
            bind_pattern(cp->sub_patterns[field_index], field_value, shape);
        }
    };

    // The scrutinee is sometimes an i64-typed ADT — this happens when the
    // value comes through a generic i64-returning runtime call (e.g.
    // `yona_rt_async_await` for a CAF returning an Option). Coerce back to
    // ptr so the heap-layout extractors work.
    TypedValue scrutinee = scrutinee_in;
    if (scrutinee.val && scrutinee.val->getType()->isIntegerTy()) {
        scrutinee.val = builder_->CreateIntToPtr(scrutinee.val,
            PointerType::get(*context_, 0), "adt.scrutinee.ptr");
        scrutinee.type = CType::ADT;
    }

    // Use heap layout if either: (a) the constructor is recursive, or
    // (b) the scrutinee is a pointer (e.g., it was loaded from a closure
    // capture, where struct values are boxed to heap before storage).
    bool use_heap_layout = ctor_it->second.is_recursive ||
                           (scrutinee.val && scrutinee.val->getType()->isPointerTy());

    auto concrete_field_type = [&](size_t field_index) -> CType {
        if (field_index < ctor_it->second.declared_field_types.size()) {
            const auto& declared = ctor_it->second.declared_field_types[field_index];
            if (!declared.is_function_type && !declared.is_tuple_type &&
                declared.type_arguments.empty()) {
                const auto params = types_.adt_type_params.find(ctor_it->second.type_name);
                if (params != types_.adt_type_params.end()) {
                    for (size_t i = 0; i < params->second.size() &&
                                       i < scrutinee.adt_type_arguments.size(); ++i) {
                        if (params->second[i] == declared.name)
                            return scrutinee.adt_type_arguments[i];
                    }
                }
            }
        }
        return field_index < scrutinee.subtypes.size()
            ? scrutinee.subtypes[field_index] : CType::INT;
    };

    auto concrete_field_adt_name = [&](size_t field_index) -> std::string {
        if (field_index >= ctor_it->second.declared_field_types.size()) return {};
        const auto& declared = ctor_it->second.declared_field_types[field_index];
        if (declared.is_function_type || declared.is_tuple_type ||
            !declared.type_arguments.empty())
            return {};
        const auto params = types_.adt_type_params.find(ctor_it->second.type_name);
        if (params == types_.adt_type_params.end()) return {};
        for (size_t i = 0; i < params->second.size() &&
                           i < scrutinee.adt_type_argument_names.size(); ++i)
            if (params->second[i] == declared.name)
                return scrutinee.adt_type_argument_names[i];
        return {};
    };
    auto concrete_field_identity = [&](size_t field_index)
            -> const SemanticTypeIdentity* {
        if (field_index >= ctor_it->second.declared_field_types.size())
            return nullptr;
        const auto& declared = ctor_it->second.declared_field_types[field_index];
        if (declared.is_function_type || declared.is_tuple_type ||
            !declared.type_arguments.empty())
            return nullptr;
        const auto params = types_.adt_type_params.find(ctor_it->second.type_name);
        if (params == types_.adt_type_params.end()) return nullptr;
        for (size_t i = 0; i < params->second.size() &&
                           i < scrutinee.adt_semantic_arguments.size(); ++i)
            if (params->second[i] == declared.name)
                return &scrutinee.adt_semantic_arguments[i];
        return nullptr;
    };

    if (use_heap_layout) {
        auto scr_tag = builder_->CreateCall(rt_.adt_get_tag_, {scrutinee.val});
        builder_->CreateCondBr(
            builder_->CreateICmpEQ(scr_tag, ConstantInt::get(i64_ty, tag)),
            body_bb, next_bb);
        builder_->SetInsertPoint(body_bb);
        const size_t field_count =
            std::min(cp->sub_patterns.size(), static_cast<size_t>(ctor_it->second.arity));
        for (size_t fi = 0; fi < field_count; fi++) {
            auto field_val = builder_->CreateCall(rt_.adt_get_field_,
                {scrutinee.val, ConstantInt::get(i64_ty, fi)});
            auto* sub_pat = cp->sub_patterns[fi];
            if (sub_pat->get_type() == AST_PATTERN_VALUE) {
                auto* pv = static_cast<PatternValue*>(sub_pat);
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                    // Prefer the runtime subtype recorded at construction
                    // time when it's more specific than the registered field
                    // type. The .yona ADT field-type registry only records
                    // a head identifier (e.g. "Stream a" → just "Stream"),
                    // so generic / parameterized fields fall back to INT.
                    // The scrutinee's subtypes carry the actual CType from
                    // codegen_adt_construct and are accurate.
                    CType registered = (fi < ctor_it->second.field_types.size())
                        ? ctor_it->second.field_types[fi] : CType::INT;
                    CType runtime_st = concrete_field_type(fi);
                    bool declared_parameter = false;
                    if (fi < ctor_it->second.declared_field_types.size()) {
                        const auto params = types_.adt_type_params.find(
                            ctor_it->second.type_name);
                        declared_parameter = params != types_.adt_type_params.end() &&
                            std::find(params->second.begin(), params->second.end(),
                                      ctor_it->second.declared_field_types[fi].name) !=
                                params->second.end();
                    }
                    CType ftype = declared_parameter &&
                            fi < scrutinee.subtypes.size()
                        ? runtime_st
                        : (registered == CType::INT && runtime_st != CType::INT)
                            ? runtime_st : registered;
                    if (ctor_it->second.type_name == "Iterator" && fi == 0)
                        ftype = CType::FUNCTION;
                    Value* typed_val = field_val;
                    if (ftype == CType::ADT || ftype == CType::SEQ ||
                        ftype == CType::STRING || ftype == CType::FUNCTION ||
                        ftype == CType::SET || ftype == CType::DICT ||
                        ftype == CType::CHANNEL || ftype == CType::TUPLE)
                        typed_val = builder_->CreateIntToPtr(field_val,
                            PointerType::get(*context_, 0));
                    else if (ftype == CType::FLOAT)
                        typed_val = builder_->CreateBitCast(
                            field_val, LType::getDoubleTy(*context_));
                    else if (ftype == CType::BOOL)
                        typed_val = builder_->CreateICmpNE(
                            field_val, ConstantInt::get(field_val->getType(), 0));
                    // Extracting a heap-typed field is a Perceus DUP: the
                    // bound name takes a new reference. The scrutinee will
                    // be dropped later, taking its own field reference with
                    // it via heap_mask, so without this dup the bound name
                    // would dangle the moment the scrutinee dies.
                    //
                    // Only fire when the runtime subtype (carried at
                    // construction) confirms the field is heap. The
                    // registered field_type is permissive — `Linear a`
                    // declares its field as ADT but accepts plain ints,
                    // and rc_inc on a primitive segfaults the runtime.
                    bool declared_heap = false;
                    if (fi < ctor_it->second.declared_field_types.size()) {
                        const auto& declared =
                            ctor_it->second.declared_field_types[fi];
                        const auto params = types_.adt_type_params.find(
                            ctor_it->second.type_name);
                        const bool is_parameter = params != types_.adt_type_params.end() &&
                            std::find(params->second.begin(), params->second.end(),
                                      declared.name) != params->second.end();
                        declared_heap = !is_parameter && is_heap_type(ftype);
                    }
                    bool runtime_heap = declared_heap ||
                        (fi < scrutinee.subtypes.size() &&
                         is_heap_type(scrutinee.subtypes[fi]));
                    if (runtime_heap && typed_val->getType()->isPointerTy())
                        emit_rc_inc(typed_val, ftype);
                    TypedValue bound{typed_val, ftype};
                    if (const auto* identity = concrete_field_identity(fi))
                        bound.semantic_subtypes = identity->arguments;
                    if (ftype == CType::ADT) {
                        bound.adt_type_name = concrete_field_adt_name(fi);
                        if (const auto* identity = concrete_field_identity(fi)) {
                            bound.adt_semantic_arguments = identity->arguments;
                            for (const auto& argument : identity->arguments) {
                                bound.adt_type_arguments.push_back(argument.type);
                                bound.adt_type_argument_names.push_back(argument.adt_name);
                            }
                        }
                    }
                    if (ctor_it->second.type_name == "Result" ||
                        ctor_it->second.type_name == "Option")
                        bound.boxed_heap = scrutinee.boxed_heap || is_heap_type(ftype);
                    // For function-typed fields, propagate the recorded
                    // return CType (and ADT name) so call sites generate
                    // the correct closure invocation.
                    if (ftype == CType::FUNCTION &&
                        fi < ctor_it->second.field_fn_return_types.size()) {
                        bound.subtypes = {ctor_it->second.field_fn_return_types[fi]};
                        if (fi < ctor_it->second.field_fn_return_adt_names.size())
                            bound.adt_type_name = ctor_it->second.field_fn_return_adt_names[fi];
                    } else if (ctor_it->second.type_name == "Iterator" && fi == 0) {
                        bound.subtypes = {CType::ADT};
                        bound.adt_type_name = "Option";
                    }
                    named_values_[(*id)->name->value] = bound;
                }
            } else if (sub_pat->get_type() == AST_TUPLE_PATTERN) {
                bind_field_pattern(fi, field_val);
            }
        }
    } else {
        auto scr_tag = builder_->CreateExtractValue(scrutinee.val, {0});
        builder_->CreateCondBr(
            builder_->CreateICmpEQ(scr_tag, ConstantInt::get(tag_ty, tag)),
            body_bb, next_bb);
        builder_->SetInsertPoint(body_bb);
        const size_t field_count =
            std::min(cp->sub_patterns.size(), static_cast<size_t>(ctor_it->second.arity));
        for (size_t fi = 0; fi < field_count; fi++) {
            auto field_val = builder_->CreateExtractValue(scrutinee.val, {(unsigned)(fi + 1)});
            auto* sub_pat = cp->sub_patterns[fi];
            if (sub_pat->get_type() == AST_PATTERN_VALUE) {
                auto* pv = static_cast<PatternValue*>(sub_pat);
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                    // Prefer the runtime subtype recorded at construction
                    // time when it's more specific than the registered field
                    // type. The .yona ADT field-type registry only records
                    // a head identifier (e.g. "Stream a" → just "Stream"),
                    // so generic / parameterized fields fall back to INT.
                    // The scrutinee's subtypes carry the actual CType from
                    // codegen_adt_construct and are accurate.
                    CType registered = (fi < ctor_it->second.field_types.size())
                        ? ctor_it->second.field_types[fi] : CType::INT;
                    CType runtime_st = concrete_field_type(fi);
                    CType ftype = (registered == CType::INT && runtime_st != CType::INT)
                        ? runtime_st : registered;
                    if (ctor_it->second.type_name == "Iterator" && fi == 0)
                        ftype = CType::FUNCTION;
                    Value* typed_val = field_val;
                    if (ftype == CType::FUNCTION || ftype == CType::SEQ ||
                        ftype == CType::STRING || ftype == CType::ADT ||
                        ftype == CType::CHANNEL || ftype == CType::TUPLE)
                        typed_val = builder_->CreateIntToPtr(field_val,
                            PointerType::get(*context_, 0));
                    // Perceus DUP at field extraction — only fire when the
                    // runtime subtype confirms the field is heap (see the
                    // matching note in the heap-layout branch above).
                    bool runtime_heap = fi < scrutinee.subtypes.size() &&
                                        is_heap_type(scrutinee.subtypes[fi]);
                    if (runtime_heap && typed_val->getType()->isPointerTy())
                        emit_rc_inc(typed_val, ftype);
                    TypedValue bound{typed_val, ftype};
                    if (const auto* identity = concrete_field_identity(fi))
                        bound.semantic_subtypes = identity->arguments;
                    if (ftype == CType::ADT) {
                        bound.adt_type_name = concrete_field_adt_name(fi);
                        if (const auto* identity = concrete_field_identity(fi)) {
                            bound.adt_semantic_arguments = identity->arguments;
                            for (const auto& argument : identity->arguments) {
                                bound.adt_type_arguments.push_back(argument.type);
                                bound.adt_type_argument_names.push_back(argument.adt_name);
                            }
                        }
                    }
                    if (ctor_it->second.type_name == "Result" ||
                        ctor_it->second.type_name == "Option")
                        bound.boxed_heap = scrutinee.boxed_heap || is_heap_type(ftype);
                    named_values_[(*id)->name->value] = bound;
                }
            } else if (sub_pat->get_type() == AST_TUPLE_PATTERN) {
                bind_field_pattern(fi, field_val);
            }
        }
    }
    return true;
}

// Coerce a value to a target LLVM type for PHI node compatibility.
static Value* coerce_for_phi(Value* val, LType* target, IRBuilder<>& builder, LLVMContext& ctx) {
    auto* src = val->getType();
    if (src == target) return val;
    if (src->isVoidTy())
        return Constant::getNullValue(target);
    if (src->isIntegerTy() && target->isIntegerTy())
        return builder.CreateZExtOrTrunc(val, target);
    if (src->isPointerTy() && target->isIntegerTy())
        return builder.CreatePtrToInt(val, target);
    if (src->isIntegerTy() && target->isPointerTy())
        return builder.CreateIntToPtr(val, target);
    if (src->isFloatingPointTy() && target->isIntegerTy())
        return builder.CreateFPToSI(val, target);
    if (src->isIntegerTy() && target->isFloatingPointTy())
        return builder.CreateSIToFP(val, target);
    if (src->isStructTy() && target->isPointerTy()) {
        auto* alloca = builder.CreateAlloca(src);
        builder.CreateStore(val, alloca);
        return alloca;
    }
    if (src->isStructTy() && target->isIntegerTy()) {
        auto* alloca = builder.CreateAlloca(src);
        builder.CreateStore(val, alloca);
        return builder.CreatePtrToInt(alloca, target);
    }
    if (!src->isVoidTy() && target->isPointerTy()) {
        auto* alloca = builder.CreateAlloca(src);
        builder.CreateStore(val, alloca);
        return alloca;
    }
    return val;
}

static LType* common_phi_type(LType* a, LType* b, LLVMContext& ctx) {
    if (a == b) return a;
    if (a->isStructTy() || b->isStructTy())
        return PointerType::get(ctx, 0);
    if (a->isPointerTy() || b->isPointerTy())
        return PointerType::get(ctx, 0);
    if (a->isIntegerTy() && b->isIntegerTy()) {
        unsigned wa = a->getIntegerBitWidth(), wb = b->getIntegerBitWidth();
        return wa >= wb ? a : b;
    }
    if (a->isFloatingPointTy() || b->isFloatingPointTy())
        return a->isDoubleTy() || b->isDoubleTy()
            ? LType::getDoubleTy(ctx)
            : LType::getFloatTy(ctx);
    return LType::getInt64Ty(ctx);
}

Codegen::CasePatternAnalysis Codegen::analyze_case_patterns(CaseExpr* node) const {
    CasePatternAnalysis result;
    if (!node) return result;
    const pattern_analysis::ConstructorCatalog constructors{
        .lookup = [this](std::string_view name) -> std::optional<pattern_analysis::ConstructorInfo> {
            const auto it = types_.adt_constructors.find(std::string(name));
            if (it == types_.adt_constructors.end()) return std::nullopt;
            return pattern_analysis::ConstructorInfo{it->second.type_name};
        },
        .members = [this](std::string_view family) {
            std::vector<std::string> members;
            for (const auto& [name, info] : types_.adt_constructors)
                if (info.type_name == family) members.push_back(name);
            return members;
        },
    };
    auto analysis = pattern_analysis::analyze_case(*node, constructors);
    result.unreachable_clauses = std::move(analysis.unreachable_clauses);
    if (analysis.incomplete)
        result.incomplete = FiniteCaseCoverage{std::move(analysis.incomplete->family), std::move(analysis.incomplete->missing)};
    return result;

    std::string adt_type_name;
    bool has_wildcard = false;
    std::unordered_set<std::string> covered_ctors;
    std::unordered_set<std::string> covered_atoms;
    bool saw_bool = false, saw_unit = false;
    std::vector<PatternNode*> prior_unguarded;
    auto identify_adt = [&](const auto& self, PatternNode* pat) -> void {
        if (!pat || !adt_type_name.empty()) return;
        std::string ctor_name;
        if (pat->get_type() == AST_CONSTRUCTOR_PATTERN)
            ctor_name = static_cast<ConstructorPattern*>(pat)->constructor_name;
        else if (pat->get_type() == AST_RECORD_PATTERN)
            ctor_name = static_cast<RecordPattern*>(pat)->recordType;
        else if (pat->get_type() == AST_OR_PATTERN) {
            for (const auto& alternative : static_cast<OrPattern*>(pat)->patterns)
                self(self, alternative.get());
            return;
        }
        if (!ctor_name.empty()) {
            auto it = types_.adt_constructors.find(ctor_name);
            if (it != types_.adt_constructors.end()) adt_type_name = it->second.type_name;
        }
    };
    auto collect = [&](const auto& self, PatternNode* pat) -> void {
        if (!pat || has_wildcard) return;
        if (pat->get_type() == AST_CONSTRUCTOR_PATTERN) {
            auto* cp = static_cast<ConstructorPattern*>(pat);
            covered_ctors.insert(cp->constructor_name);
            if (cp->constructor_name == "True" || cp->constructor_name == "False") {
                saw_bool = true;
                covered_atoms.insert(cp->constructor_name);
            }
            if (adt_type_name.empty()) {
                auto it = types_.adt_constructors.find(cp->constructor_name);
                if (it != types_.adt_constructors.end()) adt_type_name = it->second.type_name;
            }
        } else if (pat->get_type() == AST_RECORD_PATTERN) {
            auto* rp = static_cast<RecordPattern*>(pat);
            covered_ctors.insert(rp->recordType);
            if (adt_type_name.empty()) {
                auto it = types_.adt_constructors.find(rp->recordType);
                if (it != types_.adt_constructors.end()) adt_type_name = it->second.type_name;
            }
        } else if (pat->get_type() == AST_UNDERSCORE_PATTERN) {
            has_wildcard = true;
        } else if (pat->get_type() == AST_PATTERN_VALUE) {
            auto* pv = static_cast<PatternValue*>(pat);
            if (std::get_if<IdentifierExpr*>(&pv->expr)) {
                has_wildcard = true;
            } else if (auto* literal = std::get_if<LiteralExpr<void*>*>(&pv->expr)) {
                auto literal_type = (*literal)->get_type();
                if (literal_type == AST_TRUE_LITERAL_EXPR) { saw_bool = true; covered_atoms.insert("True"); }
                if (literal_type == AST_FALSE_LITERAL_EXPR) { saw_bool = true; covered_atoms.insert("False"); }
                if (literal_type == AST_UNIT_EXPR) { saw_unit = true; covered_atoms.insert("()"); }
            }
        } else if (pat->get_type() == AST_OR_PATTERN) {
            for (const auto& alternative : static_cast<OrPattern*>(pat)->patterns)
                self(self, alternative.get());
        }
    };

    for (size_t index = 0; index < node->clauses.size(); ++index) {
        auto* clause = node->clauses[index];
        if (!clause) continue;
        identify_adt(identify_adt, clause->pattern);
        if (!clause->guard) {
            bool covered_by_prior = false;
            if (clause->pattern->get_type() == AST_OR_PATTERN) {
                covered_by_prior = true;
                for (const auto& alternative : static_cast<OrPattern*>(clause->pattern)->patterns) {
                    bool covered = false;
                    for (auto* previous : prior_unguarded)
                        covered = covered || pattern_analysis::covers(previous, alternative.get());
                    covered_by_prior = covered_by_prior && covered;
                }
            } else {
                for (auto* previous : prior_unguarded)
                    covered_by_prior = covered_by_prior || pattern_analysis::covers(previous, clause->pattern);
            }
            bool before_wildcard = has_wildcard;
            collect(collect, clause->pattern);
            if (covered_by_prior || before_wildcard)
                result.unreachable_clauses.push_back(index);
            prior_unguarded.push_back(clause->pattern);
        }
    }
    if (has_wildcard) return result;

    std::vector<std::string> missing;
    if (!adt_type_name.empty()) {
        for (const auto& [name, info] : types_.adt_constructors)
            if (info.type_name == adt_type_name && covered_ctors.count(name) == 0)
                missing.push_back(name);
    } else if (saw_bool) {
        adt_type_name = "Bool";
        for (const auto& value : {"False", "True"})
            if (!covered_atoms.count(value)) missing.push_back(value);
    } else if (saw_unit && !covered_atoms.count("()")) {
        adt_type_name = "Unit";
        missing.push_back("()");
    }
    std::sort(missing.begin(), missing.end());
    if (!adt_type_name.empty() && !missing.empty())
        result.incomplete = FiniteCaseCoverage{std::move(adt_type_name), std::move(missing)};
    return result;
}

std::optional<Codegen::FiniteCaseCoverage> Codegen::finite_case_coverage(CaseExpr* node) const {
    return analyze_case_patterns(node).incomplete;
}

TypedValue Codegen::codegen_case(CaseExpr* node) {
    set_debug_loc(node->source_context);
    auto scrutinee = auto_await(codegen(node->expr));
    if (!scrutinee) return {};

    // If scrutinee is not SUM but patterns are typed patterns, auto-box it
    if (scrutinee.type != CType::SUM && !node->clauses.empty()) {
        for (auto* clause : node->clauses) {
            if (clause->pattern->get_type() == AST_TYPED_PATTERN) {
                scrutinee = box_as_sum(scrutinee);
                break;
            }
        }
    }

    // If scrutinee is an integer-encoded ADT and patterns are ADT constructors
    // or named record patterns,
    // switch to the heap-layout view before matching. This covers imported ADT
    // results, closure-returned ADTs, and annotated ADT parameters.
    if ((scrutinee.type == CType::INT ||
         (scrutinee.type == CType::ADT && scrutinee.val &&
          scrutinee.val->getType()->isIntegerTy())) &&
        !node->clauses.empty()) {
        auto* first_pat = node->clauses[0]->pattern;
        std::string ctor_name;
        if (first_pat->get_type() == AST_CONSTRUCTOR_PATTERN)
            ctor_name = static_cast<ConstructorPattern*>(first_pat)->constructor_name;
        else if (first_pat->get_type() == AST_RECORD_PATTERN)
            ctor_name = static_cast<RecordPattern*>(first_pat)->recordType;
        if (!ctor_name.empty()) {
            auto ctor_it = types_.adt_constructors.find(ctor_name);
            if (ctor_it != types_.adt_constructors.end()) {
                if (!(ctor_it->second.max_arity == 0 && !ctor_it->second.is_recursive)) {
                    scrutinee.val = builder_->CreateIntToPtr(scrutinee.val,
                        PointerType::get(*context_, 0));
                    scrutinee.type = CType::ADT;
                    scrutinee.adt_type_name = ctor_it->second.type_name;
                }
            }
        }
    }

    // Exhaustiveness check for finite ADT scrutinees. This is deliberately a
    // warning: a catch-all remains optional unless the caller promotes
    // warnings with --Werror.
    if (diag_) {
        auto analysis = analyze_case_patterns(node);
        for (size_t index : analysis.unreachable_clauses) {
            if (index < node->clauses.size() && node->clauses[index])
                diag_->warning(node->clauses[index]->source_context,
                               "unreachable pattern: earlier unguarded arms already cover every value it can match",
                               compiler::WarningFlag::OverlappingPatterns);
        }
        if (auto coverage = analysis.incomplete) {
                std::string message = "non-exhaustive pattern match on " + coverage->adt_name +
                                      " — missing constructor" +
                                      (coverage->missing.size() == 1 ? " " : "s ");
                for (size_t i = 0; i < coverage->missing.size(); ++i) {
                    if (i) message += ", ";
                    message += coverage->missing[i];
                }
                diag_->warning(node->source_context, message,
                               compiler::WarningFlag::IncompletePatterns);
        }
    }

    auto fn = builder_->GetInsertBlock()->getParent();
    auto merge_bb = BasicBlock::Create(*context_, "case.end");
    std::vector<std::pair<TypedValue, BasicBlock*>> results;

    // Transfer reconciliation may need the sequence value in an arm which
    // does not itself use a head-tail pattern. Materialize the ABI pointer
    // before creating any arm blocks so it dominates every reconciliation
    // path, rather than materializing it only in the consuming arm.
    if (scrutinee.type == CType::SEQ && scrutinee.val &&
        scrutinee.val->getType()->isIntegerTy()) {
        scrutinee.val = builder_->CreateIntToPtr(scrutinee.val,
            PointerType::get(*context_, 0), "case.seq.ptr");
    }

    // A finite constructor match that covers every variant has no legitimate
    // fall-through edge after its final arm. Sending that impossible edge to
    // the value merge creates a predecessor which bypasses values bound by an
    // outer pattern (and therefore violates LLVM dominance during cleanup).
    std::string exhaustive_adt;
    std::unordered_set<std::string> covered_ctors;
    bool finite_constructor_match = !node->clauses.empty();
    for (auto* clause : node->clauses) {
        if (!clause || clause->guard || clause->pattern->get_type() != AST_CONSTRUCTOR_PATTERN) {
            finite_constructor_match = false;
            break;
        }
        auto* constructor = static_cast<ConstructorPattern*>(clause->pattern);
        auto it = types_.adt_constructors.find(constructor->constructor_name);
        if (it == types_.adt_constructors.end() ||
            (!exhaustive_adt.empty() && exhaustive_adt != it->second.type_name)) {
            finite_constructor_match = false;
            break;
        }
        exhaustive_adt = it->second.type_name;
        covered_ctors.insert(constructor->constructor_name);
    }
    if (finite_constructor_match) {
        for (const auto& [name, info] : types_.adt_constructors) {
            if (info.type_name == exhaustive_adt && !covered_ctors.count(name)) {
                finite_constructor_match = false;
                break;
            }
        }
    }
    BasicBlock* impossible_match_bb = finite_constructor_match
        ? BasicBlock::Create(*context_, "case.impossible", fn) : nullptr;

    // Wrap the arm loop in a transfer scope. Each arm is a branch; if
    // some arms transfer a seq (e.g. head-tail consume or passing the
    // scrutinee to a consumer) and others don't, the transfer_scope
    // exit emits compensating rc_decs in the non-transferring arms so
    // seq transfers at merge reflect "transferred on all live
    // paths" without leaking on the non-transfer paths. This is the
    // case-arm extension of the codegen_if per-branch scoping.
    transfer_scope_enter();

    for (size_t i = 0; i < node->clauses.size(); i++) {
        auto* clause = node->clauses[i];
        auto* pat = clause->pattern;
        const bool catch_all = !clause->guard &&
            (pat->get_type() == AST_UNDERSCORE_PATTERN ||
             (pat->get_type() == AST_PATTERN_VALUE &&
              std::get_if<IdentifierExpr*>(&static_cast<PatternValue*>(pat)->expr) != nullptr));
        auto arm_named_values = named_values_;
        auto body_bb = BasicBlock::Create(*context_, "case.body." + std::to_string(i), fn);
        auto next_bb = (!catch_all && i + 1 < node->clauses.size())
            ? BasicBlock::Create(*context_, "case.next." + std::to_string(i+1), fn)
            : (impossible_match_bb ? impossible_match_bb : merge_bb);

        bool body_inline = false;

        transfer_branch_begin();

        // Enter arm scope for drop tracking. Pattern-introduced heap-typed
        // bindings (currently just head-tail `rest`) accumulate here and
        // are rc_dec'd after the arm body is codegen'd.
        arm_drop_stack_.push_back({});

        if (pat->get_type() == AST_UNDERSCORE_PATTERN) {
            builder_->CreateBr(body_bb);
        } else if (pat->get_type() == AST_PATTERN_VALUE) {
            body_inline = codegen_pattern_value(static_cast<PatternValue*>(pat),
                                                 scrutinee, body_bb, next_bb);
        } else if (pat->get_type() == AST_HEAD_TAILS_PATTERN) {
            Value* seq_ptr = scrutinee.val;
            if (!seq_ptr->getType()->isPointerTy())
                seq_ptr = builder_->CreateIntToPtr(seq_ptr, PointerType::get(*context_, 0));
            body_inline = codegen_pattern_headtail(static_cast<HeadTailsPattern*>(pat),
                                                    node, clause, scrutinee, seq_ptr,
                                                    body_bb, next_bb);
        } else if (pat->get_type() == AST_SEQ_PATTERN) {
            body_inline = codegen_pattern_seq(static_cast<SeqPattern*>(pat),
                                               scrutinee, body_bb, next_bb);
        } else if (pat->get_type() == AST_TUPLE_PATTERN) {
            body_inline = codegen_pattern_tuple(static_cast<TuplePattern*>(pat),
                                                 scrutinee, body_bb, next_bb);
        } else if (pat->get_type() == AST_OR_PATTERN) {
            auto* op = static_cast<OrPattern*>(pat);
            // For each alternative in the or-pattern, test and branch to body_bb on match
            for (size_t oi = 0; oi < op->patterns.size(); oi++) {
                auto* alt = op->patterns[oi].get();
                auto alt_next = (oi + 1 < op->patterns.size())
                    ? BasicBlock::Create(*context_, "case.or." + std::to_string(i) + "." + std::to_string(oi+1), fn)
                    : next_bb;

                if (alt->get_type() == AST_UNDERSCORE_PATTERN) {
                    builder_->CreateBr(body_bb);
                } else if (alt->get_type() == AST_PATTERN_VALUE) {
                    auto* pv = static_cast<PatternValue*>(alt);
                    if (auto* sym = std::get_if<SymbolExpr*>(&pv->expr)) {
                        int64_t sym_id = intern_symbol((*sym)->value);
                        auto sym_val = ConstantInt::get(LType::getInt64Ty(*context_), sym_id);
                        auto cmp = builder_->CreateICmpEQ(scrutinee.val, sym_val);
                        builder_->CreateCondBr(cmp, body_bb, alt_next);
                    } else if (auto* lit = std::get_if<LiteralExpr<void*>*>(&pv->expr)) {
                        auto* an = reinterpret_cast<AstNode*>(*lit);
                        if (an->get_type() == AST_INTEGER_EXPR) {
                            auto* ie = static_cast<IntegerExpr*>(an);
                            auto mv = ConstantInt::get(LType::getInt64Ty(*context_), ie->value);
                            auto cmp = builder_->CreateICmpEQ(scrutinee.val, mv);
                            builder_->CreateCondBr(cmp, body_bb, alt_next);
                        } else {
                            builder_->CreateBr(body_bb);
                        }
                    } else if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                        named_values_[(*id)->name->value] = scrutinee;
                        builder_->CreateBr(body_bb);
                    } else {
                        builder_->CreateBr(body_bb);
                    }
                } else {
                    builder_->CreateBr(body_bb);
                }

                if (oi + 1 < op->patterns.size())
                    builder_->SetInsertPoint(alt_next);
            }
        } else if (pat->get_type() == AST_CONSTRUCTOR_PATTERN) {
            body_inline = codegen_pattern_constructor(static_cast<ConstructorPattern*>(pat),
                                                       scrutinee, body_bb, next_bb);
        } else if (pat->get_type() == AST_RECORD_PATTERN) {
            // Named field pattern: Person { name = n, age = a }
            auto* rp = static_cast<RecordPattern*>(pat);
            auto ctor_it = types_.adt_constructors.find(rp->recordType);
            if (ctor_it != types_.adt_constructors.end()) {
                int8_t tag = static_cast<int8_t>(ctor_it->second.tag);
                auto tag_ty = LType::getInt64Ty(*context_);
                auto i64_ty = LType::getInt64Ty(*context_);
                using FieldShape = AdtInfo::FieldShape;
                std::function<void(PatternNode*, Value*, const FieldShape&)> bind_field;
                bind_field = [&](PatternNode* field_pattern, Value* raw_value,
                                 const FieldShape& shape) {
                    if (!field_pattern || !raw_value) return;
                    if (shape.type == CType::TUPLE &&
                        field_pattern->get_type() == AST_TUPLE_PATTERN) {
                        auto* tuple_pattern = static_cast<TuplePattern*>(field_pattern);
                        Value* tuple_ptr = raw_value;
                        if (tuple_ptr->getType()->isIntegerTy())
                            tuple_ptr = builder_->CreateIntToPtr(tuple_ptr,
                                PointerType::get(*context_, 0), "record_tuple_field_ptr");
                        for (size_t element = 0;
                             element < tuple_pattern->patterns.size() &&
                             element < shape.tuple_elements.size(); ++element) {
                            auto* slot = builder_->CreateGEP(i64_ty, tuple_ptr,
                                {ConstantInt::get(i64_ty, element + 2)}, "record_tuple_gep");
                            auto* value = builder_->CreateLoad(i64_ty, slot, "record_tuple_element");
                            bind_field(tuple_pattern->patterns[element], value,
                                       shape.tuple_elements[element]);
                        }
                        return;
                    }
                    if (field_pattern->get_type() != AST_PATTERN_VALUE) return;
                    auto* value_pattern = static_cast<PatternValue*>(field_pattern);
                    auto* identifier = std::get_if<IdentifierExpr*>(&value_pattern->expr);
                    if (!identifier) return;
                    Value* typed_value = raw_value;
                    if (shape.type == CType::FLOAT && raw_value->getType()->isIntegerTy())
                        typed_value = builder_->CreateBitCast(raw_value, LType::getDoubleTy(*context_));
                    else if (is_heap_type(shape.type) && raw_value->getType()->isIntegerTy())
                        typed_value = builder_->CreateIntToPtr(raw_value,
                            PointerType::get(*context_, 0), "record_field_ptr");
                    TypedValue bound{typed_value, shape.type};
                    if (shape.type == CType::FUNCTION) {
                        bound.subtypes = {shape.function_return_type};
                        bound.adt_type_name = shape.function_return_adt_name;
                    } else if (shape.type == CType::TUPLE) {
                        for (const auto& element : shape.tuple_elements)
                            bound.subtypes.push_back(element.type);
                    }
                    named_values_[(*identifier)->name->value] = bound;
                };

                bool use_heap_layout = ctor_it->second.is_recursive ||
                    (scrutinee.val && scrutinee.val->getType()->isPointerTy());
                if (use_heap_layout) {
                    auto scr_tag = builder_->CreateCall(rt_.adt_get_tag_, {scrutinee.val});
                    builder_->CreateCondBr(builder_->CreateICmpEQ(scr_tag, ConstantInt::get(tag_ty, tag)),
                                           body_bb, next_bb);
                    builder_->SetInsertPoint(body_bb);
                    for (auto& [name_expr, pattern] : rp->items) {
                        if (!name_expr) continue;
                        for (size_t fi = 0; fi < ctor_it->second.field_names.size(); fi++) {
                            if (ctor_it->second.field_names[fi] == name_expr->value) {
                                auto val = builder_->CreateCall(rt_.adt_get_field_,
                                    {scrutinee.val, ConstantInt::get(i64_ty, fi)});
                                FieldShape fallback;
                                if (fi < ctor_it->second.field_types.size())
                                    fallback.type = ctor_it->second.field_types[fi];
                                if (fi < ctor_it->second.field_fn_return_types.size())
                                    fallback.function_return_type = ctor_it->second.field_fn_return_types[fi];
                                if (fi < ctor_it->second.field_fn_return_adt_names.size())
                                    fallback.function_return_adt_name = ctor_it->second.field_fn_return_adt_names[fi];
                                const auto& shape = fi < ctor_it->second.field_shapes.size()
                                    ? ctor_it->second.field_shapes[fi] : fallback;
                                bind_field(pattern, val, shape);
                                break;
                            }
                        }
                    }
                } else {
                    auto scr_tag = builder_->CreateExtractValue(scrutinee.val, {0});
                    builder_->CreateCondBr(builder_->CreateICmpEQ(scr_tag, ConstantInt::get(tag_ty, tag)),
                                           body_bb, next_bb);
                    builder_->SetInsertPoint(body_bb);
                    for (auto& [name_expr, pattern] : rp->items) {
                        if (!name_expr) continue;
                        for (size_t fi = 0; fi < ctor_it->second.field_names.size(); fi++) {
                            if (ctor_it->second.field_names[fi] == name_expr->value) {
                                auto val = builder_->CreateExtractValue(scrutinee.val, {(unsigned)(fi + 1)});
                                FieldShape fallback;
                                if (fi < ctor_it->second.field_types.size())
                                    fallback.type = ctor_it->second.field_types[fi];
                                if (fi < ctor_it->second.field_fn_return_types.size())
                                    fallback.function_return_type = ctor_it->second.field_fn_return_types[fi];
                                if (fi < ctor_it->second.field_fn_return_adt_names.size())
                                    fallback.function_return_adt_name = ctor_it->second.field_fn_return_adt_names[fi];
                                const auto& shape = fi < ctor_it->second.field_shapes.size()
                                    ? ctor_it->second.field_shapes[fi] : fallback;
                                bind_field(pattern, val, shape);
                                break;
                            }
                        }
                    }
                }
                body_inline = true;
            } else {
                builder_->CreateBr(body_bb);
            }
        } else if (pat->get_type() == AST_TYPED_PATTERN) {
            body_inline = codegen_pattern_typed(static_cast<TypedPattern*>(pat),
                                                 scrutinee, body_bb, next_bb);
        } else {
            builder_->CreateBr(body_bb);
        }

        if (!body_inline) builder_->SetInsertPoint(body_bb);

        // Guard expression: pattern | guard -> body
        if (clause->guard) {
            auto guard_val = codegen(clause->guard);
            if (!guard_val) return {};
            Value* cond = guard_val.val;
            if (guard_val.type == CType::INT)
                cond = builder_->CreateICmpNE(cond, ConstantInt::get(LType::getInt64Ty(*context_), 0));
            auto guarded_bb = BasicBlock::Create(*context_, "case.guarded." + std::to_string(i), fn);
            builder_->CreateCondBr(cond, guarded_bb, next_bb);
            builder_->SetInsertPoint(guarded_bb);
        }

        auto body_tv = codegen(clause->body);
        if (!body_tv) return {};
        // Emit arm-scope drops for pattern-bound heap values BEFORE the arm
        // branches to the merge block. If the body value is one of the
        // scheduled drops (e.g., `[h|t] -> t`), skip that drop — the value
        // escapes the arm and the caller becomes its owner. Also skip
        // values whose seq ownership was transferred to a consumer during
        // the body (user-defined call or nested pattern-match consume).
        if (!arm_drop_stack_.empty()) {
            for (auto& [val, ct] : arm_drop_stack_.back()) {
                if (val == body_tv.val) continue;
                if (ct == CType::SEQ &&
                    is_transferred(val, TransferDomain::Seq)) continue;
                emit_rc_dec(val, ct);
            }
            arm_drop_stack_.pop_back();
        }
        named_values_ = std::move(arm_named_values);
        BasicBlock* arm_exit = current_block_terminated()
            ? nullptr : builder_->GetInsertBlock();
        if (arm_exit && body_tv.type == CType::SEQ)
            mark_transferred(body_tv.val, TransferDomain::Seq);
        if (arm_exit) builder_->CreateBr(merge_bb);
        results.push_back({body_tv, arm_exit ? arm_exit : builder_->GetInsertBlock()});
        transfer_branch_end(arm_exit);

        // Later arms cannot be reached after an unguarded catch-all.  Besides
        // avoiding useless IR, stopping here keeps dead blocks out of PHI
        // construction (which otherwise dereferences a block without a
        // terminator). Diagnostics were emitted before code generation.
        if (catch_all) break;

        if (i + 1 < node->clauses.size() && next_bb != merge_bb)
            builder_->SetInsertPoint(next_bb);
    }

    transfer_scope_exit();

    if (impossible_match_bb) {
        builder_->SetInsertPoint(impossible_match_bb);
        builder_->CreateUnreachable();
    }

    fn->insert(fn->end(), merge_bb);
    builder_->SetInsertPoint(merge_bb);
    if (results.empty()) return {};

    unsigned pred_count = 0;
    for (auto it = pred_begin(merge_bb); it != pred_end(merge_bb); ++it) pred_count++;

    // Determine common PHI type across all branches
    LType* phi_type = results[0].first.val->getType();
    for (size_t ri = 1; ri < results.size(); ri++)
        phi_type = common_phi_type(phi_type, results[ri].first.val->getType(), *context_);

    auto phi = builder_->CreatePHI(phi_type, pred_count);
    for (auto& [tv, bb] : results) {
        Value* incoming = tv.val;
        if (incoming->getType() != phi_type) {
            // Insert coercion before the branch terminator in the source block
            builder_->SetInsertPoint(block_terminator(bb));
            // A non-recursive ADT is represented locally as a flat struct,
            // while a recursive call returning the same ADT is a heap pointer.
            // The case result must use the runtime ADT representation, not an
            // alloca of the flat struct: `adt_get_tag` / `adt_get_field` expect
            // the runtime header and payload layout.
            if (tv.type == CType::ADT && incoming->getType()->isStructTy() &&
                phi_type->isPointerTy()) {
                auto* struct_type = cast<StructType>(incoming->getType());
                auto* i64_type = LType::getInt64Ty(*context_);
                auto* tag = builder_->CreateExtractValue(incoming, {0}, "case_adt_tag");
                if (tag->getType() != i64_type)
                    tag = builder_->CreateZExtOrTrunc(tag, i64_type);
                auto* boxed = builder_->CreateCall(
                    rt_.adt_alloc_,
                    {tag, ConstantInt::get(i64_type, struct_type->getNumElements() - 1)},
                    "case_adt_box");
                int64_t heap_mask = 0;
                for (unsigned field = 1; field < struct_type->getNumElements(); ++field) {
                    Value* value = builder_->CreateExtractValue(incoming, {field});
                    if (value->getType()->isPointerTy())
                        value = builder_->CreatePtrToInt(value, i64_type);
                    else if (value->getType()->isIntegerTy() && value->getType() != i64_type)
                        value = builder_->CreateZExtOrTrunc(value, i64_type);
                    else if (value->getType()->isDoubleTy())
                        value = builder_->CreateBitCast(value, i64_type);
                    builder_->CreateCall(rt_.adt_set_field_,
                        {boxed, ConstantInt::get(i64_type, field - 1), value});
                    size_t subtype = field - 1;
                    if (subtype < tv.subtypes.size() && is_heap_type(tv.subtypes[subtype]))
                        heap_mask |= (int64_t{1} << subtype);
                }
                if (heap_mask != 0)
                    builder_->CreateCall(rt_.adt_set_heap_mask_,
                        {boxed, ConstantInt::get(i64_type, heap_mask)});
                incoming = boxed;
            } else {
                incoming = coerce_for_phi(incoming, phi_type, *builder_, *context_);
            }
            builder_->SetInsertPoint(merge_bb);
        }
        phi->addIncoming(incoming, bb);
    }
    for (auto it = pred_begin(merge_bb); it != pred_end(merge_bb); ++it) {
        bool found = false;
        for (auto& [tv, bb] : results) if (bb == *it) { found = true; break; }
        if (!found) phi->addIncoming(Constant::getNullValue(phi_type), *it);
    }
    return {phi, results[0].first.type, results[0].first.subtypes};
}

// ===== Sum Type Support =====

int Codegen::ctype_tag(CType ct) {
    switch (ct) {
        case CType::INT:      return 0;
        case CType::FLOAT:    return 1;
        case CType::BOOL:     return 2;
        case CType::STRING:   return 3;
        case CType::SEQ:      return 4;
        case CType::TUPLE:    return 5;
        case CType::UNIT:     return 6;
        case CType::FUNCTION: return 7;
        case CType::SYMBOL:   return 8;
        case CType::PROMISE:  return 9;
        case CType::SET:      return 10;
        case CType::DICT:     return 11;
        case CType::ADT:      return 12;
        case CType::BYTE_ARRAY:    return 13;
        case CType::SUM:      return 14;
        case CType::RECORD:
        case CType::INT_ARRAY:
        case CType::FLOAT_ARRAY:
        case CType::CHANNEL:
            return 0; // These types are not currently represented in sum payload tags.
    }
    return 0;
}

CType Codegen::type_name_to_ctype(const std::string& name) {
    if (name == "Int")      return CType::INT;
    if (name == "Float")    return CType::FLOAT;
    if (name == "Bool")     return CType::BOOL;
    if (name == "String")   return CType::STRING;
    if (name == "Seq")      return CType::SEQ;
    if (name == "Tuple")    return CType::TUPLE;
    if (name == "Unit")     return CType::UNIT;
    if (name == "Function") return CType::FUNCTION;
    if (name == "Symbol")   return CType::SYMBOL;
    if (name == "Promise")  return CType::PROMISE;
    if (name == "Set")      return CType::SET;
    if (name == "Dict")     return CType::DICT;
    if (name == "ByteArray")    return CType::BYTE_ARRAY;
    if (name == "IntArray")     return CType::INT_ARRAY;
    if (name == "FloatArray")   return CType::FLOAT_ARRAY;
    return CType::INT; // fallback
}

TypedValue Codegen::box_as_sum(const TypedValue& value) {
    auto i64_ty = LType::getInt64Ty(*context_);
    int tag = ctype_tag(value.type);

    // Allocate a 2-element tuple: [tag, value]
    auto* tuple_ptr = builder_->CreateCall(rt_.tuple_alloc_,
        {ConstantInt::get(i64_ty, 2)}, "sum");

    // Set element 0: type tag
    builder_->CreateCall(rt_.tuple_set_,
        {tuple_ptr, ConstantInt::get(i64_ty, 0), ConstantInt::get(i64_ty, tag)});

    // Set element 1: actual value (normalized to i64)
    Value* val_i64 = value.val;
    if (val_i64->getType()->isPointerTy())
        val_i64 = builder_->CreatePtrToInt(val_i64, i64_ty);
    else if (val_i64->getType()->isDoubleTy())
        val_i64 = builder_->CreateBitCast(val_i64, i64_ty);
    else if (val_i64->getType()->isIntegerTy() && val_i64->getType() != i64_ty)
        val_i64 = builder_->CreateZExtOrTrunc(val_i64, i64_ty);
    builder_->CreateCall(rt_.tuple_set_,
        {tuple_ptr, ConstantInt::get(i64_ty, 1), val_i64});

    // Set heap mask if the value is heap-allocated (bit 1)
    if (is_heap_type(value.type))
        builder_->CreateCall(rt_.tuple_set_heap_mask_,
            {tuple_ptr, ConstantInt::get(i64_ty, 2)}); // bit 1 = position 1

    auto* sum_i64 = builder_->CreatePtrToInt(tuple_ptr, i64_ty, "sum_i64");
    return {sum_i64, CType::SUM, {value.type}};
}

bool Codegen::codegen_pattern_typed(TypedPattern* pat, const TypedValue& scrutinee,
                                     BasicBlock* body_bb, BasicBlock* next_bb) {
    auto i64_ty = LType::getInt64Ty(*context_);

    // The scrutinee is a sum value (2-tuple). Extract tag and compare.
    Value* tuple_ptr = scrutinee.val;
    if (tuple_ptr->getType()->isIntegerTy())
        tuple_ptr = builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0));

    // Read element 0: type tag (tuple header is 2 i64s, so element at offset 2)
    auto* tag_gep = builder_->CreateGEP(i64_ty, tuple_ptr,
        {ConstantInt::get(i64_ty, 2)}, "sum_tag_gep");
    auto* tag_val = builder_->CreateLoad(i64_ty, tag_gep, "sum_tag");

    // Compare tag with expected type
    CType expected_ct = type_name_to_ctype(pat->type_name);
    int expected_tag = ctype_tag(expected_ct);
    auto* cmp = builder_->CreateICmpEQ(tag_val,
        ConstantInt::get(i64_ty, expected_tag), "sum_tag_match");
    builder_->CreateCondBr(cmp, body_bb, next_bb);

    // In the body block, extract the value and bind it
    builder_->SetInsertPoint(body_bb);
    auto* val_gep = builder_->CreateGEP(i64_ty, tuple_ptr,
        {ConstantInt::get(i64_ty, 3)}, "sum_val_gep");
    auto* raw_val = builder_->CreateLoad(i64_ty, val_gep, "sum_val");

    // Convert raw i64 back to the appropriate type
    Value* typed_val = raw_val;
    if (expected_ct == CType::FLOAT)
        typed_val = builder_->CreateBitCast(raw_val, LType::getDoubleTy(*context_));
    else if (expected_ct == CType::BOOL)
        typed_val = builder_->CreateTrunc(raw_val, LType::getInt1Ty(*context_));
    else if (expected_ct == CType::STRING || expected_ct == CType::FUNCTION ||
             expected_ct == CType::BYTE_ARRAY || expected_ct == CType::PROMISE)
        typed_val = builder_->CreateIntToPtr(raw_val, PointerType::get(*context_, 0));
    else if (expected_ct == CType::SEQ || expected_ct == CType::SET || expected_ct == CType::DICT)
        typed_val = builder_->CreateIntToPtr(raw_val, PointerType::get(*context_, 0));

    named_values_[pat->binding_name] = {typed_val, expected_ct};
    return true; // already positioned in body_bb
}

} // namespace yona::compiler::codegen
