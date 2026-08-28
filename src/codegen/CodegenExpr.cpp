//
// Codegen — Expression code generation
//
// Literals, arithmetic, comparisons, let bindings, if/do, identifiers,
// raise/try-catch.
//

#include "Codegen.h"
#include "typechecker/TypeChecker.h"
#include <llvm/IR/Dominators.h>
#include "analysis/BorrowEscapeAnalysis.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <iostream>

namespace yona::compiler::codegen {

// Constants — match runtime defines
static constexpr int64_t ARENA_DEFAULT_SIZE = 4096;
using namespace llvm;
using LType = llvm::Type;

static std::optional<CType> equality_type_argument(
        typechecker::MonoTypePtr type) {
    if (!type) return std::nullopt;
    if (type->tag == typechecker::MonoType::Con) {
        switch (type->con) {
            case typechecker::TyCon::Int: return CType::INT;
            case typechecker::TyCon::Float: return CType::FLOAT;
            case typechecker::TyCon::Bool: return CType::BOOL;
            case typechecker::TyCon::String: return CType::STRING;
            case typechecker::TyCon::Char:
            case typechecker::TyCon::Byte: return CType::INT;
            case typechecker::TyCon::Symbol: return CType::SYMBOL;
            case typechecker::TyCon::Unit: return CType::UNIT;
            case typechecker::TyCon::Seq: return CType::SEQ;
            case typechecker::TyCon::Set: return CType::SET;
            case typechecker::TyCon::Dict: return CType::DICT;
            case typechecker::TyCon::Tuple: return CType::TUPLE;
            case typechecker::TyCon::Function: return CType::FUNCTION;
            case typechecker::TyCon::Promise: return CType::PROMISE;
            case typechecker::TyCon::ByteArray: return CType::BYTE_ARRAY;
        }
    }
    if (type->tag == typechecker::MonoType::Arrow) return CType::FUNCTION;
    if (type->tag == typechecker::MonoType::MTuple) return CType::TUPLE;
    if (type->tag == typechecker::MonoType::App) {
        if (type->type_name == "Seq") return CType::SEQ;
        if (type->type_name == "Set") return CType::SET;
        if (type->type_name == "Dict") return CType::DICT;
        if (type->type_name == "Channel") return CType::CHANNEL;
        if (type->type_name == "IntArray") return CType::INT_ARRAY;
        if (type->type_name == "FloatArray") return CType::FLOAT_ARRAY;
        if (type->type_name == "ByteArray") return CType::BYTE_ARRAY;
        return CType::ADT;
    }
    return std::nullopt;
}

static SemanticTypeIdentity semantic_type_identity(
        typechecker::TypeChecker& checker, typechecker::MonoTypePtr type) {
    SemanticTypeIdentity identity;
    type = checker.zonk(type);
    identity.type = equality_type_argument(type).value_or(CType::INT);
    if (type && type->tag == typechecker::MonoType::App) {
        identity.adt_name = type->type_name;
        for (auto* argument : type->args)
            identity.arguments.push_back(semantic_type_identity(checker, argument));
    } else if (type && type->tag == typechecker::MonoType::MTuple) {
        for (auto* element : type->elements)
            identity.arguments.push_back(semantic_type_identity(checker, element));
    }
    return identity;
}

static void append_semantic_type_key(std::string& key,
                                     const SemanticTypeIdentity& identity) {
    key += std::to_string(static_cast<int>(identity.type));
    if (!identity.adt_name.empty()) key += "_" + identity.adt_name;
    if (!identity.arguments.empty()) {
        key += "_of";
        for (const auto& argument : identity.arguments) {
            key += "_";
            append_semantic_type_key(key, argument);
        }
    }
}

// Coerce a value to a target LLVM type for PHI node compatibility.
// Handles: i1→i64, ptr↔i64, struct→ptr (via alloca), different int widths.
static Value* coerce_for_phi(Value* val, LType* target, IRBuilder<>& builder, LLVMContext& ctx) {
    auto* src = val->getType();
    if (src == target) return val;
    if (src->isVoidTy())
        return Constant::getNullValue(target);

    // Integer widening (e.g., i1 → i64)
    if (src->isIntegerTy() && target->isIntegerTy())
        return builder.CreateZExtOrTrunc(val, target);

    // ptr → i64
    if (src->isPointerTy() && target->isIntegerTy())
        return builder.CreatePtrToInt(val, target);

    // i64 → ptr
    if (src->isIntegerTy() && target->isPointerTy())
        return builder.CreateIntToPtr(val, target);

    if (src->isFloatingPointTy() && target->isIntegerTy())
        return builder.CreateFPToSI(val, target);

    if (src->isIntegerTy() && target->isFloatingPointTy())
        return builder.CreateSIToFP(val, target);

    // struct → ptr (box into alloca)
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

    // ptr → struct: can't safely unbox without knowing layout, return as-is
    // (this case shouldn't normally happen)
    return val;
}

// Determine the widest common LLVM type for PHI merging.
static LType* common_phi_type(LType* a, LType* b, LLVMContext& ctx) {
    if (a == b) return a;

    // If either is a struct, use ptr (boxed representation)
    if (a->isStructTy() || b->isStructTy())
        return PointerType::get(ctx, 0);

    // ptr wins over integers
    if (a->isPointerTy() || b->isPointerTy())
        return PointerType::get(ctx, 0);

    // Wider integer wins
    if (a->isIntegerTy() && b->isIntegerTy()) {
        unsigned wa = a->getIntegerBitWidth(), wb = b->getIntegerBitWidth();
        return wa >= wb ? a : b;
    }

    if (a->isFloatingPointTy() || b->isFloatingPointTy())
        return a->isDoubleTy() || b->isDoubleTy()
            ? LType::getDoubleTy(ctx)
            : LType::getFloatTy(ctx);

    // Fallback: i64
    return LType::getInt64Ty(ctx);
}

// ===== Literals =====

TypedValue Codegen::codegen_integer(IntegerExpr* node) {
    set_debug_loc(node->source_context);
    return {ConstantInt::get(LType::getInt64Ty(*context_), node->value), CType::INT};
}
TypedValue Codegen::codegen_float(FloatExpr* node) {
    set_debug_loc(node->source_context);
    return {ConstantFP::get(LType::getDoubleTy(*context_), node->value), CType::FLOAT};
}
TypedValue Codegen::codegen_bool_true(TrueLiteralExpr* node) {
    set_debug_loc(node->source_context);
    return {ConstantInt::getTrue(*context_), CType::BOOL};
}
TypedValue Codegen::codegen_bool_false(FalseLiteralExpr* node) {
    set_debug_loc(node->source_context);
    return {ConstantInt::getFalse(*context_), CType::BOOL};
}
TypedValue Codegen::codegen_string(StringExpr* node) {
    set_debug_loc(node->source_context);
    // Emit string literal as an RC-managed static global so that Perceus
    // DUP/DROP at call sites don't dereference a missing RC header. Layout:
    //   { i64 refcount = INT64_MAX (arena sentinel),
    //     i64 encoded_tag = RC_TYPE_STRING(6) | (len << 16) | (cls=-1 → 0 << 8),
    //     [N x i8] bytes (including trailing NUL) }
    // rc_inc/rc_dec both short-circuit on the sentinel.
    const auto& s = node->value;
    const size_t len = s.size();
    auto* i64_ty = LType::getInt64Ty(*context_);
    auto* i8_ty = LType::getInt8Ty(*context_);
    auto* bytes_ty = ArrayType::get(i8_ty, len + 1);
    std::vector<Constant*> byte_consts;
    byte_consts.reserve(len + 1);
    for (size_t i = 0; i < len; i++)
        byte_consts.push_back(ConstantInt::get(i8_ty, (uint8_t)s[i]));
    byte_consts.push_back(ConstantInt::get(i8_ty, 0));
    auto* bytes_init = ConstantArray::get(bytes_ty, byte_consts);
    auto* struct_ty = StructType::get(*context_, {i64_ty, i64_ty, bytes_ty});
    constexpr int64_t RC_ARENA_SENTINEL = INT64_MAX;
    constexpr int64_t RC_TYPE_STRING = 6;
    int64_t encoded_tag = RC_TYPE_STRING | ((int64_t)len << 16);
    auto* init = ConstantStruct::get(struct_ty, {
        ConstantInt::get(i64_ty, RC_ARENA_SENTINEL),
        ConstantInt::get(i64_ty, encoded_tag),
        bytes_init,
    });
    // Not constant — rc_inc would write into .rodata otherwise. Sentinel check
    // makes rc_inc a no-op, but we set isConstant=false to keep the symbol in a
    // writable section just in case any other path mutates the header.
    auto* gv = new GlobalVariable(*module_, struct_ty, /*isConstant=*/false,
                                  GlobalValue::PrivateLinkage, init, ".strlit");
    gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    // Return pointer to the bytes field (index 2).
    auto* bytes_ptr = builder_->CreateConstInBoundsGEP2_32(struct_ty, gv, 0, 2);
    // Cast to i8* for consistency.
    auto* as_i8 = builder_->CreateBitCast(bytes_ptr, PointerType::get(*context_, 0));
    return {as_i8, CType::STRING};
}
TypedValue Codegen::codegen_unit(UnitExpr* node) {
    set_debug_loc(node->source_context);
    return {ConstantInt::get(LType::getInt64Ty(*context_), 0), CType::UNIT};
}

TypedValue Codegen::codegen_symbol(SymbolExpr* node) {
    set_debug_loc(node->source_context);
    int64_t id = intern_symbol(node->value);
    return {ConstantInt::get(LType::getInt64Ty(*context_), id), CType::SYMBOL};
}

// ===== Arithmetic (type-directed) =====

TypedValue Codegen::codegen_binary(AstNode* left_node, AstNode* right_node, const std::string& op) {
    set_debug_loc(left_node->source_context);
    auto left = auto_await(codegen(left_node));
    auto right = auto_await(codegen(right_node));
    if (!left || !right) return {};

    // Type validation for arithmetic operators
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        bool left_numeric = (left.type == CType::INT || left.type == CType::FLOAT);
        bool right_numeric = (right.type == CType::INT || right.type == CType::FLOAT);
        bool both_string = (left.type == CType::STRING && right.type == CType::STRING);
        if (!left_numeric && !right_numeric && !both_string) {
            report_error(left_node->source_context,
                "type error: operator '" + op + "' requires numeric or string operands");
            return {};
        }
    }

    // String concatenation
    if (left.type == CType::STRING && right.type == CType::STRING && op == "+") {
        return {builder_->CreateCall(rt_.string_concat_, {left.val, right.val}), CType::STRING};
    }

    // Promote int to float if mixed
    if (left.type == CType::INT && right.type == CType::FLOAT) {
        left.val = builder_->CreateSIToFP(left.val, LType::getDoubleTy(*context_));
        left.type = CType::FLOAT;
    }
    if (left.type == CType::FLOAT && right.type == CType::INT) {
        right.val = builder_->CreateSIToFP(right.val, LType::getDoubleTy(*context_));
        right.type = CType::FLOAT;
    }

    bool is_float = (left.type == CType::FLOAT);
    Value* result = nullptr;

    if (op == "+") result = is_float ? builder_->CreateFAdd(left.val, right.val) : builder_->CreateAdd(left.val, right.val);
    else if (op == "-") result = is_float ? builder_->CreateFSub(left.val, right.val) : builder_->CreateSub(left.val, right.val);
    else if (op == "*") result = is_float ? builder_->CreateFMul(left.val, right.val) : builder_->CreateMul(left.val, right.val);
    else if (op == "/") result = is_float ? builder_->CreateFDiv(left.val, right.val) : builder_->CreateSDiv(left.val, right.val);
    else if (op == "%") result = builder_->CreateSRem(left.val, right.val);

    return {result, is_float ? CType::FLOAT : CType::INT};
}

TypedValue Codegen::codegen_comparison(AstNode* left_node, AstNode* right_node, const std::string& op) {
    set_debug_loc(left_node->source_context);
    auto left = auto_await(codegen(left_node));
    auto right = auto_await(codegen(right_node));
    if (!left || !right) return {};

    auto attach_adt_arguments = [&](TypedValue& value, AstNode* source) {
        // Imported GENFN bodies are reparsed after the importing module was
        // checked. Their node addresses must never be used to query that
        // checker's pointer-keyed cache: allocator reuse can turn a lookup
        // into unrelated semantic arguments and select the wrong generic
        // specialization.
        if (!type_checker_ || genfn_isolation_depth_ != 0 || !source)
            return;
        auto* inferred = type_checker_->zonk(type_checker_->type_of(source));
        if (!inferred) return;
        std::vector<typechecker::MonoTypePtr> arguments;
        if (inferred->tag == typechecker::MonoType::App) {
            if (value.adt_type_name.empty()) value.adt_type_name = inferred->type_name;
            arguments = inferred->args;
        } else if (inferred->tag == typechecker::MonoType::MTuple) {
            value.adt_type_name = "Tuple";
            arguments = inferred->elements;
        } else {
            return;
        }
        std::vector<CType> concrete_arguments;
        std::vector<std::string> concrete_argument_names;
        std::vector<SemanticTypeIdentity> semantic_arguments;
        concrete_arguments.reserve(arguments.size());
        concrete_argument_names.reserve(arguments.size());
        for (auto* argument : arguments) {
            auto* semantic_argument = type_checker_->zonk(argument);
            const auto concrete = equality_type_argument(semantic_argument);
            // A phantom/unselected ADT parameter (for example the error type
            // of `Ok value`) still occupies its declared position. Preserve
            // arity with the generic i64 ABI placeholder instead of falling
            // back to constructor payload subtypes and losing the other
            // nominal arguments.
            concrete_arguments.push_back(concrete.value_or(CType::INT));
            concrete_argument_names.push_back(
                semantic_argument->tag == typechecker::MonoType::App
                    ? semantic_argument->type_name : std::string{});
            semantic_arguments.push_back(
                semantic_type_identity(*type_checker_, semantic_argument));
        }
        value.adt_type_arguments = std::move(concrete_arguments);
        value.adt_type_argument_names = std::move(concrete_argument_names);
        value.adt_semantic_arguments = semantic_arguments;
        if (value.type == CType::TUPLE || value.type == CType::SEQ ||
            value.type == CType::SET || value.type == CType::DICT) {
            value.subtypes = value.adt_type_arguments;
            value.semantic_subtypes = std::move(semantic_arguments);
        }
    };
    attach_adt_arguments(left, left_node);
    attach_adt_arguments(right, right_node);

    struct ResolvedMethod {
        std::string symbol;
        CompiledFunction* function = nullptr;
    };
    auto resolve_adt_method = [&](const std::string& method_name) -> ResolvedMethod {
        std::vector<TypedValue> args = {left, right};
        const std::string instance_symbol = resolve_trait_method(
            method_name, left.type, left.adt_type_name,
            method_name == "eq" ? "Eq" : "Ord");
        std::string callable_symbol = instance_symbol;
        auto instance = compiled_functions_.end();

        if (!instance_symbol.empty()) {
            auto source = imports_.imported_sources.find(instance_symbol);
            const auto& semantic_arguments = !left.adt_type_arguments.empty()
                ? left.adt_type_arguments : left.subtypes;
            if (source != imports_.imported_sources.end() &&
                !semantic_arguments.empty()) {
                std::string specialization = instance_symbol + "__";
                for (size_t i = 0; i < semantic_arguments.size(); ++i) {
                    if (i < left.adt_semantic_arguments.size())
                        append_semantic_type_key(
                            specialization, left.adt_semantic_arguments[i]);
                    else {
                        specialization += std::to_string(
                            static_cast<int>(semantic_arguments[i]));
                        if (i < left.adt_type_argument_names.size() &&
                            !left.adt_type_argument_names[i].empty())
                            specialization += "_" +
                                left.adt_type_argument_names[i];
                    }
                    specialization += "_";
                }
                instance = compiled_functions_.find(specialization);
                if (instance == compiled_functions_.end()) {
                    GenfnNameIsolation iso(*this, instance_symbol);
                    install_private_genfn_ctors(instance_symbol);
                    auto reparsed = reparse_genfn(
                        source->second.local_name, source->second.source_text);
                    if (reparsed && !reparsed->functions.empty()) {
                        auto* function = reparsed->functions.front();
                        reparsed->functions.clear();
                        imports_.imported_ast_nodes.push_back(
                            std::unique_ptr<FunctionExpr>(function));
                        register_sibling_genfns(instance_symbol);
                        codegen_function_def(function, specialization);
                        if (auto deferred = deferred_functions_.find(specialization);
                            deferred != deferred_functions_.end()) {
                            compile_function(specialization, deferred->second, args);
                            instance = compiled_functions_.find(specialization);
                        }
                        if (instance != compiled_functions_.end()) {
                            CompiledFunction specialized = instance->second;
                            iso.restore();
                            compiled_functions_[specialization] = std::move(specialized);
                            instance = compiled_functions_.find(specialization);
                        }
                    }
                }
                if (instance != compiled_functions_.end()) callable_symbol = specialization;
            }
            if (instance == compiled_functions_.end()) {
                instance = compiled_functions_.find(instance_symbol);
                if (instance == compiled_functions_.end()) {
                    if (auto deferred = deferred_functions_.find(instance_symbol);
                        deferred != deferred_functions_.end()) {
                        compile_function(instance_symbol, deferred->second, args);
                        instance = compiled_functions_.find(instance_symbol);
                    }
                }
            }
        }
        return instance == compiled_functions_.end()
            ? ResolvedMethod{} : ResolvedMethod{callable_symbol, &instance->second};
    };

    // Aggregate equality is semantic trait dispatch, never an LLVM aggregate
    // `icmp` or field-layout walk. Generic derived methods are specialized by
    // the complete source-level ADT application at the operator site.
    const bool aggregate_equality = left.type == CType::ADT || left.type == CType::SEQ ||
        left.type == CType::TUPLE || left.type == CType::SET || left.type == CType::DICT;
    if ((op == "==" || op == "!=") && aggregate_equality && left.type == right.type) {
        auto resolved = resolve_adt_method("eq");
        if (!resolved.function) {
            const std::string type_name = !left.adt_type_name.empty()
                ? left.adt_type_name : ctype_to_type_name(left.type);
            report_error(left_node->source_context,
                "no Eq instance for '" + type_name +
                "'; add `deriving Eq`, define an instance, or use an explicit comparator");
            return {};
        }
        auto equal = emit_direct_call(resolved.symbol, *resolved.function, {left, right});
        if (!equal) return {};
        auto* eq_bool = equal.val;
        if (!eq_bool->getType()->isIntegerTy(1)) {
            eq_bool = builder_->CreateICmpNE(
                eq_bool, ConstantInt::get(eq_bool->getType(), 0), "eq_bool");
        }
        return {op == "==" ? eq_bool : builder_->CreateNot(eq_bool, "neq"), CType::BOOL};
    }

    if ((op == "<" || op == ">" || op == "<=" || op == ">=") &&
        (left.type == CType::ADT || left.type == CType::SEQ ||
         left.type == CType::TUPLE) &&
        left.type == right.type) {
        auto resolved = resolve_adt_method("compare");
        if (!resolved.function) {
            const std::string type_name = !left.adt_type_name.empty()
                ? left.adt_type_name : ctype_to_type_name(left.type);
            report_error(left_node->source_context,
                "no Ord instance for '" + type_name +
                "'; add `deriving Ord`, define an instance, or use an explicit comparator");
            return {};
        }
        auto ordering = emit_direct_call(resolved.symbol, *resolved.function, {left, right});
        if (!ordering) return {};
        Value* tag = ordering.val;
        if (tag->getType()->isStructTy())
            tag = builder_->CreateExtractValue(tag, {0}, "ordering.tag");
        else if (tag->getType()->isPointerTy())
            tag = builder_->CreateCall(rt_.adt_get_tag_, {tag}, "ordering.tag");
        if (!tag->getType()->isIntegerTy(64))
            tag = builder_->CreateZExtOrTrunc(tag, LType::getInt64Ty(*context_));
        auto* equal_tag = ConstantInt::get(LType::getInt64Ty(*context_), 1);
        Value* comparison = nullptr;
        if (op == "<") comparison = builder_->CreateICmpULT(tag, equal_tag);
        else if (op == ">") comparison = builder_->CreateICmpUGT(tag, equal_tag);
        else if (op == "<=") comparison = builder_->CreateICmpULE(tag, equal_tag);
        else comparison = builder_->CreateICmpUGE(tag, equal_tag);
        return {comparison, CType::BOOL};
    }

    bool is_float = (left.type == CType::FLOAT || right.type == CType::FLOAT);
    if (is_float) {
        if (left.type == CType::INT) left.val = builder_->CreateSIToFP(left.val, LType::getDoubleTy(*context_));
        if (right.type == CType::INT) right.val = builder_->CreateSIToFP(right.val, LType::getDoubleTy(*context_));
    }

    Value* result = nullptr;
    if (is_float) {
        if (op == "==") result = builder_->CreateFCmpOEQ(left.val, right.val);
        else if (op == "!=") result = builder_->CreateFCmpONE(left.val, right.val);
        else if (op == "<") result = builder_->CreateFCmpOLT(left.val, right.val);
        else if (op == ">") result = builder_->CreateFCmpOGT(left.val, right.val);
        else if (op == "<=") result = builder_->CreateFCmpOLE(left.val, right.val);
        else if (op == ">=") result = builder_->CreateFCmpOGE(left.val, right.val);
    } else if ((left.type == CType::STRING || right.type == CType::STRING) &&
               (op == "==" || op == "!=")) {
        // Handler args (and other i64-boxed strings) compare by content.
        auto as_str = [&](const TypedValue& tv) -> Value* {
            if (tv.val->getType()->isPointerTy()) return tv.val;
            return builder_->CreateIntToPtr(tv.val, PointerType::get(*context_, 0));
        };
        auto* eq = builder_->CreateCall(rt_.string_eq_, {as_str(left), as_str(right)});
        auto* zero = ConstantInt::get(LType::getInt64Ty(*context_), 0);
        result = (op == "==") ? builder_->CreateICmpNE(eq, zero)
                              : builder_->CreateICmpEQ(eq, zero);
    } else {
        if (left.val->getType() != right.val->getType()) {
            if (right.val->getType()->isPointerTy() && left.val->getType()->isIntegerTy())
                left.val = builder_->CreateIntToPtr(left.val, right.val->getType());
            else if (left.val->getType()->isPointerTy() && right.val->getType()->isIntegerTy())
                right.val = builder_->CreateIntToPtr(right.val, left.val->getType());
            else if (left.val->getType()->isIntegerTy() && right.val->getType()->isIntegerTy())
                left.val = builder_->CreateZExtOrTrunc(left.val, right.val->getType());
        }
        if (op == "==") result = builder_->CreateICmpEQ(left.val, right.val);
        else if (op == "!=") result = builder_->CreateICmpNE(left.val, right.val);
        else if (op == "<") result = builder_->CreateICmpSLT(left.val, right.val);
        else if (op == ">") result = builder_->CreateICmpSGT(left.val, right.val);
        else if (op == "<=") result = builder_->CreateICmpSLE(left.val, right.val);
        else if (op == ">=") result = builder_->CreateICmpSGE(left.val, right.val);
    }
    return {result, CType::BOOL};
}

// ===== Flow-sensitive transfer scoping (Perceus-linear branching) =====
//
// These helpers wrap the branches of an if/case/... construct so that
// Seq-domain transfer tracking reflects what's transferred on the current path
// rather than in any path's codegen. Asymmetric transfers — values
// transferred in one branch but not another — get compensating
// rc_decs emitted at the end of branches that didn't transfer them,
// so downstream scope cleanups can treat the post-scope set as "union
// of all branch transfers" without double-dropping.

void Codegen::mark_transferred(llvm::Value* val, TransferDomain domain) {
    if (!val) return;
    auto& mask = transferred_values_[val];
    mask |= static_cast<TransferMask>(domain);
}

bool Codegen::is_transferred(llvm::Value* val, TransferDomain domain) const {
    if (!val) return false;
    auto it = transferred_values_.find(val);
    if (it == transferred_values_.end()) return false;
    return (it->second & static_cast<TransferMask>(domain)) != 0;
}

void Codegen::clear_transferred(TransferDomain domain) {
    const auto bit = static_cast<TransferMask>(domain);
    for (auto it = transferred_values_.begin(); it != transferred_values_.end();) {
        it->second &= static_cast<TransferMask>(~bit);
        if (it->second == 0) {
            it = transferred_values_.erase(it);
        } else {
            ++it;
        }
    }
}

std::unordered_set<llvm::Value*> Codegen::snapshot_transferred(TransferDomain domain) const {
    std::unordered_set<llvm::Value*> out;
    const auto bit = static_cast<TransferMask>(domain);
    for (const auto& [val, mask] : transferred_values_) {
        if ((mask & bit) != 0) out.insert(val);
    }
    return out;
}

void Codegen::restore_transferred(TransferDomain domain,
                                  const std::unordered_set<llvm::Value*>& snapshot) {
    clear_transferred(domain);
    for (auto* v : snapshot) mark_transferred(v, domain);
}

bool Codegen::is_cross_branch_droppable(
    llvm::Value* v,
    uint64_t pre_scope_block_ordinal) {
    if (llvm::isa<llvm::Argument>(v)) return true;
    if (auto* inst = llvm::dyn_cast<llvm::Instruction>(v)) {
        auto it = transfer_block_ordinals_.find(inst->getParent());
        if (it == transfer_block_ordinals_.end()) return false;
        return it->second <= pre_scope_block_ordinal;
    }
    // Constants and other non-instruction values are safe to reference
    // from any branch.
    return true;
}

void Codegen::refresh_transfer_block_ordinals(llvm::Function* fn) {
    if (!fn) return;
    if (transfer_block_ordinal_fn_ != fn) {
        transfer_block_ordinal_fn_ = fn;
        transfer_block_ordinal_next_ = 0;
        transfer_block_ordinals_.clear();
        for (auto& bb : *fn) transfer_block_ordinals_.emplace(&bb, ++transfer_block_ordinal_next_);
        return;
    }

    // Common case: new blocks are appended. Walk from tail until the first
    // known block, then assign ordinals in forward order for the unknown tail.
    std::vector<llvm::BasicBlock*> tail_unknown;
    std::vector<llvm::BasicBlock*> all_blocks;
    all_blocks.reserve(fn->size());
    for (auto& bb : *fn) all_blocks.push_back(&bb);
    for (auto it = all_blocks.rbegin(); it != all_blocks.rend(); ++it) {
        if (transfer_block_ordinals_.count(*it)) break;
        tail_unknown.push_back(*it);
    }
    for (auto it = tail_unknown.rbegin(); it != tail_unknown.rend(); ++it) {
        transfer_block_ordinals_.emplace(*it, ++transfer_block_ordinal_next_);
    }

    if (tail_unknown.empty() && transfer_block_ordinals_.size() != fn->size()) {
        // Fallback path for non-append insertions.
        for (auto& bb : *fn) {
            if (!transfer_block_ordinals_.count(&bb))
                transfer_block_ordinals_.emplace(&bb, ++transfer_block_ordinal_next_);
        }
    }
}

void Codegen::transfer_scope_enter() {
    // Invariant: must run BEFORE any branch BasicBlocks are created so
    // pre_blocks captures only pre-scope blocks. Values defined inside
    // branches fail cross-branch droppability and won't be dropped from
    // sibling branches. See Codegen.h TransferScope doc for the pool
    // UAF that breaking this invariant causes.
    TransferScope s;
    s.entry_snapshot = snapshot_transferred(TransferDomain::Seq);
    if (builder_->GetInsertBlock()) {
        auto* fn = builder_->GetInsertBlock()->getParent();
        refresh_transfer_block_ordinals(fn);
        s.pre_scope_block_ordinal = transfer_block_ordinal_next_;
    }
    transfer_scope_stack_.push_back(std::move(s));
}

void Codegen::transfer_branch_begin() {
    if (transfer_scope_stack_.empty()) return;
    restore_transferred(TransferDomain::Seq, transfer_scope_stack_.back().entry_snapshot);
}

void Codegen::transfer_branch_end(llvm::BasicBlock* exit_bb) {
    if (transfer_scope_stack_.empty()) return;
    TransferScope::Branch b;
    b.exit_bb = exit_bb;
    b.transfers = snapshot_transferred(TransferDomain::Seq);
    transfer_scope_stack_.back().branches.push_back(std::move(b));
}

void Codegen::transfer_scope_exit() {
    if (transfer_scope_stack_.empty()) return;
    TransferScope scope = std::move(transfer_scope_stack_.back());
    transfer_scope_stack_.pop_back();

    // Gather all values transferred by ANY non-terminated branch that
    // weren't already transferred at scope entry. Terminated branches
    // don't reach merge, so their transfers don't affect the post-scope
    // view and we don't emit drops in them.
    std::unordered_set<llvm::Value*> any_transferred;
    for (auto& b : scope.branches) {
        if (!b.exit_bb) continue;
        for (auto* v : b.transfers)
            if (!scope.entry_snapshot.count(v))
                any_transferred.insert(v);
    }

    // For each such value, emit rc_dec in branches that DIDN'T transfer
    // it. Skip terminated branches (no cleanup needed). Filter by
    // cross-branch droppability so we don't try to drop a value defined
    // inside a branch (SSA-unreachable outside it).
    for (auto* v : any_transferred) {
        if (!is_cross_branch_droppable(v, scope.pre_scope_block_ordinal)) continue;
        for (auto& b : scope.branches) {
            if (!b.exit_bb) continue;
            if (b.transfers.count(v)) continue;
            // Ordinal filtering rejects values introduced after the scope,
            // but a pre-scope block can still sit on only one control-flow
            // path. Require actual dominance before inserting a compensating
            // drop in this sibling exit block.
            if (auto* inst = llvm::dyn_cast<llvm::Instruction>(v)) {
                llvm::DominatorTree dominators(*b.exit_bb->getParent());
                if (!dominators.dominates(inst, block_terminator(b.exit_bb)))
                    continue;
            }
            auto saved_ip = builder_->saveIP();
            builder_->SetInsertPoint(block_terminator(b.exit_bb));
            emit_rc_dec(v, CType::SEQ);
            builder_->restoreIP(saved_ip);
        }
    }

    // Post-scope seq transfers are the union over non-terminated
    // branches. Any asymmetric transfers were compensated with drops
    // above, so downstream cleanups can skip based on this union.
    restore_transferred(TransferDomain::Seq, scope.entry_snapshot);
    for (auto& b : scope.branches) {
        if (!b.exit_bb) continue;
        for (auto* v : b.transfers) mark_transferred(v, TransferDomain::Seq);
    }
}

// ===== Stream fusion: count identifier references in AST =====
// (shared implementation in analysis/BorrowEscapeAnalysis.cpp)

int Codegen::count_identifier_refs(AstNode* node, const std::string& name) {
    return compiler::analysis::count_identifier_refs(node, name);
}

// ===== Borrow inference: escape analysis for function params =====
//
// Returns true if `name` appears in a position where it would be
// stored (escapes the function scope): returned, captured in a
// closure, inserted into a collection literal, or passed as an ADT
// constructor field. `is_return_position` tracks whether we're at
// the tail position of the function body (where the value would be
// the return value).
//
// Conservative: returns true on any ambiguous case. Only params
// confirmed as non-escaping get the borrow optimization.

static bool contains_raise_expr(AstNode* node) {
    if (!node) return false;
    auto ty = node->get_type();
    if (ty == AST_RAISE_EXPR) return true;

    if (dynamic_cast<BinaryOpExpr*>(node)) {
        auto* b = static_cast<BinaryOpExpr*>(node);
        return contains_raise_expr(b->left) || contains_raise_expr(b->right);
    }
    if (ty == AST_IF_EXPR) {
        auto* e = static_cast<IfExpr*>(node);
        return contains_raise_expr(e->condition)
            || contains_raise_expr(e->thenExpr)
            || contains_raise_expr(e->elseExpr);
    }
    if (ty == AST_LET_EXPR) {
        auto* e = static_cast<LetExpr*>(node);
        for (auto* a : e->aliases) {
            if (auto* va = dynamic_cast<ValueAlias*>(a)) {
                if (contains_raise_expr(va->expr)) return true;
            }
        }
        return contains_raise_expr(e->expr);
    }
    if (ty == AST_CASE_EXPR) {
        auto* e = static_cast<CaseExpr*>(node);
        if (contains_raise_expr(e->expr)) return true;
        for (auto* clause : e->clauses) {
            if (contains_raise_expr(clause->guard)) return true;
            if (contains_raise_expr(clause->body)) return true;
        }
        return false;
    }
    if (ty == AST_DO_EXPR) {
        auto* e = static_cast<DoExpr*>(node);
        for (auto* step : e->steps)
            if (contains_raise_expr(step)) return true;
        return false;
    }
    if (ty == AST_TRY_CATCH_EXPR) {
        auto* e = static_cast<TryCatchExpr*>(node);
        if (contains_raise_expr(e->tryExpr)) return true;
        for (auto* cp : e->catchExpr->patterns) {
            if (auto* bwg = std::get_if<PatternWithoutGuards*>(&cp->pattern))
                if (*bwg && contains_raise_expr((*bwg)->expr)) return true;
            if (auto* guarded = std::get_if<std::vector<PatternWithGuards*>>(&cp->pattern)) {
                for (auto* pg : *guarded) {
                    if (contains_raise_expr(pg->guard)) return true;
                    if (contains_raise_expr(pg->expr)) return true;
                }
            }
        }
        return false;
    }
    if (ty == AST_APPLY_EXPR) {
        auto* e = static_cast<ApplyExpr*>(node);
        for (auto& arg : e->args) {
            if (auto* expr = std::get_if<ExprNode*>(&arg))
                if (*expr && contains_raise_expr(*expr)) return true;
        }
        return false;
    }
    if (ty == AST_FUNCTION_EXPR) {
        // Nested functions are analyzed independently; a raise there does not
        // make this function's parameter contract unwind-unsafe unless the
        // parameter is captured, which has_escaping_use already rejects.
        return false;
    }
    return false;
}

bool Codegen::has_escaping_use(AstNode* node, const std::string& name,
                                bool is_return_position,
                                const BorrowInferenceContext* context) {
    if (!node) return false;
    auto ty = node->get_type();

    if (ty == AST_IDENTIFIER_EXPR) {
        if (static_cast<IdentifierExpr*>(node)->name->value == name)
            return is_return_position;  // in return position → escapes
        return false;
    }

    // Collection literals store their elements — if name appears, it escapes.
    if (ty == AST_VALUES_SEQUENCE_EXPR) {
        auto* e = static_cast<ValuesSequenceExpr*>(node);
        for (auto* v : e->values)
            if (count_identifier_refs(v, name) > 0) return true;
        return false;
    }
    if (ty == AST_TUPLE_EXPR) {
        auto* e = static_cast<TupleExpr*>(node);
        for (auto* v : e->values)
            if (count_identifier_refs(v, name) > 0) return true;
        return false;
    }
    if (ty == AST_RECORD_INSTANCE_EXPR) {
        auto* e = static_cast<RecordInstanceExpr*>(node);
        for (auto& [field, value] : e->items)
            if (count_identifier_refs(value, name) > 0) return true;
        return false;
    }
    if (ty == AST_RECORD_LITERAL_EXPR) {
        auto* e = static_cast<RecordLiteralExpr*>(node);
        for (auto& [field, value] : e->fields)
            if (count_identifier_refs(value, name) > 0) return true;
        return false;
    }

    // Cons (::) stores the element in a seq.
    if (ty == AST_CONS_LEFT_EXPR) {
        auto* e = static_cast<ConsLeftExpr*>(node);
        if (count_identifier_refs(e->left, name) > 0) return true;
        if (count_identifier_refs(e->right, name) > 0) return true;
        return false;
    }

    // Lambda captures: if name is free in the lambda body, it's captured.
    if (ty == AST_FUNCTION_EXPR) {
        auto* f = static_cast<FunctionExpr*>(node);
        // Check if name is shadowed by a lambda pattern param.
        for (auto* pat : f->patterns) {
            if (auto* pv = dynamic_cast<PatternValue*>(pat)) {
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                    if ((*id)->name->value == name) return false;
            }
        }
        // If name appears in the body, it's captured → escapes.
        for (auto* body : f->bodies)
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
                if (count_identifier_refs(bwg->expr, name) > 0) return true;
        return false;
    }

    if (ty == AST_REMOVE_EXPR) {
        // Set difference consumes its left operand.  Borrow inference runs
        // before concrete collection types are always available, therefore
        // a left-hand parameter must conservatively satisfy the owning ABI.
        // The right operand is borrowed by both sequence and set difference.
        auto* e = static_cast<RemoveExpr*>(node);
        if (count_identifier_refs(e->left, name) > 0) return true;
        return has_escaping_use(e->right, name, false, context);
    }

    // Other binary ops neither store nor consume their operands.
    if (dynamic_cast<BinaryOpExpr*>(node)) {
        auto* b = static_cast<BinaryOpExpr*>(node);
        return has_escaping_use(b->left, name, false, context)
            || has_escaping_use(b->right, name, false, context);
    }

    // If-expression: both branches are in the same return position.
    if (ty == AST_IF_EXPR) {
        auto* e = static_cast<IfExpr*>(node);
        return has_escaping_use(e->condition, name, false, context)
            || has_escaping_use(e->thenExpr, name, is_return_position, context)
            || has_escaping_use(e->elseExpr, name, is_return_position, context);
    }

    // Let: aliases are non-return; the body inherits return position.
    if (ty == AST_LET_EXPR) {
        auto* e = static_cast<LetExpr*>(node);
        for (auto* a : e->aliases) {
            if (auto* va = dynamic_cast<ValueAlias*>(a)) {
                if (has_escaping_use(va->expr, name, false, context)) return true;
                if (va->identifier->name->value == name) return false;
            } else if (auto* la = dynamic_cast<LambdaAlias*>(a)) {
                if (has_escaping_use(la->lambda, name, false, context)) return true;
                if (la->name->value == name) return false;
            }
        }
        return has_escaping_use(e->expr, name, is_return_position, context);
    }

    if (ty == AST_IMPORT_EXPR)
        return has_escaping_use(static_cast<ImportExpr*>(node)->expr,
                                name, is_return_position, context);

    // Case: if the name IS the scrutinee, it may be consumed by
    // seq_tail_consume in a head-tail pattern — that's an ownership
    // transfer, so it escapes. Arm bodies inherit return position.
    if (ty == AST_CASE_EXPR) {
        auto* e = static_cast<CaseExpr*>(node);
        if (count_identifier_refs(e->expr, name) > 0) return true;
        for (auto* clause : e->clauses)
            if (has_escaping_use(clause->body, name, is_return_position, context))
                return true;
        return false;
    }

    // Apply: forwarding a param only remains a borrow if the callee is known
    // to borrow that argument. Unknown callees and callee-owned params escape
    // because the callee may consume or retain the value.
    if (ty == AST_APPLY_EXPR) {
        auto* e = static_cast<ApplyExpr*>(node);
        std::string callee_name;
        if (auto* nc = dynamic_cast<NameCall*>(e->call))
            callee_name = nc->name->value;
        // Juxtaposition is left-associative: `f x y` is represented as an
        // outer application whose callee is the inner `f x` application.
        // A parameter forwarded by that inner call is still an ownership
        // boundary, even though it is not an argument of the outer node.
        // Traverse expression callees before deciding that the parameter is
        // safe to borrow.
        if (auto* ec = dynamic_cast<ExprCall*>(e->call)) {
            if (has_escaping_use(ec->expr, name, false, context)) return true;
        }
        auto cf_it = callee_name.empty()
            ? compiled_functions_.end()
            : compiled_functions_.find(callee_name);
        const std::vector<bool>* callee_borrowed = nullptr;
        if (context && callee_name == context->function_name)
            callee_borrowed = context->recursive_borrowed_params;
        else if (cf_it != compiled_functions_.end())
            callee_borrowed = &cf_it->second.borrowed_params;
        for (size_t ai = 0; ai < e->args.size(); ai++) {
            AstNode* arg = nullptr;
            if (auto* expr = std::get_if<ExprNode*>(&e->args[ai]))
                arg = *expr;
            else if (auto* value = std::get_if<ValueExpr*>(&e->args[ai]))
                arg = *value;
            if (!arg) continue;
            if (has_escaping_use(arg, name, false, context)) return true;
            if (compiler::analysis::count_identifier_value_refs(arg, name) == 0)
                continue;
            if (!callee_borrowed || ai >= callee_borrowed->size()
                || !(*callee_borrowed)[ai])
                return true;
        }
        return false;
    }

    // Do-expression: last step is return position.
    if (ty == AST_DO_EXPR) {
        auto* e = static_cast<DoExpr*>(node);
        for (size_t i = 0; i < e->steps.size(); i++) {
            bool last = (i == e->steps.size() - 1);
            if (has_escaping_use(e->steps[i], name, last && is_return_position,
                                 context))
                return true;
        }
        return false;
    }

    // Conservative default: if we don't recognize the node type and name
    // appears in it, assume it escapes.
    return count_identifier_refs(node, name) > 0;
}

std::vector<bool> Codegen::infer_borrowed_params(const std::string& function_name,
                                                  const DeferredFunction& def,
                                                  const std::vector<CType>& param_ctypes) {
    std::vector<bool> borrowed(def.param_names.size(), false);
    if (def.ast->bodies.empty()) return borrowed;

    auto* body = def.ast->bodies[0];
    auto* bwg = dynamic_cast<BodyWithoutGuards*>(body);
    if (!bwg) return borrowed;
    if (contains_raise_expr(bwg->expr)) return borrowed;

    // Start with the greatest possible borrow contract. Recursive forwarding
    // is safe only while the destination parameter is also borrowed; removing
    // an unsafe parameter can therefore make another parameter unsafe. Iterate
    // to the greatest fixed point instead of depending on compilation order.
    // Borrowing describes how a parameter is used, not how the current
    // monomorph happens to represent it.  Infer it for every parameter so a
    // closure compiled before its argument types are known still carries the
    // correct contract.  Scalar borrow bits are harmless; call sites consult
    // them only for heap values.
    std::fill(borrowed.begin(), borrowed.end(), true);

    bool changed;
    do {
        changed = false;
        BorrowInferenceContext context{function_name, &borrowed};
        auto next = borrowed;
        for (size_t pi = 0; pi < def.param_names.size(); pi++) {
            if (!borrowed[pi]) continue;
            if (pi < def.ast->param_borrow.size() && def.ast->param_borrow[pi])
                continue;
            if (has_escaping_use(bwg->expr, def.param_names[pi], true, &context)) {
                next[pi] = false;
                changed = true;
            }
        }
        borrowed = std::move(next);
    } while (changed);
    return borrowed;
}

// ===== Let Bindings =====

// Analyze which let-bound names don't escape the scope (for arena allocation).
std::unordered_set<std::string> Codegen::analyze_let_escaping(LetExpr* node) {
    std::unordered_set<std::string> local_non_escaping;
    if (node->aliases.size() < 2) return local_non_escaping;

    std::unordered_set<std::string> local_fns;
    for (auto& [name, _] : deferred_functions_) local_fns.insert(name);
    for (auto& [name, _] : compiled_functions_) local_fns.insert(name);

    std::unordered_set<std::string> let_names;
    for (auto* alias : node->aliases) {
        if (auto* va = dynamic_cast<ValueAlias*>(alias))
            let_names.insert(va->identifier->name->value);
        else if (auto* la = dynamic_cast<LambdaAlias*>(alias))
            let_names.insert(la->name->value);
    }

    std::unordered_set<std::string> escaping;
    std::function<void(AstNode*, bool)> check_escape = [&](AstNode* n, bool ret_pos) {
        if (!n) return;
        if (n->get_type() == AST_IDENTIFIER_EXPR) {
            auto* id = static_cast<IdentifierExpr*>(n);
            if (ret_pos && let_names.count(id->name->value))
                escaping.insert(id->name->value);
            return;
        }
        if (n->get_type() == AST_LET_EXPR) { check_escape(static_cast<LetExpr*>(n)->expr, ret_pos); return; }
        if (n->get_type() == AST_IF_EXPR) {
            auto* ie = static_cast<IfExpr*>(n);
            check_escape(ie->thenExpr, ret_pos);
            check_escape(ie->elseExpr, ret_pos);
            return;
        }
        if (n->get_type() == AST_CASE_EXPR) {
            for (auto* clause : static_cast<CaseExpr*>(n)->clauses) check_escape(clause->body, ret_pos);
            return;
        }
        if (n->get_type() == AST_DO_EXPR) {
            auto* de = static_cast<DoExpr*>(n);
            if (!de->steps.empty()) check_escape(de->steps.back(), ret_pos);
            return;
        }
        if (n->get_type() == AST_FUNCTION_EXPR) {
            auto* fe = static_cast<FunctionExpr*>(n);
            std::function<void(AstNode*)> walk = [&](AstNode* nd) {
                if (!nd) return;
                if (nd->get_type() == AST_IDENTIFIER_EXPR) {
                    if (let_names.count(static_cast<IdentifierExpr*>(nd)->name->value))
                        escaping.insert(static_cast<IdentifierExpr*>(nd)->name->value);
                    return;
                }
                if (nd->get_type() == AST_LET_EXPR) { walk(static_cast<LetExpr*>(nd)->expr); return; }
                if (nd->get_type() == AST_IF_EXPR) { auto* i = static_cast<IfExpr*>(nd); walk(i->condition); walk(i->thenExpr); walk(i->elseExpr); return; }
                if (nd->get_type() == AST_CASE_EXPR) { auto* c = static_cast<CaseExpr*>(nd); walk(c->expr); for (auto* cl : c->clauses) walk(cl->body); return; }
                if (nd->get_type() == AST_APPLY_EXPR) { auto* a = static_cast<ApplyExpr*>(nd); for (auto& arg : a->args) { if (std::holds_alternative<ExprNode*>(arg)) walk(std::get<ExprNode*>(arg)); else walk(std::get<ValueExpr*>(arg)); } return; }
            };
            for (auto* body : fe->bodies)
                if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) walk(bwg->expr);
            return;
        }
        if (n->get_type() == AST_APPLY_EXPR) {
            auto* ae = static_cast<ApplyExpr*>(n);
            std::string callee;
            if (auto* nc = dynamic_cast<NameCall*>(ae->call)) callee = nc->name->value;
            bool is_local = local_fns.count(callee) > 0;
            for (auto& arg : ae->args) {
                AstNode* a = std::holds_alternative<ExprNode*>(arg)
                    ? static_cast<AstNode*>(std::get<ExprNode*>(arg))
                    : static_cast<AstNode*>(std::get<ValueExpr*>(arg));
                check_escape(a, !is_local);
            }
        }
    };
    check_escape(node->expr, true);

    for (auto& name : let_names) {
        if (escaping.count(name)) continue;
        for (auto* alias : node->aliases) {
            if (auto* va = dynamic_cast<ValueAlias*>(alias)) {
                if (va->identifier->name->value != name) continue;
                auto ty = va->expr->get_type();
                if (ty == AST_VALUES_SEQUENCE_EXPR || ty == AST_TUPLE_EXPR ||
                    ty == AST_SET_EXPR || ty == AST_DICT_EXPR ||
                    ty == AST_SEQ_GENERATOR_EXPR || ty == AST_SET_GENERATOR_EXPR ||
                    ty == AST_DICT_GENERATOR_EXPR || ty == AST_FUNCTION_EXPR)
                    local_non_escaping.insert(name);
            }
        }
    }
    return local_non_escaping;
}

// Set up arena allocator for non-escaping bindings (if enough qualify).
llvm::Value* Codegen::setup_let_arena(const std::unordered_set<std::string>& non_escaping) {
    if (non_escaping.size() < 2) return nullptr;
    auto i64_ty = LType::getInt64Ty(*context_);
    return builder_->CreateCall(rt_.arena_create_,
        {ConstantInt::get(i64_ty, ARENA_DEFAULT_SIZE)}, "arena");
}

// `spawn` is tagged IO in Task.yonai so use-sites can auto-await, but the
// C function returns a thread-pool promise pointer — not an io_uring cookie.
static bool let_apply_is_spawn(AstNode* n) {
    if (!n || n->get_type() != AST_APPLY_EXPR) return false;
    auto* cur = static_cast<ApplyExpr*>(n);
    while (cur) {
        if (auto* nc = dynamic_cast<NameCall*>(cur->call))
            return nc->name && nc->name->value == "spawn";
        if (auto* ec = dynamic_cast<ExprCall*>(cur->call)) {
            if (auto* inner = dynamic_cast<ApplyExpr*>(ec->expr))
                cur = inner;
            else
                break;
        } else {
            break;
        }
    }
    return false;
}

// Codegen all let aliases: ValueAlias, LambdaAlias, PatternAlias.
void Codegen::codegen_let_aliases(LetExpr* node, llvm::Value* arena,
                                   const std::unordered_set<std::string>& non_escaping,
                                   std::vector<TypedValue>& scope_bindings,
                                   std::vector<bool>& binding_is_arena) {
    auto saved_arena = current_arena_;

    for (size_t ai = 0; ai < node->aliases.size(); ai++) {
        auto* alias = node->aliases[ai];
        if (auto* va = dynamic_cast<ValueAlias*>(alias)) {
            std::string vname = va->identifier->name->value;

            // Stream fusion: defer single-use generator bindings
            if (va->expr->get_type() == AST_SEQ_GENERATOR_EXPR) {
                int refs = count_identifier_refs(node->expr, vname);
                if (refs == 1) {
                    deferred_generators_[vname] = static_cast<SeqGeneratorExpr*>(va->expr);
                    continue;
                }
            }

            int total_refs = count_identifier_refs(node->expr, vname);
            for (size_t aj = ai + 1; aj < node->aliases.size(); aj++) {
                auto* other = node->aliases[aj];
                if (auto* vb = dynamic_cast<ValueAlias*>(other))
                    total_refs += count_identifier_refs(vb->expr, vname);
                else if (auto* lb = dynamic_cast<LambdaAlias*>(other))
                    total_refs += count_identifier_refs(lb->lambda, vname);
                else if (auto* pb = dynamic_cast<PatternAlias*>(other))
                    total_refs += count_identifier_refs(pb->expr, vname);
            }
            bool use_arena = arena && non_escaping.count(vname);
#ifdef _WIN32
            // Windows: arena-backed seq values are currently unsafe when consumed
            // in later expressions (e.g. grouped-let pattern matching). Keep
            // arena allocation only for bindings that are never referenced.
            if (use_arena && total_refs > 0) use_arena = false;
#endif
            if (use_arena) current_arena_ = arena;
            auto tv = codegen(va->expr);
            // Sequential let + real io_uring: await so a later close cannot
            // race an in-flight writeBytes/readBytes. Skip spawn (IO-tagged
            // but thread-pool). Multi-binding lets stay promises until
            // group_await_all.
            if (node->aliases.size() <= 1 && tv.type == CType::PROMISE
                && tv.promise_await == PromiseAwaitPath::IoUring
                && !let_apply_is_spawn(va->expr))
                tv = auto_await(tv);
            if (use_arena) current_arena_ = saved_arena;

            if (tv) {
                named_values_[vname] = tv;

                // Seq protection: rc_inc to prevent unique-owner tail
                // mutation when the binding is used more than once. If
                // the body + subsequent aliases reference vname at most
                // once, the single use (or drop) takes the existing ref
                // and the protection is unnecessary — saving a copy on
                // the downstream seq_tail_consume fast path.
                if (tv.type == CType::SEQ && tv.val && !llvm::isa<llvm::Constant>(tv.val)) {
                    int uses = total_refs;
                    if (uses > 1)
                        emit_rc_inc(tv.val, CType::SEQ);
                }

                if (is_heap_type(tv.type) && tv.val) {
                    scope_bindings.push_back(tv);
                    binding_is_arena.push_back(use_arena);
                }

                if (debug_.enabled && debug_.scope && debug_.builder && tv.val) {
                    auto* alloca = builder_->CreateAlloca(tv.val->getType(), nullptr, vname);
                    builder_->CreateStore(tv.val, alloca);
                    auto* di_var = debug_.builder->createAutoVariable(
                        debug_.scope, vname, debug_.file, va->source_context.line, di_type_for(tv.type));
                    debug_.builder->insertDeclare(alloca, di_var, debug_.builder->createExpression(),
                        DILocation::get(*context_, va->source_context.line,
                                        va->source_context.column, debug_.scope),
                        builder_->GetInsertBlock());
                    tv.val = builder_->CreateLoad(tv.val->getType(), alloca, vname);
                    named_values_[vname] = tv;
                }
            }
        } else if (auto* la = dynamic_cast<LambdaAlias*>(alias)) {
            auto tv = codegen_lambda_alias(la);
            if (tv && is_heap_type(tv.type) && tv.val) {
                scope_bindings.push_back(tv);
                binding_is_arena.push_back(false);
            }
        } else if (auto* pa = dynamic_cast<PatternAlias*>(alias)) {
            auto tv = codegen(pa->expr);
            if (node->aliases.size() <= 1 && tv.type == CType::PROMISE
                && tv.promise_await == PromiseAwaitPath::IoUring
                && !let_apply_is_spawn(pa->expr))
                tv = auto_await(tv);
            if (tv && tv.type == CType::TUPLE) {
                auto* tp = dynamic_cast<TuplePattern*>(pa->pattern);
                if (tp) {
                    auto i64_local = LType::getInt64Ty(*context_);
                    Value* tuple_ptr = tv.val;
                    if (tuple_ptr->getType()->isIntegerTy())
                        tuple_ptr = builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0));
                    for (size_t i = 0; i < tp->patterns.size(); i++) {
                        Value* elem;
                        if (tuple_ptr->getType()->isPointerTy()) {
                            auto* gep = builder_->CreateGEP(i64_local, tuple_ptr,
                                {ConstantInt::get(i64_local, i + 2)}, "let_tuple_gep");
                            elem = builder_->CreateLoad(i64_local, gep, "let_tuple_elem");
                        } else {
                            elem = builder_->CreateExtractValue(tuple_ptr, {(unsigned)i});
                        }
                        CType et = (i < tv.subtypes.size()) ? tv.subtypes[i] : CType::INT;
                        // Heap-typed elements (ADT, STRING, SEQ, etc.) were stored
                        // as i64-cast pointers; convert back so pattern matching
                        // can dispatch on heap layout.
                        Value* typed_elem = elem;
                        if (et == CType::ADT || et == CType::STRING ||
                            et == CType::FUNCTION || et == CType::SET ||
                            et == CType::DICT || et == CType::CHANNEL)
                            typed_elem = builder_->CreateIntToPtr(elem,
                                PointerType::get(*context_, 0), "tuple_elem_ptr");
                        auto* sub = tp->patterns[i];
                        if (sub->get_type() == AST_PATTERN_VALUE) {
                            auto* pv = static_cast<PatternValue*>(sub);
                            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                                named_values_[(*id)->name->value] = {typed_elem, et};
                                if (is_heap_type(et)) {
                                    scope_bindings.push_back({typed_elem, et});
                                    binding_is_arena.push_back(false);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    current_arena_ = saved_arena;
}

// Clean up scope: RC decrement bindings, destroy arena, clean deferred generators.
void Codegen::cleanup_let_scope(const std::vector<TypedValue>& scope_bindings,
                                 const std::vector<bool>& binding_is_arena,
                                 const TypedValue& result, llvm::Value* arena,
                                 bool destroy_arena_at_end) {
    if (!scope_bindings.empty() && result && result.val) {
        emit_rc_inc(result.val, result.type);
        for (size_t i = 0; i < scope_bindings.size(); i++) {
            if (i < binding_is_arena.size() && binding_is_arena[i])
                continue;
            // Perceus-linear: skip rc_dec for bindings whose ownership
            // was transferred to a consumer (user-defined call, pattern
            // match consume, or a Set/Dict callee-owns extern op).
            // Without this, we'd double-drop a binding the callee freed.
            if (scope_bindings[i].type == CType::SEQ &&
                is_transferred(scope_bindings[i].val, TransferDomain::Seq))
                continue;
            if ((scope_bindings[i].type == CType::SET || scope_bindings[i].type == CType::DICT) &&
                is_transferred(scope_bindings[i].val, TransferDomain::Map))
                continue;
            emit_rc_dec(scope_bindings[i].val, scope_bindings[i].type);
        }
    }
    if (arena && destroy_arena_at_end)
        builder_->CreateCall(rt_.arena_destroy_, {arena});
}

TypedValue Codegen::codegen_let(LetExpr* node) {
    set_debug_loc(node->source_context);

    // 1. Escape analysis
    auto non_escaping = analyze_let_escaping(node);

    // A local function is a lexical definition, not an independently
    // schedulable RHS. Keeping the task-group arena TLS-bound while creating
    // its closure allocates that RC-managed closure inside bump storage, then
    // cleanup both decrements it and destroys the arena. Mixed binding groups
    // therefore stay sequential; groups containing only value/pattern RHSs
    // retain parallel evaluation.
    const bool has_local_function = std::any_of(
        node->aliases.begin(), node->aliases.end(),
        [](AliasExpr* alias) { return dynamic_cast<LambdaAlias*>(alias) != nullptr; });
    const bool has_group = node->aliases.size() > 1 && !has_local_function;
    // 2. Arena: task groups always get a parent-thread bump arena (freed with
    //    the group / on raise unwind). Other multi-binding lets use escape-based
    //    arena only when enough bindings qualify.
    auto saved_arena = current_arena_;
    llvm::Value* arena = nullptr;
    const bool group_arena_lifecycle = has_group;
    if (has_group) {
        auto i64_ty = LType::getInt64Ty(*context_);
        arena = builder_->CreateCall(rt_.arena_create_,
            {ConstantInt::get(i64_ty, ARENA_DEFAULT_SIZE)}, "let_group_arena");
    } else {
        arena = setup_let_arena(non_escaping);
    }

    // 3. Structured concurrency: task group + arena attach + TLS bind for raise
    auto saved_group = current_group_;
    if (has_group) {
        current_group_ = builder_->CreateCall(rt_.group_begin_, {}, "let_group");
        builder_->CreateCall(rt_.group_attach_arena_, {current_group_, arena});
        builder_->CreateCall(rt_.group_arena_bind_push_, {current_group_});
    }

    // 4. Codegen all aliases
    std::vector<TypedValue> scope_bindings;
    std::vector<bool> binding_is_arena;
    codegen_let_aliases(node, arena, non_escaping, scope_bindings, binding_is_arena);

    // 5. Task group body allocation policy:
    // On Windows, keep alias allocations arena-backed for leak-free raise unwind,
    // but keep body temporaries on RC heap to avoid grouped-let seq crash.
#ifndef _WIN32
    if (has_group && arena)
        current_arena_ = arena;
#endif

    // 6. Codegen body
    auto result = codegen(node->expr);

    if (has_group && arena)
        current_arena_ = saved_arena;

    const bool body_terminated = current_block_terminated();

    // 7. Await children before scope cleanup (only on fall-through path; raise/
    //    other terminators skip IR here — runtime unwind calls yona_rt_group_end).
    if (has_group && !body_terminated)
        builder_->CreateCall(rt_.group_await_all_, {current_group_});

    // 8. Cleanup scope (group arena: yona_rt_group_end destroys bump memory)
    if (!body_terminated)
        cleanup_let_scope(scope_bindings, binding_is_arena, result, arena,
                          !group_arena_lifecycle);

    if (has_group) {
        if (!body_terminated) {
            builder_->CreateCall(rt_.group_arena_bind_pop_, {});
            builder_->CreateCall(rt_.group_end_, {current_group_});
        }
        current_group_ = saved_group;
    }

    current_arena_ = saved_arena;

    // 9. Clean up deferred generators not consumed by fusion
    for (auto* alias : node->aliases)
        if (auto* va = dynamic_cast<ValueAlias*>(alias))
            deferred_generators_.erase(va->identifier->name->value);

    return result;
}

// ===== If Expression =====

TypedValue Codegen::codegen_if(IfExpr* node) {
    set_debug_loc(node->source_context);
    auto cond = auto_await(codegen(node->condition));
    if (!cond) return {};
    // Ensure condition is i1 for branch — closures return i64 even for bool results
    if (cond.val->getType() != LType::getInt1Ty(*context_)) {
        if (cond.val->getType()->isIntegerTy())
            cond.val = builder_->CreateICmpNE(cond.val, ConstantInt::get(cond.val->getType(), 0));
        else if (cond.val->getType()->isPointerTy())
            cond.val = builder_->CreateICmpNE(
                builder_->CreatePtrToInt(cond.val, LType::getInt64Ty(*context_)),
                ConstantInt::get(LType::getInt64Ty(*context_), 0));
    }

    auto fn = builder_->GetInsertBlock()->getParent();

    // Wrap the two branches in a transfer scope so any asymmetric
    // seq-ownership transfers (e.g. then calls a consumer, else doesn't)
    // get compensating rc_decs emitted before the non-transferring
    // branch's merge jump. Snapshot pre_blocks BEFORE creating branch
    // blocks so the branches are correctly identified as "inside-scope".
    transfer_scope_enter();

    // Every successor must be owned by its Function before it is reachable.
    // A nested case can reconcile transfers while this if is still being
    // lowered; that reconciliation builds a function-wide DominatorTree and
    // LLVM assumes every successor it visits has a parent Function.
    auto then_bb = BasicBlock::Create(*context_, "then", fn);
    auto else_bb = BasicBlock::Create(*context_, "else", fn);
    auto merge_bb = BasicBlock::Create(*context_, "ifcont", fn);
    builder_->CreateCondBr(cond.val, then_bb, else_bb);

    builder_->SetInsertPoint(then_bb);
    transfer_branch_begin();
    auto then_tv = codegen(node->thenExpr);
    if (!then_tv) { transfer_scope_exit(); return {}; }
    bool then_terminated = current_block_terminated();
    BasicBlock* then_end = nullptr;
    if (!then_terminated) {
        if (then_tv.type == CType::SEQ)
            mark_transferred(then_tv.val, TransferDomain::Seq);
        builder_->CreateBr(merge_bb);
        then_end = builder_->GetInsertBlock();
    }
    transfer_branch_end(then_end);

    builder_->SetInsertPoint(else_bb);
    transfer_branch_begin();
    auto else_tv = codegen(node->elseExpr);
    if (!else_tv) { transfer_scope_exit(); return {}; }
    bool else_terminated = current_block_terminated();
    BasicBlock* else_end = nullptr;
    if (!else_terminated) {
        if (else_tv.type == CType::SEQ)
            mark_transferred(else_tv.val, TransferDomain::Seq);
        builder_->CreateBr(merge_bb);
        else_end = builder_->GetInsertBlock();
    }
    transfer_branch_end(else_end);

    transfer_scope_exit();
    builder_->SetInsertPoint(merge_bb);
    unsigned phi_count = (then_end ? 1 : 0) + (else_end ? 1 : 0);
    if (phi_count == 0) {
        // Both branches terminate (e.g., both raise) — merge is dead
        return then_tv;
    }
    // Determine common type for PHI — branches may return different LLVM types
    // (e.g., ADT struct vs symbol i64, or i1 vs i64)
    LType* then_ty = then_end ? then_tv.val->getType() : nullptr;
    LType* else_ty = else_end ? else_tv.val->getType() : nullptr;
    LType* phi_type;
    if (then_ty && else_ty)
        phi_type = common_phi_type(then_ty, else_ty, *context_);
    else
        phi_type = then_ty ? then_ty : else_ty;

    auto phi = builder_->CreatePHI(phi_type, phi_count);
    if (then_end) {
        // Coerce then value if needed — insert before the branch terminator
        if (then_tv.val->getType() != phi_type) {
            auto saved = builder_->GetInsertPoint();
            builder_->SetInsertPoint(block_terminator(then_end));
            then_tv.val = coerce_for_phi(then_tv.val, phi_type, *builder_, *context_);
            builder_->SetInsertPoint(merge_bb);
        }
        phi->addIncoming(then_tv.val, then_end);
    }
    if (else_end) {
        if (else_tv.val->getType() != phi_type) {
            auto saved = builder_->GetInsertPoint();
            builder_->SetInsertPoint(block_terminator(else_end));
            else_tv.val = coerce_for_phi(else_tv.val, phi_type, *builder_, *context_);
            builder_->SetInsertPoint(merge_bb);
        }
        phi->addIncoming(else_tv.val, else_end);
    }
    return {phi, then_tv.type, then_tv.subtypes};
}

// ===== Identifier =====

TypedValue Codegen::codegen_identifier(IdentifierExpr* node) {
    set_debug_loc(node->source_context);
    // Materialize deferred generator if referenced outside fusion context
    {
        auto dg_it = deferred_generators_.find(node->name->value);
        if (dg_it != deferred_generators_.end()) {
            auto* gen = dg_it->second;
            deferred_generators_.erase(dg_it);
            auto tv = codegen_seq_generator(gen);
            if (tv) named_values_[node->name->value] = tv;
            return tv;
        }
    }

    auto it = named_values_.find(node->name->value);
    if (it != named_values_.end()) {
        // If it's a FUNCTION, check whether the underlying definition is
        // a 0-arity CAF. If so, auto-force it here so call sites that
        // expect a value (e.g. `pi > 3.14`) don't see a function
        // reference instead. Applies both when `val == nullptr`
        // (deferred, not yet compiled) and when `val` points to an
        // already-compiled 0-arg Function*.
        if (it->second.type == CType::FUNCTION) {
            // Deferred CAF — compile and call with no args.
            auto def_it = deferred_functions_.find(node->name->value);
            if (def_it != deferred_functions_.end() && def_it->second.param_names.empty()) {
                auto cf = compile_function(node->name->value, def_it->second, {});
                if (cf.fn) {
                    std::vector<llvm::Value*> no_args;
                    if (cf.closure_env) no_args.push_back(cf.closure_env);
                    auto* call = builder_->CreateCall(cf.fn, no_args, "caf_call");
                    TypedValue result{call, cf.return_type};
                    if (!cf.return_adt_name.empty()) result.adt_type_name = cf.return_adt_name;
                    if (!cf.return_subtypes.empty()) result.subtypes = cf.return_subtypes;
                    return result;
                }
            }
            // Already-compiled 0-arg CAF — emit the call directly.
            auto cf_it = compiled_functions_.find(node->name->value);
            if (cf_it != compiled_functions_.end()) {
                auto& cf = cf_it->second;
                size_t user_arity = cf.param_types.size() - cf.capture_names.size();
                if (user_arity == 0 && cf.extern_promise == ast::ExternPromiseKind::Sync &&
                    cf.return_type != CType::PROMISE) {
                    auto ext_it = imports_.extern_functions.find(node->name->value);
                    if (ext_it != imports_.extern_functions.end()) {
                        std::string mangled = ext_it->second;
                        auto genfn_it = imports_.imported_sources.find(mangled);
                        if (genfn_it != imports_.imported_sources.end()) {
                            auto reparsed = reparse_genfn(genfn_it->second.local_name,
                                                           genfn_it->second.source_text);
                            if (reparsed && !reparsed->functions.empty()) {
                                auto* func_ast = reparsed->functions[0];
                                reparsed->functions.clear();
                                imports_.imported_ast_nodes.push_back(std::unique_ptr<FunctionExpr>(func_ast));
                                GenfnNameIsolation iso(*this, mangled);
                                install_private_genfn_ctors(mangled);
                                register_sibling_genfns(mangled);
                                codegen_function_def(func_ast, node->name->value);
                                auto local_def_it = deferred_functions_.find(node->name->value);
                                if (local_def_it == deferred_functions_.end()) {
                                    iso.restore();
                                    return it->second;
                                }
                                auto local_cf = compile_function(node->name->value,
                                                                 local_def_it->second, {});
                                iso.restore();
                                std::vector<llvm::Value*> local_args;
                                if (local_cf.closure_env) local_args.push_back(local_cf.closure_env);
                                auto* local_call = local_cf.fn->getReturnType()->isVoidTy()
                                    ? builder_->CreateCall(local_cf.fn, local_args)
                                    : builder_->CreateCall(local_cf.fn, local_args, "caf_call");
                                Value* local_val = local_cf.fn->getReturnType()->isVoidTy()
                                    ? static_cast<Value*>(ConstantInt::get(LType::getInt64Ty(*context_), 0))
                                    : static_cast<Value*>(local_call);
                                TypedValue result{local_val, local_cf.return_type};
                                if (!local_cf.return_adt_name.empty()) result.adt_type_name = local_cf.return_adt_name;
                                if (!local_cf.return_subtypes.empty()) result.subtypes = local_cf.return_subtypes;
                                return result;
                            }
                        }
                    }
                    std::vector<llvm::Value*> no_args;
                    if (cf.closure_env) no_args.push_back(cf.closure_env);
                    auto* call = builder_->CreateCall(cf.fn, no_args, "caf_call");
                    TypedValue result{call, cf.return_type};
                    if (!cf.return_adt_name.empty()) result.adt_type_name = cf.return_adt_name;
                    if (!cf.return_subtypes.empty()) result.subtypes = cf.return_subtypes;
                    return result;
                }
            }
            if (!it->second.val) {
                if (imports_.extern_functions.count(node->name->value)) {
                    auto imported = materialize_imported_function_value(node->name->value);
                    if (imported) return imported;
                }
                last_lambda_name_ = node->name->value;
            }
        }
        return it->second;
    }
    // Check if it's a compiled function. 0-arity CAFs ("constants that
    // compute") are auto-forced on every reference, same as the deferred
    // branch below — otherwise a second reference would return the
    // function pointer itself and downstream code (e.g. `pi > 3.14`)
    // type-errors against a callable.
    auto fit = compiled_functions_.find(node->name->value);
    if (fit != compiled_functions_.end()) {
        auto& cf = fit->second;
        size_t user_arity = cf.param_types.size() - cf.capture_names.size();
        if (cf.closure_env) {
            if (auto* env_argument = dyn_cast<Argument>(cf.closure_env);
                env_argument && builder_->GetInsertBlock() &&
                env_argument->getParent() ==
                    builder_->GetInsertBlock()->getParent()) {
                // A recursive closure used as a first-class value must pass
                // its live environment object. Wrapping the raw LLVM body
                // function would manufacture a closure with no captures and
                // recursive calls would read user arguments as an env header.
                return {cf.closure_env, CType::FUNCTION, {cf.return_type}};
            }
        }
        if (user_arity == 0 && cf.extern_promise == ast::ExternPromiseKind::Sync &&
            cf.return_type != CType::PROMISE) {
            auto ext_it = imports_.extern_functions.find(node->name->value);
            if (ext_it != imports_.extern_functions.end()) {
                std::string mangled = ext_it->second;
                auto genfn_it = imports_.imported_sources.find(mangled);
                if (genfn_it != imports_.imported_sources.end()) {
                    auto reparsed = reparse_genfn(genfn_it->second.local_name,
                                                   genfn_it->second.source_text);
                    if (reparsed && !reparsed->functions.empty()) {
                        auto* func_ast = reparsed->functions[0];
                        reparsed->functions.clear();
                        imports_.imported_ast_nodes.push_back(std::unique_ptr<FunctionExpr>(func_ast));
                        GenfnNameIsolation iso(*this, mangled);
                        install_private_genfn_ctors(mangled);
                        register_sibling_genfns(mangled);
                        codegen_function_def(func_ast, node->name->value);
                        auto local_def_it = deferred_functions_.find(node->name->value);
                        if (local_def_it != deferred_functions_.end()) {
                            auto local_cf = compile_function(node->name->value,
                                                             local_def_it->second, {});
                            iso.restore();
                            std::vector<llvm::Value*> local_args;
                            if (local_cf.closure_env) local_args.push_back(local_cf.closure_env);
                            auto* local_call = local_cf.fn->getReturnType()->isVoidTy()
                                ? builder_->CreateCall(local_cf.fn, local_args)
                                : builder_->CreateCall(local_cf.fn, local_args, "caf_call");
                            Value* local_val = local_cf.fn->getReturnType()->isVoidTy()
                                ? static_cast<Value*>(ConstantInt::get(LType::getInt64Ty(*context_), 0))
                                : static_cast<Value*>(local_call);
                            TypedValue result{local_val, local_cf.return_type};
                            if (!local_cf.return_adt_name.empty()) result.adt_type_name = local_cf.return_adt_name;
                            if (!local_cf.return_subtypes.empty()) result.subtypes = local_cf.return_subtypes;
                            return result;
                        }
                        iso.restore();
                    }
                }
            }
            std::vector<llvm::Value*> no_args;
            if (cf.closure_env) no_args.push_back(cf.closure_env);
            auto* call = builder_->CreateCall(cf.fn, no_args, "caf_call");
            TypedValue result{call, cf.return_type};
            if (!cf.return_adt_name.empty()) result.adt_type_name = cf.return_adt_name;
            if (!cf.return_subtypes.empty()) result.subtypes = cf.return_subtypes;
            return result;
        }
        return {cf.fn, CType::FUNCTION, {cf.return_type}};
    }
    // Check if it's a deferred function (not yet compiled — referenced as value).
    // For 0-arity definitions like `naturals = range 0 N`, an identifier
    // reference should auto-force the function (Haskell-style CAF / value
    // binding). Compile and call with no args, returning the value.
    auto def_it = deferred_functions_.find(node->name->value);
    if (def_it != deferred_functions_.end()) {
        if (def_it->second.param_names.empty()) {
            auto cf = compile_function(node->name->value, def_it->second, {});
            if (cf.fn) {
                std::vector<llvm::Value*> no_args;
                if (cf.closure_env) no_args.push_back(cf.closure_env);
                auto* call = builder_->CreateCall(cf.fn, no_args, "caf_call");
                TypedValue result{call, cf.return_type};
                if (!cf.return_adt_name.empty()) result.adt_type_name = cf.return_adt_name;
                if (!cf.return_subtypes.empty()) result.subtypes = cf.return_subtypes;
                return result;
            }
        }
        last_lambda_name_ = node->name->value;
        return {nullptr, CType::FUNCTION};
    }
    // Check if it's a zero-arity ADT constructor
    auto adt_it = types_.adt_constructors.find(node->name->value);
    if (adt_it != types_.adt_constructors.end() && adt_it->second.arity == 0) {
        auto tag_ty = LType::getInt64Ty(*context_);
        auto i64_ty = LType::getInt64Ty(*context_);

        auto* node_ptr = builder_->CreateCall(rt_.adt_alloc_,
            {ConstantInt::get(tag_ty, adt_it->second.tag),
             ConstantInt::get(i64_ty, 0)}, "adt_node");
        TypedValue result{node_ptr, CType::ADT};
        result.adt_type_name = adt_it->second.type_name;
        return result;
    }
    // Check if it's a non-zero-arity ADT constructor (used as a function reference)
    if (adt_it != types_.adt_constructors.end()) {
        last_lambda_name_ = node->name->value;
        return {nullptr, CType::FUNCTION};
    }
    std::string msg = "undefined variable '" + node->name->value + "'";
    auto suggestion = suggest_similar(node->name->value);
    if (!suggestion.empty()) msg += "; did you mean '" + suggestion + "'?";
    report_error(node->source_context, msg);
    return {};
}

TypedValue Codegen::codegen_main_node(MainNode* node) { return codegen(node->node); }

// ===== Do Expression =====

TypedValue Codegen::codegen_do(DoExpr* node) {
    set_debug_loc(node->source_context);
    TypedValue last;
    for (size_t i = 0; i < node->steps.size(); ++i) {
        last = codegen(node->steps[i]);
        // `do` is the language's ordered-effect construct. A submitted
        // operation must complete before the next step starts; the final
        // value remains untouched so callers retain normal Promise
        // composition/auto-await behavior.
        if (last.type == CType::PROMISE && i + 1 < node->steps.size())
            last = auto_await(last);
    }
    return last ? last : TypedValue{ConstantInt::get(LType::getInt64Ty(*context_), 0), CType::INT};
}

// ===== Exception Handling =====

TypedValue Codegen::codegen_raise(RaiseExpr* node) {
    set_debug_loc(node->source_context);
    auto exc_val = codegen(node->value);
    if (!exc_val) return {};

    auto i64_ty = LType::getInt64Ty(*context_);
    Value* tag_val;
    Value* payload_val;

    if (exc_val.type == CType::ADT && exc_val.val->getType()->isStructTy()) {
        // Non-recursive ADT: extract tag and first payload field
        tag_val = builder_->CreateExtractValue(exc_val.val, {0});
        tag_val = builder_->CreateZExt(tag_val, i64_ty);
        if (cast<StructType>(exc_val.val->getType())->getNumElements() > 1) {
            payload_val = builder_->CreateExtractValue(exc_val.val, {1});
            // If payload is a pointer (string), keep as-is via inttoptr
            if (payload_val->getType()->isIntegerTy())
                payload_val = builder_->CreateIntToPtr(payload_val, PointerType::get(*context_, 0));
        } else {
            payload_val = ConstantPointerNull::get(PointerType::get(*context_, 0));
        }
    } else if (exc_val.type == CType::ADT && exc_val.val->getType()->isPointerTy()) {
        // Recursive ADT: use runtime accessors
        tag_val = builder_->CreateZExt(
            builder_->CreateCall(rt_.adt_get_tag_, {exc_val.val}), i64_ty);
        auto field = builder_->CreateCall(rt_.adt_get_field_, {exc_val.val, ConstantInt::get(i64_ty, 0)});
        payload_val = builder_->CreateIntToPtr(field, PointerType::get(*context_, 0));
    } else {
        // Fallback: treat as integer tag with no payload
        tag_val = exc_val.val;
        if (tag_val->getType() != i64_ty)
            tag_val = builder_->CreateZExtOrTrunc(tag_val, i64_ty);
        payload_val = ConstantPointerNull::get(PointerType::get(*context_, 0));
    }

    builder_->CreateCall(rt_.raise_, {tag_val, payload_val});
    builder_->CreateUnreachable();
    return {UndefValue::get(i64_ty), CType::UNIT};
}

TypedValue Codegen::codegen_try_catch(TryCatchExpr* node) {
    set_debug_loc(node->source_context);
    auto fn = builder_->GetInsertBlock()->getParent();
    auto i32_ty = LType::getInt32Ty(*context_);
    auto i64_ty = LType::getInt64Ty(*context_);

    // Keep every live successor in the Function while lowering nested
    // expressions.  A nested transfer reconciliation constructs a
    // function-wide DominatorTree, which cannot safely traverse a detached
    // merge block reached from try/catch code.
    auto try_bb = BasicBlock::Create(*context_, "try.body", fn);
    auto catch_bb = BasicBlock::Create(*context_, "catch.entry", fn);
    auto merge_bb = BasicBlock::Create(*context_, "try.merge", fn);

    // SJLJ try-entry. We deliberately bypass the C runtime's setjmp/longjmp:
    // on Windows MSVC, setjmp records SEH unwind state and longjmp walks the
    // SEH chain — fatal when raise() longjmps from a worker frame back into
    // codegen-emitted main, which has no SEH metadata. The buffer (void*[5])
    // is owned by the runtime via try_push: slot 0 = FP, slot 1 = resume IP,
    // slot 2 = SP. yona_rt_raise restores those and branches to slot 1.
    //
    // On AArch64, llvm.eh.sjlj.setjmp lowers to a no-op (`mov w0, #0; ret`)
    // and never writes the resume IP, so we store catch.entry's blockaddress
    // ourselves. Other targets still use the intrinsic.
    auto jmp_buf_ptr = builder_->CreateCall(rt_.try_begin_, {}, "jmp.buf");
    auto* ptr_ty = llvm::PointerType::get(*context_, 0);
    auto* fa_fn = llvm::Intrinsic::getOrInsertDeclaration(
        module_.get(), llvm::Intrinsic::frameaddress, {ptr_ty});
    auto* ss_fn = llvm::Intrinsic::getOrInsertDeclaration(
        module_.get(), llvm::Intrinsic::stacksave, {ptr_ty});
    auto* fa = builder_->CreateCall(fa_fn, {ConstantInt::get(i32_ty, 0)}, "sjlj.fp");
    builder_->CreateStore(fa, jmp_buf_ptr);
    auto* sp = builder_->CreateCall(ss_fn, {}, "sjlj.sp");
    auto* sp_slot = builder_->CreateGEP(ptr_ty, jmp_buf_ptr,
                                        ConstantInt::get(i32_ty, 2), "sjlj.sp.slot");
    builder_->CreateStore(sp, sp_slot);
    fn->addFnAttr(llvm::Attribute::NoInline);
#if defined(__aarch64__)
    // Match include/yona/runtime/sjlj.h — Clang rejects __builtin_setjmp here
    // and llvm.eh.sjlj.setjmp is a no-op on AArch64 Darwin.
    auto* sj_ty = llvm::FunctionType::get(i32_ty, {ptr_ty}, false);
    auto* sj_asm = llvm::InlineAsm::get(
        sj_ty,
        "str x29, [$1]\n\t"
        "adr x2, 1f\n\t"
        "str x2, [$1, #8]\n\t"
        "mov x2, sp\n\t"
        "str x2, [$1, #16]\n\t"
        "mov ${0:w}, wzr\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov ${0:w}, #1\n\t"
        "2:",
        "=&r,r,~{x2},~{memory}",
        true);
    auto* try_result = builder_->CreateCall(sj_asm, {jmp_buf_ptr}, "try.setjmp");
    llvm::cast<llvm::CallInst>(try_result)->setCanReturnTwice();
#else
    auto* sj_fn = llvm::Intrinsic::getOrInsertDeclaration(
        module_.get(), llvm::Intrinsic::eh_sjlj_setjmp);
    auto try_result = builder_->CreateCall(sj_fn, {jmp_buf_ptr}, "try.setjmp");
#endif
    auto is_exc = builder_->CreateICmpNE(try_result, ConstantInt::get(i32_ty, 0));
    builder_->CreateCondBr(is_exc, catch_bb, try_bb);

    // Try body
    builder_->SetInsertPoint(try_bb);
    auto try_val = codegen(node->tryExpr);
    bool try_terminated = current_block_terminated();
    BasicBlock* try_end_bb = nullptr;
    if (!try_terminated) {
        builder_->CreateCall(rt_.try_end_, {});
        if (!try_val) try_val = {ConstantInt::get(i64_ty, 0), CType::INT};
        builder_->CreateBr(merge_bb);
        try_end_bb = builder_->GetInsertBlock();
    } else {
        if (!try_val) try_val = {ConstantInt::get(i64_ty, 0), CType::INT};
    }

    // Catch body: get exception tag and payload, pattern match
    builder_->SetInsertPoint(catch_bb);
    auto exc_tag = builder_->CreateCall(rt_.get_exc_sym_, {}, "exc.tag");
    auto exc_payload = builder_->CreateCall(rt_.get_exc_msg_, {}, "exc.payload");

    TypedValue catch_val = {ConstantInt::get(i64_ty, 0), CType::INT};
    std::vector<std::pair<TypedValue, BasicBlock*>> catch_results;

    if (node->catchExpr) {
        for (size_t ci = 0; ci < node->catchExpr->patterns.size(); ci++) {
            auto* cp = node->catchExpr->patterns[ci];
            auto body_bb = BasicBlock::Create(*context_, "catch.body." + std::to_string(ci), fn);
            BasicBlock* next_bb;
            if (ci + 1 < node->catchExpr->patterns.size())
                next_bb = BasicBlock::Create(*context_, "catch.next." + std::to_string(ci+1), fn);
            else
                next_bb = BasicBlock::Create(*context_, "catch.reraise", fn);

            auto* pat = cp->matchPattern;

            if (pat->get_type() == AST_CONSTRUCTOR_PATTERN) {
                // ADT constructor pattern: RuntimeError msg -> ...
                auto* cpat = static_cast<ConstructorPattern*>(pat);
                auto ctor_it = types_.adt_constructors.find(cpat->constructor_name);
                if (ctor_it != types_.adt_constructors.end()) {
                    auto tag_val = ConstantInt::get(i64_ty, ctor_it->second.tag);
                    auto cmp = builder_->CreateICmpEQ(exc_tag, tag_val);
                    builder_->CreateCondBr(cmp, body_bb, next_bb);
                    builder_->SetInsertPoint(body_bb);
                    // Bind sub-patterns: payload is the first field
                    for (size_t fi = 0; fi < cpat->sub_patterns.size(); fi++) {
                        auto* sub = cpat->sub_patterns[fi];
                        if (sub->get_type() == AST_PATTERN_VALUE) {
                            auto* pv = static_cast<PatternValue*>(sub);
                            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                                CType ftype = (fi < ctor_it->second.field_types.size())
                                    ? ctor_it->second.field_types[fi] : CType::INT;
                                if (fi == 0) {
                                    named_values_[(*id)->name->value] = {exc_payload, ftype};
                                } else {
                                    named_values_[(*id)->name->value] = {
                                        ConstantInt::get(i64_ty, 0), CType::INT};
                                }
                            }
                        }
                    }
                } else {
                    builder_->CreateBr(body_bb);
                    builder_->SetInsertPoint(body_bb);
                }
            } else if (pat->get_type() == AST_UNDERSCORE_PATTERN) {
                builder_->CreateBr(body_bb);
                builder_->SetInsertPoint(body_bb);
            } else if (pat->get_type() == AST_PATTERN_VALUE) {
                auto* pv = static_cast<PatternValue*>(pat);
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                    // Bind exception as a tuple (tag, payload)
                    auto* st = StructType::get(*context_, {i64_ty, llvm_type(CType::STRING)});
                    Value* tup = UndefValue::get(st);
                    tup = builder_->CreateInsertValue(tup, exc_tag, {0});
                    tup = builder_->CreateInsertValue(tup, exc_payload, {1});
                    named_values_[(*id)->name->value] = {tup, CType::TUPLE, {CType::INT, CType::STRING}};
                }
                builder_->CreateBr(body_bb);
                builder_->SetInsertPoint(body_bb);
            } else {
                builder_->CreateBr(body_bb);
                builder_->SetInsertPoint(body_bb);
            }

            // Compile handler body
            TypedValue handler_val;
            if (auto* pwog = std::get_if<PatternWithoutGuards*>(&cp->pattern))
                handler_val = codegen((*pwog)->expr);
            if (!handler_val) handler_val = {ConstantInt::get(i64_ty, 0), CType::INT};

            if (!current_block_terminated()) {
                builder_->CreateBr(merge_bb);
                catch_results.push_back({handler_val, builder_->GetInsertBlock()});
            }

            if (ci + 1 < node->catchExpr->patterns.size())
                builder_->SetInsertPoint(next_bb);
            else {
                builder_->SetInsertPoint(next_bb);
                builder_->CreateCall(rt_.raise_, {exc_tag, exc_payload});
                builder_->CreateUnreachable();
            }
        }
    }

    // Merge
    builder_->SetInsertPoint(merge_bb);

    if (catch_results.empty()) {
        if (!try_end_bb) {
            // Try body and every catch arm terminate (raise/return). Do not
            // leave merge as a live success continuation: the parent would
            // treat the try as producing try_val (UNIT from raise).
            builder_->CreateUnreachable();
            return {UndefValue::get(i64_ty), CType::UNIT};
        }
        return try_val;
    }

    // Determine result type: use catch type if try body always raises (terminated)
    CType result_type = try_end_bb ? try_val.type
        : (!catch_results.empty() ? catch_results[0].first.type : CType::INT);
    // Determine common PHI type across try body and all catch handlers
    LType* result_llvm = try_end_bb ? try_val.val->getType()
        : (!catch_results.empty() ? catch_results[0].first.val->getType() : LType::getInt64Ty(*context_));
    for (auto& [tv, bb] : catch_results)
        result_llvm = common_phi_type(result_llvm, tv.val->getType(), *context_);

    unsigned pred_count = (try_end_bb ? 1 : 0) + catch_results.size();
    if (pred_count == 0) return try_val;

    auto phi = builder_->CreatePHI(result_llvm, pred_count, "try.result");
    if (try_end_bb) {
        Value* incoming = try_val.val;
        if (incoming->getType() != result_llvm) {
            builder_->SetInsertPoint(block_terminator(try_end_bb));
            incoming = coerce_for_phi(incoming, result_llvm, *builder_, *context_);
            builder_->SetInsertPoint(merge_bb);
        }
        phi->addIncoming(incoming, try_end_bb);
    }
    for (auto& [tv, bb] : catch_results) {
        Value* incoming = tv.val;
        if (incoming->getType() != result_llvm) {
            builder_->SetInsertPoint(block_terminator(bb));
            incoming = coerce_for_phi(incoming, result_llvm, *builder_, *context_);
            builder_->SetInsertPoint(merge_bb);
        }
        phi->addIncoming(incoming, bb);
    }
    return {phi, result_type};
}

// ===== With Expression (resource management) =====
//
// with handle = openFile "data.txt" in readAll handle
//
// Desugars to: bind resource, try body, close on both success and failure.
// Guarantees close() is called regardless of exceptions.
// The resource type must have a Closeable instance (checked at compile time).

TypedValue Codegen::codegen_with(WithExpr* node) {
    set_debug_loc(node->source_context);
    auto fn = builder_->GetInsertBlock()->getParent();
    auto i32_ty = LType::getInt32Ty(*context_);
    auto i64_ty = LType::getInt64Ty(*context_);

    // 1. Evaluate resource expression and bind to name
    auto resource = codegen(node->contextExpr);
    if (!resource) return {};

    // 2. Resolve Closeable.close for the resource type via trait dispatch
    std::string adt_name = resource.adt_type_name;
    auto resolved = resolve_trait_method("close", resource.type, adt_name,
                                         "Closeable");
    if (resolved.empty()) {
        std::string type_name = ctype_to_type_name(resource.type);
        if (!adt_name.empty()) type_name = adt_name;
        report_error(node->source_context,
            "type '" + type_name + "' does not implement Closeable (required by 'with')");
        return {};
    }

    // Find the close function
    auto close_cf = compiled_functions_.find(resolved);
    if (close_cf == compiled_functions_.end()) {
        auto def_it = deferred_functions_.find(resolved);
        if (def_it != deferred_functions_.end()) {
            compile_function(resolved, def_it->second, {resource});
            close_cf = compiled_functions_.find(resolved);
        }
    }
    if (close_cf == compiled_functions_.end()) {
        // Try as extern
        auto* close_fn = module_->getFunction(resolved);
        if (!close_fn) {
            auto fn_type = llvm::FunctionType::get(LType::getVoidTy(*context_),
                {resource.val->getType()}, false);
            close_fn = Function::Create(fn_type, Function::ExternalLinkage, resolved, module_.get());
        }
        CompiledFunction cf;
        cf.fn = close_fn;
        cf.return_type = CType::UNIT;
        cf.param_types = {resource.type};
        compiled_functions_[resolved] = cf;
        close_cf = compiled_functions_.find(resolved);
    }

    auto* close_fn = close_cf->second.fn;

    std::string var_name = node->name->value;
    auto saved_values = named_values_;
    ActiveNamedValueSnapshot saved_values_snapshot(*this, saved_values);
    named_values_[var_name] = resource;

    // Helper to emit close call using the resolved trait method
    auto emit_close = [&]() {
        Value* arg = resource.val;
        if (close_fn->arg_size() > 0) {
            auto* expected_ty = close_fn->getArg(0)->getType();
            if (arg->getType() != expected_ty) {
                if (arg->getType()->isPointerTy() && expected_ty->isIntegerTy())
                    arg = builder_->CreatePtrToInt(arg, expected_ty);
                else if (arg->getType()->isIntegerTy() && expected_ty->isPointerTy())
                    arg = builder_->CreateIntToPtr(arg, expected_ty);
                else if (arg->getType()->isIntegerTy() && expected_ty->isIntegerTy())
                    arg = builder_->CreateZExtOrTrunc(arg, expected_ty);
            }
        }
        builder_->CreateCall(close_fn, {arg});
    };

    // 3. Evaluate body expression
    auto body_val = codegen(node->bodyExpr);
    if (!body_val) body_val = {ConstantInt::get(i64_ty, 0), CType::INT};

    // 4. Close resource (always, regardless of body result)
    if (!current_block_terminated())
        emit_close();

    named_values_ = saved_values;
    return body_val;
}

} // namespace yona::compiler::codegen
