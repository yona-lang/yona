//
// Codegen — Function application (call sites)
//
// Apply-chain flattening, argument evaluation, partial/curry apply,
// direct/extern/higher-order call emission, auto-await, and the
// codegen_apply entry point itself.
//
// Split out from CodegenFunction.cpp which remains responsible for
// function *definition*, free-var analysis, and compile_function.
//

#include "Codegen.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <iostream>

namespace yona::compiler::codegen {

// Closure layout constants — must match CodegenFunction.cpp / compiled_runtime.c
static constexpr int CLOSURE_FIELD_FN = 0;      // fn_ptr
static constexpr int CLOSURE_FIELD_ARITY = 2;   // arity
static constexpr int CLOSURE_HDR_SIZE = 5;       // fn_ptr, ret_type, arity, num_captures, heap_mask

using namespace llvm;
using LType = llvm::Type;

// ===== codegen_apply helpers =====

Codegen::ApplyChain Codegen::flatten_apply_chain(ApplyExpr* node) {
    ApplyChain result;
    ApplyExpr* cur = node;
    while (cur) {
        result.chain.push_back(cur);
        if (auto* nc = dynamic_cast<NameCall*>(cur->call)) {
            result.fn_name = nc->name->value;
            break;
        } else if (auto* mc = dynamic_cast<ModuleCall*>(cur->call)) {
            // FQN call: Std\List::map — auto-load module interface
            result.fn_name = mc->funName->value;
            if (auto* fqn = std::get_if<FqnExpr*>(&mc->fqn)) {
                auto [fqn_str, fqn_path] = build_fqn_path(*fqn);
                result.module_fqn = fqn_str;
                load_module_interface(fqn_path);
                register_import(fqn_str, result.fn_name, result.fn_name);
            }
            break;
        } else if (auto* ec = dynamic_cast<ExprCall*>(cur->call)) {
            if (auto* inner = dynamic_cast<ApplyExpr*>(ec->expr)) cur = inner;
            else break;
        } else break;
    }
    return result;
}

Codegen::EvaluatedArgs Codegen::evaluate_apply_args(const std::vector<ApplyExpr*>& chain) {
    EvaluatedArgs result;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        for (auto& a : (*it)->args) {
            last_lambda_name_.clear();
            TypedValue tv;
            if (std::holds_alternative<ExprNode*>(a)) tv = codegen(std::get<ExprNode*>(a));
            else tv = codegen(std::get<ValueExpr*>(a));
            // FUNCTION args may have nullptr val (deferred compilation) — that's OK
            if (tv.type != CType::FUNCTION && !tv) { result.all_args.clear(); return result; }
            // Auto-await PROMISE args (functions expect concrete values)
            if (tv.type == CType::PROMISE) tv = auto_await(tv);
            result.all_args.push_back(tv);
            result.arg_lambda_names.push_back(last_lambda_name_);
        }
    }
    return result;
}

void Codegen::precompile_function_args(EvaluatedArgs& args) {
    for (size_t ai = 0; ai < args.all_args.size(); ai++) {
        if (args.all_args[ai].type == CType::FUNCTION && !args.all_args[ai].val && !args.arg_lambda_names[ai].empty()) {
            auto& lname = args.arg_lambda_names[ai];
            auto def_it = deferred_functions_.find(lname);
            if (def_it != deferred_functions_.end()) {
                // Compile with INT args as default type hint
                std::vector<TypedValue> hint_args;
                for (size_t pi = 0; pi < def_it->second.param_names.size(); pi++)
                    hint_args.push_back({ConstantInt::get(LType::getInt64Ty(*context_), 0), CType::INT});
                auto cf = compile_function(lname, def_it->second, hint_args);
                args.all_args[ai] = {cf.fn, CType::FUNCTION, {cf.return_type}};
            }
        }
    }
}

void Codegen::wrap_function_args_in_closures(std::vector<TypedValue>& all_args) {
    for (size_t ai = 0; ai < all_args.size(); ai++) {
        if (all_args[ai].type == CType::FUNCTION && all_args[ai].val && isa<Function>(all_args[ai].val)) {
            // Ensure subtypes have return type info
            if (all_args[ai].subtypes.empty()) {
                auto cf_it = compiled_functions_.find(cast<Function>(all_args[ai].val)->getName().str());
                if (cf_it != compiled_functions_.end())
                    all_args[ai].subtypes = {cf_it->second.return_type};
            }
            CType fn_ret = (!all_args[ai].subtypes.empty()) ? all_args[ai].subtypes[0] : CType::INT;
            auto* underlying_fn = cast<Function>(all_args[ai].val);
            all_args[ai].val = wrap_in_closure(underlying_fn, fn_ret);
            // Remember the closure → wrapper function mapping for devirtualization.
            // The wrapper has the env-passing signature: (ptr env, user_args...).
            auto wrapper_name = underlying_fn->getName().str() + "_env";
            if (auto* wrapper_fn = module_->getFunction(wrapper_name))
                closure_known_fn_[all_args[ai].val] = wrapper_fn;
        }
    }
}

TypedValue Codegen::codegen_adt_construct(const std::string& fn_name, const std::vector<TypedValue>& all_args) {
    auto& info = types_.adt_constructors[fn_name];
    auto tag_ty = LType::getInt64Ty(*context_);
    auto i64_ty = LType::getInt64Ty(*context_);

    // Helper: cast value to i64 for storage. ADT structs (multi-i64) don't
    // fit in a single i64 — box them to heap first and pass the pointer.
    // The boxed ADT inherits its inner field heap_mask from `subtypes`.
    auto to_i64 = [&](const TypedValue& tv) -> Value* {
        Value* arg_val = tv.val;
        if (arg_val->getType() == i64_ty) return arg_val;
        if (arg_val->getType()->isIntegerTy())
            return builder_->CreateZExtOrTrunc(arg_val, i64_ty);
        if (arg_val->getType()->isPointerTy())
            return builder_->CreatePtrToInt(arg_val, i64_ty);
        if (arg_val->getType()->isDoubleTy())
            return builder_->CreateBitCast(arg_val, i64_ty);
        if (arg_val->getType()->isStructTy()) {
            // Non-recursive ADT struct {tag, fields...} — box to heap.
            auto* sty = llvm::cast<llvm::StructType>(arg_val->getType());
            unsigned num_fields = sty->getNumElements();
            if (num_fields == 1)
                return builder_->CreateExtractValue(arg_val, {0}, "adt_tag_value");
            auto* tag_v = builder_->CreateExtractValue(arg_val, {0});
            auto* boxed = builder_->CreateCall(rt_.adt_alloc_,
                {tag_v, ConstantInt::get(i64_ty, num_fields - 1)});
            for (unsigned fi = 1; fi < num_fields; fi++) {
                auto* fv = builder_->CreateExtractValue(arg_val, {fi});
                builder_->CreateCall(rt_.adt_set_field_,
                    {boxed, ConstantInt::get(i64_ty, fi - 1), fv});
            }
            // Carry the inner ADT's per-field heap mask so this newly boxed
            // copy frees its heap fields when destroyed.
            int64_t inner_mask = 0;
            for (size_t fi = 0; fi < tv.subtypes.size() && fi < 64; fi++)
                if (is_heap_type(tv.subtypes[fi]))
                    inner_mask |= ((int64_t)1 << fi);
            if (inner_mask != 0)
                builder_->CreateCall(rt_.adt_set_heap_mask_,
                    {boxed, ConstantInt::get(i64_ty, inner_mask)});
            return builder_->CreatePtrToInt(boxed, i64_ty, "adt_field_box");
        }
        return arg_val;
    };

    // Capture per-field CTypes of the supplied args for downstream pattern
    // matching — ADT's field_types is often empty for generic fields (e.g.
    // `type Linear a = Linear a`), so we can't rely on the registry.
    std::vector<CType> field_subtypes;
    for (size_t ai = 0; ai < all_args.size() && ai < (size_t)info.arity; ai++)
        field_subtypes.push_back(all_args[ai].type);

    if (info.is_recursive) {
        // Recursive ADT: heap-allocate via runtime
        auto* node_ptr = builder_->CreateCall(rt_.adt_alloc_,
            {ConstantInt::get(tag_ty, info.tag),
             ConstantInt::get(i64_ty, info.arity)}, "adt_node");
        int64_t adt_heap_mask = 0;
        for (size_t ai = 0; ai < all_args.size() && ai < (size_t)info.arity; ai++) {
            Value* arg_val = to_i64(all_args[ai]);
            // Storing a heap-typed value into the ADT transfers/shares
            // ownership — rc_inc so the field's lifetime is tied to the ADT,
            // not the original binding.
            if (is_heap_value(all_args[ai]) && !all_args[ai].val->getType()->isStructTy())
                emit_rc_inc(all_args[ai].val, all_args[ai].type);
            builder_->CreateCall(rt_.adt_set_field_,
                {node_ptr, ConstantInt::get(i64_ty, ai), arg_val});
            if (is_heap_value(all_args[ai]) && ai < 64)
                adt_heap_mask |= ((int64_t)1 << ai);
        }
        if (adt_heap_mask != 0)
            builder_->CreateCall(rt_.adt_set_heap_mask_,
                {node_ptr, ConstantInt::get(i64_ty, adt_heap_mask)});
        TypedValue result{node_ptr, CType::ADT};
        result.adt_type_name = info.type_name;
        result.subtypes = field_subtypes;
        return result;
    } else {
        // Non-recursive: flat struct {i8, i64*max_arity}
        std::vector<LType*> fields = {tag_ty};
        for (int f = 0; f < info.max_arity; f++) fields.push_back(i64_ty);
        auto* struct_type = StructType::get(*context_, fields);
        Value* val = UndefValue::get(struct_type);
        val = builder_->CreateInsertValue(val, ConstantInt::get(tag_ty, info.tag), {0});
        for (size_t ai = 0; ai < all_args.size() && ai < (size_t)info.arity; ai++) {
            // rc_inc heap field values: same ownership rule as the recursive
            // path — the ADT (eventually boxed) becomes a co-owner.
            if (is_heap_value(all_args[ai]) && !all_args[ai].val->getType()->isStructTy())
                emit_rc_inc(all_args[ai].val, all_args[ai].type);
            Value* arg_val = to_i64(all_args[ai]);
            val = builder_->CreateInsertValue(val, arg_val, {(unsigned)(ai + 1)});
        }
        TypedValue result{val, CType::ADT};
        result.adt_type_name = info.type_name;
        result.subtypes = field_subtypes;
        return result;
    }
}

std::unordered_map<std::string, Codegen::CompiledFunction>::iterator
Codegen::resolve_apply_function(const std::string& fn_name, const std::vector<TypedValue>& all_args) {
    // First try direct lookup, then resolve through named_values_ indirection
    auto cf_it = compiled_functions_.find(fn_name);
    if (cf_it == compiled_functions_.end()) {
        // Check if fn_name is an alias for a compiled function (e.g., let g = partial_fn)
        auto nv_it = named_values_.find(fn_name);
        if (nv_it != named_values_.end() && nv_it->second.type == CType::FUNCTION && nv_it->second.val) {
            if (auto* llvm_fn = dyn_cast<Function>(nv_it->second.val)) {
                // Find by LLVM function name
                cf_it = compiled_functions_.find(llvm_fn->getName().str());
            }
        }
    }
    if (cf_it == compiled_functions_.end()) {
        // Check deferred
        auto def_it = deferred_functions_.find(fn_name);
        if (def_it != deferred_functions_.end()) {
            compile_function(fn_name, def_it->second, all_args);
            cf_it = compiled_functions_.find(fn_name);
        }
    }

    // Trait method resolution: if not found yet, check if fn_name is a trait method
    if (cf_it == compiled_functions_.end()) {
        CType first_arg_type = all_args.empty() ? CType::INT : all_args[0].type;
        std::string first_adt_name = all_args.empty() ? "" : all_args[0].adt_type_name;
        auto resolved = resolve_trait_method(fn_name, first_arg_type, first_adt_name);
        if (!resolved.empty()) {
            // Try compiled functions first
            cf_it = compiled_functions_.find(resolved);
            if (cf_it == compiled_functions_.end()) {
                // Try deferred
                auto def_it = deferred_functions_.find(resolved);
                if (def_it != deferred_functions_.end()) {
                    compile_function(resolved, def_it->second, all_args);
                    cf_it = compiled_functions_.find(resolved);
                }
            }
        }
    }
    return cf_it;
}

TypedValue Codegen::codegen_higher_order_call(const std::string& fn_name, const std::vector<TypedValue>& all_args) {
    auto var_it = named_values_.find(fn_name);
    // Filter out Unit arguments (thunk calls: t () → call with 0 args)
    std::vector<LType*> arg_types;
    std::vector<Value*> vals;
    for (auto& a : all_args) {
        if (a.type == CType::UNIT) continue; // skip unit args for thunk calls
        arg_types.push_back(a.val->getType());
        vals.push_back(a.val);
    }
    // For return type inference, ignore the synthetic Unit arg that `f()`
    // inserts at parse time — it doesn't reflect the callee's actual return.
    CType first_nonunit = CType::INT;
    for (auto& a : all_args) { if (a.type != CType::UNIT) { first_nonunit = a.type; break; } }
    CType ret_ctype = (!var_it->second.subtypes.empty()) ? var_it->second.subtypes[0]
                                                         : first_nonunit;
    auto ret_llvm = llvm_type(ret_ctype);
    auto var_val = var_it->second.val;

    if (isa<Function>(var_val) || effect_resume_names_.count(fn_name)) {
        // Direct function pointer or effect resume fn ptr
        auto fn_type = llvm::FunctionType::get(ret_llvm, arg_types, false);
        auto result = builder_->CreateCall(fn_type, var_val, vals, "indirect_call");
        return {result, ret_ctype};
    } else {
        // Closure call. Check if we know the underlying Function*
        // (from closure devirtualization) for a direct call.
        auto kf_it = closure_known_fn_.find(var_val);
        if (kf_it != closure_known_fn_.end()) {
            // Devirtualized: direct call to the known wrapper function.
            // Coerce args to match wrapper's expected param types.
            auto* known_fn = kf_it->second;
            std::vector<Value*> call_vals = {var_val}; // env = closure
            size_t param_idx = 1; // skip env param
            for (auto& a : all_args) {
                if (a.type == CType::UNIT) continue;
                Value* arg_val = a.val;
                if (param_idx < known_fn->arg_size()) {
                    auto* expected = known_fn->getArg(param_idx)->getType();
                    if (arg_val->getType() != expected) {
                        if (arg_val->getType()->isPointerTy() && expected->isIntegerTy())
                            arg_val = builder_->CreatePtrToInt(arg_val, expected);
                        else if (arg_val->getType()->isIntegerTy() && expected->isPointerTy())
                            arg_val = builder_->CreateIntToPtr(arg_val, expected);
                    }
                }
                call_vals.push_back(arg_val);
                param_idx++;
            }
            auto* result = builder_->CreateCall(known_fn, call_vals, "devirt_call");
            return {result, ret_ctype};
        }

        // Generic closure: load fn_ptr from closure[0]
        auto i64_ty = LType::getInt64Ty(*context_);
        auto ptr_ty = PointerType::get(*context_, 0);

        // Load arity from closure[2] to handle over-application (currying)
        auto* arity_gep = builder_->CreateGEP(i64_ty, var_val,
            {ConstantInt::get(i64_ty, CLOSURE_FIELD_ARITY)}, "arity_ptr");
        auto* arity_val = builder_->CreateLoad(i64_ty, arity_gep, "arity");

        // Apply args, handling over-application by iterating
        Value* current_closure = var_val;
        size_t args_consumed = 0;

        while (args_consumed < vals.size()) {
            auto* fn_i64 = builder_->CreateLoad(i64_ty, current_closure, "closure_fn_i64");
            auto* fn_ptr = builder_->CreateIntToPtr(fn_i64, ptr_ty, "closure_fn_ptr");

            // Load arity for current closure
            auto* cur_arity_gep = builder_->CreateGEP(i64_ty, current_closure,
                {ConstantInt::get(i64_ty, CLOSURE_FIELD_ARITY)}, "cur_arity_ptr");
            auto* cur_arity = builder_->CreateLoad(i64_ty, cur_arity_gep, "cur_arity");

            // For compile-time constant arity (common case), use it directly.
            // Otherwise, call with all remaining args (non-curried).
            int64_t static_arity = -1;
            if (auto* ci = dyn_cast<ConstantInt>(cur_arity))
                static_arity = ci->getZExtValue();

            size_t n_args_this_call;
            if (static_arity >= 0) {
                n_args_this_call = std::min(static_cast<size_t>(static_arity),
                                             vals.size() - args_consumed);
            } else {
                n_args_this_call = vals.size() - args_consumed;
            }

            // Build closure call: fn(env, args...)
            std::vector<LType*> call_arg_types = {ptr_ty};
            std::vector<Value*> call_vals = {current_closure};
            for (size_t ai = 0; ai < n_args_this_call; ai++) {
                call_arg_types.push_back(vals[args_consumed + ai]->getType());
                call_vals.push_back(vals[args_consumed + ai]);
            }

            // Use the actual return type for the closure call
            auto* closure_ret_ty = (args_consumed + n_args_this_call < vals.size())
                ? (LType*)ptr_ty  // intermediate result is a closure (pointer)
                : llvm_type(ret_ctype);  // final result uses inferred type
            auto* call_type = llvm::FunctionType::get(closure_ret_ty, call_arg_types, false);
            auto* result = builder_->CreateCall(call_type, fn_ptr, call_vals, "closure_call");

            args_consumed += n_args_this_call;

            if (args_consumed < vals.size()) {
                current_closure = result; // already ptr type
            } else {
                // Perceus: when a callee chain (lambda → CType-upgraded
                // function) handles the ownership drop for a heap-typed
                // arg, foldl's function-exit must NOT also dec it. But we
                // don't know at compile time whether the callee dec'd —
                // it depends on the runtime path (empty arm vs head-tail,
                // in-place vs copy). Use the result != arg test as a
                // proxy: if the closure returned a DIFFERENT pointer, the
                // arg was consumed (by the callee's Perceus or by the
                // operation itself). Store the flag in an alloca so the
                // function-exit can check it at runtime.
                for (size_t ai = 0; ai < all_args.size(); ai++) {
                    if (!is_heap_type(all_args[ai].type)) continue;
                    if (!all_args[ai].val || isa<Constant>(all_args[ai].val)) continue;
                    if (all_args[ai].val->getType()->isStructTy()) continue;
                    bool is_named = false;
                    for (auto& [k, v] : named_values_)
                        if (v.val == all_args[ai].val) { is_named = true; break; }
                    if (!is_named) continue;
                    Value* arg_ptr = all_args[ai].val;
                    Value* res_ptr = result;
                    if (arg_ptr->getType()->isIntegerTy())
                        arg_ptr = builder_->CreateIntToPtr(arg_ptr, ptr_ty);
                    if (res_ptr->getType()->isIntegerTy())
                        res_ptr = builder_->CreateIntToPtr(res_ptr, ptr_ty);
                    if (arg_ptr->getType()->isPointerTy() && res_ptr->getType()->isPointerTy()) {
                        auto* consumed = builder_->CreateICmpNE(arg_ptr, res_ptr, "closure_consumed");
                        // Store in an alloca so function-exit can load it
                        // (the icmp is in a case arm; function-exit is at merge).
                        auto* flag_alloca = closure_consumed_flags_[all_args[ai].val];
                        if (!flag_alloca) {
                            auto* fn_parent = builder_->GetInsertBlock()->getParent();
                            IRBuilder<> entry_ir(&fn_parent->getEntryBlock(),
                                                  fn_parent->getEntryBlock().begin());
                            flag_alloca = entry_ir.CreateAlloca(
                                LType::getInt1Ty(*context_), nullptr, "consumed_flag");
                            entry_ir.CreateStore(
                                ConstantInt::getFalse(*context_), flag_alloca);
                            closure_consumed_flags_[all_args[ai].val] = flag_alloca;
                        }
                        builder_->CreateStore(consumed, flag_alloca);
                    }
                }
                return {result, ret_ctype};
            }
        }

        // No args case (thunk: t ())
        auto* fn_i64 = builder_->CreateLoad(i64_ty, current_closure, "closure_fn_i64");
        auto* fn_ptr_val = builder_->CreateIntToPtr(fn_i64, ptr_ty, "closure_fn_ptr");
        auto* thunk_ret_ty = llvm_type(ret_ctype);
        auto* call_type = llvm::FunctionType::get(thunk_ret_ty, {ptr_ty}, false);
        auto* result = builder_->CreateCall(call_type, fn_ptr_val, {current_closure}, "closure_call");
        return {result, ret_ctype};
    }
}

TypedValue Codegen::codegen_extern_call(ApplyExpr* node, const std::string& fn_name,
                                         const std::vector<TypedValue>& all_args) {
    auto ext_it = imports_.extern_functions.find(fn_name);
    std::string mangled = ext_it->second;

    // On-demand GENFN re-parse: if the function has source available
    // and call-site arg types differ from the pre-compiled signature,
    // re-parse and compile locally (cross-module monomorphization).
    auto genfn_it = imports_.imported_sources.find(mangled);
    bool types_differ = false;
    if (genfn_it != imports_.imported_sources.end()) {
        auto meta_it2 = imports_.meta.find(mangled);
        if (meta_it2 != imports_.meta.end()) {
            auto& meta = meta_it2->second;
            // Check if ALL metadata params are INT (indicates a boxed
            // extern wrapper where all types are i64 at the ABI level).
            bool all_meta_int = true;
            for (auto ct : meta.param_types)
                if (ct != CType::INT) { all_meta_int = false; break; }

            for (size_t i = 0; i < all_args.size() && i < meta.param_types.size(); i++) {
                CType a = all_args[i].type, m = meta.param_types[i];
                if (a == m) continue;
                // For boxed extern wrappers (all-INT metadata), skip GENFN
                // when the arg type is still i64-compatible AND doesn't
                // need different semantic codegen. STRING, BOOL, SYMBOL
                // are fine (i64 values or pointers treated as i64).
                // ADT, TUPLE, FUNCTION need GENFN (different codegen).
                if (all_meta_int && m == CType::INT &&
                    (a == CType::STRING || a == CType::BOOL ||
                     a == CType::SYMBOL || a == CType::SEQ ||
                     a == CType::SET || a == CType::DICT ||
                     a == CType::BYTE_ARRAY)) continue;
                types_differ = true;
                break;
            }
        }
    }
    if (genfn_it != imports_.imported_sources.end()) {
        auto& ifs = genfn_it->second;
        auto reparsed = reparse_genfn(ifs.local_name, ifs.source_text);
        if (reparsed && !reparsed->functions.empty()) {
            auto* func_ast = reparsed->functions[0];
            reparsed->functions.clear();
            imports_.imported_ast_nodes.push_back(std::unique_ptr<FunctionExpr>(func_ast));
            auto saved_externs = imports_.extern_functions;
            std::vector<std::string> scoped_cafs;
            auto sep = mangled.rfind("__");
            std::string module_prefix = (sep == std::string::npos) ? "" : mangled.substr(0, sep + 2);
            if (!module_prefix.empty()) {
                for (const auto& [dep_mangled, dep_meta] : imports_.meta) {
                    if (dep_mangled.rfind(module_prefix, 0) != 0) continue;
                    auto dep_sep = dep_mangled.rfind("__");
                    if (dep_sep == std::string::npos) continue;
                    std::string dep_name = dep_mangled.substr(dep_sep + 2);
                    imports_.extern_functions.emplace(dep_name, dep_mangled);
                    if (dep_meta.param_types.empty() &&
                        compiled_functions_.find(dep_name) == compiled_functions_.end()) {
                        auto* ret_ty = llvm_type(dep_meta.return_type);
                        auto* fn_type = llvm::FunctionType::get(ret_ty, {}, false);
                        auto* fn = module_->getFunction(dep_mangled);
                        if (!fn) fn = Function::Create(fn_type, Function::ExternalLinkage,
                                                       dep_mangled, module_.get());
                        compiled_functions_[dep_name] =
                            compiled_function_from_meta(fn, dep_meta, dep_meta.return_type);
                        scoped_cafs.push_back(dep_name);
                    }
                }
            }
            codegen_function_def(func_ast, fn_name);
            auto def_it2 = deferred_functions_.find(fn_name);
            if (def_it2 != deferred_functions_.end()) {
                compile_function(fn_name, def_it2->second, all_args);
                auto cf_it2 = compiled_functions_.find(fn_name);
                if (cf_it2 != compiled_functions_.end()) {
                    // Remove from extern so future calls use local copy
                    imports_.extern_functions = std::move(saved_externs);
                    for (const auto& scoped_caf : scoped_cafs)
                        compiled_functions_.erase(scoped_caf);
                    imports_.extern_functions.erase(fn_name);
                    imports_.imported_sources.erase(mangled);
                    auto& cf2 = cf_it2->second;
                    size_t genfn_arity = cf2.param_types.size() - cf2.capture_names.size();
                    if (all_args.size() < genfn_arity)
                        return codegen_partial_apply(fn_name, cf2, all_args);
                    std::vector<Value*> vals;
                    for (auto& a : all_args) vals.push_back(a.val);
                    return {builder_->CreateCall(cf2.fn, vals, "genfn_call"), cf2.return_type};
                }
            }
            imports_.extern_functions = std::move(saved_externs);
            for (const auto& scoped_caf : scoped_cafs)
                compiled_functions_.erase(scoped_caf);
        }
        // Fallthrough: if re-parse failed, call as extern
    }

    // Try to get return type from module metadata
    CType ret_ctype = CType::INT; // default
    auto meta_it = imports_.meta.find(mangled);
    if (meta_it != imports_.meta.end()) {
        ret_ctype = meta_it->second.return_type;
    } else {
        // Check CFFI signatures
        auto cffi_it = types_.cffi_signatures.find(mangled);
        if (cffi_it != types_.cffi_signatures.end())
            ret_ctype = cffi_it->second.return_type;
    }

    // Declare the extern function. Boxed wrappers (all-INT metadata)
    // return i64; typed wrappers (with FLOAT etc.) return their native type.
    auto i64_ty_local = LType::getInt64Ty(*context_);
    bool is_boxed = true;
    if (meta_it != imports_.meta.end()) {
        for (auto ct : meta_it->second.param_types)
            if (ct != CType::INT) { is_boxed = false; break; }
        if (meta_it->second.return_type != CType::INT &&
            meta_it->second.return_type != CType::BOOL &&
            meta_it->second.return_type != CType::SYMBOL)
            is_boxed = (meta_it->second.return_type == CType::STRING ||
                        meta_it->second.return_type == CType::SEQ ||
                        meta_it->second.return_type == CType::SET ||
                        meta_it->second.return_type == CType::DICT ||
                        meta_it->second.return_type == CType::ADT) && is_boxed;
    }

    std::vector<LType*> arg_types;
    if (meta_it != imports_.meta.end()) {
        for (size_t ai = 0; ai < all_args.size(); ai++) {
            if (!is_boxed && ai < meta_it->second.param_types.size())
                arg_types.push_back(llvm_type(meta_it->second.param_types[ai]));
            else
                arg_types.push_back(all_args[ai].val->getType());
        }
    } else {
        for (auto& a : all_args) arg_types.push_back(a.val->getType());
    }
    auto ret_llvm = is_boxed ? i64_ty_local : llvm_type(ret_ctype);
    auto fn_type = llvm::FunctionType::get(ret_llvm, arg_types, false);

    auto* ext_fn = module_->getFunction(mangled);
    if (!ext_fn) {
        ext_fn = Function::Create(fn_type, Function::ExternalLinkage,
                                   mangled, module_.get());
    }

    std::optional<CompiledFunction> ext_cf;
    if (meta_it != imports_.meta.end()) {
        ext_cf = compiled_function_from_meta(ext_fn, meta_it->second, ret_ctype);
        prepare_callee_owned_heap_args(*ext_cf, all_args);
    }

    std::vector<Value*> vals;
    for (size_t ai = 0; ai < all_args.size(); ai++) {
        Value* arg_val = all_args[ai].val;
        auto* expected_ty = ai < arg_types.size() ? arg_types[ai] : arg_val->getType();
        if (arg_val->getType() != expected_ty) {
            if (arg_val->getType()->isStructTy() && expected_ty->isIntegerTy()) {
                arg_val = builder_->CreateExtractValue(arg_val, {0}, "adt_tag_arg");
                if (arg_val->getType() != expected_ty)
                    arg_val = builder_->CreateZExtOrTrunc(arg_val, expected_ty);
            } else if (arg_val->getType()->isIntegerTy() && expected_ty->isPointerTy())
                arg_val = builder_->CreateIntToPtr(arg_val, expected_ty);
            else if (arg_val->getType()->isPointerTy() && expected_ty->isIntegerTy())
                arg_val = builder_->CreatePtrToInt(arg_val, expected_ty);
            else if (arg_val->getType()->isIntegerTy() && expected_ty->isIntegerTy())
                arg_val = builder_->CreateZExtOrTrunc(arg_val, expected_ty);
        }
        vals.push_back(arg_val);
    }
    Value* ext_result = ext_fn->getReturnType()->isVoidTy()
        ? builder_->CreateCall(ext_fn, vals)
        : builder_->CreateCall(ext_fn, vals, "extern_call");
    if (ext_cf)
        cleanup_borrowed_temporary_args(*ext_cf, all_args);
    if (ext_fn->getReturnType()->isVoidTy())
        ext_result = ConstantInt::get(LType::getInt64Ty(*context_), 0);

    // Callee-owns for Set/Dict extern ops that consume their heap input
    // (e.g. Set.insert, Dict.put). For SET/DICT args and SET/DICT returns
    // (interchangeable since `{}` parses as SET), the callee either mutates
    // in place or path-copies + rc_dec's the old. Mark the arg as
    // transferred so the caller's function-exit DROP doesn't double-dec.
    // Mark MAP-domain ownership transfer so cleanup/exit skip rc_dec for
    // consumed SET/DICT values. Per-branch transfer_scope remains SEQ-only.
    if (!all_args.empty()) {
        CType a0 = all_args[0].type;
        bool a0_map = (a0 == CType::SET || a0 == CType::DICT);
        bool ret_map = (ret_ctype == CType::SET || ret_ctype == CType::DICT);
        if (a0_map && ret_map) {
            if (all_args[0].val && !isa<Constant>(all_args[0].val)) {
                mark_transferred(all_args[0].val, TransferDomain::Map);
                emit_frame_transfer(all_args[0].val);
            }
        }
    }

    // Convert boxed i64 result to the correct LLVM type
    if (is_boxed) {
        if (ret_ctype == CType::BOOL) {
            ext_result = builder_->CreateICmpNE(ext_result,
                ConstantInt::get(i64_ty_local, 0), "bool_conv");
        } else if (ret_ctype == CType::STRING || ret_ctype == CType::SEQ ||
                   ret_ctype == CType::SET || ret_ctype == CType::DICT ||
                   ret_ctype == CType::FUNCTION || ret_ctype == CType::ADT) {
            ext_result = builder_->CreateIntToPtr(ext_result,
                PointerType::get(*context_, 0), "ptr_conv");
        }
    } else if (ret_ctype == CType::ADT && ext_result->getType()->isIntegerTy()) {
        // Non-boxed call but ADT return: convert i64 to ptr for downstream
        // pattern matching to use the heap layout path.
        ext_result = builder_->CreateIntToPtr(ext_result,
            PointerType::get(*context_, 0), "adt_ptr_conv");
    }
    TypedValue result{ext_result, ret_ctype};
    if (ret_ctype == CType::ADT && meta_it != imports_.meta.end() &&
        !meta_it->second.return_adt_name.empty())
        result.adt_type_name = meta_it->second.return_adt_name;
    return result;
}

TypedValue Codegen::codegen_partial_apply(const std::string& fn_name, CompiledFunction& cf,
                                           const std::vector<TypedValue>& all_args) {
    size_t func_arity = cf.param_types.size() - cf.capture_names.size();
    size_t n_provided = all_args.size();
    size_t n_remaining = func_arity - n_provided;
    auto ptr_ty = PointerType::get(*context_, 0);
    auto i64_ty = LType::getInt64Ty(*context_);

    bool can_embed_args = true;
    for (const auto& arg : all_args) {
        if (!arg.val || (!isa<Constant>(arg.val) && !isa<Function>(arg.val))) {
            can_embed_args = false;
            break;
        }
    }
    if (can_embed_args) {
        std::vector<LType*> wrapper_params;
        for (size_t i = n_provided; i < func_arity; i++)
            wrapper_params.push_back(llvm_type(cf.param_types[i]));
        auto* wrapper_type = llvm::FunctionType::get(cf.fn->getReturnType(), wrapper_params, false);
        std::string wrapper_name = fn_name + "_partial" + std::to_string(n_provided) + "of" + std::to_string(func_arity);
        auto* wrapper = Function::Create(wrapper_type, Function::InternalLinkage,
                                          wrapper_name, module_.get());
        auto saved_block = builder_->GetInsertBlock();
        auto saved_point = builder_->GetInsertPoint();
        auto* entry = BasicBlock::Create(*context_, "entry", wrapper);
        builder_->SetInsertPoint(entry);
        std::vector<Value*> inner_call_args;
        for (auto& a : all_args)
            inner_call_args.push_back(a.val);
        for (auto& arg : wrapper->args())
            inner_call_args.push_back(&arg);
        for (auto& cap_name : cf.capture_names) {
            auto cap_it = named_values_.find(cap_name);
            if (cap_it != named_values_.end())
                inner_call_args.push_back(cap_it->second.val);
        }
        auto* result = builder_->CreateCall(cf.fn, inner_call_args, "partial_call");
        builder_->CreateRet(result);
        builder_->SetInsertPoint(saved_block, saved_point);
        CompiledFunction partial_cf;
        partial_cf.fn = wrapper;
        partial_cf.return_type = cf.return_type;
        for (size_t i = n_provided; i < func_arity; i++)
            partial_cf.param_types.push_back(cf.param_types[i]);
        compiled_functions_[wrapper_name] = partial_cf;
        named_values_[wrapper_name] = {wrapper, CType::FUNCTION};
        return {wrapper, CType::FUNCTION, {cf.return_type}};
    }

    // Build the wrapper's parameter types (remaining params)
    std::vector<LType*> wrapper_params = {ptr_ty};
    for (size_t i = n_provided; i < func_arity; i++)
        wrapper_params.push_back(llvm_type(cf.param_types[i]));

    auto ret_llvm = i64_ty;
    auto wrapper_type = llvm::FunctionType::get(ret_llvm, wrapper_params, false);
    std::string wrapper_name = fn_name + "_partial" + std::to_string(n_provided) + "of" + std::to_string(func_arity);
    auto* wrapper = Function::Create(wrapper_type, Function::InternalLinkage,
                                      wrapper_name, module_.get());

    // Save state
    auto saved_block = builder_->GetInsertBlock();
    auto saved_point = builder_->GetInsertPoint();

    auto* entry = BasicBlock::Create(*context_, "entry", wrapper);
    builder_->SetInsertPoint(entry);

    std::vector<Value*> inner_call_args;
    auto arg_it = wrapper->arg_begin();
    Value* env = &*arg_it++;

    // First: the partially applied args captured in the closure env.
    for (size_t i = 0; i < n_provided; i++) {
        Value* cap = builder_->CreateCall(rt_.closure_get_cap_,
            {env, ConstantInt::get(i64_ty, i)}, "partial_cap");
        auto* expected_ty = cf.fn->getArg(i)->getType();
        if (cap->getType() != expected_ty) {
            if (expected_ty->isPointerTy())
                cap = builder_->CreateIntToPtr(cap, expected_ty);
            else if (expected_ty->isIntegerTy())
                cap = builder_->CreateZExtOrTrunc(cap, expected_ty);
        }
        inner_call_args.push_back(cap);
    }

    // Then: the remaining params (wrapper's normal parameters).
    for (; arg_it != wrapper->arg_end(); ++arg_it)
        inner_call_args.push_back(&*arg_it);

    // Then: original function's captures (from enclosing scope)
    for (auto& cap_name : cf.capture_names) {
        auto cap_it = named_values_.find(cap_name);
        if (cap_it != named_values_.end())
            inner_call_args.push_back(cap_it->second.val);
    }

    auto* result = builder_->CreateCall(cf.fn, inner_call_args, "partial_call");
    Value* ret_val = result;
    if (ret_val->getType() != i64_ty) {
        if (ret_val->getType()->isPointerTy())
            ret_val = builder_->CreatePtrToInt(ret_val, i64_ty);
        else if (ret_val->getType()->isDoubleTy())
            ret_val = builder_->CreateBitCast(ret_val, i64_ty);
        else if (ret_val->getType()->isIntegerTy())
            ret_val = builder_->CreateZExtOrTrunc(ret_val, i64_ty);
    }
    builder_->CreateRet(ret_val);

    // Restore
    builder_->SetInsertPoint(saved_block, saved_point);

    Value* closure = builder_->CreateCall(rt_.closure_create_,
        {wrapper, ConstantInt::get(i64_ty, static_cast<int64_t>(cf.return_type)),
         ConstantInt::get(i64_ty, n_remaining),
         ConstantInt::get(i64_ty, n_provided)}, wrapper_name + "_closure");
    int64_t heap_mask = 0;
    for (size_t i = 0; i < n_provided; i++) {
        Value* cap = all_args[i].val;
        Value* stored = cap;
        if (stored->getType()->isPointerTy())
            stored = builder_->CreatePtrToInt(stored, i64_ty);
        else if (stored->getType()->isIntegerTy() && stored->getType() != i64_ty)
            stored = builder_->CreateZExtOrTrunc(stored, i64_ty);
        builder_->CreateCall(rt_.closure_set_cap_,
            {closure, ConstantInt::get(i64_ty, i), stored});
        if (is_heap_value(all_args[i])) {
            emit_rc_inc(cap, all_args[i].type);
            if (i < 64) heap_mask |= ((int64_t)1 << i);
        }
    }
    if (heap_mask != 0)
        builder_->CreateCall(rt_.closure_set_heap_mask_,
            {closure, ConstantInt::get(i64_ty, heap_mask)});
    return {closure, CType::FUNCTION, {cf.return_type}};
}

TypedValue Codegen::codegen_curry_apply(const std::string& fn_name, CompiledFunction& cf,
                                         const std::vector<TypedValue>& all_args) {
    size_t func_arity = cf.param_types.size() - cf.capture_names.size();
    auto i64_ty = LType::getInt64Ty(*context_);
    auto ptr_ty = PointerType::get(*context_, 0);

    // Call with first func_arity args
    std::vector<Value*> first_args;
    for (size_t ai = 0; ai < func_arity; ai++) {
        Value* arg_val = all_args[ai].val;
        if (ai < cf.fn->arg_size()) {
            auto* expected_ty = cf.fn->getArg(ai)->getType();
            if (arg_val->getType() != expected_ty) {
                if (arg_val->getType()->isIntegerTy() && expected_ty->isPointerTy())
                    arg_val = builder_->CreateIntToPtr(arg_val, expected_ty);
                else if (arg_val->getType()->isPointerTy() && expected_ty->isIntegerTy())
                    arg_val = builder_->CreatePtrToInt(arg_val, expected_ty);
            }
        }
        first_args.push_back(arg_val);
    }
    for (auto& cap_name : cf.capture_names) {
        auto it = named_values_.find(cap_name);
        if (it != named_values_.end()) first_args.push_back(it->second.val);
    }

    Value* current_fn = builder_->CreateCall(cf.fn, first_args, "curry_call");

    // Apply remaining args one at a time via closure convention
    for (size_t ai = func_arity; ai < all_args.size(); ai++) {
        // current_fn is a closure ptr (or i64 encoding of one)
        if (current_fn->getType()->isIntegerTy())
            current_fn = builder_->CreateIntToPtr(current_fn, ptr_ty);

        auto* fn_i64 = builder_->CreateLoad(i64_ty, current_fn, "curry_fn_i64");
        auto* fn_ptr = builder_->CreateIntToPtr(fn_i64, ptr_ty, "curry_fn_ptr");

        std::vector<LType*> arg_types = {ptr_ty};
        std::vector<Value*> call_vals = {current_fn};

        if (all_args[ai].type != CType::UNIT) {
            arg_types.push_back(all_args[ai].val->getType());
            call_vals.push_back(all_args[ai].val);
        }

        auto* call_type = llvm::FunctionType::get(i64_ty, arg_types, false);
        current_fn = builder_->CreateCall(call_type, fn_ptr, call_vals, "curry_apply");
    }

    // Final result is i64
    return {current_fn, CType::INT};
}

void Codegen::prepare_callee_owned_heap_args(const CompiledFunction& cf,
                                             const std::vector<TypedValue>& all_args) {
    // Perceus DUP for heap args. Standard rule: at non-last-use sites we
    // rc_inc so the consumed-by-callee ref doesn't pull the caller's
    // binding out from under us. Last-use (or single-use globally) sites
    // skip the inc and transfer the existing ref directly — this is what
    // unlocks the in-place tail fast path in the foldl/map/filter
    // recursion, where each recursive call passes the pattern-bound `t`
    // exactly once.
    for (size_t ai = 0; ai < all_args.size(); ai++) {
        CType ct = all_args[ai].type;
        if (!is_heap_type(ct)) continue;
        if (!all_args[ai].val || isa<Constant>(all_args[ai].val)) continue;
        if (all_args[ai].val->getType()->isStructTy()) continue;
        std::string named_as;
        for (auto& [k, v] : named_values_)
            if (v.val == all_args[ai].val) { named_as = k; break; }
        if (named_as.empty()) continue;  // anonymous → transfer (no inc)
        // Borrow inference: if the callee borrows this param, no rc_inc
        // needed — the caller retains ownership and the callee only reads.
        if (ai < cf.borrowed_params.size() && cf.borrowed_params[ai])
            continue;
        // For SEQ args, skip the inc when the binding has exactly one
        // textual occurrence in the enclosing function body — that
        // single use is also the last use, so we can transfer.
        if (ct == CType::SEQ && current_fn_body_) {
            int uses = count_identifier_refs(current_fn_body_, named_as);
            if (uses <= 1) {
                mark_transferred(all_args[ai].val, TransferDomain::Seq);
                emit_frame_transfer(all_args[ai].val);
                continue;
            }
        }
        // Same single-use check for SET/DICT under the callee-owns ABI
        // extended to user-defined calls. Last-use args are transferred
        // (no rc_inc); the callee's function-exit drop handles cleanup.
        if ((ct == CType::SET || ct == CType::DICT) && current_fn_body_) {
            int uses = count_identifier_refs(current_fn_body_, named_as);
            if (uses <= 1) {
                mark_transferred(all_args[ai].val, TransferDomain::Map);
                emit_frame_transfer(all_args[ai].val);
                continue;
            }
        }
        emit_rc_inc(all_args[ai].val, ct);
    }
}

void Codegen::cleanup_borrowed_temporary_args(const CompiledFunction& cf,
                                              const std::vector<TypedValue>& all_args) {
    for (size_t ai = 0; ai < all_args.size(); ai++) {
        if (ai >= cf.borrowed_params.size() || !cf.borrowed_params[ai])
            continue;
        CType ct = all_args[ai].type;
        if (!is_heap_type(ct)) continue;
        if (!all_args[ai].val || isa<Constant>(all_args[ai].val)) continue;
        if (all_args[ai].val->getType()->isStructTy()) continue;

        bool is_named = false;
        for (auto& [k, v] : named_values_)
            if (v.val == all_args[ai].val) { is_named = true; break; }
        if (!is_named)
            emit_rc_dec(all_args[ai].val, ct);
    }
}

TypedValue Codegen::emit_direct_call(const std::string& fn_name, CompiledFunction& cf,
                                      const std::vector<TypedValue>& all_args) {
    prepare_callee_owned_heap_args(cf, all_args);

    std::vector<Value*> call_args;

    // For recursive closure calls, prepend the env pointer as first argument
    if (cf.closure_env)
        call_args.push_back(cf.closure_env);

    size_t fn_arg_offset = cf.closure_env ? 1 : 0;
    for (size_t ai = 0; ai < all_args.size(); ai++) {
        Value* arg_val = all_args[ai].val;
        // Coerce arg type if it doesn't match the function's expected param type.
        // This handles closures returning i64 when the callee expects ptr (ADT).
        if (ai + fn_arg_offset < cf.fn->arg_size()) {
            auto* expected_ty = cf.fn->getArg(ai + fn_arg_offset)->getType();
            if (arg_val->getType() != expected_ty) {
                if (arg_val->getType()->isIntegerTy() && expected_ty->isPointerTy())
                    arg_val = builder_->CreateIntToPtr(arg_val, expected_ty);
                else if (arg_val->getType()->isPointerTy() && expected_ty->isIntegerTy())
                    arg_val = builder_->CreatePtrToInt(arg_val, expected_ty);
                else if (arg_val->getType()->isStructTy() && expected_ty->isIntegerTy()) {
                    auto* st = llvm::cast<llvm::StructType>(arg_val->getType());
                    auto i64_ty = LType::getInt64Ty(*context_);
                    if (st->getNumElements() == 1) {
                        arg_val = builder_->CreateExtractValue(arg_val, {0}, "adt_tag_arg");
                        if (arg_val->getType() != expected_ty)
                            arg_val = builder_->CreateZExtOrTrunc(arg_val, expected_ty);
                    } else {
                        auto* tag_val = builder_->CreateExtractValue(arg_val, {0}, "adt_tag_arg");
                        if (tag_val->getType() != i64_ty)
                            tag_val = builder_->CreateZExtOrTrunc(tag_val, i64_ty);
                        auto* boxed = builder_->CreateCall(rt_.adt_alloc_,
                            {tag_val, ConstantInt::get(i64_ty, st->getNumElements() - 1)},
                            "adt_box_arg");
                        int64_t heap_mask = 0;
                        for (unsigned fi = 1; fi < st->getNumElements(); fi++) {
                            auto* field = builder_->CreateExtractValue(arg_val, {fi}, "adt_field_arg");
                            if (field->getType() != i64_ty) {
                                if (field->getType()->isPointerTy())
                                    field = builder_->CreatePtrToInt(field, i64_ty);
                                else if (field->getType()->isIntegerTy())
                                    field = builder_->CreateZExtOrTrunc(field, i64_ty);
                            }
                            builder_->CreateCall(rt_.adt_set_field_,
                                {boxed, ConstantInt::get(i64_ty, fi - 1), field});
                            size_t subtype_index = fi - 1;
                            if (subtype_index < all_args[ai].subtypes.size() &&
                                all_args[ai].subtypes[subtype_index] != CType::ADT &&
                                is_heap_type(all_args[ai].subtypes[subtype_index])) {
                                if (fi <= 64) heap_mask |= (int64_t{1} << subtype_index);
                                emit_rc_inc(field, all_args[ai].subtypes[subtype_index]);
                            }
                        }
                        if (heap_mask != 0)
                            builder_->CreateCall(rt_.adt_set_heap_mask_,
                                {boxed, ConstantInt::get(i64_ty, heap_mask)});
                        arg_val = builder_->CreatePtrToInt(boxed, expected_ty);
                    }
                }
            }
        }
        call_args.push_back(arg_val);
    }

    // Append capture values
    for (auto& cap_name : cf.capture_names) {
        auto it = named_values_.find(cap_name);
        if (it != named_values_.end()) call_args.push_back(it->second.val);
    }

    // Native promise (NAT / extern native): C returns yona_promise_t* — call
    // directly; auto-await uses yona_rt_async_await (same as thread-pool promises).
    if (cf.return_type == CType::PROMISE && cf.extern_promise == ast::ExternPromiseKind::NativePtr) {
        CType inner_ret = CType::INT;
        auto nv_it = named_values_.find(fn_name);
        if (nv_it != named_values_.end() && !nv_it->second.subtypes.empty())
            inner_ret = nv_it->second.subtypes[0];
        auto* result = builder_->CreateCall(cf.fn, call_args, "native_promise");
        return TypedValue{result, CType::PROMISE, {inner_ret}};
    }

    // io-async: call directly — function submits to io_uring and returns ID
    if (cf.return_type == CType::PROMISE && cf.extern_promise == ast::ExternPromiseKind::IoUring) {
        CType inner_ret = CType::INT;
        auto nv_it = named_values_.find(fn_name);
        if (nv_it != named_values_.end() && !nv_it->second.subtypes.empty())
            inner_ret = nv_it->second.subtypes[0];
        // Call the function directly — it returns the uring ID as i64
        auto* result = builder_->CreateCall(cf.fn, call_args, "io_submit");
        TypedValue tv{result, CType::PROMISE, {inner_ret}};
        tv.promise_await = PromiseAwaitPath::IoUring;
        return tv;
    }

    // Thread-pool async (extern async): wrap in thread pool call → returns PROMISE
    if (cf.return_type == CType::PROMISE) {
        CType inner_ret = CType::INT;
        auto nv_it = named_values_.find(fn_name);
        if (nv_it != named_values_.end() && !nv_it->second.subtypes.empty())
            inner_ret = nv_it->second.subtypes[0];

        auto i64_ty = LType::getInt64Ty(*context_);
        auto ptr_ty = PointerType::get(*context_, 0);

        // Helper: coerce a value to i64 for the async runtime interface
        auto to_i64 = [&](Value* v) -> Value* {
            if (v->getType() == i64_ty) return v;
            if (v->getType()->isPointerTy()) return builder_->CreatePtrToInt(v, i64_ty);
            if (v->getType()->isIntegerTy()) return builder_->CreateZExtOrTrunc(v, i64_ty);
            if (v->getType()->isDoubleTy()) return builder_->CreateBitCast(v, i64_ty);
            return v;
        };

        Value* promise;
        if (call_args.size() <= 1) {
            // 0 or 1 arg: use yona_rt_async_call(fn, arg) [or grouped variant]
            Value* arg = call_args.empty()
                ? ConstantInt::get(i64_ty, 0)
                : to_i64(call_args[0]);
            if (current_group_)
                promise = builder_->CreateCall(rt_.async_call_grouped_, {cf.fn, arg, current_group_}, "async_call_g");
            else
                promise = builder_->CreateCall(rt_.async_call_, {cf.fn, arg}, "async_call");
        } else {
            // Multi-arg: generate a thunk that captures all args via globals
            auto thunk_type = llvm::FunctionType::get(i64_ty, {}, false);
            auto thunk_name = fn_name + "_async_thunk_" + std::to_string(lambda_counter_++);
            auto thunk_fn = Function::Create(thunk_type, Function::InternalLinkage, thunk_name, module_.get());

            // Create globals for each dynamic arg before building the thunk body
            std::vector<GlobalVariable*> arg_globals;
            for (size_t ai = 0; ai < call_args.size(); ai++) {
                auto* gv = new GlobalVariable(*module_, call_args[ai]->getType(), false,
                    GlobalValue::InternalLinkage, Constant::getNullValue(call_args[ai]->getType()),
                    thunk_name + ".arg" + std::to_string(ai));
                arg_globals.push_back(gv);
            }

            // Save current state
            auto saved_block = builder_->GetInsertBlock();
            auto saved_point = builder_->GetInsertPoint();

            // Build thunk body
            auto thunk_entry = BasicBlock::Create(*context_, "entry", thunk_fn);
            builder_->SetInsertPoint(thunk_entry);

            std::vector<Value*> thunk_call_args;
            for (size_t ai = 0; ai < call_args.size(); ai++) {
                thunk_call_args.push_back(builder_->CreateLoad(call_args[ai]->getType(), arg_globals[ai]));
            }

            auto* thunk_result = builder_->CreateCall(cf.fn, thunk_call_args, "thunk_call");
            // Coerce return to i64 for the promise
            builder_->CreateRet(to_i64(thunk_result));

            // Restore insertion point
            builder_->SetInsertPoint(saved_block, saved_point);

            // Store args to globals at call site (before submitting to thread pool)
            for (size_t ai = 0; ai < call_args.size(); ai++) {
                builder_->CreateStore(call_args[ai], arg_globals[ai]);
            }

            if (current_group_)
                promise = builder_->CreateCall(rt_.async_call_thunk_grouped_, {thunk_fn, current_group_}, "async_thunk_g");
            else
                promise = builder_->CreateCall(rt_.async_call_thunk_, {thunk_fn}, "async_thunk_call");
        }
        return {promise, CType::PROMISE, {inner_ret}};
    }

    // TCO: for self-recursive tail calls, emit RC cleanup BEFORE the call
    // so LLVM's TailCallElimination can convert the call to a loop.
    auto* current_fn = builder_->GetInsertBlock()->getParent();
    bool is_self_recursive = (cf.fn == current_fn) && !tco_fn_name_.empty();

    if (is_self_recursive && !tco_cleanup_done_) {
        // Emit Perceus DROP for heap params that are NOT passed through.
        // Pass-through = same LLVM value in both the param and the call arg.
        auto ptr_ty = PointerType::get(*context_, 0);
        for (size_t pi = 0; pi < tco_param_names_.size() && pi < tco_param_ctypes_.size(); pi++) {
            CType ct = tco_param_ctypes_[pi];
            if (ct == CType::SEQ || !is_heap_type(ct)) continue;
            if (pi < tco_borrowed_params_.size() && tco_borrowed_params_[pi]) continue;
            if (pi >= current_fn->arg_size()) continue;
            auto* param = current_fn->getArg((unsigned)pi);
            if (param->getType()->isStructTy()) continue;

            // Check if this param is passed through unchanged
            bool is_pass_through = false;
            if (pi < call_args.size() && call_args[pi] == param)
                is_pass_through = true;
            // Skip if already consumed by a callee-owns extern op
            // (e.g., Dict.put / Set.insert transferred the param)
            if (is_transferred(param, TransferDomain::Map)) continue;
            if (is_transferred(param, TransferDomain::Seq)) continue;

            if (!is_pass_through) {
                emit_rc_dec(param, ct);
            }
        }
        tco_cleanup_done_ = true;
    }

    auto* call_inst = cf.fn->getReturnType()->isVoidTy()
        ? builder_->CreateCall(cf.fn, call_args)
        : builder_->CreateCall(cf.fn, call_args, "calltmp");
    cleanup_borrowed_temporary_args(cf, all_args);
    if (is_self_recursive)
        call_inst->setTailCall(true);
    else if (cf.fn == current_fn)
        call_inst->setTailCall(true);

    // Perceus-linear: user-defined callees are callee-owns for seq args.
    // For ANONYMOUS seq args (fresh expression results): ownership passes
    // cleanly to the callee — mark as transferred so outer scope cleanups
    // skip their rc_dec. For NAMED args we needed an rc_inc BEFORE the
    // call (otherwise the callee might free our only ref) — that's
    // handled in the precall_seq_dups loop above where args were prepared.
    for (size_t ai = 0; ai < all_args.size(); ai++) {
        CType ct = all_args[ai].type;
        if (ct != CType::SEQ && ct != CType::SET && ct != CType::DICT) continue;
        if (!all_args[ai].val || isa<Constant>(all_args[ai].val)) continue;
        if (all_args[ai].val->getType()->isStructTy()) continue;
        bool is_named = false;
        for (auto& [k, v] : named_values_)
            if (v.val == all_args[ai].val) { is_named = true; break; }
        if (!is_named) {
            if (ai < cf.borrowed_params.size() && cf.borrowed_params[ai])
                continue;
            if (ct == CType::SEQ)
                mark_transferred(all_args[ai].val, TransferDomain::Seq);
            else
                mark_transferred(all_args[ai].val, TransferDomain::Map);
            emit_frame_transfer(all_args[ai].val);
        }
    }

    Value* result_val = cf.fn->getReturnType()->isVoidTy()
        ? static_cast<Value*>(ConstantInt::get(LType::getInt64Ty(*context_), 0))
        : static_cast<Value*>(call_inst);
    TypedValue result{result_val, cf.return_type};
    if (cf.return_type == CType::ADT && !cf.return_adt_name.empty())
        result.adt_type_name = cf.return_adt_name;
    if (!cf.return_subtypes.empty())
        result.subtypes = cf.return_subtypes;
    return result;
}

// ===== codegen_apply — main dispatcher =====

TypedValue Codegen::codegen_apply(ApplyExpr* node) {
    set_debug_loc(node->source_context);

    // 1. Flatten juxtaposition chain: f x y → collect all args and root name
    auto [fn_name, module_fqn, chain] = flatten_apply_chain(node);

    // 2. Evaluate all arguments
    auto eval = evaluate_apply_args(chain);
    auto& all_args = eval.all_args;
    if (all_args.empty() && !chain.empty() && !chain.back()->args.empty())
        return {}; // evaluation failed (signalled by cleared all_args)

    // 3. Pre-compile deferred lambda args and wrap Function* in closures
    precompile_function_args(eval);
    wrap_function_args_in_closures(all_args);

    // 4. Check if it's an ADT constructor call
    auto adt_it = types_.adt_constructors.find(fn_name);
    if (adt_it != types_.adt_constructors.end() && adt_it->second.arity > 0)
        return codegen_adt_construct(fn_name, all_args);

    // 4b. Compile-time intrinsic: typeOf x → Type ADT constructor based on argument's CType
    if (fn_name == "typeOf" && all_args.size() == 1) {
        std::string ctor_name;
        switch (all_args[0].type) {
            case CType::INT:         ctor_name = "TInt"; break;
            case CType::FLOAT:       ctor_name = "TFloat"; break;
            case CType::BOOL:        ctor_name = "TBool"; break;
            case CType::STRING:      ctor_name = "TString"; break;
            case CType::SYMBOL:      ctor_name = "TSymbol"; break;
            case CType::UNIT:        ctor_name = "TUnit"; break;
            case CType::SEQ:         ctor_name = "TSeq"; break;
            case CType::SET:         ctor_name = "TSet"; break;
            case CType::DICT:        ctor_name = "TDict"; break;
            case CType::TUPLE:       ctor_name = "TTuple"; break;
            case CType::FUNCTION:    ctor_name = "TFunction"; break;
            case CType::PROMISE:     ctor_name = "TPromise"; break;
            case CType::BYTE_ARRAY:  ctor_name = "TByteArray"; break;
            case CType::INT_ARRAY:   ctor_name = "TIntArray"; break;
            case CType::FLOAT_ARRAY: ctor_name = "TFloatArray"; break;
            case CType::SUM:         ctor_name = "TSum"; break;
            case CType::RECORD:      ctor_name = "TRecord"; break;
            case CType::ADT: {
                // TAdt String — construct with the ADT type name as a String field
                ctor_name = "TAdt";
                std::string adt_name = !all_args[0].adt_type_name.empty()
                    ? all_args[0].adt_type_name : "Unknown";
                auto* str = builder_->CreateGlobalStringPtr(adt_name, "type_adt_name");
                std::vector<TypedValue> ctor_args;
                ctor_args.push_back({str, CType::STRING});
                return codegen_adt_construct("TAdt", ctor_args);
            }
        }
        // Zero-arity Type constructor — use codegen_adt_construct with no args
        return codegen_adt_construct(ctor_name, {});
    }

    // Explicit imports shadow trait-method fallback. This matters for names
    // like `close`, where Std\Channel.close must not resolve to Closeable Int.
    if (imports_.extern_functions.find(fn_name) != imports_.extern_functions.end() &&
        compiled_functions_.find(fn_name) == compiled_functions_.end() &&
        deferred_functions_.find(fn_name) == deferred_functions_.end())
        return codegen_extern_call(node, fn_name, all_args);

    // 5. Resolve the function (compiled, deferred, or trait method)
    auto cf_it = resolve_apply_function(fn_name, all_args);

    // 6. If not found as compiled function, try higher-order, extern, or raw LLVM
    if (cf_it == compiled_functions_.end()) {
        // Higher-order call (FUNCTION-typed variable)
        auto var_it = named_values_.find(fn_name);
        if (var_it != named_values_.end() && var_it->second.type == CType::FUNCTION && var_it->second.val)
            return codegen_higher_order_call(fn_name, all_args);

        // Imported extern function
        auto ext_it = imports_.extern_functions.find(fn_name);
        if (ext_it != imports_.extern_functions.end())
            return codegen_extern_call(node, fn_name, all_args);

        // Try as an LLVM function in the module
        auto* fn = module_->getFunction(fn_name);
        if (!fn) {
            std::string msg = "undefined function '" + fn_name + "'";
            auto suggestion = suggest_similar(fn_name);
            if (!suggestion.empty()) msg += "; did you mean '" + suggestion + "'?";
            report_error(node->source_context, msg);
            return {};
        }
        std::vector<Value*> vals;
        for (auto& a : all_args) vals.push_back(a.val);
        return {builder_->CreateCall(fn, vals), CType::INT};
    }

    auto& cf = cf_it->second;
    size_t func_arity = cf.param_types.size() - cf.capture_names.size();

    // If the callee is a closure (has closure_env) and closure_env is an
    // Argument of a DIFFERENT function than the one we're currently emitting
    // into, we can't emit a direct call — the env Argument isn't in scope.
    // This happens when a recursive let-bound closure `pull` is referenced
    // from a nested lambda (e.g. `\_ -> pull ()`); the lambda captures `pull`
    // as a closure and must call through it via higher_order_call, which
    // loads the closure fn+env from the lambda's own captures.
    if (cf.closure_env) {
        if (auto* env_arg = dyn_cast<Argument>(cf.closure_env)) {
            auto* current_fn = builder_->GetInsertBlock()->getParent();
            if (env_arg->getParent() != current_fn) {
                auto var_it = named_values_.find(fn_name);
                if (var_it != named_values_.end() && var_it->second.type == CType::FUNCTION && var_it->second.val)
                    return codegen_higher_order_call(fn_name, all_args);
            }
        }
    }

    // 7. Dispatch based on arg count vs arity.
    // `f ()` on a 0-arity function: drop the synthetic Unit arg — the parser
    // inserts one for every `f()` paren-call form, but 0-arity callees
    // (CAFs like `uuid4`, `now`) don't want it. A 1-arity function whose
    // parameter happens to be unit-typed keeps the arg and receives ().
    if (func_arity == 0 && all_args.size() == 1 && all_args[0].type == CType::UNIT)
        all_args.clear();
    if (all_args.size() < func_arity)
        return codegen_partial_apply(fn_name, cf, all_args);
    if (all_args.size() > func_arity)
        return codegen_curry_apply(fn_name, cf, all_args);

    // 8. Exact arity: emit the direct call
    return emit_direct_call(fn_name, cf, all_args);
}

// ===== Closure wrapping =====

Value* Codegen::wrap_in_closure(Function* fn, CType ret_type) {
    auto ptr_ty = PointerType::get(*context_, 0);
    auto i64_ty = LType::getInt64Ty(*context_);

    // Check if wrapper already exists
    std::string wrapper_name = fn->getName().str() + "_env";
    auto* existing = module_->getFunction(wrapper_name);
    if (!existing) {
        // Build wrapper: fn_env(ptr env, original_args...) -> i64
        std::vector<LType*> wrapper_params = {ptr_ty};
        for (auto& arg : fn->args())
            wrapper_params.push_back(arg.getType());

        auto* wrapper_type = llvm::FunctionType::get(i64_ty, wrapper_params, false);
        existing = Function::Create(wrapper_type, Function::InternalLinkage,
                                    wrapper_name, module_.get());

        auto saved_block = builder_->GetInsertBlock();
        auto saved_point = builder_->GetInsertPoint();

        auto* entry = BasicBlock::Create(*context_, "entry", existing);
        builder_->SetInsertPoint(entry);

        // Forward args (skip env)
        auto arg_it = existing->arg_begin();
        arg_it->setName("env");
        ++arg_it;
        std::vector<Value*> forward_args;
        for (; arg_it != existing->arg_end(); ++arg_it)
            forward_args.push_back(&*arg_it);

        auto* result = builder_->CreateCall(fn, forward_args, "fwd");

        // Coerce return to i64 (universal closure return)
        Value* ret_val = result;
        if (ret_val->getType() != i64_ty) {
            if (ret_val->getType()->isPointerTy())
                ret_val = builder_->CreatePtrToInt(ret_val, i64_ty);
            else if (ret_val->getType()->isDoubleTy())
                ret_val = builder_->CreateBitCast(ret_val, i64_ty);
            else if (ret_val->getType()->isIntegerTy())
                ret_val = builder_->CreateZExtOrTrunc(ret_val, i64_ty);
            else if (ret_val->getType()->isStructTy()) {
                // Non-recursive ADT struct: heap-allocate and return as i64 pointer
                auto* sty = llvm::cast<llvm::StructType>(ret_val->getType());
                unsigned nf = sty->getNumElements();
                auto* tag_val = builder_->CreateExtractValue(ret_val, {0});
                auto* adt_ptr = builder_->CreateCall(rt_.adt_alloc_,
                    {tag_val, ConstantInt::get(i64_ty, nf - 1)});
                for (unsigned fi = 1; fi < nf; fi++) {
                    auto* fv = builder_->CreateExtractValue(ret_val, {fi});
                    builder_->CreateCall(rt_.adt_set_field_,
                        {adt_ptr, ConstantInt::get(i64_ty, fi - 1), fv});
                }
                ret_val = builder_->CreatePtrToInt(adt_ptr, i64_ty);
            }
        }
        builder_->CreateRet(ret_val);

        if (saved_block) builder_->SetInsertPoint(saved_block, saved_point);
    }

    // Create trivial closure {wrapper_fn_ptr, ret_tag, arity, <no captures>}
    int64_t arity = fn->arg_size(); // user args (wrapper has env + original params)
    return builder_->CreateCall(rt_.closure_create_,
        {existing, ConstantInt::get(i64_ty, static_cast<int64_t>(ret_type)),
         ConstantInt::get(i64_ty, arity),
         ConstantInt::get(i64_ty, 0)}, wrapper_name + "_closure");
}

TypedValue Codegen::auto_await(TypedValue tv) {
    if (!tv || tv.type != CType::PROMISE) return tv;

    // Dispatch: io_uring cookie vs opaque promise pointer (pool or extern native)
    Value* awaited;
    if (tv.promise_await == PromiseAwaitPath::IoUring) {
        awaited = builder_->CreateCall(rt_.io_await_, {tv.val}, "io_await");
    } else {
        llvm::Function* await_fn =
            current_group_ ? rt_.async_await_keep_ : rt_.async_await_;
        awaited = builder_->CreateCall(await_fn, {tv.val}, "await");
    }

    // The awaited value's type is stored in subtypes[0]
    CType inner = (!tv.subtypes.empty()) ? tv.subtypes[0] : CType::INT;
    // The await returns i64 — coerce to the actual type
    Value* result = awaited;
    auto* expected = llvm_type(inner);
    if (expected->isPointerTy())
        result = builder_->CreateIntToPtr(awaited, expected);
    else if (expected == LType::getInt1Ty(*context_))
        result = builder_->CreateTrunc(awaited, expected);
    else if (expected->isDoubleTy())
        result = builder_->CreateBitCast(awaited, expected);
    return {result, inner};
}

// ===== Local static helpers for type annotations =====

static CType yona_type_to_ctype(const types::Type& t) {
    if (std::holds_alternative<types::BuiltinType>(t)) {
        switch (std::get<types::BuiltinType>(t)) {
            case types::SignedInt64: case types::SignedInt32:
            case types::SignedInt16: case types::SignedInt128:
            case types::UnsignedInt64: case types::UnsignedInt32:
            case types::UnsignedInt16: case types::UnsignedInt128:
                return CType::INT;
            case types::Float64: case types::Float32: case types::Float128:
                return CType::FLOAT;
            case types::Bool: return CType::BOOL;
            case types::String: return CType::STRING;
            case types::Symbol: return CType::SYMBOL;
            case types::Unit: return CType::UNIT;
            case types::Seq: return CType::SEQ;
            case types::Set: return CType::SET;
            case types::Dict: return CType::DICT;
            default: return CType::INT;
        }
    }
    if (std::holds_alternative<std::shared_ptr<types::FunctionType>>(t))
        return CType::FUNCTION;
    if (std::holds_alternative<std::shared_ptr<types::SingleItemCollectionType>>(t)) {
        auto& col = std::get<std::shared_ptr<types::SingleItemCollectionType>>(t);
        return (col->kind == types::SingleItemCollectionType::Seq) ? CType::SEQ : CType::SET;
    }
    if (std::holds_alternative<std::shared_ptr<types::DictCollectionType>>(t))
        return CType::DICT;
    if (std::holds_alternative<std::shared_ptr<types::ProductType>>(t))
        return CType::TUPLE;
    if (std::holds_alternative<std::shared_ptr<types::NamedType>>(t)) {
        auto& nt = std::get<std::shared_ptr<types::NamedType>>(t);
        if (nt->name == "Channel") return CType::CHANNEL;
        if (nt->name == "FloatArray") return CType::FLOAT_ARRAY;
        if (nt->name == "IntArray") return CType::INT_ARRAY;
        if (nt->name == "ByteArray") return CType::BYTE_ARRAY;
        return CType::ADT;
    }
    if (std::holds_alternative<std::shared_ptr<types::PromiseType>>(t))
        return CType::PROMISE;
    if (std::holds_alternative<std::shared_ptr<types::SumType>>(t))
        return CType::SUM;
    if (std::holds_alternative<std::shared_ptr<types::RefinedType>>(t))
        return yona_type_to_ctype(std::get<std::shared_ptr<types::RefinedType>>(t)->base_type);
    return CType::INT;
}

static std::pair<std::vector<CType>, CType> uncurry_type_signature(const types::Type& t) {
    std::vector<CType> params;
    const types::Type* current = &t;
    while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(*current)) {
        auto& ft = std::get<std::shared_ptr<types::FunctionType>>(*current);
        params.push_back(yona_type_to_ctype(ft->argumentType));
        current = &ft->returnType;
    }
    return {params, yona_type_to_ctype(*current)};
}


} // namespace yona::compiler::codegen
