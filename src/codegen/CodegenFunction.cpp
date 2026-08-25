//
// Codegen — Function definition
//
// Free-variable analysis, function definition, lambda aliases, and
// function compilation (compile_function). Call-site / apply logic
// lives in CodegenApply.cpp.
//

#include "Codegen.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <iostream>

namespace yona::compiler::codegen {

// Closure layout constants — match CLOSURE_HDR_SIZE in compiled_runtime.c
static constexpr int CLOSURE_FIELD_FN = 0;      // fn_ptr
static constexpr int CLOSURE_FIELD_ARITY = 2;   // arity
static constexpr int CLOSURE_HDR_SIZE = 5;       // fn_ptr, ret_type, arity, num_captures, heap_mask

// Perceus phase 3: match YONA_MAX_FRAME_DROPS in src/runtime/exceptions.c
static constexpr int YONA_MAX_FRAME_DROPS = 16;

using namespace llvm;
using LType = llvm::Type;

// Layout must match yona_frame_t in src/runtime/exceptions.c:
//   struct yona_frame { yona_frame* prev; int drop_count; int pad;
//                       void* drops[YONA_MAX_FRAME_DROPS]; }
StructType* Codegen::get_frame_type() {
    if (yona_frame_ty_) return yona_frame_ty_;
    auto* ptr_ty = PointerType::get(*context_, 0);
    auto* i32_ty = LType::getInt32Ty(*context_);
    auto* drops_arr_ty = ArrayType::get(ptr_ty, YONA_MAX_FRAME_DROPS);
    yona_frame_ty_ = StructType::create(*context_,
        {ptr_ty, i32_ty, i32_ty, drops_arr_ty},
        "yona_frame_t");
    return yona_frame_ty_;
}

// Emit a yona_rt_frame_transfer(ptr) call — no-op unless the ptr is in
// the currently active frame's drops. Safe to call unconditionally from
// any transfer site; the runtime does a bounded linear scan.
void Codegen::emit_frame_transfer(Value* ptr) {
    // Only emit when the current function has a frame (non-TCO function
    // with heap params). Most hot-path functions (foldl, map, filter)
    // are TCO and skip frame setup, so this is a no-op for them.
    if (!current_frame_alloca_ || !rt_.frame_transfer_ || !ptr) return;
    if (!builder_->GetInsertBlock()) return;
    auto* ptr_ty = PointerType::get(*context_, 0);
    Value* arg = ptr;
    if (arg->getType()->isIntegerTy())
        arg = builder_->CreateIntToPtr(arg, ptr_ty);
    else if (!arg->getType()->isPointerTy())
        return;
    builder_->CreateCall(rt_.frame_transfer_, {arg});
}

// Forward declarations for type annotation support
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

// ===== Free variable analysis =====

static void bind_pattern_names(AstNode* pat, std::unordered_set<std::string>& bound) {
    if (!pat) return;
    switch (pat->get_type()) {
        case AST_PATTERN_VALUE: {
            auto* pv = static_cast<PatternValue*>(pat);
            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                bound.insert((*id)->name->value);
            break;
        }
        case AST_TUPLE_PATTERN:
            for (auto* p : static_cast<TuplePattern*>(pat)->patterns)
                bind_pattern_names(p, bound);
            break;
        case AST_SEQ_PATTERN:
            for (auto* p : static_cast<SeqPattern*>(pat)->patterns)
                bind_pattern_names(p, bound);
            break;
        case AST_HEAD_TAILS_PATTERN: {
            auto* hp = static_cast<HeadTailsPattern*>(pat);
            for (auto* h : hp->heads) bind_pattern_names(h, bound);
            bind_pattern_names(hp->tail, bound);
            break;
        }
        case AST_TAILS_HEAD_PATTERN: {
            auto* tp = static_cast<TailsHeadPattern*>(pat);
            bind_pattern_names(tp->tail, bound);
            for (auto* h : tp->heads) bind_pattern_names(h, bound);
            break;
        }
        case AST_HEAD_TAILS_HEAD_PATTERN: {
            auto* hp = static_cast<HeadTailsHeadPattern*>(pat);
            for (auto* h : hp->left) bind_pattern_names(h, bound);
            bind_pattern_names(hp->tail, bound);
            for (auto* h : hp->right) bind_pattern_names(h, bound);
            break;
        }
        case AST_CONSTRUCTOR_PATTERN:
            for (auto* p : static_cast<ConstructorPattern*>(pat)->sub_patterns)
                bind_pattern_names(p, bound);
            break;
        case AST_AS_DATA_STRUCTURE_PATTERN: {
            auto* ap = static_cast<AsDataStructurePattern*>(pat);
            if (ap->identifier) bound.insert(ap->identifier->name->value);
            bind_pattern_names(ap->pattern, bound);
            break;
        }
        case AST_TYPED_PATTERN:
            bound.insert(static_cast<TypedPattern*>(pat)->binding_name);
            break;
        case AST_OR_PATTERN:
            for (auto& p : static_cast<OrPattern*>(pat)->patterns)
                bind_pattern_names(p.get(), bound);
            break;
        case AST_DICT_PATTERN:
            for (auto& kv : static_cast<DictPattern*>(pat)->keyValuePairs) {
                bind_pattern_names(kv.first, bound);
                bind_pattern_names(kv.second, bound);
            }
            break;
        case AST_RECORD_PATTERN:
            for (auto& item : static_cast<RecordPattern*>(pat)->items)
                bind_pattern_names(item.second, bound);
            break;
        default:
            break;
    }
}

void Codegen::collect_free_vars(AstNode* node, const std::unordered_set<std::string>& bound,
                                 std::unordered_set<std::string>& free_vars) {
    if (!node) return;
    switch (node->get_type()) {
        case AST_IDENTIFIER_EXPR: {
            auto* id = static_cast<IdentifierExpr*>(node);
            if (!bound.count(id->name->value)) free_vars.insert(id->name->value);
            break;
        }
        case AST_ADD_EXPR: case AST_SUBTRACT_EXPR: case AST_MULTIPLY_EXPR:
        case AST_DIVIDE_EXPR: case AST_MODULO_EXPR:
        case AST_EQ_EXPR: case AST_NEQ_EXPR:
        case AST_LT_EXPR: case AST_GT_EXPR: case AST_LTE_EXPR: case AST_GTE_EXPR:
        case AST_LOGICAL_AND_EXPR: case AST_LOGICAL_OR_EXPR:
        case AST_CONS_LEFT_EXPR: case AST_CONS_RIGHT_EXPR: case AST_JOIN_EXPR:
        case AST_IN_EXPR: case AST_REMOVE_EXPR: {
            auto* bin = static_cast<AddExpr*>(node);
            collect_free_vars(bin->left, bound, free_vars);
            collect_free_vars(bin->right, bound, free_vars);
            break;
        }
        case AST_IF_EXPR: {
            auto* e = static_cast<IfExpr*>(node);
            collect_free_vars(e->condition, bound, free_vars);
            collect_free_vars(e->thenExpr, bound, free_vars);
            collect_free_vars(e->elseExpr, bound, free_vars);
            break;
        }
        case AST_LET_EXPR: {
            auto* e = static_cast<LetExpr*>(node);
            auto nb = bound;
            for (auto* a : e->aliases) {
                if (auto* va = dynamic_cast<ValueAlias*>(a)) {
                    collect_free_vars(va->expr, bound, free_vars);
                    nb.insert(va->identifier->name->value);
                } else if (auto* la = dynamic_cast<LambdaAlias*>(a))
                    nb.insert(la->name->value);
            }
            collect_free_vars(e->expr, nb, free_vars);
            break;
        }
        case AST_CASE_EXPR: {
            auto* e = static_cast<CaseExpr*>(node);
            collect_free_vars(e->expr, bound, free_vars);
            for (auto* c : e->clauses) {
                auto nb = bound;
                bind_pattern_names(c->pattern, nb);
                if (c->guard) collect_free_vars(c->guard, nb, free_vars);
                collect_free_vars(c->body, nb, free_vars);
            }
            break;
        }
        case AST_APPLY_EXPR: {
            auto* e = static_cast<ApplyExpr*>(node);
            if (auto* nc = dynamic_cast<NameCall*>(e->call)) {
                if (!bound.count(nc->name->value)) free_vars.insert(nc->name->value);
            } else if (auto* ec = dynamic_cast<ExprCall*>(e->call)) {
                if (ec->expr) collect_free_vars(ec->expr, bound, free_vars);
            }
            for (auto& a : e->args) {
                if (std::holds_alternative<ExprNode*>(a))
                    collect_free_vars(std::get<ExprNode*>(a), bound, free_vars);
                else collect_free_vars(std::get<ValueExpr*>(a), bound, free_vars);
            }
            break;
        }
        case AST_FUNCTION_EXPR: {
            // Lambda expression: \x -> body — add params to bound, recurse into body
            auto* fn = static_cast<FunctionExpr*>(node);
            auto nb = bound;
            for (auto* pat : fn->patterns)
                bind_pattern_names(pat, nb);
            for (auto* body : fn->bodies) {
                if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
                    collect_free_vars(bwg->expr, nb, free_vars);
            }
            break;
        }
        case AST_TUPLE_EXPR: {
            auto* e = static_cast<TupleExpr*>(node);
            for (auto* v : e->values) collect_free_vars(v, bound, free_vars);
            break;
        }
        case AST_VALUES_SEQUENCE_EXPR: {
            auto* e = static_cast<ValuesSequenceExpr*>(node);
            for (auto* v : e->values) collect_free_vars(v, bound, free_vars);
            break;
        }
        case AST_DO_EXPR: {
            auto* e = static_cast<DoExpr*>(node);
            for (auto* step : e->steps)
                collect_free_vars(step, bound, free_vars);
            break;
        }
        default: break;
    }
}

void Codegen::bind_parameter_pattern(PatternNode* pat, const TypedValue& value) {
    if (!pat || !value.val) return;
    auto i64_ty = LType::getInt64Ty(*context_);

    if (pat->get_type() == AST_TUPLE_PATTERN && value.type == CType::TUPLE) {
        auto* tuple = static_cast<TuplePattern*>(pat);
        Value* tuple_ptr = value.val;
        if (!tuple_ptr->getType()->isPointerTy())
            tuple_ptr = builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0));
        for (size_t i = 0; i < tuple->patterns.size(); ++i) {
            auto* field = tuple->patterns[i];
            if (field->get_type() != AST_PATTERN_VALUE) continue;
            auto* pv = static_cast<PatternValue*>(field);
            auto* id = std::get_if<IdentifierExpr*>(&pv->expr);
            if (!id) continue;
            auto* gep = builder_->CreateGEP(i64_ty, tuple_ptr,
                {ConstantInt::get(i64_ty, i + 2)}, "param_tuple_gep");
            auto* element = builder_->CreateLoad(i64_ty, gep, "param_tuple_elem");
            CType field_type = i < value.subtypes.size() ? value.subtypes[i] : CType::INT;
            named_values_[(*id)->name->value] = {element, field_type};
        }
        return;
    }

    if (pat->get_type() != AST_CONSTRUCTOR_PATTERN || value.type != CType::ADT) return;
    auto* ctor_pat = static_cast<ConstructorPattern*>(pat);
    auto ctor_it = types_.adt_constructors.find(ctor_pat->constructor_name);
    if (ctor_it == types_.adt_constructors.end()) return;
    const auto& ctor = ctor_it->second;
    bool heap_layout = ctor.is_recursive || value.val->getType()->isPointerTy();
    Value* adt_ptr = value.val;
    if (heap_layout && !adt_ptr->getType()->isPointerTy())
        adt_ptr = builder_->CreateIntToPtr(adt_ptr, PointerType::get(*context_, 0));

    for (size_t i = 0; i < ctor_pat->sub_patterns.size(); ++i) {
        auto* field = ctor_pat->sub_patterns[i];
        if (field->get_type() != AST_PATTERN_VALUE) continue;
        auto* pv = static_cast<PatternValue*>(field);
        auto* id = std::get_if<IdentifierExpr*>(&pv->expr);
        if (!id) continue;
        CType field_type = i < ctor.field_types.size() ? ctor.field_types[i] : CType::INT;
        Value* field_value = heap_layout
            ? builder_->CreateCall(rt_.adt_get_field_, {adt_ptr, ConstantInt::get(i64_ty, i)})
            : builder_->CreateExtractValue(value.val, {static_cast<unsigned>(i + 1)});
        if (heap_layout && (field_type == CType::ADT || field_type == CType::SEQ ||
                            field_type == CType::STRING || field_type == CType::FUNCTION ||
                            field_type == CType::SET || field_type == CType::DICT ||
                            field_type == CType::CHANNEL || field_type == CType::TUPLE))
            field_value = builder_->CreateIntToPtr(field_value, PointerType::get(*context_, 0));
        named_values_[(*id)->name->value] = {field_value, field_type};
    }
}

// ===== Functions (deferred compilation) =====

TypedValue Codegen::codegen_function_def(FunctionExpr* node, const std::string& name) {
    set_debug_loc(node->source_context);
    std::string fn_name = name.empty() ? (node->name.empty() ? "lambda_" + std::to_string(lambda_counter_++) : node->name) : name;

    // Extract parameter names and collect bound variables from patterns.
    // For simple variable patterns: param name is the variable name.
    // For complex patterns (tuples, sequences): param name is synthetic,
    // and the pattern's variables are added to the bound set for free var analysis.
    DeferredFunction def;
    def.ast = node;
    std::unordered_set<std::string> bound;
    for (size_t i = 0; i < node->patterns.size(); i++) {
        std::string pname = "arg" + std::to_string(i);
        auto* pat = node->patterns[i];
        if (pat->get_type() == AST_PATTERN_VALUE) {
            auto* pv = static_cast<PatternValue*>(pat);
            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                pname = (*id)->name->value;
                bound.insert(pname);
            }
        } else if (pat->get_type() == AST_TUPLE_PATTERN) {
            // Collect variable names from tuple elements
            auto* tp = static_cast<TuplePattern*>(pat);
            for (auto* elem : tp->patterns) {
                if (elem->get_type() == AST_PATTERN_VALUE) {
                    auto* pv = static_cast<PatternValue*>(elem);
                    if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                        bound.insert((*id)->name->value);
                }
            }
        } else if (pat->get_type() == AST_HEAD_TAILS_PATTERN) {
            auto* hp = static_cast<HeadTailsPattern*>(pat);
            for (auto* head : hp->heads) {
                if (head->get_type() == AST_PATTERN_VALUE) {
                    auto* pv = static_cast<PatternValue*>(head);
                    if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                        bound.insert((*id)->name->value);
                }
            }
            if (hp->tail && hp->tail->get_type() == AST_PATTERN_VALUE) {
                auto* pv = static_cast<PatternValue*>(hp->tail);
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                    bound.insert((*id)->name->value);
            }
        }
        def.param_names.push_back(pname);
        bound.insert(pname);
    }
    std::unordered_set<std::string> fv_set;
    if (!node->bodies.empty()) {
        auto* body = node->bodies[0];
        if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
            collect_free_vars(bwg->expr, bound, fv_set);
    }
    for (auto& v : fv_set) {
        auto it = named_values_.find(v);
        if (it != named_values_.end()) {
            // Skip known functions (compiled or deferred) — they're global symbols.
            // But DO capture function pointers (runtime values: parameters, closures).
            if (it->second.type == CType::FUNCTION) {
                // Compiled function: usually a global LLVM function (skip), but
                // an enclosing closure under compilation also registers itself
                // in named_values_ as a Function* for recursive-call resolution.
                // That case has closure_env set in compiled_functions_; we must
                // capture it so the inner lambda can call the recursive closure
                // with the correct env.
                if (it->second.val && isa<Function>(it->second.val)) {
                    auto cf_it = compiled_functions_.find(v);
                    bool is_closure_under_compile =
                        cf_it != compiled_functions_.end() && cf_it->second.closure_env;
                    if (!is_closure_under_compile) continue;
                }
                // Deferred function: skip (will be compiled later as a global)
                else if (deferred_functions_.count(v) > 0)
                    continue;
                // Extern/imported function: skip (resolved at link time)
                if (imports_.extern_functions.count(v) > 0)
                    continue;
                // Null-valued function (e.g., not yet compiled): skip
                if (!it->second.val)
                    continue;
                // Runtime function pointer (parameter, closure): capture it
            }
            def.free_vars.push_back(v);
        }
    }

    if (!def.free_vars.empty()) {
        // Free vars exist: compile as closure with env-passing convention.
        // Function signature: (ptr env, regular_args...) -> ret
        // Captures are loaded from env[1], env[2], ... inside the function body.
        // A closure object {fn_ptr, cap0, cap1, ...} is created at the current site.
        auto ptr_ty = PointerType::get(*context_, 0);
        auto i64_ty = LType::getInt64Ty(*context_);

        // Collect capture values and types from current scope.
        // Special case: if the free var names a closure that is itself still
        // being compiled (an enclosing `let pull _ = …` that we're inside
        // the body of), capture that closure's env pointer — which, inside
        // pull's body, is pull's own closure. Capturing the raw Function*
        // would drop the env and any calls from the inner lambda would pass
        // a garbage env.
        std::vector<TypedValue> capture_tvs;
        for (auto& fv : def.free_vars) {
            auto it = named_values_.find(fv);
            TypedValue cap = it->second;
            auto cf_it = compiled_functions_.find(fv);
            if (cf_it != compiled_functions_.end() && cf_it->second.closure_env &&
                cap.val && isa<Function>(cap.val)) {
                cap.val = cf_it->second.closure_env;
            }
            capture_tvs.push_back(cap);
        }

        // Build param types: (ptr env, i64 args...)
        std::vector<LType*> param_types = {ptr_ty};
        for (size_t pi = 0; pi < def.param_names.size(); pi++)
            param_types.push_back(i64_ty);

        // Infer return type from body
        CType ret_ctype = CType::INT;
        LType* ret_llvm = i64_ty;
        if (!node->bodies.empty()) {
            auto* body = node->bodies[0];
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
                auto [lt, ct] = infer_return_type(bwg->expr);
                ret_llvm = lt;
                ret_ctype = ct;
            }
        }
        auto fn_type = llvm::FunctionType::get(ret_llvm, param_types, false);
        auto* fn = Function::Create(fn_type, Function::InternalLinkage, fn_name, module_.get());

        // Register preliminary for recursive calls (closure_env set below after entry block)
        CompiledFunction cf_pre;
        cf_pre.fn = fn;
        cf_pre.return_type = ret_ctype;
        for (size_t pi = 0; pi < def.param_names.size(); pi++)
            cf_pre.param_types.push_back(CType::INT);
        compiled_functions_[fn_name] = cf_pre;
        named_values_[fn_name] = {fn, CType::FUNCTION};

        // Save state
        auto saved_block = builder_->GetInsertBlock();
        auto saved_point = builder_->GetInsertPoint();
        auto saved_values = named_values_;
        auto saved_di_scope = debug_.scope;
        auto saved_debug_loc = builder_->getCurrentDebugLocation();
        // Nested LLVM functions must not inherit the enclosing function's
        // task-group arena / group pointers (SSA values in the outer fn).
        auto saved_outer_arena = current_arena_;
        auto saved_outer_group = current_group_;
        current_arena_ = nullptr;
        current_group_ = nullptr;

        // Create debug info for closure function
        if (debug_.enabled && debug_.builder && debug_.file) {
            auto* di_func_ty = debug_.builder->createSubroutineType(
                debug_.builder->getOrCreateTypeArray({}));
            auto* di_sp = debug_.builder->createFunction(
                debug_.file, fn_name, fn_name, debug_.file,
                node->source_context.line, di_func_ty, node->source_context.line,
                DINode::FlagZero, DISubprogram::SPFlagDefinition);
            fn->setSubprogram(di_sp);
            debug_.scope = di_sp;
        }

        auto* entry = BasicBlock::Create(*context_, "entry", fn);
        builder_->SetInsertPoint(entry);
        if (debug_.enabled && debug_.scope)
            builder_->SetCurrentDebugLocation(
                DILocation::get(*context_, node->source_context.line, 0, debug_.scope));

        // Set up scope: keep function references from outer scope
        named_values_.clear();
        for (auto& [k, v] : saved_values) {
            if (v.type == CType::FUNCTION) named_values_[k] = v;
        }
        named_values_[fn_name] = {fn, CType::FUNCTION};

        auto arg_it = fn->arg_begin();
        Value* env = &*arg_it; env->setName("env");
        ++arg_it;

        // Update preliminary entry with env for recursive closure calls
        compiled_functions_[fn_name].closure_env = env;

        // Load captures from env
        for (size_t ci = 0; ci < def.free_vars.size(); ci++) {
            auto* cap_val = builder_->CreateCall(rt_.closure_get_cap_,
                {env, ConstantInt::get(i64_ty, ci)}, def.free_vars[ci] + "_cap");
            CType cap_type = capture_tvs[ci].type;
            Value* typed_val = cap_val;
            if (cap_type == CType::FUNCTION || cap_type == CType::SEQ ||
                cap_type == CType::STRING || cap_type == CType::ADT ||
                cap_type == CType::SET || cap_type == CType::DICT)
                typed_val = builder_->CreateIntToPtr(cap_val, ptr_ty);
            else if (cap_type == CType::FLOAT)
                typed_val = builder_->CreateBitCast(cap_val, LType::getDoubleTy(*context_));
            else if (cap_type == CType::BOOL)
                typed_val = builder_->CreateTrunc(cap_val, LType::getInt1Ty(*context_));
            named_values_[def.free_vars[ci]] = {typed_val, cap_type, capture_tvs[ci].subtypes};
        }

        // Bind regular params
        for (size_t pi = 0; pi < def.param_names.size(); pi++) {
            Value* param = &*arg_it; param->setName(def.param_names[pi]);
            ++arg_it;
            named_values_[def.param_names[pi]] = {param, CType::INT};
        }

        // Compile body. Stash body AST for Perceus-linear single-use
        // detection on seq params referenced inside. Transfer tracking
        // is restored at the bottom of this branch, after any exit
        // drops — so body-local transfers are visible then.
        auto saved_fn_body = current_fn_body_;
        auto saved_transferred = transferred_values_;
        transferred_values_.clear();
        TypedValue body_tv;
        if (!node->bodies.empty()) {
            auto* body_node = node->bodies[0];
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body_node)) {
                current_fn_body_ = bwg->expr;
                body_tv = codegen(bwg->expr);
            }
        }
        current_fn_body_ = saved_fn_body;

        // Coerce body result to match the function's declared return type
        if (body_tv && body_tv.val) {
            Value* ret_val = body_tv.val;
            // Match declared return type
            if (ret_val->getType() != ret_llvm) {
                if (ret_val->getType()->isIntegerTy() && ret_llvm->isPointerTy())
                    ret_val = builder_->CreateIntToPtr(ret_val, ret_llvm);
                else if (ret_val->getType()->isPointerTy() && ret_llvm->isIntegerTy())
                    ret_val = builder_->CreatePtrToInt(ret_val, ret_llvm);
                else if (ret_val->getType()->isIntegerTy() && ret_llvm->isIntegerTy())
                    ret_val = builder_->CreateZExtOrTrunc(ret_val, ret_llvm);
            }
            if (!builder_->GetInsertBlock()->getTerminator())
                builder_->CreateRet(ret_val);
        } else {
            if (!builder_->GetInsertBlock()->getTerminator()) {
                auto* default_ret = Constant::getNullValue(ret_llvm);
                builder_->CreateRet(default_ret);
            }
        }

        // Restore state
        current_arena_ = saved_outer_arena;
        current_group_ = saved_outer_group;
        named_values_ = saved_values;
        debug_.scope = saved_di_scope;
        if (saved_block) builder_->SetInsertPoint(saved_block, saved_point);
        if (debug_.enabled) builder_->SetCurrentDebugLocation(saved_debug_loc);

        // Remove from compiled_functions_ — closure functions are called through
        // their closure pointer (indirect call path), not directly.
        // The preliminary entry was only needed during body compilation for
        // potential recursive self-calls.
        compiled_functions_.erase(fn_name);

        // Create closure object at the current insertion point
        // Store return CType tag and user-arg arity for call-site dispatch
        auto* closure = builder_->CreateCall(rt_.closure_create_,
            {fn, ConstantInt::get(i64_ty, static_cast<int64_t>(ret_ctype)),
             ConstantInt::get(i64_ty, def.param_names.size()),
             ConstantInt::get(i64_ty, def.free_vars.size())}, fn_name + "_closure");

        // Store captured values — rc_inc each heap-typed capture since the
        // closure now holds an additional reference.
        // Exception: self-capture (recursive closure) uses a weak reference
        // to break the cycle. Without this, recursive closures leak forever.
        // Build heap_mask for recursive destruction: bit i is set if capture i
        // is heap-typed (pointer that needs rc_dec when the closure is freed).
        int64_t heap_mask = 0;
        for (size_t ci = 0; ci < capture_tvs.size(); ci++) {
            bool is_self = (ci < def.free_vars.size() && def.free_vars[ci] == fn_name);
            bool is_heap = is_heap_type(capture_tvs[ci].type) &&
                           !capture_tvs[ci].val->getType()->isStructTy();
            if (is_heap && !is_self) {
                emit_rc_inc(capture_tvs[ci].val, capture_tvs[ci].type);
                if (ci < 64) heap_mask |= ((int64_t)1 << ci);
            }
            Value* cap_i64 = capture_tvs[ci].val;
            // Non-recursive ADT struct values can't fit in an i64 capture slot.
            // Box them to a heap-allocated ADT (using the recursive layout) so
            // the closure can store a pointer.
            if (capture_tvs[ci].type == CType::ADT && cap_i64->getType()->isStructTy()) {
                auto* struct_ty = cap_i64->getType();
                int num_fields = struct_ty->getStructNumElements() - 1; // minus tag
                // Extract tag
                Value* tag_val = builder_->CreateExtractValue(cap_i64, {0});
                if (tag_val->getType() != i64_ty)
                    tag_val = builder_->CreateZExtOrTrunc(tag_val, i64_ty);
                // Allocate heap ADT with the same structure
                auto* heap_adt = builder_->CreateCall(rt_.adt_alloc_,
                    {tag_val, ConstantInt::get(i64_ty, num_fields)}, "boxed_adt");
                // Copy fields
                for (int fi = 0; fi < num_fields; fi++) {
                    Value* field = builder_->CreateExtractValue(cap_i64, {(unsigned)(fi + 1)});
                    if (field->getType() != i64_ty) {
                        if (field->getType()->isPointerTy())
                            field = builder_->CreatePtrToInt(field, i64_ty);
                        else if (field->getType()->isIntegerTy())
                            field = builder_->CreateZExtOrTrunc(field, i64_ty);
                    }
                    builder_->CreateCall(rt_.adt_set_field_,
                        {heap_adt, ConstantInt::get(i64_ty, fi), field});
                }
                cap_i64 = builder_->CreatePtrToInt(heap_adt, i64_ty);
                // Mark this capture as heap so rc_dec frees it
                if (ci < 64) heap_mask |= ((int64_t)1 << ci);
            }
            else if (cap_i64->getType()->isPointerTy())
                cap_i64 = builder_->CreatePtrToInt(cap_i64, i64_ty);
            else if (cap_i64->getType()->isDoubleTy())
                cap_i64 = builder_->CreateBitCast(cap_i64, i64_ty);
            else if (cap_i64->getType()->isIntegerTy() && cap_i64->getType() != i64_ty)
                cap_i64 = builder_->CreateZExtOrTrunc(cap_i64, i64_ty);
            builder_->CreateCall(rt_.closure_set_cap_,
                {closure, ConstantInt::get(i64_ty, ci), cap_i64});
        }
        // Store heap_mask so rc_dec can recursively free captures
        if (heap_mask != 0)
            builder_->CreateCall(rt_.closure_set_heap_mask_,
                {closure, ConstantInt::get(i64_ty, heap_mask)});

        last_lambda_name_ = fn_name;
        named_values_[fn_name] = {closure, CType::FUNCTION, {ret_ctype}};
        transferred_values_ = saved_transferred;
        return {closure, CType::FUNCTION, {ret_ctype}};
    }

    deferred_functions_[fn_name] = def;
    last_lambda_name_ = fn_name;
    return {nullptr, CType::FUNCTION}; // no value yet, compiled at call site
}

TypedValue Codegen::codegen_lambda_alias(LambdaAlias* node) {
    auto fn_name = node->name->value;
    auto tv = codegen_function_def(node->lambda, fn_name);
    // For closures, tv has the closure pointer; for deferred, tv is {nullptr, FUNCTION}
    named_values_[fn_name] = tv;
    last_lambda_name_ = fn_name;
    return tv;
}

Codegen::CompiledFunction Codegen::compile_function(
    const std::string& name, const DeferredFunction& def, const std::vector<TypedValue>& args) {

    // If the function has a type annotation, use it to determine param types
    // instead of relying on caller's arg types (which may be wrong).
    std::vector<CType> annotated_ctypes;
    std::vector<std::string> annotated_adt_names;
    std::string annotated_return_adt_name;
    CType annotated_ret = CType::INT;
    bool has_annotated_ret = false;
    if (def.ast->type_signature.has_value()) {
        auto [ann_params, ann_ret] = uncurry_type_signature(*def.ast->type_signature);
        annotated_ctypes = ann_params;
        annotated_ret = ann_ret;
        has_annotated_ret = true;
        const types::Type* current_type = &*def.ast->type_signature;
        while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(*current_type)) {
            auto ft = std::get<std::shared_ptr<types::FunctionType>>(*current_type);
            if (std::holds_alternative<std::shared_ptr<types::NamedType>>(ft->argumentType))
                annotated_adt_names.push_back(
                    std::get<std::shared_ptr<types::NamedType>>(ft->argumentType)->name);
            else
                annotated_adt_names.push_back("");
            current_type = &ft->returnType;
        }
        if (std::holds_alternative<std::shared_ptr<types::NamedType>>(*current_type))
            annotated_return_adt_name =
                std::get<std::shared_ptr<types::NamedType>>(*current_type)->name;
    }

    // Build parameter types from type annotation or actual argument types.
    std::vector<LType*> param_types;
    std::vector<CType> param_ctypes;
    for (size_t i = 0; i < def.param_names.size(); i++) {
        CType ct = (!annotated_ctypes.empty() && i < annotated_ctypes.size())
            ? annotated_ctypes[i]
            : ((i < args.size()) ? args[i].type : CType::INT);
        if (i < args.size() && args[i].val && args[i].val->getType() != llvm_type(ct)) {
            // Use the actual LLVM type from the argument (e.g., struct for TUPLE)
            param_types.push_back(args[i].val->getType());
        } else {
            param_types.push_back(llvm_type(ct));
        }
        param_ctypes.push_back(ct);
    }

    // CType upgrade: when an i64 param is used as a seq case scrutinee,
    // upgrade to SEQ/ptr so Perceus fires. The matching rc_inc is handled
    // by the caller-side conditional dec in codegen_higher_order_call.
    if (!def.ast->bodies.empty()) {
        auto* body = def.ast->bodies[0];
        if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
            std::function<bool(AstNode*, const std::string&)> has_seq_case;
            has_seq_case = [&](AstNode* n, const std::string& pname) -> bool {
                if (!n) return false;
                if (n->get_type() == AST_CASE_EXPR) {
                    auto* ce = static_cast<CaseExpr*>(n);
                    if (ce->expr && ce->expr->get_type() == AST_IDENTIFIER_EXPR) {
                        auto* id = static_cast<IdentifierExpr*>(ce->expr);
                        if (id->name->value == pname) {
                            for (auto* cl : ce->clauses) {
                                auto pt = cl->pattern->get_type();
                                if (pt == AST_SEQ_PATTERN || pt == AST_HEAD_TAILS_PATTERN)
                                    return true;
                            }
                        }
                    }
                    for (auto* cl : ce->clauses)
                        if (has_seq_case(cl->body, pname)) return true;
                }
                if (n->get_type() == AST_IF_EXPR) {
                    auto* ie = static_cast<IfExpr*>(n);
                    return has_seq_case(ie->thenExpr, pname)
                        || has_seq_case(ie->elseExpr, pname);
                }
                if (n->get_type() == AST_LET_EXPR)
                    return has_seq_case(static_cast<LetExpr*>(n)->expr, pname);
                return false;
            };
            auto* ptr_ty_local = PointerType::get(*context_, 0);
            for (size_t pi = 0; pi < def.param_names.size(); pi++) {
                if (pi >= param_ctypes.size()) continue;
                if (param_ctypes[pi] != CType::INT) continue;
                if (has_seq_case(bwg->expr, def.param_names[pi])) {
                    param_ctypes[pi] = CType::SEQ;
                    param_types[pi] = ptr_ty_local;
                }
            }
        }
    }

    // Add free variable types
    std::vector<TypedValue> capture_values;
    for (auto& fv : def.free_vars) {
        auto it = named_values_.find(fv);
        if (it != named_values_.end()) {
            param_types.push_back(llvm_type(it->second.type));
            param_ctypes.push_back(it->second.type);
            capture_values.push_back(it->second);
        }
    }

    // Pre-infer return LLVM type from body structure. Returns the actual
    // LLVM type (including struct layout for tuples) so the function is
    // created with the correct signature — no recreation needed.
    auto i64_ty = LType::getInt64Ty(*context_);
    auto tag_ty = LType::getInt64Ty(*context_);

    std::function<std::pair<LType*, CType>(AstNode*)> infer_ret = [&](AstNode* node) -> std::pair<LType*, CType> {
        if (!node) return {i64_ty, CType::INT};
        auto ct = node->get_type();
        if (ct == AST_VALUES_SEQUENCE_EXPR || ct == AST_CONS_LEFT_EXPR ||
            ct == AST_CONS_RIGHT_EXPR || ct == AST_JOIN_EXPR || ct == AST_REMOVE_EXPR)
            return {llvm_type(CType::SEQ), CType::SEQ};
        if (ct == AST_IN_EXPR)
            return {llvm_type(CType::BOOL), CType::BOOL};
        if (ct == AST_SET_EXPR) return {llvm_type(CType::SET), CType::SET};
        if (ct == AST_DICT_EXPR) return {llvm_type(CType::DICT), CType::DICT};
        if (ct == AST_SYMBOL_EXPR) return {i64_ty, CType::SYMBOL};
        if (ct == AST_STRING_EXPR) return {llvm_type(CType::STRING), CType::STRING};
        if (ct == AST_INTEGER_EXPR) return {i64_ty, CType::INT};
        if (ct == AST_FLOAT_EXPR) return {llvm_type(CType::FLOAT), CType::FLOAT};
        if (ct == AST_TRUE_LITERAL_EXPR || ct == AST_FALSE_LITERAL_EXPR)
            return {llvm_type(CType::BOOL), CType::BOOL};
        if (ct == AST_FUNCTION_EXPR)
            return {PointerType::get(*context_, 0), CType::FUNCTION};
        if (ct == AST_TUPLE_EXPR) {
            // Tuples are boxed as i64 (ptrtoint'd heap pointer)
            return {i64_ty, CType::TUPLE};
        }
        if (ct == AST_FIELD_ACCESS_EXPR) {
            auto* fa = static_cast<FieldAccessExpr*>(node);
            std::string fname = fa->name->value;
            for (auto& [cname, info] : types_.adt_constructors) {
                for (size_t fi = 0; fi < info.field_names.size(); fi++) {
                    if (info.field_names[fi] == fname && fi < info.field_types.size())
                        return {llvm_type(info.field_types[fi]), info.field_types[fi]};
                }
            }
        }
        if (ct == AST_IDENTIFIER_EXPR) {
            auto* id = static_cast<IdentifierExpr*>(node);
            for (size_t pi = 0; pi < def.param_names.size(); pi++) {
                if (def.param_names[pi] == id->name->value && pi < args.size()) {
                    LType* lt = (args[pi].val && args[pi].val->getType() != llvm_type(args[pi].type))
                        ? args[pi].val->getType() : llvm_type(args[pi].type);
                    return {lt, args[pi].type};
                }
            }
            // Check ADT constructors (zero-arity)
            auto adt_it = types_.adt_constructors.find(id->name->value);
            if (adt_it != types_.adt_constructors.end()) {
                if (adt_it->second.is_recursive)
                    return {PointerType::get(*context_, 0), CType::ADT};
                std::vector<LType*> fields = {tag_ty};
                for (int f = 0; f < adt_it->second.max_arity; f++) fields.push_back(i64_ty);
                return {StructType::get(*context_, fields), CType::ADT};
            }
        }
        // ADT constructor application: Next n tail → returns the ADT type
        if (ct == AST_APPLY_EXPR) {
            auto* app = static_cast<ApplyExpr*>(node);
            // Walk the apply chain to find the root name
            ApplyExpr* a = app;
            std::string root_name;
            while (a) {
                if (auto* nc = dynamic_cast<NameCall*>(a->call)) {
                    root_name = nc->name->value; break;
                } else if (auto* ec = dynamic_cast<ExprCall*>(a->call)) {
                    if (auto* inner = dynamic_cast<ApplyExpr*>(ec->expr)) a = inner;
                    else break;
                } else break;
            }
            if (!root_name.empty()) {
                auto adt_it = types_.adt_constructors.find(root_name);
                if (adt_it != types_.adt_constructors.end()) {
                    if (adt_it->second.is_recursive)
                        return {PointerType::get(*context_, 0), CType::ADT};
                    std::vector<LType*> fields = {tag_ty};
                    for (int f = 0; f < adt_it->second.max_arity; f++) fields.push_back(i64_ty);
                    return {StructType::get(*context_, fields), CType::ADT};
                }
            }
        }
        // Recurse through wrappers
        if (ct == AST_CASE_EXPR) {
            auto* ce = static_cast<CaseExpr*>(node);
            if (!ce->clauses.empty()) return infer_ret(ce->clauses[0]->body);
        }
        if (ct == AST_LET_EXPR) return infer_ret(static_cast<LetExpr*>(node)->expr);
        if (ct == AST_IF_EXPR) return infer_ret(static_cast<IfExpr*>(node)->thenExpr);
        return {i64_ty, CType::INT};
    };

    CType preliminary_ret = CType::INT;
    LType* ret_type = i64_ty;
    if (!def.ast->bodies.empty()) {
        auto* body = def.ast->bodies[0];
        if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
            auto [lt, ct] = infer_ret(bwg->expr);
            ret_type = lt;
            preliminary_ret = ct;
        }
    }
    // Type annotations win over structural inference (e.g. `factor * x`
    // is AST_MULTIPLY and infer_ret would otherwise leave i64).
    if (has_annotated_ret) {
        preliminary_ret = annotated_ret;
        ret_type = llvm_type(annotated_ret);
    }

    auto fn_type = llvm::FunctionType::get(ret_type, param_types, false);
    auto fn = Function::Create(fn_type, Function::InternalLinkage, name, module_.get());

    // Borrow inference must run before preliminary registration so recursive
    // calls see the same ownership contract as non-recursive calls.
    std::vector<bool> borrowed = infer_borrowed_params(name, def, param_ctypes);
    auto param_is_borrowed = [&](size_t pi) {
        return pi < borrowed.size() && borrowed[pi];
    };
    auto annotated_param_is_unboxed_enum = [&](size_t pi) {
        if (annotated_adt_names.empty() || pi >= annotated_adt_names.size() ||
            annotated_adt_names[pi].empty())
            return false;
        TypedValue tv;
        tv.type = CType::ADT;
        tv.adt_type_name = annotated_adt_names[pi];
        return is_unboxed_enum_adt(tv);
    };

    CompiledFunction cf_preliminary;
    cf_preliminary.fn = fn;
    cf_preliminary.return_type = preliminary_ret;
    cf_preliminary.param_types = param_ctypes;
    cf_preliminary.borrowed_params = borrowed;
    cf_preliminary.capture_names = def.free_vars;
    compiled_functions_[name] = cf_preliminary;
    named_values_[name] = {fn, CType::FUNCTION};

    // Save state
    auto saved_block = builder_->GetInsertBlock();
    auto saved_point = builder_->GetInsertPoint();
    auto saved_values = named_values_;
    auto saved_di_scope = debug_.scope;
    auto saved_debug_loc = builder_->getCurrentDebugLocation();
    auto saved_outer_arena = current_arena_;
    auto saved_outer_group = current_group_;
    current_arena_ = nullptr;
    current_group_ = nullptr;

    // Create debug info for this function
    if (debug_.enabled && debug_.builder && debug_.file) {
        auto* di_func_ty = di_func_type(param_ctypes, preliminary_ret);
        auto* di_sp = debug_.builder->createFunction(
            debug_.file, name, name, debug_.file,
            def.ast->source_context.line, di_func_ty, def.ast->source_context.line,
            DINode::FlagZero, DISubprogram::SPFlagDefinition);
        fn->setSubprogram(di_sp);
        debug_.scope = di_sp;
    }

    auto entry = BasicBlock::Create(*context_, "entry", fn);
    builder_->SetInsertPoint(entry);

    // Set debug location to this function's scope (not the caller's)
    if (debug_.enabled && debug_.scope)
        builder_->SetCurrentDebugLocation(
            DILocation::get(*context_, def.ast->source_context.line, 0, debug_.scope));

    // Bind parameters
    named_values_.clear();
    // Keep function references from outer scope
    for (auto& [k, v] : saved_values) {
        if (v.type == CType::FUNCTION) named_values_[k] = v;
    }
    // Also keep deferred function knowledge
    named_values_[name] = {fn, CType::FUNCTION};

    size_t i = 0;
    for (auto& arg : fn->args()) {
        std::string pname;
        CType ct;
        std::vector<CType> st;
        if (i < def.param_names.size()) {
            pname = def.param_names[i];
            ct = (i < args.size()) ? args[i].type : CType::INT;
            if (i < args.size()) st = args[i].subtypes;
            // For FUNCTION args, look up return type for type propagation.
            // Do NOT propagate closure_known_fn_ to formal params — the
            // compiled function is cached and may be called with different
            // closures. Devirtualizing here hardcodes the first closure's
            // function pointer into the IR, producing wrong results for
            // subsequent calls with different closures (see bug: "Closure
            // devirtualization with polymorphic HOFs" in todo-list.md).
            if (ct == CType::FUNCTION && i < args.size() && args[i].val) {
                if (auto* fn_val = dyn_cast<Function>(args[i].val)) {
                    if (st.empty()) {
                        auto cf_it = compiled_functions_.find(fn_val->getName().str());
                        if (cf_it != compiled_functions_.end())
                            st = {cf_it->second.return_type};
                    }
                }
            }
        } else {
            pname = def.free_vars[i - def.param_names.size()];
            size_t ci = i - def.param_names.size();
            ct = (ci < capture_values.size()) ? capture_values[ci].type : CType::INT;
        }
        arg.setName(pname);
        TypedValue param_tv{&arg, ct, st};
        // Propagate ADT type name from argument
        if (ct == CType::ADT && !annotated_adt_names.empty() && i < annotated_adt_names.size() &&
            !annotated_adt_names[i].empty()) {
            param_tv.adt_type_name = annotated_adt_names[i];
        } else if (ct == CType::ADT && i < args.size() && !args[i].adt_type_name.empty()) {
            param_tv.adt_type_name = args[i].adt_type_name;
        }
        named_values_[pname] = param_tv;

        if (i < def.ast->patterns.size())
            bind_parameter_pattern(def.ast->patterns[i], param_tv);

        // Emit parameter debug info (only for user params, not captures)
        if (debug_.enabled && debug_.scope && debug_.builder && i < def.param_names.size()) {
            auto* alloca = builder_->CreateAlloca(arg.getType(), nullptr, pname + ".dbg");
            builder_->CreateStore(&arg, alloca);
            auto* di_param = debug_.builder->createParameterVariable(
                debug_.scope, pname, i + 1, debug_.file,
                def.ast->source_context.line, di_type_for(ct));
            debug_.builder->insertDeclare(alloca, di_param, debug_.builder->createExpression(),
                DILocation::get(*context_, def.ast->source_context.line, 0, debug_.scope),
                builder_->GetInsertBlock());
        }

        i++;
    }

    // Check for self-recursive tail call in the body.
    // If found, we'll move RC cleanup before the call so LLVM TCE works.
    bool has_self_tail_call = false;
    {
        std::function<bool(AstNode*)> check_tail = [&](AstNode* n) -> bool {
            if (!n) return false;
            if (n->get_type() == AST_APPLY_EXPR) {
                auto* app = static_cast<ApplyExpr*>(n);
                // Walk curried apply chain: `f a b` parses as `((f a) b)`.
                // The root NameCall is at the innermost ExprCall.
                ApplyExpr* cur = app;
                while (cur) {
                    if (auto* nc = dynamic_cast<NameCall*>(cur->call)) {
                        if (nc->name->value == name) return true;
                        break;
                    }
                    if (auto* ec = dynamic_cast<ExprCall*>(cur->call)) {
                        if (auto* inner = dynamic_cast<ApplyExpr*>(ec->expr))
                            cur = inner;
                        else break;
                    } else break;
                }
            }
            if (n->get_type() == AST_CASE_EXPR) {
                auto* ce = static_cast<CaseExpr*>(n);
                for (auto* clause : ce->clauses)
                    if (check_tail(clause->body)) return true;
            }
            if (n->get_type() == AST_IF_EXPR) {
                auto* ie = static_cast<IfExpr*>(n);
                return check_tail(ie->thenExpr) || check_tail(ie->elseExpr);
            }
            if (n->get_type() == AST_LET_EXPR)
                return check_tail(static_cast<LetExpr*>(n)->expr);
            return false;
        };
        if (!def.ast->bodies.empty()) {
            auto* body = def.ast->bodies[0];
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
                has_self_tail_call = check_tail(bwg->expr);
        }
    }

    // ===== Perceus phase 3: frame setup =====
    // If this function has any heap-typed pointer params, alloca a
    // yona_frame_t on the entry block, fill in its drops[] with the
    // param pointers, and push it onto the thread-local frame chain.
    // A raise unwinding past this frame's return will rc_dec all
    // still-live drops. Transfers (single-use arg consumed by callee)
    // NULL their slot via yona_rt_frame_transfer so we don't double-dec.
    auto saved_frame_alloca = current_frame_alloca_;
    current_frame_alloca_ = nullptr;
    {
        std::vector<Value*> heap_param_ptrs;
        auto* ptr_ty = PointerType::get(*context_, 0);
        for (size_t pi = 0; pi < def.param_names.size(); pi++) {
            if (pi >= param_ctypes.size()) continue;
            if (param_is_borrowed(pi)) continue;
            CType ct = param_ctypes[pi];
            if (!is_heap_type(ct)) continue;
            if (annotated_param_is_unboxed_enum(pi) ||
                (pi < args.size() && is_unboxed_enum_adt(args[pi])))
                continue;
            if (pi >= fn->arg_size()) continue;
            auto* param = fn->getArg(pi);
            if (param->getType()->isStructTy()) continue;
            if (!param->getType()->isPointerTy() && !param->getType()->isIntegerTy()) continue;
            Value* p = param;
            if (p->getType()->isIntegerTy())
                p = builder_->CreateIntToPtr(p, ptr_ty);
            heap_param_ptrs.push_back(p);
        }
        // Skip frame setup for TCO functions: LLVM's TailCallElimination
        // converts tail calls into loops and moves non-alloca instructions
        // (stores + push) into the loop header, causing re-push per
        // iteration. TCO + raise is rare; those params leak on unwind.
        if (!heap_param_ptrs.empty()
            && heap_param_ptrs.size() <= (size_t)YONA_MAX_FRAME_DROPS
            && builder_->GetInsertBlock()
            && !has_self_tail_call) {
            auto* i32_ty = LType::getInt32Ty(*context_);
            auto* i64_ty = LType::getInt64Ty(*context_);
            // Zero-cost when no try block is active: check the TLS
            // depth flag and skip all frame work on the fast path.
            // Only emit the frame alloca, stores, and push when
            // depth > 0 (inside a try). This keeps the hot path at
            // one TLS load + one branch (well-predicted taken).
            auto* depth_global = module_->getOrInsertGlobal(
                "yona_try_depth", i32_ty);
            if (auto* gv = dyn_cast<GlobalVariable>(depth_global)) {
                gv->setThreadLocal(true);
                gv->setThreadLocalMode(GlobalVariable::InitialExecTLSModel);
            }
            auto* depth = builder_->CreateLoad(i32_ty, depth_global, "try_depth");
            auto* in_try = builder_->CreateICmpSGT(depth,
                ConstantInt::get(i32_ty, 0), "in_try");
            auto* entry_end_bb = builder_->GetInsertBlock();
            auto* frame_setup_bb = BasicBlock::Create(*context_, "frame.setup", fn);
            auto* frame_cont_bb = BasicBlock::Create(*context_, "frame.cont", fn);
            builder_->CreateCondBr(in_try, frame_setup_bb, frame_cont_bb);

            // Cold path: allocate frame on stack, fill drops, push.
            builder_->SetInsertPoint(frame_setup_bb);
            auto* frame_ty = get_frame_type();
            auto* drops_arr_ty = ArrayType::get(ptr_ty, YONA_MAX_FRAME_DROPS);
            auto* frame = builder_->CreateAlloca(frame_ty, nullptr, "yona_frame");
            auto* dc_gep = builder_->CreateStructGEP(frame_ty, frame, 1);
            builder_->CreateStore(ConstantInt::get(i32_ty,
                (int32_t)heap_param_ptrs.size()), dc_gep);
            auto* drops_base = builder_->CreateStructGEP(frame_ty, frame, 3);
            for (size_t idx = 0; idx < heap_param_ptrs.size(); idx++) {
                auto* slot = builder_->CreateConstInBoundsGEP2_32(
                    drops_arr_ty, drops_base, 0, (unsigned)idx);
                builder_->CreateStore(heap_param_ptrs[idx], slot);
            }
            builder_->CreateCall(rt_.frame_push_, {frame});
            builder_->CreateBr(frame_cont_bb);

            // Merge: PHI selects between frame (cold) and null (hot).
            // frame_pop is a no-op when ptr is null.
            builder_->SetInsertPoint(frame_cont_bb);
            auto* frame_phi = builder_->CreatePHI(ptr_ty, 2, "frame_ptr");
            frame_phi->addIncoming(frame, frame_setup_bb);
            frame_phi->addIncoming(Constant::getNullValue(ptr_ty), entry_end_bb);
            current_frame_alloca_ = frame_phi;
        }
    }

    // Track which params need post-body cleanup (TCO may skip some)
    tco_fn_name_ = has_self_tail_call ? name : "";
    tco_param_names_ = has_self_tail_call ? def.param_names : std::vector<std::string>{};
    tco_param_ctypes_ = has_self_tail_call ? param_ctypes : std::vector<CType>{};
    tco_borrowed_params_ = has_self_tail_call ? borrowed : std::vector<bool>{};
    tco_cleanup_done_ = false;

    // Compile body. Stash the body AST so nested codegen steps (notably
    // codegen_pattern_headtail) can run single-use counts against the
    // full function body for Perceus-linear scrutinee detection.
    // Note: transfer tracking is restored AFTER the function-exit drops
    // below — we need the body's transfer marks to be visible when we
    // decide which seq params to rc_dec at exit.
    auto saved_fn_body = current_fn_body_;
    auto saved_transferred = transferred_values_;
    auto saved_closure_consumed = closure_consumed_flags_;
    transferred_values_.clear();
    closure_consumed_flags_.clear();
    TypedValue body_tv;
    if (!def.ast->bodies.empty()) {
        auto* body = def.ast->bodies[0];
        if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
            current_fn_body_ = bwg->expr;
            body_tv = codegen(bwg->expr);
            // AFN calls return PROMISE. Top-level print awaits them, but a
            // let-bound function's inferred return is usually INT — without
            // this, `let f cmd = exec cmd in f "echo x"` prints a pointer.
            if (body_tv.type == CType::PROMISE)
                body_tv = auto_await(body_tv);
        }
    }
    current_fn_body_ = saved_fn_body;

    CType ret_ctype = body_tv ? body_tv.type : CType::INT;

    if (body_tv && body_tv.val) {
        // Coerce non-recursive ADT structs to i64 ONLY when the body type
        // doesn't match the predicted return type AND the predicted type is i64.
        // This happens when a closure's infer_ret says i64 (because ADT was
        // not detected at inference time) but the body codegen produces a struct.
        // The coercion heap-allocates the struct ADT so it fits in i64.
        if (body_tv.val->getType()->isStructTy() && ret_type->isIntegerTy(64)
            && body_tv.type == CType::ADT) {
            auto* sty = llvm::cast<llvm::StructType>(body_tv.val->getType());
            unsigned num_fields = sty->getNumElements();
            auto i64_ty = LType::getInt64Ty(*context_);
            // Heap-allocate: adt_alloc(tag, num_fields - 1)
            auto* tag_val = builder_->CreateExtractValue(body_tv.val, {0});
            auto* adt_ptr = builder_->CreateCall(rt_.adt_alloc_,
                {tag_val, ConstantInt::get(i64_ty, num_fields - 1)});
            for (unsigned fi = 1; fi < num_fields; fi++) {
                auto* field_val = builder_->CreateExtractValue(body_tv.val, {fi});
                builder_->CreateCall(rt_.adt_set_field_,
                    {adt_ptr, ConstantInt::get(i64_ty, fi - 1), field_val});
            }
            body_tv.val = builder_->CreatePtrToInt(adt_ptr, i64_ty);
        }

        if (body_tv.val->getType() != ret_type) {
            // Remove the function and recreate with correct return type.
            // Phase 3: the frame alloca lives in the old fn's entry block;
            // erasing the fn destroys it. Must nullptr before erase.
            // Snapshot the LLVM type first: eraseFromParent() deletes the
            // body instruction, so body_tv.val->getType() is a UAF (null
            // on Darwin allocators; often still readable on Linux).
            LType* actual_ret_ty = body_tv.val->getType();
            current_frame_alloca_ = nullptr;
            fn->eraseFromParent();
            auto new_fn_type = llvm::FunctionType::get(actual_ret_ty, param_types, false);
            fn = Function::Create(new_fn_type, Function::InternalLinkage, name, module_.get());

            // Re-attach debug info to the recreated function
            if (debug_.enabled && debug_.builder && debug_.file) {
                auto* di_func_ty = di_func_type(param_ctypes, preliminary_ret);
                auto* di_sp = debug_.builder->createFunction(
                    debug_.file, name, name, debug_.file,
                    def.ast->source_context.line, di_func_ty, def.ast->source_context.line,
                    DINode::FlagZero, DISubprogram::SPFlagDefinition);
                fn->setSubprogram(di_sp);
                debug_.scope = di_sp;
            }

            // Update the cached compiled-function entry so any self-recursive
            // call inside the re-codegen'd body picks up the new Function*
            // instead of the just-erased one.
            cf_preliminary.fn = fn;
            cf_preliminary.return_type = body_tv.type;
            compiled_functions_[name] = cf_preliminary;

            // Recompile
            named_values_.clear();
            for (auto& [k, v] : saved_values) {
                if (v.type == CType::FUNCTION) named_values_[k] = v;
            }
            named_values_[name] = {fn, CType::FUNCTION};

            auto new_entry = BasicBlock::Create(*context_, "entry", fn);
            builder_->SetInsertPoint(new_entry);
            if (debug_.enabled && debug_.scope)
                builder_->SetCurrentDebugLocation(
                    DILocation::get(*context_, def.ast->source_context.line, 0, debug_.scope));

            i = 0;
            for (auto& arg : fn->args()) {
                std::string pname;
                CType ct;
                std::vector<CType> st;
                if (i < def.param_names.size()) {
                    pname = def.param_names[i];
                    ct = (i < args.size()) ? args[i].type : CType::INT;
                    if (i < args.size()) st = args[i].subtypes;
                    if (ct == CType::FUNCTION && st.empty() && i < args.size() && args[i].val) {
                        if (auto* fn_val = dyn_cast<Function>(args[i].val)) {
                            auto cf_it2 = compiled_functions_.find(fn_val->getName().str());
                            if (cf_it2 != compiled_functions_.end())
                                st = {cf_it2->second.return_type};
                        }
                    }
                } else {
                    pname = def.free_vars[i - def.param_names.size()];
                    size_t ci = i - def.param_names.size();
                    ct = (ci < capture_values.size()) ? capture_values[ci].type : CType::INT;
                }
                arg.setName(pname);
                named_values_[pname] = {&arg, ct, st};

                if (i < def.ast->patterns.size())
                    bind_parameter_pattern(def.ast->patterns[i], named_values_[pname]);

                i++;
            }

            body_tv = {};
            // The first specialization was erased. Its LLVM Values are now
            // invalid, so no ownership/transfer state from that discarded
            // body may participate in the regenerated function.
            transferred_values_.clear();
            closure_consumed_flags_.clear();
            arm_drop_stack_.clear();
            if (!def.ast->bodies.empty()) {
                auto* body = def.ast->bodies[0];
                if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
                    body_tv = codegen(bwg->expr);
                    if (body_tv.type == CType::PROMISE)
                        body_tv = auto_await(body_tv);
                }
            }
            ret_ctype = body_tv ? body_tv.type : CType::INT;
        }
        if (!builder_->GetInsertBlock()->getTerminator()) {
            // Perceus callee-owns DROP for all heap params (seqs under the
            // new Perceus-linear ABI, plus non-seq types that were always
            // callee-owns). Skip if TCO already handled cleanup before
            // the tail call, and skip params whose ownership was
            // transferred during body codegen (tracked in seq-domain transfers).
            if (body_tv.val && !tco_cleanup_done_) {
                auto ptr_ty = PointerType::get(*context_, 0);
                for (size_t pi = 0; pi < def.param_names.size(); pi++) {
                    if (pi >= param_ctypes.size()) continue;
                    CType ct = param_ctypes[pi];
                    if (!is_heap_type(ct)) continue;
                    if (annotated_param_is_unboxed_enum(pi) ||
                        (pi < args.size() && is_unboxed_enum_adt(args[pi])))
                        continue;
                    if (pi >= fn->arg_size()) continue;
                    auto* param = fn->getArg(pi);
                    if (param->getType()->isStructTy()) continue;
                    // Borrow inference: borrowed params don't need rc_dec.
                    if (param_is_borrowed(pi)) continue;
                    // SEQ domain tracks Perceus last-use; MAP domain tracks
                    // SET/DICT callee-owns and closure-consumed heap args.
                    if (ct == CType::SEQ &&
                        is_transferred(param, TransferDomain::Seq))
                        continue;
                    if (is_transferred(param, TransferDomain::Map))
                        continue;

                    // If a closure call already consumed this param at
                    // runtime (result != param), skip the function-exit
                    // dec to avoid double-free.
                    auto ccf_it = closure_consumed_flags_.find(param);
                    if (ccf_it != closure_consumed_flags_.end()) {
                        auto* consumed_val = builder_->CreateLoad(
                            LType::getInt1Ty(*context_), ccf_it->second, "consumed");
                        auto* not_consumed = builder_->CreateNot(consumed_val, "not_consumed");
                        auto* check_bb = BasicBlock::Create(*context_, "check_param", fn);
                        auto* skip_bb = BasicBlock::Create(*context_, "skip_param", fn);
                        builder_->CreateCondBr(not_consumed, check_bb, skip_bb);
                        builder_->SetInsertPoint(check_bb);
                        // Normal param_is_ret check for non-consumed path
                        Value* param_ptr = param;
                        Value* ret_ptr = body_tv.val;
                        if (param_ptr->getType()->isIntegerTy())
                            param_ptr = builder_->CreateIntToPtr(param_ptr, ptr_ty);
                        if (ret_ptr->getType()->isIntegerTy())
                            ret_ptr = builder_->CreateIntToPtr(ret_ptr, ptr_ty);
                        if (param_ptr->getType()->isPointerTy() && ret_ptr->getType()->isPointerTy()) {
                            auto* is_same = builder_->CreateICmpEQ(param_ptr, ret_ptr, "param_is_ret");
                            auto* drop_bb = BasicBlock::Create(*context_, "drop_param", fn);
                            builder_->CreateCondBr(is_same, skip_bb, drop_bb);
                            builder_->SetInsertPoint(drop_bb);
                            emit_rc_dec(param, ct);
                            builder_->CreateBr(skip_bb);
                        } else {
                            emit_rc_dec(param, ct);
                            builder_->CreateBr(skip_bb);
                        }
                        builder_->SetInsertPoint(skip_bb);
                    } else {
                        Value* param_ptr = param;
                        Value* ret_ptr = body_tv.val;
                        if (param_ptr->getType()->isIntegerTy())
                            param_ptr = builder_->CreateIntToPtr(param_ptr, ptr_ty);
                        if (ret_ptr->getType()->isIntegerTy())
                            ret_ptr = builder_->CreateIntToPtr(ret_ptr, ptr_ty);
                        if (param_ptr->getType()->isPointerTy() && ret_ptr->getType()->isPointerTy()) {
                            auto* is_same = builder_->CreateICmpEQ(param_ptr, ret_ptr, "param_is_ret");
                            auto* drop_bb = BasicBlock::Create(*context_, "drop_param", fn);
                            auto* cont_bb = BasicBlock::Create(*context_, "cont", fn);
                            builder_->CreateCondBr(is_same, cont_bb, drop_bb);
                            builder_->SetInsertPoint(drop_bb);
                            emit_rc_dec(param, ct);
                            builder_->CreateBr(cont_bb);
                            builder_->SetInsertPoint(cont_bb);
                        } else {
                            emit_rc_dec(param, ct);
                        }
                    }
                }
            }
            if (current_frame_alloca_)
                builder_->CreateCall(rt_.frame_pop_, {current_frame_alloca_});
            builder_->CreateRet(body_tv.val);
        }
    } else {
        if (!builder_->GetInsertBlock()->getTerminator()) {
            if (current_frame_alloca_)
                builder_->CreateCall(rt_.frame_pop_, {current_frame_alloca_});
            builder_->CreateRet(Constant::getNullValue(ret_type));
        }
    }

    // Restore
    current_arena_ = saved_outer_arena;
    current_group_ = saved_outer_group;
    named_values_ = saved_values;
    debug_.scope = saved_di_scope;
    if (saved_block) builder_->SetInsertPoint(saved_block, saved_point);
    if (debug_.enabled) builder_->SetCurrentDebugLocation(saved_debug_loc);

    CompiledFunction cf;
    cf.fn = fn;
    cf.return_type = ret_ctype;
    cf.param_types = param_ctypes;
    cf.borrowed_params = borrowed;
    cf.capture_names = def.free_vars;
    if (ret_ctype == CType::ADT) {
        cf.return_adt_name = !annotated_return_adt_name.empty()
            ? annotated_return_adt_name
            : body_tv ? body_tv.adt_type_name : "";
    }
    if (body_tv && !body_tv.subtypes.empty())
        cf.return_subtypes = body_tv.subtypes;
    // Propagate io-promise-ness from the body so a Yona wrapper like
    // `println s = writeLine stdoutFd s` is treated at its call sites as
    // an io_uring submit (direct call + yona_rt_io_await) rather than a
    // generic thread-pool Promise (which would double-schedule it on
    // the worker pool and corrupt string refcount on the boundary).
    if (body_tv && body_tv.promise_await == PromiseAwaitPath::IoUring)
        cf.extern_promise = ast::ExternPromiseKind::IoUring;
    compiled_functions_[name] = cf;
    named_values_[name] = {fn, CType::FUNCTION};

    // Restore transfer tracking from saved now that function-exit drops
    // are emitted — the function's body-local transfers should not leak
    // out to the enclosing scope's transfer tracking.
    transferred_values_ = saved_transferred;
    closure_consumed_flags_ = saved_closure_consumed;
    current_frame_alloca_ = saved_frame_alloca;
    return cf;
}

} // namespace yona::compiler::codegen
