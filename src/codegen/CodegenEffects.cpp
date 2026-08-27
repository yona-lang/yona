///
/// Codegen — Algebraic Effect System
///
/// `perform Effect.op args` invokes an effect operation.
/// `handle body with clauses end` installs effect handlers.
///
/// Handlers are direct LLVM functions: (op_args..., resume_fn_ptr) -> i64.
/// Resume is a raw function pointer (i64 (*)(i64)) passed to the handler.
/// Calling `resume value` does an indirect call through the pointer.
///

#include "Codegen.h"
#include <string>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

namespace yona::compiler::codegen {
using namespace llvm;
using LType = llvm::Type;

/// Codegen for `perform Effect.op args`
TypedValue Codegen::codegen_perform(PerformExpr* node) {
    set_debug_loc(node->source_context);
    auto i64_ty = LType::getInt64Ty(*context_);

    // Built-in Cancel.check effect: check group cancellation flag
    if (node->effect_name == "Cancel" && node->operation_name == "check") {
        if (current_group_) {
            auto* fn = builder_->GetInsertBlock()->getParent();
            auto* cancelled = builder_->CreateCall(rt_.group_is_cancelled_, {current_group_}, "cancelled");
            auto* is_cancelled = builder_->CreateICmpNE(cancelled,
                ConstantInt::get(i64_ty, 0), "is_cancelled");
            auto* cancel_bb = BasicBlock::Create(*context_, "cancel", fn);
            auto* continue_bb = BasicBlock::Create(*context_, "continue", fn);
            builder_->CreateCondBr(is_cancelled, cancel_bb, continue_bb);
            builder_->SetInsertPoint(cancel_bb);
            // Raise :Cancelled
            int64_t sym_id = intern_symbol("Cancelled");
            auto* msg = builder_->CreateGlobalString("task cancelled", "cancel_msg");
            builder_->CreateCall(rt_.raise_, {ConstantInt::get(i64_ty, sym_id), msg});
            builder_->CreateUnreachable();
            builder_->SetInsertPoint(continue_bb);
        }
        return {ConstantInt::get(i64_ty, 0), CType::UNIT};
    }

    // Find handler
    std::string op_key = node->effect_name + "." + node->operation_name;
    Function* handler_fn = nullptr;
    for (auto it = handler_stack_.rbegin(); it != handler_stack_.rend(); ++it) {
        auto h = it->handler_closures.find(op_key);
        if (h != it->handler_closures.end()) {
            handler_fn = cast<Function>(h->second);
            break;
        }
    }
    if (!handler_fn) {
        // Module-export compile of effectful GENFN (effect rows on the
        // arrow / `.yonai`) has no lexical `handle`. Emit a runtime raise
        // so the precompiled object is well-formed; importers remonomorphize
        // the GENFN body inside a user `handle`, where handler_stack_ binds.
        if (compiling_unhandled_perform_ok_ && rt_.raise_) {
            int64_t sym_id = intern_symbol("UnhandledEffect");
            std::string msg_str = "unhandled effect operation: " + op_key;
            auto* msg = builder_->CreateGlobalString(msg_str, "unhandled_effect_msg");
            builder_->CreateCall(rt_.raise_, {ConstantInt::get(i64_ty, sym_id), msg});
            /* raise is noreturn; keep a dummy value so case/PHI CFGs stay valid. */
            return {ConstantInt::get(i64_ty, 0), CType::UNIT};
        }
        report_error(node->source_context, "unhandled effect operation: " + op_key);
        return {};
    }

    // Evaluate args (skip unit)
    std::vector<Value*> call_args;
    for (auto* arg : node->args) {
        auto tv = codegen(arg);
        if (!tv) return {};
        if (tv.type == CType::UNIT) continue;
        Value* v = tv.val;
        if (v->getType()->isPointerTy()) v = builder_->CreatePtrToInt(v, i64_ty);
        call_args.push_back(v);
    }

    // Identity continuation
    auto* id_type = llvm::FunctionType::get(i64_ty, {i64_ty}, false);
    auto* id_fn = Function::Create(id_type, Function::InternalLinkage,
                                    "resume_id_" + std::to_string(lambda_counter_++), module_.get());
    {
        auto sb = builder_->GetInsertBlock(); auto sp = builder_->GetInsertPoint();
        builder_->SetInsertPoint(BasicBlock::Create(*context_, "entry", id_fn));
        builder_->CreateRet(&*id_fn->arg_begin());
        builder_->SetInsertPoint(sb, sp);
    }

    call_args.push_back(id_fn);
    return {builder_->CreateCall(handler_fn, call_args, "perform"), CType::INT};
}

/// Codegen for `handle body with clauses end`
TypedValue Codegen::codegen_handle(HandleExpr* node) {
    set_debug_loc(node->source_context);
    auto i64_ty = LType::getInt64Ty(*context_);
    auto ptr_ty = PointerType::get(*context_, 0);

    HandlerContext ctx;

    for (auto* clause : node->clauses) {
        if (clause->is_return_clause) continue;
        std::string op_key = clause->effect_name + "." + clause->operation_name;

        // Handler signature: (op_args..., resume_fn_ptr) -> i64
        std::vector<LType*> params;
        for (size_t i = 0; i < clause->arg_names.size(); i++) params.push_back(i64_ty);
        params.push_back(ptr_ty); // resume: i64 (*)(i64)

        auto* fn = Function::Create(
            llvm::FunctionType::get(i64_ty, params, false),
            Function::InternalLinkage,
            "handler_" + op_key + "_" + std::to_string(lambda_counter_++),
            module_.get());

        auto sb = builder_->GetInsertBlock(); auto sp = builder_->GetInsertPoint();
        auto saved = named_values_;

        builder_->SetInsertPoint(BasicBlock::Create(*context_, "entry", fn));
        auto arg_it = fn->arg_begin();

        for (auto& name : clause->arg_names) {
            named_values_[name] = {&*arg_it, CType::INT};
            ++arg_it;
        }

        // Resume is a raw function pointer — bind as FUNCTION so
        // `resume 42` goes through the indirect call path in codegen_apply.
        named_values_[clause->resume_name] = {&*arg_it, CType::FUNCTION, {CType::INT}};
        effect_resume_names_.insert(clause->resume_name);

        auto result = codegen(clause->body);
        if (!current_block_terminated()) {
            Value* rv = result ? result.val : ConstantInt::get(i64_ty, 0);
            if (rv->getType()->isPointerTy()) rv = builder_->CreatePtrToInt(rv, i64_ty);
            else if (rv->getType() != i64_ty && rv->getType()->isIntegerTy())
                rv = builder_->CreateZExtOrTrunc(rv, i64_ty);
            builder_->CreateRet(rv);
        }

        named_values_ = saved;
        effect_resume_names_.erase(clause->resume_name);
        builder_->SetInsertPoint(sb, sp);
        ctx.handler_closures[op_key] = fn;
    }

    handler_stack_.push_back(ctx);
    auto body_result = codegen(node->body);
    handler_stack_.pop_back();

    for (auto* clause : node->clauses) {
        if (clause->is_return_clause && body_result) {
            auto saved = named_values_;
            named_values_[clause->return_binding] = body_result;
            body_result = codegen(clause->body);
            named_values_ = saved;
            break;
        }
    }
    return body_result;
}

} // namespace yona::compiler::codegen
