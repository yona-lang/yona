//
// LLVM Code Generation for Yona — Type-directed codegen (core)
//
// Every expression produces a TypedValue (LLVM Value + CType tag).
// Types propagate structurally: codegen_integer returns {i64, INT},
// codegen_add checks operand CTypes to choose iadd vs fadd, etc.
// Functions are deferred until call sites where arg types are known.
//

#include "Codegen.h"
#include "DeriveEngine.h"
#include "Parser.h"
#include "typechecker/TypeChecker.h"
#include <llvm/Linker/Linker.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/SourceMgr.h>

#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/BinaryFormat/Dwarf.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <map>

namespace yona::compiler::codegen {

using namespace llvm;
using LType = llvm::Type; // avoid collision with yona::compiler::types::Type

// ===== Constructor / Init =====

Codegen::Codegen(const std::string& module_name, compiler::DiagnosticEngine* diag) {
    if (diag) {
        diag_ = diag;
    } else {
        owned_diag_ = std::make_unique<compiler::DiagnosticEngine>();
        diag_ = owned_diag_.get();
    }
    context_ = std::make_unique<LLVMContext>();
    module_ = std::make_unique<Module>(module_name, *context_);
    builder_ = std::make_unique<IRBuilder<>>(*context_);
    init_target();
    declare_runtime();

    // Built-in Closeable trait (prelude) — enables `with` expression
    TraitInfo closeable;
    closeable.name = "Closeable";
    closeable.type_param = "a";
    closeable.type_params = {"a"};
    closeable.method_names.push_back("close");
    types_.traits["Closeable"] = closeable;

    // Closeable Int — for file descriptors and socket handles
    TraitInstanceInfo closeable_int;
    closeable_int.trait_name = "Closeable";
    closeable_int.type_name = "Int";
    closeable_int.type_names = {"Int"};
    closeable_int.method_mangled_names["close"] = "Closeable_Int__close";
    types_.trait_instances["Closeable:Int"] = closeable_int;
    // Register rt_close as the implementation
    compiled_functions_["Closeable_Int__close"] = {rt_.close_, CType::UNIT, {CType::INT}};

    // Closeable FileHandle — for binary file handles
    TraitInstanceInfo closeable_fh;
    closeable_fh.trait_name = "Closeable";
    closeable_fh.type_name = "FileHandle";
    closeable_fh.type_names = {"FileHandle"};
    closeable_fh.method_mangled_names["close"] = "yona_Std_File__closeFileHandle";
    types_.trait_instances["Closeable:FileHandle"] = closeable_fh;
}
Codegen::~Codegen() = default;

static void append_yona_path_dirs(std::vector<std::string>& module_paths) {
    const char* env = std::getenv("YONA_PATH");
    if (!env || !*env)
        return;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string cur;
    auto flush = [&]() {
        if (cur.empty())
            return;
        std::error_code ec;
        auto p = std::filesystem::path(cur);
        if (!std::filesystem::is_directory(p, ec)) {
            cur.clear();
            return;
        }
        auto canon = std::filesystem::canonical(p, ec);
        if (ec) {
            cur.clear();
            return;
        }
        std::string s = canon.string();
        if (std::find(module_paths.begin(), module_paths.end(), s) == module_paths.end())
            module_paths.push_back(s);
        cur.clear();
    };
    for (const char* c = env; *c; ++c) {
        if (*c == sep)
            flush();
        else
            cur.push_back(*c);
    }
    flush();
}

void Codegen::load_prelude(parser::Parser* parser,
                            typechecker::TypeChecker* type_checker) {
    append_yona_path_dirs(module_paths_);
    // 1. Load Prelude.yonai — populates types_.adt_constructors and imports_.meta
    load_module_interface(std::filesystem::path("Prelude"));

    // 2. Auto-import ALL Prelude exports (same as wildcard `import Prelude in ...`)
    register_all_imports("Prelude");

    // 4. Register constructors in parser (if provided)
    if (parser) {
        for (auto& [name, info] : types_.adt_constructors) {
            parser->register_constructor(name, info.type_name, info.tag, info.arity, info.field_names);
        }
    }

    // 5. Register ADTs and function types in type checker (if provided)
    if (type_checker) {
        // Collect ADTs by type name
        std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> adt_ctors;
        std::unordered_map<std::string, std::vector<std::string>> adt_params;
        for (auto& [name, info] : types_.adt_constructors) {
            adt_ctors[info.type_name].push_back({name, info.arity});
        }
        // Infer type params from arity (simple heuristic: one param per max arity)
        for (auto& [type_name, ctors] : adt_ctors) {
            int max_arity = 0;
            for (auto& [_, arity] : ctors) max_arity = std::max(max_arity, arity);
            std::vector<std::string> params;
            for (int i = 0; i < max_arity; i++) {
                params.push_back(std::string(1, 'a' + i));
            }
            // Special case: Result has 2 params
            if (type_name == "Result") params = {"a", "e"};
            type_checker->register_adt(type_name, params, ctors);
        }

        // Register prelude functions as fully polymorphic in the type checker.
        auto& arena = type_checker->arena();
        // CType info from .yonai is too coarse for proper HM inference, so we
        // use fresh type variables for all params — the type checker will infer
        // the concrete types at each call site.
        const std::string tc_prefix = "yona_Prelude__";
        for (auto& [mangled, meta] : imports_.meta) {
            if (mangled.find(tc_prefix) != 0) continue;
            std::string local_name = mangled.substr(tc_prefix.size());

            // Build polymorphic function type with fresh vars for all params + return.
            // register_trait_method will generalize (quantify free vars).
            auto* ret_var = arena.fresh_var(0);
            typechecker::MonoTypePtr fn_type = ret_var;

            for (int i = (int)meta.param_types.size() - 1; i >= 0; i--) {
                auto* param_var = arena.fresh_var(0);
                fn_type = arena.make_arrow(param_var, fn_type);
            }

            type_checker->register_trait_method("Prelude", local_name, fn_type);
        }

        // Register trait methods as polymorphic functions so the type checker
        // accepts calls like `length arr` without an explicit import.
        for (auto& [trait_name, trait_info] : types_.traits) {
            for (auto& method_name : trait_info.method_names) {
                auto* ret_var = arena.fresh_var(0);
                typechecker::MonoTypePtr fn_type = ret_var;
                // Trait methods have at least one parameter (the receiver)
                auto* recv_var = arena.fresh_var(0);
                fn_type = arena.make_arrow(recv_var, fn_type);
                type_checker->register_trait_method(trait_name, method_name, fn_type);
            }
        }

        // Register typeOf as a built-in compile-time intrinsic: a -> Type
        // The codegen intercepts calls to typeOf and constructs a Type ADT
        // based on the argument's compile-time CType. Zero runtime cost.
        {
            auto* arg_var = arena.fresh_var(0);
            auto* type_adt = arena.make_app("Type", {});
            auto* fn_type = arena.make_arrow(arg_var, type_adt);
            type_checker->register_trait_method("Prelude", "typeOf", fn_type);
        }
    }
}

// ===== DWARF Debug Info =====

void Codegen::set_debug_info(bool enabled, const std::string& filename) {
    debug_.enabled = enabled;
    if (enabled) init_debug_info(filename);
}

void Codegen::init_debug_info(const std::string& filename) {
    debug_.builder = std::make_unique<DIBuilder>(*module_);
    // Use DW_LANG_C as closest match for Yona
    auto file_path = std::filesystem::path(filename);
    auto dir = file_path.parent_path().string();
    auto file = file_path.filename().string();
    if (dir.empty()) dir = ".";
    debug_.file = debug_.builder->createFile(file, dir);
    debug_.cu = debug_.builder->createCompileUnit(
        dwarf::DW_LANG_C, debug_.file, "yonac", false, "", 0);
    debug_.scope = debug_.cu;
    // Add debug info flag to module
    module_->addModuleFlag(Module::Warning, "Debug Info Version",
                           DEBUG_METADATA_VERSION);
    module_->addModuleFlag(Module::Warning, "Dwarf Version", 4);
}

void Codegen::finalize_debug_info() {
    if (debug_.builder) debug_.builder->finalize();
}

void Codegen::set_debug_loc(const SourceLocation& loc) {
    if (!debug_.enabled || !debug_.scope) return;
    if (loc.line == 0) return; // skip unknown locations
    builder_->SetCurrentDebugLocation(
        DILocation::get(*context_, loc.line, loc.column, debug_.scope));
}

DIType* Codegen::di_type_for(CType ct) {
    if (!debug_.builder) return nullptr;
    switch (ct) {
        case CType::INT:    return debug_.builder->createBasicType("Int", 64, dwarf::DW_ATE_signed);
        case CType::FLOAT:  return debug_.builder->createBasicType("Float", 64, dwarf::DW_ATE_float);
        case CType::BOOL:   return debug_.builder->createBasicType("Bool", 8, dwarf::DW_ATE_boolean);
        case CType::STRING: return debug_.builder->createPointerType(
            debug_.builder->createBasicType("Char", 8, dwarf::DW_ATE_signed_char), 64);
        case CType::SYMBOL: return debug_.builder->createBasicType("Symbol", 64, dwarf::DW_ATE_signed);
        case CType::UNIT:   return debug_.builder->createBasicType("Unit", 64, dwarf::DW_ATE_signed);
        case CType::FUNCTION: return debug_.builder->createPointerType(nullptr, 64);
        case CType::SEQ:
        case CType::SET:
        case CType::DICT:
        case CType::ADT:
        case CType::PROMISE:
            return debug_.builder->createPointerType(
                debug_.builder->createBasicType("Opaque", 8, dwarf::DW_ATE_unsigned), 64);
        case CType::TUPLE:
            return debug_.builder->createBasicType("Tuple", 64, dwarf::DW_ATE_signed);
    }
    return debug_.builder->createBasicType("Unknown", 64, dwarf::DW_ATE_signed);
}

DISubroutineType* Codegen::di_func_type(const std::vector<CType>& param_types, CType ret_type) {
    SmallVector<Metadata*, 8> types;
    types.push_back(di_type_for(ret_type)); // return type first
    for (auto ct : param_types)
        types.push_back(di_type_for(ct));
    return debug_.builder->createSubroutineType(debug_.builder->getOrCreateTypeArray(types));
}

void Codegen::init_target() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    auto triple_str = sys::getDefaultTargetTriple();
    Triple triple(triple_str);
    module_->setTargetTriple(triple);
    std::string err;
    auto target = TargetRegistry::lookupTarget(triple_str, err);
    if (!target) { std::cerr << "Target error: " << err << "\n"; return; }
    TargetOptions opt;
    target_machine_ = target->createTargetMachine(triple, "generic", "", opt, Reloc::PIC_);
    module_->setDataLayout(target_machine_->createDataLayout());
}

LType* Codegen::llvm_type(CType ct) {
    switch (ct) {
        case CType::INT:    return LType::getInt64Ty(*context_);
        case CType::FLOAT:  return LType::getDoubleTy(*context_);
        case CType::BOOL:   return LType::getInt1Ty(*context_);
        case CType::STRING: return PointerType::get(LType::getInt8Ty(*context_), 0);
        case CType::SEQ:    return PointerType::get(LType::getInt64Ty(*context_), 0);
        case CType::TUPLE:  return LType::getInt64Ty(*context_); // ptrtoint'd boxed ptr
        case CType::UNIT:   return LType::getInt64Ty(*context_);
        case CType::FUNCTION: return PointerType::get(LType::getInt8Ty(*context_), 0);
        case CType::SYMBOL: return LType::getInt64Ty(*context_); // interned symbol ID
        case CType::SET:    return PointerType::get(LType::getInt64Ty(*context_), 0);
        case CType::DICT:   return PointerType::get(LType::getInt64Ty(*context_), 0);
        case CType::PROMISE: return PointerType::get(LType::getInt8Ty(*context_), 0);
        case CType::ADT:    return LType::getInt64Ty(*context_); // overridden per-ADT
        case CType::BYTE_ARRAY:  return PointerType::get(LType::getInt8Ty(*context_), 0);
        case CType::INT_ARRAY: return PointerType::get(LType::getInt64Ty(*context_), 0);
        case CType::FLOAT_ARRAY: return PointerType::get(LType::getDoubleTy(*context_), 0);
        case CType::CHANNEL: return PointerType::get(LType::getInt8Ty(*context_), 0);
        case CType::SUM:    return LType::getInt64Ty(*context_); // boxed tagged value (2-tuple)
        case CType::RECORD: return LType::getInt64Ty(*context_); // boxed tuple (ptrtoint'd)
    }
    return LType::getInt64Ty(*context_);
}

void Codegen::declare_runtime() {
    auto i64 = LType::getInt64Ty(*context_);
    auto f64 = LType::getDoubleTy(*context_);
    auto vd = LType::getVoidTy(*context_);
    auto i1 = LType::getInt1Ty(*context_);
    auto ptr = PointerType::get(LType::getInt8Ty(*context_), 0);
    auto i64p = PointerType::get(i64, 0);

    auto decl = [&](const char* name, LType* ret, std::vector<LType*> args) {
        return Function::Create(llvm::FunctionType::get(ret, args, false),
                                Function::ExternalLinkage, name, module_.get());
    };

    rt_.print_int_     = decl("yona_rt_print_int", vd, {i64});
    rt_.print_float_   = decl("yona_rt_print_float", vd, {f64});
    rt_.print_string_  = decl("yona_rt_print_string", vd, {ptr});
    rt_.print_bool_    = decl("yona_rt_print_bool", vd, {i1});
    rt_.print_newline_ = decl("yona_rt_print_newline", vd, {});
    rt_.print_seq_     = decl("yona_rt_print_seq", vd, {i64p});
    rt_.string_concat_ = decl("yona_rt_string_concat", ptr, {ptr, ptr});
    rt_.string_eq_     = decl("yona_Prelude__Eq_String__eq", i64, {ptr, ptr});
    rt_.seq_alloc_     = decl("yona_rt_seq_alloc", i64p, {i64});
    rt_.seq_set_       = decl("yona_rt_seq_set", vd, {i64p, i64, i64});
    rt_.seq_set_heap_  = decl("yona_rt_seq_set_heap", vd, {i64p, i64});
    rt_.seq_get_       = decl("yona_rt_seq_get", i64, {i64p, i64});
    rt_.seq_length_    = decl("yona_rt_seq_length", i64, {i64p});
    rt_.seq_cons_      = decl("yona_rt_seq_cons", i64p, {i64, i64p});
    rt_.seq_join_      = decl("yona_rt_seq_join", i64p, {i64p, i64p});
    rt_.seq_head_      = decl("yona_rt_seq_head", i64, {i64p});
    rt_.seq_tail_      = decl("yona_rt_seq_tail", i64p, {i64p});
    rt_.seq_tail_consume_ = decl("yona_rt_seq_tail_consume", i64p, {i64p});
    rt_.seq_is_empty_  = decl("yona_rt_seq_is_empty", i64, {i64p});
    rt_.seq_snoc_      = decl("yona_rt_seq_snoc", i64p, {i64p, i64});  // append to end
    rt_.print_symbol_  = decl("yona_rt_print_symbol", vd, {ptr}); // takes char* name

    // Set runtime
    rt_.set_alloc_     = decl("yona_rt_set_alloc", i64p, {i64});
    rt_.set_put_       = decl("yona_rt_set_put", vd, {i64p, i64, i64});
    rt_.set_insert_    = decl("yona_rt_set_insert", i64p, {i64p, i64});
    rt_.set_contains_  = decl("yona_rt_set_contains", i64, {i64p, i64});
    rt_.set_size_      = decl("yona_rt_set_size", i64, {i64p});
    rt_.set_elements_  = decl("yona_rt_set_elements", i64p, {i64p});
    rt_.set_union_     = decl("yona_rt_set_union", i64p, {i64p, i64p});
    rt_.set_intersection_ = decl("yona_rt_set_intersection", i64p, {i64p, i64p});
    rt_.set_difference_ = decl("yona_rt_set_difference", i64p, {i64p, i64p});
    rt_.print_set_     = decl("yona_rt_print_set", vd, {i64p});

    // Dict runtime
    rt_.dict_alloc_    = decl("yona_rt_dict_alloc", i64p, {i64});
    rt_.dict_set_      = decl("yona_rt_dict_set", vd, {i64p, i64, i64, i64});
    rt_.dict_put_      = decl("yona_rt_dict_put", i64p, {i64p, i64, i64});
    rt_.dict_get_      = decl("yona_rt_dict_get", i64, {i64p, i64, i64});
    rt_.dict_size_     = decl("yona_rt_dict_size", i64, {i64p});
    rt_.dict_contains_ = decl("yona_rt_dict_contains", i64, {i64p, i64});
    rt_.dict_keys_     = decl("yona_rt_dict_keys", i64p, {i64p});
    rt_.print_dict_    = decl("yona_rt_print_dict", vd, {i64p});

    // Async runtime: promise = async_call(fn_ptr, arg), result = async_await(promise)
    // fn_ptr type: i64 (*)(i64) — function pointer taking and returning i64
    auto fn_ptr_ty = PointerType::get(llvm::FunctionType::get(i64, {i64}, false), 0);
    auto promise_ptr = ptr; // opaque pointer to yona_promise_t
    rt_.async_call_    = decl("yona_rt_async_call", promise_ptr, {fn_ptr_ty, i64});
    auto thunk_ptr_ty = PointerType::get(llvm::FunctionType::get(i64, {}, false), 0);
    rt_.async_call_thunk_ = decl("yona_rt_async_call_thunk", promise_ptr, {thunk_ptr_ty});
    rt_.async_await_       = decl("yona_rt_async_await", i64, {promise_ptr});
    rt_.async_await_keep_ = decl("yona_rt_async_await_keep", i64, {promise_ptr});

    // Task groups (structured concurrency)
    auto group_ptr = ptr; // opaque pointer to yona_task_group_t
    rt_.group_begin_   = decl("yona_rt_group_begin", group_ptr, {});
    rt_.group_register_ = decl("yona_rt_group_register", vd, {group_ptr, promise_ptr});
    rt_.group_register_io_ = decl("yona_rt_group_register_io", vd, {group_ptr, i64});
    rt_.group_await_all_ = decl("yona_rt_group_await_all", i64, {group_ptr});
    rt_.group_end_     = decl("yona_rt_group_end", vd, {group_ptr});
    rt_.group_cancel_  = decl("yona_rt_group_cancel", vd, {group_ptr});
    rt_.group_is_cancelled_ = decl("yona_rt_group_is_cancelled", i64, {group_ptr});
    rt_.async_call_grouped_ = decl("yona_rt_async_call_grouped", promise_ptr, {fn_ptr_ty, i64, group_ptr});
    rt_.async_call_thunk_grouped_ = decl("yona_rt_async_call_thunk_grouped", promise_ptr, {thunk_ptr_ty, group_ptr});
    rt_.group_attach_arena_ = decl("yona_rt_group_attach_arena", vd, {group_ptr, ptr});
    rt_.group_arena_bind_push_ = decl("yona_rt_group_arena_bind_push", vd, {group_ptr});
    rt_.group_arena_bind_pop_ = decl("yona_rt_group_arena_bind_pop", vd, {});

    // ADT runtime (recursive types)
    auto i8 = LType::getInt8Ty(*context_);
    rt_.adt_alloc_     = decl("yona_rt_adt_alloc", ptr, {i64, i64});
    rt_.adt_get_tag_   = decl("yona_rt_adt_get_tag", i64, {ptr});
    rt_.adt_get_field_ = decl("yona_rt_adt_get_field", i64, {ptr, i64});
    rt_.adt_set_field_ = decl("yona_rt_adt_set_field", vd, {ptr, i64, i64});
    rt_.adt_set_heap_mask_ = decl("yona_rt_adt_set_heap_mask", vd, {ptr, i64});

    // General closures: {fn_ptr, ret_tag, arity, cap0, ...} with env-passing
    rt_.closure_create_  = decl("yona_rt_closure_create", ptr, {ptr, i64, i64, i64});
    rt_.closure_set_cap_ = decl("yona_rt_closure_set_cap", vd, {ptr, i64, i64});
    rt_.closure_get_cap_ = decl("yona_rt_closure_get_cap", i64, {ptr, i64});
    rt_.closure_set_heap_mask_ = decl("yona_rt_closure_set_heap_mask", vd, {ptr, i64});

    // Tuple allocation with metadata
    rt_.tuple_alloc_ = decl("yona_rt_tuple_alloc", ptr, {i64});
    rt_.tuple_set_ = decl("yona_rt_tuple_set", vd, {ptr, i64, i64});
    rt_.tuple_set_heap_mask_ = decl("yona_rt_tuple_set_heap_mask", vd, {ptr, i64});

    // Reference counting
    rt_.rc_inc_ = decl("yona_rt_rc_inc", vd, {ptr});
    rt_.rc_dec_ = decl("yona_rt_rc_dec", vd, {ptr});

    // Arena allocator
    rt_.arena_create_  = decl("yona_rt_arena_create", ptr, {i64});
    rt_.arena_alloc_   = decl("yona_rt_arena_alloc", ptr, {ptr, i64, i64});
    rt_.arena_destroy_ = decl("yona_rt_arena_destroy", vd, {ptr});

    // io_uring await
    rt_.io_await_ = decl("yona_rt_io_await", i64, {i64});

    // Resource cleanup (with expression)
    // Bytes
    rt_.byte_array_alloc_       = decl("yona_rt_byte_array_alloc", ptr, {i64});
    rt_.byte_array_length_      = decl("yona_rt_byte_array_length", i64, {ptr});
    rt_.byte_array_get_         = decl("yona_rt_byte_array_get", i64, {ptr, i64});
    rt_.byte_array_set_         = decl("yona_rt_byte_array_set", vd, {ptr, i64, i64});
    rt_.byte_array_concat_      = decl("yona_rt_byte_array_concat", ptr, {ptr, ptr});
    rt_.byte_array_slice_       = decl("yona_rt_byte_array_slice", ptr, {ptr, i64, i64});
    rt_.byte_array_from_string_ = decl("yona_rt_byte_array_from_string", ptr, {ptr});
    rt_.byte_array_to_string_   = decl("yona_rt_byte_array_to_string", ptr, {ptr});
    rt_.byte_array_from_seq_    = decl("yona_rt_byte_array_from_seq", ptr, {ptr});
    rt_.byte_array_to_seq_      = decl("yona_rt_byte_array_to_seq", ptr, {ptr});
    rt_.print_byte_array_       = decl("yona_rt_print_byte_array", vd, {ptr});

    // IntArray
    rt_.int_array_alloc_   = decl("yona_rt_int_array_alloc", ptr, {i64});
    rt_.int_array_length_  = decl("yona_rt_int_array_length", i64, {ptr});
    rt_.int_array_get_     = decl("yona_rt_int_array_get", i64, {ptr, i64});
    rt_.int_array_set_     = decl("yona_rt_int_array_set", vd, {ptr, i64, i64});
    rt_.int_array_head_    = decl("yona_rt_int_array_head", i64, {ptr});
    rt_.int_array_tail_    = decl("yona_rt_int_array_tail", ptr, {ptr});
    rt_.int_array_cons_    = decl("yona_rt_int_array_cons", ptr, {i64, ptr});
    rt_.int_array_join_    = decl("yona_rt_int_array_join", ptr, {ptr, ptr});
    rt_.print_int_array_   = decl("yona_rt_print_int_array", vd, {ptr});

    // Channels
    rt_.channel_new_      = decl("yona_rt_channel_new", ptr, {i64});
    rt_.channel_send_     = decl("yona_rt_channel_send", vd, {ptr, i64});
    rt_.channel_recv_     = decl("yona_rt_channel_recv", i64, {ptr});
    rt_.channel_try_recv_ = decl("yona_rt_channel_try_recv", i64, {ptr});
    rt_.channel_close_    = decl("yona_rt_channel_close", vd, {ptr});
    rt_.channel_is_closed_ = decl("yona_rt_channel_is_closed", i64, {ptr});
    rt_.channel_length_   = decl("yona_rt_channel_length", i64, {ptr});
    rt_.channel_capacity_ = decl("yona_rt_channel_capacity", i64, {ptr});

    // FloatArray
    auto dbl = LType::getDoubleTy(*context_);
    rt_.float_array_alloc_  = decl("yona_rt_float_array_alloc", ptr, {i64});
    rt_.float_array_length_ = decl("yona_rt_float_array_length", i64, {ptr});
    rt_.float_array_get_    = decl("yona_rt_float_array_get", dbl, {ptr, i64});
    rt_.float_array_set_    = decl("yona_rt_float_array_set", vd, {ptr, i64, dbl});
    rt_.float_array_head_   = decl("yona_rt_float_array_head", dbl, {ptr});
    rt_.float_array_tail_   = decl("yona_rt_float_array_tail", ptr, {ptr});
    rt_.float_array_cons_   = decl("yona_rt_float_array_cons", ptr, {dbl, ptr});
    rt_.float_array_join_   = decl("yona_rt_float_array_join", ptr, {ptr, ptr});
    rt_.print_float_array_  = decl("yona_rt_print_float_array", vd, {ptr});

    rt_.box_ = decl("yona_rt_box", ptr, {ptr, i64});
    rt_.close_ = decl("yona_rt_close", vd, {i64});

    // Exception handling (SJLJ via llvm.eh.sjlj.setjmp + __builtin_longjmp).
    // Codegen emits the SJLJ intrinsic in the user's stack frame; runtime
    // raise() does __builtin_longjmp into that buffer. See exceptions.c.
    auto i32 = LType::getInt32Ty(*context_);
    rt_.try_begin_     = decl("yona_rt_try_push", ptr, {});  // returns void*[5]
    rt_.try_end_       = decl("yona_rt_try_end", vd, {});
    rt_.raise_         = decl("yona_rt_raise", vd, {i64, ptr});
    rt_.get_exc_sym_   = decl("yona_rt_get_exception_symbol", i64, {});
    rt_.get_exc_msg_   = decl("yona_rt_get_exception_message", ptr, {});
    rt_.raise_->addFnAttr(llvm::Attribute::NoReturn);

    // Perceus phase 3: frame-scoped heap cleanup on raise. See
    // src/runtime/exceptions.c for the runtime layout.
    rt_.frame_push_     = decl("yona_rt_frame_push", vd, {ptr});
    rt_.frame_pop_      = decl("yona_rt_frame_pop", vd, {ptr});
    rt_.frame_transfer_ = decl("yona_rt_frame_transfer", vd, {ptr});
    rt_.try_depth_      = decl("yona_rt_try_depth", i32, {});
}

void Codegen::report_error(const SourceLocation& loc, const std::string& message) {
    error_count_++;
    diag_->error(loc, message);
}

std::string Codegen::suggest_similar(const std::string& name) const {
    // Levenshtein distance for "did you mean?" suggestions
    auto edit_distance = [](const std::string& a, const std::string& b) -> int {
        int m = a.size(), n = b.size();
        std::vector<int> dp(n + 1);
        for (int j = 0; j <= n; j++) dp[j] = j;
        for (int i = 1; i <= m; i++) {
            int prev = dp[0];
            dp[0] = i;
            for (int j = 1; j <= n; j++) {
                int tmp = dp[j];
                dp[j] = std::min({dp[j] + 1, dp[j-1] + 1, prev + (a[i-1] != b[j-1] ? 1 : 0)});
                prev = tmp;
            }
        }
        return dp[n];
    };

    std::string best;
    int best_dist = 4; // max distance threshold
    for (auto& [k, _] : named_values_) {
        int d = edit_distance(name, k);
        if (d < best_dist) { best = k; best_dist = d; }
    }
    for (auto& [k, _] : deferred_functions_) {
        int d = edit_distance(name, k);
        if (d < best_dist) { best = k; best_dist = d; }
    }
    for (auto& [k, _] : imports_.extern_functions) {
        int d = edit_distance(name, k);
        if (d < best_dist) { best = k; best_dist = d; }
    }
    for (auto& [k, _] : types_.adt_constructors) {
        int d = edit_distance(name, k);
        if (d < best_dist) { best = k; best_dist = d; }
    }
    return best;
}

// ===== Public API =====

std::string Codegen::mangle_name(const std::string& module_fqn, const std::string& func_name) {
    // Replace backslash with underscore: Test\Test → Test_Test
    std::string mangled = "yona_";
    for (char c : module_fqn) {
        mangled += (c == '\\' || c == '/') ? '_' : c;
    }
    mangled += "__";
    mangled += func_name;
    return mangled;
}

Module* Codegen::compile(AstNode* node) {
    auto fn = codegen_main(node);
    if (!fn) return nullptr;
    finalize_debug_info();
    std::string err;
    raw_string_ostream os(err);
    if (verifyModule(*module_, &os)) {
        std::cerr << "Module verification failed:\n" << err << "\n";
        return nullptr;
    }
    optimize();
    return module_.get();
}

// Forward declarations for type annotation support
static CType yona_type_to_ctype(const types::Type& t);
static std::pair<std::vector<CType>, CType> uncurry_type_signature(const types::Type& t);

Module* Codegen::compile_module(ModuleDecl* mod) {
    imports_.interface_symbols.clear();

    // Build the module FQN string
    std::string fqn;
    if (mod->fqn->packageName.has_value()) {
        auto* pkg = mod->fqn->packageName.value();
        for (size_t i = 0; i < pkg->parts.size(); i++) {
            if (i > 0) fqn += "\\";
            fqn += pkg->parts[i]->value;
        }
        fqn += "\\";
    }
    fqn += mod->fqn->moduleName->value;

    // Build export set for visibility control
    std::unordered_set<std::string> export_set(mod->exports.begin(), mod->exports.end());

    // Build exported types set — constructors will be added after ADT processing
    std::unordered_set<std::string> exported_type_set(mod->exported_types.begin(), mod->exported_types.end());

    // First pass: collect ADTs in this module that contain a function-typed
    // field. Any ADT that references one of these in a field must also be
    // heap-allocated, because the closure ABI for function fields returns
    // a pointer (i64), and a flat-struct ADT containing such a function
    // field would not survive a closure-returning-it round trip. We then
    // iterate to a fixed point so mutual references like
    // `Stream a = Stream (() -> Step a)` / `Step a = Yield a (Stream a) | Done`
    // both end up heap-allocated.
    std::unordered_set<std::string> heap_adts;
    for (auto* adt : mod->adt_declarations) {
        for (auto* ctor : adt->variants) {
            for (auto& ft : ctor->field_type_names) {
                if (ft.is_function_type) { heap_adts.insert(adt->name); break; }
            }
            if (heap_adts.count(adt->name)) break;
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* adt : mod->adt_declarations) {
            if (heap_adts.count(adt->name)) continue;
            for (auto* ctor : adt->variants) {
                for (auto& ft : ctor->field_type_names) {
                    std::string head = ft.name;
                    auto sp = head.find(' ');
                    if (sp != std::string::npos) head = head.substr(0, sp);
                    if (heap_adts.count(head)) {
                        heap_adts.insert(adt->name);
                        changed = true;
                        break;
                    }
                }
                if (heap_adts.count(adt->name)) break;
            }
        }
    }

    // Process ADT declarations: register constructors, detect recursion
    for (auto* adt : mod->adt_declarations) {
        int max_arity = 0;
        bool is_recursive = heap_adts.count(adt->name) > 0;
        for (auto* ctor : adt->variants) {
            int a = static_cast<int>(ctor->field_type_names.size());
            if (a > max_arity) max_arity = a;
            // Check if any field type references the ADT itself or contains
            // a function type — function fields make ADTs heap-allocated because
            // closures may return values of the same type (lazy data structures)
            for (auto& ft : ctor->field_type_names) {
                if (ft.is_function_type || ft.name == adt->name ||
                    ft.name == "Fn" || ft.name == "fn" || ft.name == "Function") {
                    is_recursive = true; break;
                }
            }
        }

        for (size_t ci = 0; ci < adt->variants.size(); ci++) {
            auto* ctor = adt->variants[ci];
            int arity = static_cast<int>(ctor->field_type_names.size());
            // Map field types to CTypes
            std::vector<CType> ftypes;
            std::vector<CType> fn_rets;
            std::vector<std::string> fn_ret_adt_names;
            auto field_name_to_ctype = [](std::string name) -> CType {
                auto sp = name.find(' ');
                if (sp != std::string::npos) name = name.substr(0, sp);
                if (name == "Int" || name == "a" || name == "b" || name == "e" || name == "s") return CType::INT;
                if (name == "Float") return CType::FLOAT;
                if (name == "String") return CType::STRING;
                if (name == "Bool") return CType::BOOL;
                if (name == "Symbol") return CType::SYMBOL;
                if (name == "Seq") return CType::SEQ;
                if (name == "Set") return CType::SET;
                if (name == "Dict") return CType::DICT;
                if (name == "Channel") return CType::CHANNEL;
                return CType::ADT;
            };
            for (auto& ft : ctor->field_type_names) {
                if (ft.is_function_type || ft.name == "()" || ft.name == "Fn" ||
                    ft.name == "fn" || ft.name == "Function") {
                    ftypes.push_back(CType::FUNCTION);
                    if (!ft.return_types.empty()) {
                        std::string ret_name = ft.return_types[0].name;
                        auto ret_ct = field_name_to_ctype(ret_name);
                        auto sp = ret_name.find(' ');
                        if (sp != std::string::npos) ret_name = ret_name.substr(0, sp);
                        fn_rets.push_back(ret_ct);
                        fn_ret_adt_names.push_back(ret_ct == CType::ADT ? ret_name : "");
                    } else {
                        fn_rets.push_back(CType::INT);
                        fn_ret_adt_names.push_back("");
                    }
                } else {
                    auto ct = ft.name == adt->name ? CType::ADT : field_name_to_ctype(ft.name);
                    ftypes.push_back(ct);
                    fn_rets.push_back(CType::INT);
                    fn_ret_adt_names.push_back("");
                }
            }

            types_.adt_constructors[ctor->name] = {adt->name, static_cast<int>(ci), arity,
                                              static_cast<int>(adt->variants.size()), max_arity, is_recursive,
                                              ctor->field_names, ftypes, fn_rets, fn_ret_adt_names};
        }
    }

    // Expand exported types: add all constructors of exported types to export_set
    for (auto* adt : mod->adt_declarations) {
        if (exported_type_set.count(adt->name) > 0) {
            for (auto* ctor : adt->variants) {
                export_set.insert(ctor->name);
            }
        }
    }

    // Load re-export source modules so their functions are available
    // for use within this module's own functions
    for (auto& re : mod->re_exports) {
        std::string path_str;
        for (char c : re.source_module) path_str += (c == '\\') ? '/' : c;
        load_module_interface(std::filesystem::path(path_str));
        for (auto& name : re.names)
            register_import(re.source_module, name, name);
    }

    // Ensure Prelude trait instance methods (Show_Int__show, etc.) are declared
    // as extern functions before derive expansion can call them.
    register_trait_externs();

    // ===== Auto-derive expansion =====
    // For each ADT with a `deriving` clause, look up the registered strategy
    // and generate + compile trait instance methods. Fully registry-driven —
    // no hardcoded trait names here.
    // Keep reparsed modules alive so deferred function AST pointers don't dangle.
    std::vector<std::unique_ptr<ast::ModuleDecl>> derived_modules;
    for (auto* adt : mod->adt_declarations) {
        if (adt->derive_traits.empty()) continue;

        // Collect constructor metadata
        DeriveAdtInfo dai;
        dai.type_name = adt->name;
        dai.type_params = adt->type_params;
        for (size_t ci = 0; ci < adt->variants.size(); ci++) {
            auto* ctor = adt->variants[ci];
            DeriveCtorInfo dci;
            dci.name = ctor->name;
            dci.tag = static_cast<int>(ci);
            dci.arity = static_cast<int>(ctor->field_type_names.size());
            dci.field_names = ctor->field_names;
            for (auto& ft : ctor->field_type_names) {
                bool is_param = false;
                for (auto& tp : adt->type_params)
                    if (ft.name == tp) { is_param = true; break; }
                dci.field_type_refs.push_back(is_param ? ft.name : "");
                dci.field_type_names.push_back(ft.name);
            }
            dai.constructors.push_back(dci);
        }
        auto adt_it = types_.adt_constructors.find(adt->variants[0]->name);
        if (adt_it != types_.adt_constructors.end()) dai.is_recursive = adt_it->second.is_recursive;

        for (auto& trait_name : adt->derive_traits) {
            auto* strategy = DeriveEngine::get_strategy(trait_name);
            if (!strategy) {
                if (diag_) {
                    auto available = DeriveEngine::all_derivable_traits();
                    std::string avail_str;
                    for (size_t i = 0; i < available.size(); i++) {
                        if (i > 0) avail_str += ", ";
                        avail_str += available[i];
                    }
                    diag_->error(adt->source_context, compiler::ErrorCode::E0400,
                        "unknown derivable trait '" + trait_name + "'; available: " + avail_str);
                }
                continue;
            }

            // Generate and compile each method from the strategy
            for (auto& method_name : strategy->method_names) {
                std::string method_source = strategy->generator(dai);
                if (method_source.empty()) continue;

                std::string mangled = trait_name + "_" + adt->name + "__" + method_name;
                auto mod_result = reparse_genfn(method_name, method_source);
                if (!mod_result) continue;

                // Register the trait instance
                std::string key = trait_name + ":" + adt->name;
                auto& tii = types_.trait_instances[key];
                tii.trait_name = trait_name;
                tii.type_name = adt->name;
                tii.type_names = {adt->name};
                tii.method_mangled_names[method_name] = mangled;

                // Compile the reparsed function
                for (auto* fn_decl : mod_result->functions)
                    codegen_function_def(fn_decl, mangled);

                // Store GENFN source for cross-module monomorphization
                imports_.imported_sources[mangled] = {method_source, method_name};

                // Keep module alive (deferred functions hold AST pointers)
                derived_modules.push_back(std::move(mod_result));
            }
        }
    }

    // Register trait declarations (Phase 3: with superclasses and default impls)
    for (auto* trait : mod->trait_declarations) {
        TraitInfo ti;
        ti.name = trait->name;
        ti.type_param = trait->type_param;
        ti.type_params = trait->type_params;
        ti.superclasses = std::vector<std::pair<std::string, std::string>>(
            trait->superclasses.begin(), trait->superclasses.end());
        for (auto& m : trait->methods) {
            ti.method_names.push_back(m.name);
            if (m.default_impl) {
                ti.default_impls[m.name] = m.default_impl;
            }
        }
        types_.traits[trait->name] = ti;
    }

    // Register trait instances: mangle method names and register as deferred functions
    // Phase 2: handle constrained instances with ADT type names
    // Phase 3: fill in default methods from trait declaration
    for (auto* inst : mod->instance_declarations) {
        // Multi-param key: "Trait:Type1:Type2"
        std::string key = inst->trait_name;
        for (auto& tn : inst->type_names) key += ":" + tn;
        if (inst->type_names.empty()) key += ":" + inst->type_name;
        TraitInstanceInfo tii;
        tii.trait_name = inst->trait_name;
        tii.type_name = inst->type_name;
        tii.type_names = inst->type_names;
        tii.constraints = std::vector<std::pair<std::string, std::string>>(
            inst->constraints.begin(), inst->constraints.end());

        // Phase 3: Verify superclass instances exist
        auto trait_it = types_.traits.find(inst->trait_name);
        if (trait_it != types_.traits.end()) {
            for (auto& [sc_trait, sc_var] : trait_it->second.superclasses) {
                std::string sc_key = sc_trait + ":" + inst->type_name;
                if (types_.trait_instances.find(sc_key) == types_.trait_instances.end()) {
                    std::cerr << "Warning: instance " << inst->trait_name << " " << inst->type_name
                              << " requires " << sc_trait << " " << inst->type_name
                              << " (superclass), but no such instance found yet\n";
                }
            }
        }

        // Collect provided method names
        std::unordered_set<std::string> provided_methods;
        for (auto* method : inst->methods) {
            // Mangle: TraitName_TypeName__methodName
            std::string mangled = inst->trait_name + "_" + inst->type_name + "__" + method->name;
            tii.method_mangled_names[method->name] = mangled;
            provided_methods.insert(method->name);

            // Register as deferred function
            codegen_function_def(method, mangled);
        }

        // Phase 3: Fill in default implementations for missing methods
        if (trait_it != types_.traits.end()) {
            for (auto& [method_name, default_fn] : trait_it->second.default_impls) {
                if (provided_methods.find(method_name) == provided_methods.end()) {
                    // Method not provided by instance — use default from trait
                    std::string mangled = inst->trait_name + "_" + inst->type_name + "__" + method_name;
                    tii.method_mangled_names[method_name] = mangled;
                    codegen_function_def(default_fn, mangled);
                }
            }
        }

        types_.trait_instances[key] = tii;
    }

    // Process module-level extern declarations
    for (auto* ext : mod->extern_declarations) {
        codegen_extern_decl(ext);
        // Add to export set if exported
        if (export_set.count(ext->name)) {
            std::string mangled = mangle_name(fqn, ext->name);
            // The extern is already declared; create a wrapper with the mangled name
            auto cf_it = compiled_functions_.find(ext->name);
            if (cf_it != compiled_functions_.end()) {
                auto& cf = cf_it->second;
                imports_.meta[mangled] = module_meta_from_compiled(cf);
                imports_.interface_symbols.insert(mangled);
                // Create a forwarding wrapper
                if (cf.fn->getName() != mangled) {
                    auto* wrapper = Function::Create(
                        cf.fn->getFunctionType(),
                        Function::ExternalLinkage,
                        mangled, module_.get());
                    auto* bb = BasicBlock::Create(*context_, "entry", wrapper);
                    builder_->SetInsertPoint(bb);
                    std::vector<Value*> args;
                    for (auto& arg : wrapper->args()) args.push_back(&arg);
                    auto* result = builder_->CreateCall(cf.fn, args);
                    builder_->CreateRet(result);
                }
            }
        }
    }

    // First pass: register all functions as deferred
    for (auto* func : mod->functions) {
        std::string fn_name = func->name;
        codegen_function_def(func, fn_name);

        // Store source text for exported generic functions (.yonai emission)
        if (export_set.count(fn_name) && !func->type_signature.has_value() &&
            !func->source_text.empty()) {
            std::string mangled = mangle_name(fqn, fn_name);
            imports_.function_source[mangled] = func->source_text;
            imports_.interface_symbols.insert(mangled);
        }
    }

    // Second pass: compile only EXPORTED functions with inferred types.
    // Non-exported functions stay deferred and compile on demand at call sites
    // with correct argument types (monomorphization).
    for (auto* func : mod->functions) {
        std::string fn_name = func->name;
        bool is_exported = export_set.count(fn_name) > 0;
        if (!is_exported) continue; // internal functions compile at call sites

        auto def_it = deferred_functions_.find(fn_name);
        if (def_it == deferred_functions_.end()) continue;

        // Use explicit type annotation if present, otherwise infer from patterns/body
        std::vector<CType> annotated_param_types;
        std::vector<std::vector<CType>> annotated_param_subtypes;
        std::vector<std::string> annotated_param_adt_names;
        if (func->type_signature.has_value()) {
            auto [params, ret] = uncurry_type_signature(*func->type_signature);
            annotated_param_types = params;
            const types::Type* current_type = &*func->type_signature;
            while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(*current_type)) {
                auto ft = std::get<std::shared_ptr<types::FunctionType>>(*current_type);
                if (std::holds_alternative<std::shared_ptr<types::NamedType>>(ft->argumentType))
                    annotated_param_adt_names.push_back(
                        std::get<std::shared_ptr<types::NamedType>>(ft->argumentType)->name);
                else
                    annotated_param_adt_names.push_back("");
                std::vector<CType> subtypes;
                if (std::holds_alternative<std::shared_ptr<types::FunctionType>>(ft->argumentType)) {
                    auto arg_ft = std::get<std::shared_ptr<types::FunctionType>>(ft->argumentType);
                    subtypes.push_back(yona_type_to_ctype(arg_ft->returnType));
                }
                annotated_param_subtypes.push_back(std::move(subtypes));
                current_type = &ft->returnType;
            }
        }

        auto inferred = func->type_signature.has_value()
            ? std::vector<InferredParamType>() // not needed when annotated
            : infer_param_types(func);

        std::vector<TypedValue> typed_args;
        for (size_t i = 0; i < func->patterns.size(); i++) {
            CType ct;
            if (!annotated_param_types.empty() && i < annotated_param_types.size())
                ct = annotated_param_types[i];
            else
                ct = (i < inferred.size()) ? inferred[i].type : CType::INT;
            auto* dummy_val = ConstantInt::get(LType::getInt64Ty(*context_), 0);

            if (ct == CType::TUPLE) {
                // Tuples are i64 (ptrtoint'd heap pointers). Extract subtypes from pattern.
                PatternNode* src = (i < inferred.size()) ? inferred[i].source_pattern : nullptr;
                auto* tp = src ? dynamic_cast<TuplePattern*>(src) : nullptr;
                if (tp) {
                    std::vector<CType> elem_ctypes;
                    for (size_t j = 0; j < tp->patterns.size(); j++)
                        elem_ctypes.push_back(infer_type_from_pattern(static_cast<PatternNode*>(tp->patterns[j])));
                    typed_args.push_back({dummy_val, CType::TUPLE, elem_ctypes});
                } else {
                    typed_args.push_back({dummy_val, ct});
                }
            } else if (ct == CType::SEQ) {
                auto* ptr_type = PointerType::get(*context_, 0);
                std::vector<CType> elem_ctypes =
                    (i < inferred.size()) ? inferred[i].subtypes : std::vector<CType>{};
                typed_args.push_back({ConstantPointerNull::get(ptr_type), CType::SEQ, elem_ctypes});
            } else if (ct == CType::STRING) {
                auto* ptr_type = PointerType::get(*context_, 0);
                typed_args.push_back({ConstantPointerNull::get(ptr_type), CType::STRING});
            } else if (ct == CType::FLOAT) {
                typed_args.push_back({ConstantFP::get(LType::getDoubleTy(*context_), 0.0), CType::FLOAT});
            } else if (ct == CType::BOOL) {
                typed_args.push_back({ConstantInt::get(LType::getInt1Ty(*context_), 0), CType::BOOL});
            } else if (ct == CType::SYMBOL) {
                typed_args.push_back({ConstantInt::get(LType::getInt64Ty(*context_), 0), CType::SYMBOL});
            } else if (ct == CType::FUNCTION) {
                auto* ptr_type = PointerType::get(*context_, 0);
                std::vector<CType> subtypes =
                    (!annotated_param_subtypes.empty() && i < annotated_param_subtypes.size())
                        ? annotated_param_subtypes[i]
                        : std::vector<CType>{};
                typed_args.push_back({ConstantPointerNull::get(ptr_type), CType::FUNCTION, subtypes});
            } else if (ct == CType::ADT) {
                if (!annotated_param_adt_names.empty() && i < annotated_param_adt_names.size() &&
                    !annotated_param_adt_names[i].empty()) {
                    typed_args.push_back({dummy_val, CType::ADT});
                    typed_args.back().adt_type_name = annotated_param_adt_names[i];
                    continue;
                }
                // Build the ADT struct type based on the constructor pattern
                // Find which ADT type by looking at the case patterns
                PatternNode* src = (i < inferred.size()) ? inferred[i].source_pattern : nullptr;
                // Find constructor name from either ConstructorPattern or RecordPattern
                std::string ctor_name_lookup;
                if (auto* cp = src ? dynamic_cast<ConstructorPattern*>(src) : nullptr)
                    ctor_name_lookup = cp->constructor_name;
                else if (auto* rp = src ? dynamic_cast<RecordPattern*>(src) : nullptr)
                    ctor_name_lookup = rp->recordType;
                if (!ctor_name_lookup.empty()) {
                    auto ctor_it = types_.adt_constructors.find(ctor_name_lookup);
                    if (ctor_it != types_.adt_constructors.end()) {
                        TypedValue tv;
                        tv.type = CType::ADT;
                        tv.adt_type_name = ctor_it->second.type_name;
                        if (ctor_it->second.is_recursive) {
                            auto* ptr_type = PointerType::get(*context_, 0);
                            tv.val = ConstantPointerNull::get(ptr_type);
                        } else {
                            auto tag_ty = LType::getInt64Ty(*context_);
                            auto i64_ty = LType::getInt64Ty(*context_);
                            std::vector<LType*> fields = {tag_ty};
                            for (int f = 0; f < ctor_it->second.max_arity; f++)
                                fields.push_back(i64_ty);
                            auto* st = StructType::get(*context_, fields);
                            tv.val = UndefValue::get(st);
                        }
                        typed_args.push_back(tv);
                    } else {
                        typed_args.push_back({dummy_val, ct});
                    }
                } else {
                    // No constructor pattern — inferred from field access or other usage.
                    // Field-access inference represents parameters with the heap ABI
                    // (i64 pointer value) because exported/imported ADTs cross module
                    // boundaries boxed even when local constructors use flat structs.
                    bool found = false;
                    for (auto& [cname, cinfo] : types_.adt_constructors) {
                        bool matches_fields = true;
                        const auto& accessed_fields =
                            (i < inferred.size()) ? inferred[i].accessed_fields
                                                  : std::vector<std::string>{};
                        for (const auto& field : accessed_fields) {
                            if (std::find(cinfo.field_names.begin(), cinfo.field_names.end(), field) ==
                                cinfo.field_names.end()) {
                                matches_fields = false;
                                break;
                            }
                        }
                        if (!cinfo.field_names.empty() && matches_fields) {
                            TypedValue tv;
                            tv.type = CType::ADT;
                            tv.adt_type_name = cinfo.type_name;
                            tv.val = ConstantInt::get(LType::getInt64Ty(*context_), 0);
                            typed_args.push_back(tv);
                            found = true;
                            break;
                        }
                    }
                    if (!found) typed_args.push_back({dummy_val, ct});
                }
            } else {
                typed_args.push_back({dummy_val, ct});
            }
        }

        auto cf = compile_function(fn_name, def_it->second, typed_args);

        if (is_exported) {
            // Store type metadata for importers
            std::string mangled = mangle_name(fqn, fn_name);
            imports_.meta[mangled] = module_meta_from_compiled(cf);
            imports_.interface_symbols.insert(mangled);

            // Check if the function already has the right linkage
            if (cf.fn->getName() != mangled) {
                // Create a wrapper function with external linkage and mangled name
                auto* wrapper = Function::Create(
                    cf.fn->getFunctionType(),
                    Function::ExternalLinkage,
                    mangled, module_.get());

                auto* bb = BasicBlock::Create(*context_, "entry", wrapper);
                builder_->SetInsertPoint(bb);

                std::vector<Value*> args;
                for (auto& arg : wrapper->args()) args.push_back(&arg);
                auto* result = builder_->CreateCall(cf.fn, args);
                builder_->CreateRet(result);
            } else {
                cf.fn->setLinkage(Function::ExternalLinkage);
            }
        }
    }

    // Non-blocking: sibling-aware effect rows for .yonai FN lines.
    {
        DiagnosticEngine fx_diag;
        typechecker::TypeChecker fx_tc(fx_diag);
        fx_tc.check_module(mod);
        for (auto* func : mod->functions) {
            if (!func || !export_set.count(func->name)) continue;
            auto* ty = fx_tc.type_of(func);
            if (!ty) continue;
            auto row = fx_tc.effect_row_info(fx_tc.zonk(ty));
            if (row.ops.empty() && !row.open_rest) continue;
            std::string mangled = mangle_name(fqn, func->name);
            auto meta_it = imports_.meta.find(mangled);
            if (meta_it == imports_.meta.end()) continue;
            meta_it->second.effect_ops = std::move(row.ops);
            meta_it->second.effect_open_rest = row.open_rest;
            meta_it->second.effect_hof = row.hof;
        }
    }

    // Third pass: compile trait instance methods with ExternalLinkage
    // so importing modules can call them via resolved trait dispatch.
    for (auto& [key, inst] : types_.trait_instances) {
        for (auto& [method_name, mangled] : inst.method_mangled_names) {
            auto cf_it = compiled_functions_.find(mangled);
            if (cf_it != compiled_functions_.end()) {
                cf_it->second.fn->setLinkage(Function::ExternalLinkage);
                // Emit FN metadata for the .yonai file
                if (imports_.meta.find(mangled) == imports_.meta.end()) {
                    imports_.meta[mangled] = module_meta_from_compiled(cf_it->second);
                }
                imports_.interface_symbols.insert(mangled);
                continue;
            }
            auto def_it = deferred_functions_.find(mangled);
            if (def_it != deferred_functions_.end()) {
                // Compile with inferred types, using correct LLVM types for ADTs
                auto inferred = infer_param_types(def_it->second.ast);
                std::vector<TypedValue> typed_args;
                for (size_t i = 0; i < def_it->second.param_names.size(); i++) {
                    CType ct = (i < inferred.size()) ? inferred[i].type : CType::INT;
                    TypedValue tv;
                    tv.type = ct;
                    if (ct == CType::ADT) {
                        // Find the ADT type from the instance or pattern
                        PatternNode* src = (i < inferred.size()) ? inferred[i].source_pattern : nullptr;
                        std::string ctor_name;
                        if (auto* cp = src ? dynamic_cast<ConstructorPattern*>(src) : nullptr)
                            ctor_name = cp->constructor_name;
                        if (!ctor_name.empty()) {
                            auto ctor_it = types_.adt_constructors.find(ctor_name);
                            if (ctor_it != types_.adt_constructors.end()) {
                                tv.adt_type_name = ctor_it->second.type_name;
                                if (ctor_it->second.is_recursive) {
                                    tv.val = ConstantPointerNull::get(PointerType::get(*context_, 0));
                                } else {
                                    std::vector<LType*> fields = {LType::getInt64Ty(*context_)};
                                    for (int f = 0; f < ctor_it->second.max_arity; f++)
                                        fields.push_back(LType::getInt64Ty(*context_));
                                    auto* st = StructType::get(*context_, fields);
                                    tv.val = UndefValue::get(st);
                                }
                            } else {
                                tv.val = ConstantInt::get(LType::getInt64Ty(*context_), 0);
                            }
                        } else {
                            // Try the instance type name
                            tv.adt_type_name = inst.type_name;
                            // Find any constructor for this type
                            for (auto& [cn, ci] : types_.adt_constructors) {
                                if (ci.type_name == inst.type_name) {
                                    if (ci.is_recursive) {
                                        tv.val = ConstantPointerNull::get(PointerType::get(*context_, 0));
                                    } else {
                                        std::vector<LType*> fields = {LType::getInt64Ty(*context_)};
                                        for (int f = 0; f < ci.max_arity; f++)
                                            fields.push_back(LType::getInt64Ty(*context_));
                                        auto* st = StructType::get(*context_, fields);
                                        tv.val = UndefValue::get(st);
                                    }
                                    break;
                                }
                            }
                            if (!tv.val) tv.val = ConstantInt::get(LType::getInt64Ty(*context_), 0);
                        }
                    } else {
                        tv.val = ConstantInt::get(LType::getInt64Ty(*context_), 0);
                    }
                    typed_args.push_back(tv);
                }
                auto cf = compile_function(mangled, def_it->second, typed_args);
                if (cf.fn) {
                    cf.fn->setLinkage(Function::ExternalLinkage);
                    imports_.meta[mangled] = module_meta_from_compiled(cf);
                    imports_.interface_symbols.insert(mangled);
                }
            }
        }
    }

    // Process re-exports: load source module interfaces and create forwarding wrappers
    for (auto& re : mod->re_exports) {
        // Build filesystem path from FQN
        std::filesystem::path mod_path;
        std::string src_fqn = re.source_module;
        for (char& c : mod_path.string()) { /* unused, build below */ }
        // Convert backslash FQN to filesystem path
        std::string path_str;
        for (char c : src_fqn) path_str += (c == '\\') ? '/' : c;
        mod_path = std::filesystem::path(path_str);

        // Load the source module's interface
        load_module_interface(mod_path);

        for (auto& name : re.names) {
            // Check if it's an ADT constructor
            auto ctor_it = types_.adt_constructors.find(name);
            if (ctor_it != types_.adt_constructors.end()) {
                // ADT constructors are re-exported via the interface file (no wrapper needed)
                continue;
            }

            // Function re-export: create a forwarding wrapper
            std::string src_mangled = mangle_name(src_fqn, name);
            std::string dst_mangled = mangle_name(fqn, name);

            // Look up source function metadata
            auto meta_it = imports_.meta.find(src_mangled);
            if (meta_it == imports_.meta.end()) {
                report_error(mod->source_context,
                    "re-export: function '" + name + "' not found in module '" + src_fqn + "'");
                continue;
            }

            auto& meta = meta_it->second;

            // Build function type from metadata
            std::vector<LType*> param_types;
            for (auto ct : meta.param_types) param_types.push_back(llvm_type(ct));
            auto* ret_llvm = llvm_type(meta.return_type);
            auto* fn_type = llvm::FunctionType::get(ret_llvm, param_types, false);

            // Declare the source function (external)
            auto* src_fn = module_->getFunction(src_mangled);
            if (!src_fn)
                src_fn = Function::Create(fn_type, Function::ExternalLinkage, src_mangled, module_.get());

            // Create forwarding wrapper with this module's mangled name
            auto* wrapper = Function::Create(fn_type, Function::ExternalLinkage, dst_mangled, module_.get());
            auto* bb = BasicBlock::Create(*context_, "entry", wrapper);
            builder_->SetInsertPoint(bb);
            std::vector<Value*> args;
            for (auto& arg : wrapper->args()) args.push_back(&arg);
            auto* result = builder_->CreateCall(src_fn, args);
            builder_->CreateRet(result);

            // Register in imports_.meta so the interface file includes it
            imports_.meta[dst_mangled] = meta;
            imports_.interface_symbols.insert(dst_mangled);
        }
    }

    // Clear builder insert point after module compilation
    builder_->ClearInsertionPoint();

    finalize_debug_info();

    // Verify
    std::string err;
    raw_string_ostream os(err);
    if (verifyModule(*module_, &os)) {
        std::cerr << "Module verification failed:\n" << err << "\n";
        return nullptr;
    }
    optimize();
    return module_.get();
}



bool Codegen::emit_object_file(const std::string& path) {
    if (!target_machine_) return false;
    // Ensure parent directory exists
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::error_code ec;
    raw_fd_ostream dest(path, ec, sys::fs::OF_None);
    if (ec) return false;
    legacy::PassManager pass;
    if (target_machine_->addPassesToEmitFile(pass, dest, nullptr, CodeGenFileType::ObjectFile))
        return false;
    pass.run(*module_);
    dest.flush();
    return true;
}

static std::string ctype_to_string(CType ct) {
    switch (ct) {
        case CType::INT: return "INT";
        case CType::FLOAT: return "FLOAT";
        case CType::BOOL: return "BOOL";
        case CType::STRING: return "STRING";
        case CType::SEQ: return "SEQ";
        case CType::TUPLE: return "TUPLE";
        case CType::UNIT: return "UNIT";
        case CType::FUNCTION: return "FUNCTION";
        case CType::SYMBOL: return "SYMBOL";
        case CType::PROMISE: return "PROMISE";
        case CType::SET: return "SET";
        case CType::DICT: return "DICT";
        case CType::ADT: return "ADT";
        case CType::BYTE_ARRAY: return "BYTE_ARRAY";
        case CType::INT_ARRAY: return "INT_ARRAY";
        case CType::FLOAT_ARRAY: return "FLOAT_ARRAY";
        case CType::CHANNEL: return "CHANNEL";
        case CType::SUM: return "SUM";
        case CType::RECORD: return "RECORD";
    }
    return "INT";
}

std::string Codegen::emit_ir() {
    std::string ir;
    raw_string_ostream os(ir);
    module_->print(os, nullptr);
    return ir;
}

bool Codegen::link_runtime_bitcode(const std::string& bc_path) {
    if (!module_) return false;

    llvm::SMDiagnostic err;
    auto rt_module = llvm::parseIRFile(bc_path, err, *context_);
    if (!rt_module) return false;

    // Link the runtime module into our module.
    // OverrideFromSrc: if both modules define a function, keep the
    // runtime's definition (it has the body, ours has just a declaration).
    return !llvm::Linker::linkModules(*module_, std::move(rt_module),
                                       llvm::Linker::OverrideFromSrc);
}

void Codegen::apply_fastcc() {
    if (!module_) return;

    for (auto& fn : *module_) {
        if (fn.isDeclaration()) continue;
        if (fn.getLinkage() != Function::InternalLinkage) continue;
        if (fn.hasAddressTaken()) continue;

        // fastcc for internal functions not used as HOF values
        fn.setCallingConv(llvm::CallingConv::Fast);
        for (auto* user : fn.users()) {
            if (auto* call = dyn_cast<CallInst>(user))
                call->setCallingConv(llvm::CallingConv::Fast);
        }

        // Inlining hints for small functions (≤20 basic blocks).
        // Helps LLVM inline recursive functions more aggressively.
        if (fn.size() <= 20)
            fn.addFnAttr(llvm::Attribute::InlineHint);
    }
}

void Codegen::optimize() {
    if (!module_) return;

    // Apply fastcc before optimization so LLVM can exploit it
    apply_fastcc();

    // Use the new PassManager for all optimization levels
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Select optimization level
    llvm::OptimizationLevel level;
    switch (opt_level_) {
        case 0: level = llvm::OptimizationLevel::O0; break;
        case 1: level = llvm::OptimizationLevel::O1; break;
        case 3: level = llvm::OptimizationLevel::O3; break;
        default: level = llvm::OptimizationLevel::O2; break;
    }

    llvm::ModulePassManager MPM;
    if (opt_level_ == 0) {
        // O0: only run AlwaysInliner for marked functions
        MPM = PB.buildO0DefaultPipeline(level);
    } else {
        // O1-O3: full pipeline including:
        // - Function inlining (cost-based at O2+)
        // - SROA (scalar replacement of aggregates — decomposes structs to registers)
        // - Loop optimizations (LICM, unrolling at O2+, vectorization at O3)
        // - Dead argument elimination
        // - Tail call elimination
        // - GVN, instcombine, CFG simplification, mem2reg
        MPM = PB.buildPerModuleDefaultPipeline(level);
    }

    MPM.run(*module_, MAM);
}

// ===== Entry Point =====

Function* Codegen::codegen_main(AstNode* node) {
    auto i32 = LType::getInt32Ty(*context_);
    auto fn = Function::Create(llvm::FunctionType::get(i32, {}, false),
                                Function::ExternalLinkage, "main", module_.get());
    // Create debug info for main function
    if (debug_.enabled && debug_.builder && debug_.file) {
        auto* di_func_ty = debug_.builder->createSubroutineType(
            debug_.builder->getOrCreateTypeArray({debug_.builder->createBasicType("Int", 32, dwarf::DW_ATE_signed)}));
        auto* di_sp = debug_.builder->createFunction(
            debug_.file, "main", "main", debug_.file,
            node->source_context.line, di_func_ty, node->source_context.line,
            DINode::FlagZero, DISubprogram::SPFlagDefinition);
        fn->setSubprogram(di_sp);
        debug_.scope = di_sp;
    }
    auto bb = BasicBlock::Create(*context_, "entry", fn);
    builder_->SetInsertPoint(bb);
    set_debug_loc(node->source_context);

    auto result = codegen(node);
    // Don't add print/ret if the block is already terminated (e.g., by raise)
    if (!builder_->GetInsertBlock()->getTerminator()) {
        if (result) codegen_print(result);
        builder_->CreateRet(ConstantInt::get(i32, 0));
    }
    return fn;
}

// ===== Print (type-directed) =====

void Codegen::codegen_print_value(const TypedValue& tv) {
    if (!tv.val) return;
    Value* v = tv.val;
    switch (tv.type) {
        case CType::INT:    builder_->CreateCall(rt_.print_int_, {v}); break;
        case CType::FLOAT:  builder_->CreateCall(rt_.print_float_, {v}); break;
        case CType::BOOL:   builder_->CreateCall(rt_.print_bool_, {v}); break;
        case CType::STRING: builder_->CreateCall(rt_.print_string_, {v}); break;
        case CType::SEQ:    builder_->CreateCall(rt_.print_seq_, {v}); break;
        case CType::TUPLE: {
            builder_->CreateCall(rt_.print_string_, {builder_->CreateGlobalStringPtr("(")});
            if (!tv.subtypes.empty()) {
                // Boxed tuple (i64 = ptrtoint'd ptr to i64 array): GEP + load
                auto i64_ty_local = LType::getInt64Ty(*context_);
                Value* tuple_ptr = tv.val;
                if (tuple_ptr->getType()->isIntegerTy())
                    tuple_ptr = builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0));
                for (unsigned i = 0; i < tv.subtypes.size(); i++) {
                    if (i > 0)
                        builder_->CreateCall(rt_.print_string_, {builder_->CreateGlobalStringPtr(", ")});
                    auto* gep = builder_->CreateGEP(i64_ty_local, tuple_ptr,
                        {ConstantInt::get(i64_ty_local, i + 2)}); // +2 for tuple header
                    auto* elem = builder_->CreateLoad(i64_ty_local, gep);
                    CType et = tv.subtypes[i];
                    // Tuple slots are always i64; restore the LLVM type print
                    // helpers expect (i1 for Bool, ptr for String, f64, …).
                    Value* typed = elem;
                    LType* want = llvm_type(et);
                    if (typed->getType() != want) {
                        if (want->isPointerTy())
                            typed = builder_->CreateIntToPtr(typed, want);
                        else if (want->isDoubleTy())
                            typed = builder_->CreateBitCast(typed, want);
                        else if (want->isIntegerTy() &&
                                 want->getIntegerBitWidth() < 64)
                            typed = builder_->CreateTrunc(typed, want);
                    }
                    codegen_print_value({typed, et});
                }
            }
            builder_->CreateCall(rt_.print_string_, {builder_->CreateGlobalStringPtr(")")});
            break;
        }
        case CType::SET:    builder_->CreateCall(rt_.print_set_, {tv.val}); break;
        case CType::DICT:   builder_->CreateCall(rt_.print_dict_, {tv.val}); break;
        case CType::UNIT:     builder_->CreateCall(rt_.print_string_, {builder_->CreateGlobalStringPtr("()")}); break;
        case CType::FUNCTION: builder_->CreateCall(rt_.print_string_, {builder_->CreateGlobalStringPtr("<function>")}); break;
        case CType::SYMBOL: {
            // Symbol is an interned i64 ID. Look up the string for printing.
            if (auto* ci = dyn_cast<ConstantInt>(tv.val)) {
                int64_t id = ci->getSExtValue();
                if (id >= 0 && id < (int64_t)symbols_.strings.size()) {
                    builder_->CreateCall(rt_.print_symbol_, {symbols_.strings[id]});
                }
            } else {
                // Runtime symbol value — need a table lookup.
                // Emit a GEP into the symbol names table (emitted at finalization).
                // For now, emit a placeholder.
                builder_->CreateCall(rt_.print_string_, {builder_->CreateGlobalStringPtr(":<dynamic>")});
            }
            break;
        }
        case CType::ADT: {
            builder_->CreateCall(rt_.print_string_, {builder_->CreateGlobalStringPtr("<adt>")});
            break;
        }
        case CType::BYTE_ARRAY: {
            builder_->CreateCall(rt_.print_byte_array_, {tv.val});
            break;
        }
        case CType::INT_ARRAY: {
            builder_->CreateCall(rt_.print_int_array_, {tv.val});
            break;
        }
        case CType::FLOAT_ARRAY: {
            builder_->CreateCall(rt_.print_float_array_, {tv.val});
            break;
        }
        default: break;
    }
}

void Codegen::codegen_print(const TypedValue& tv) {
    auto resolved = auto_await(tv);
    codegen_print_value(resolved);
    builder_->CreateCall(rt_.print_newline_, {});
}

// ===== Core Dispatch =====

TypedValue Codegen::codegen(AstNode* node) {
    if (!node) return {};
    switch (node->get_type()) {
        case AST_MAIN:            return codegen_main_node(static_cast<MainNode*>(node));
        case AST_INTEGER_EXPR:    return codegen_integer(static_cast<IntegerExpr*>(node));
        case AST_FLOAT_EXPR:      return codegen_float(static_cast<FloatExpr*>(node));
        case AST_TRUE_LITERAL_EXPR:  return codegen_bool_true(static_cast<TrueLiteralExpr*>(node));
        case AST_FALSE_LITERAL_EXPR: return codegen_bool_false(static_cast<FalseLiteralExpr*>(node));
        case AST_STRING_EXPR:     return codegen_string(static_cast<StringExpr*>(node));
        case AST_UNIT_EXPR:       return codegen_unit(static_cast<UnitExpr*>(node));
        case AST_SYMBOL_EXPR:    return codegen_symbol(static_cast<SymbolExpr*>(node));
        case AST_ADD_EXPR:        return codegen_binary(static_cast<AddExpr*>(node)->left, static_cast<AddExpr*>(node)->right, "+");
        case AST_SUBTRACT_EXPR:   return codegen_binary(static_cast<SubtractExpr*>(node)->left, static_cast<SubtractExpr*>(node)->right, "-");
        case AST_MULTIPLY_EXPR:   return codegen_binary(static_cast<MultiplyExpr*>(node)->left, static_cast<MultiplyExpr*>(node)->right, "*");
        case AST_DIVIDE_EXPR:     return codegen_binary(static_cast<DivideExpr*>(node)->left, static_cast<DivideExpr*>(node)->right, "/");
        case AST_MODULO_EXPR:     return codegen_binary(static_cast<ModuloExpr*>(node)->left, static_cast<ModuloExpr*>(node)->right, "%");
        case AST_EQ_EXPR:         return codegen_comparison(static_cast<EqExpr*>(node)->left, static_cast<EqExpr*>(node)->right, "==");
        case AST_NEQ_EXPR:        return codegen_comparison(static_cast<NeqExpr*>(node)->left, static_cast<NeqExpr*>(node)->right, "!=");
        case AST_LT_EXPR:         return codegen_comparison(static_cast<LtExpr*>(node)->left, static_cast<LtExpr*>(node)->right, "<");
        case AST_GT_EXPR:         return codegen_comparison(static_cast<GtExpr*>(node)->left, static_cast<GtExpr*>(node)->right, ">");
        case AST_LTE_EXPR:        return codegen_comparison(static_cast<LteExpr*>(node)->left, static_cast<LteExpr*>(node)->right, "<=");
        case AST_GTE_EXPR:        return codegen_comparison(static_cast<GteExpr*>(node)->left, static_cast<GteExpr*>(node)->right, ">=");
        case AST_LOGICAL_AND_EXPR: { auto l = codegen(static_cast<LogicalAndExpr*>(node)->left); auto r = codegen(static_cast<LogicalAndExpr*>(node)->right); return {builder_->CreateAnd(l.val, r.val), CType::BOOL}; }
        case AST_LOGICAL_OR_EXPR:  { auto l = codegen(static_cast<LogicalOrExpr*>(node)->left); auto r = codegen(static_cast<LogicalOrExpr*>(node)->right); return {builder_->CreateOr(l.val, r.val), CType::BOOL}; }
        case AST_LOGICAL_NOT_OP_EXPR: { auto v = codegen(static_cast<LogicalNotOpExpr*>(node)->expr); return {builder_->CreateNot(v.val), CType::BOOL}; }
        case AST_PIPE_RIGHT_EXPR: {
            // x |> f          →  f x
            // x |> f a b      →  f a b x   (append lhs as the last argument
            //                                of the rhs apply chain)
            //
            // We don't construct a synthetic ApplyExpr — its destructor
            // would delete the borrowed children. Instead, evaluate all
            // arguments (rhs's existing args plus pe->left) directly and
            // dispatch through resolve_apply_function + emit_direct_call,
            // matching codegen_apply's flow.
            auto* pe = static_cast<PipeRightExpr*>(node);

            // Determine the root function name. For `x |> f`, that's `f`.
            // For `x |> f a b`, walk the rhs's call chain to its root.
            std::string fn_name;
            std::vector<std::variant<ExprNode*, ValueExpr*>> rhs_args;
            if (pe->right->get_type() == AST_IDENTIFIER_EXPR) {
                fn_name = static_cast<IdentifierExpr*>(pe->right)->name->value;
            } else if (pe->right->get_type() == AST_APPLY_EXPR) {
                // Walk inner ExprCall chain to find the innermost NameCall
                // and gather args in surface order (innermost first, then
                // outer). This mirrors codegen_apply::flatten_apply_chain.
                std::vector<ApplyExpr*> chain;
                ApplyExpr* cur = static_cast<ApplyExpr*>(pe->right);
                while (cur) {
                    chain.push_back(cur);
                    if (auto* nc = dynamic_cast<NameCall*>(cur->call)) {
                        fn_name = nc->name->value; break;
                    } else if (auto* ec = dynamic_cast<ExprCall*>(cur->call)) {
                        if (auto* inner = dynamic_cast<ApplyExpr*>(ec->expr)) {
                            cur = inner;
                        } else break;
                    } else break;
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    for (auto& a : (*it)->args) rhs_args.push_back(a);
                }
            }

            if (!fn_name.empty()) {
                // Evaluate rhs args (allowing nullptr val for deferred
                // FUNCTION args, which precompile_function_args / wrap step
                // resolves), then pe->left as the final argument.
                EvaluatedArgs eval;
                for (auto& a : rhs_args) {
                    last_lambda_name_.clear();
                    TypedValue tv;
                    if (std::holds_alternative<ExprNode*>(a)) tv = codegen(std::get<ExprNode*>(a));
                    else tv = codegen(std::get<ValueExpr*>(a));
                    if (tv.type != CType::FUNCTION && !tv) return {};
                    if (tv.type == CType::PROMISE) tv = auto_await(tv);
                    eval.all_args.push_back(tv);
                    eval.arg_lambda_names.push_back(last_lambda_name_);
                }
                {
                    last_lambda_name_.clear();
                    auto tv = codegen(pe->left);
                    if (tv.type != CType::FUNCTION && !tv) return {};
                    if (tv.type == CType::PROMISE) tv = auto_await(tv);
                    eval.all_args.push_back(tv);
                    eval.arg_lambda_names.push_back(last_lambda_name_);
                }
                precompile_function_args(eval);
                wrap_function_args_in_closures(eval.all_args);

                auto& all_args = eval.all_args;

                // ADT constructor as the pipe target — `[1,2,3] |> Some`.
                auto adt_it = types_.adt_constructors.find(fn_name);
                if (adt_it != types_.adt_constructors.end() && adt_it->second.arity > 0)
                    return codegen_adt_construct(fn_name, all_args);

                auto cf_it = resolve_apply_function(fn_name, all_args);
                if (cf_it != compiled_functions_.end()) {
                    auto& cf = cf_it->second;
                    size_t func_arity = cf.param_types.size() - cf.capture_names.size();
                    if (all_args.size() < func_arity)
                        return codegen_partial_apply(fn_name, cf, all_args);
                    if (all_args.size() > func_arity)
                        return codegen_curry_apply(fn_name, cf, all_args);
                    return emit_direct_call(fn_name, cf, all_args);
                }

                auto ext_it = imports_.extern_functions.find(fn_name);
                if (ext_it != imports_.extern_functions.end())
                    return codegen_extern_call(nullptr, fn_name, all_args);

                // Higher-order call via named_values_
                auto var_it = named_values_.find(fn_name);
                if (var_it != named_values_.end() && var_it->second.type == CType::FUNCTION && var_it->second.val)
                    return codegen_higher_order_call(fn_name, all_args);
            }

            // rhs is some other expression (lambda literal, parens, etc.)
            // — codegen it as a value, then call it via indirect call.
            auto arg = codegen(pe->left);
            auto fn = codegen(pe->right);
            if (!arg || !fn) return {};
            if (fn.type != CType::FUNCTION) {
                report_error(pe->source_context, "pipe: right side must be a function");
                return {};
            }
            CType ret_ct = !fn.subtypes.empty() ? fn.subtypes[0] : CType::INT;
            auto* ret_llvm = llvm_type(ret_ct);
            if (fn.val->getType()->isPointerTy() && !llvm::isa<llvm::Function>(fn.val)) {
                auto i64_ty = llvm::Type::getInt64Ty(*context_);
                auto ptr_ty = llvm::PointerType::get(*context_, 0);
                auto* fn_i64 = builder_->CreateLoad(i64_ty, fn.val, "pipe_closure_fn_i64");
                auto* fn_ptr = builder_->CreateIntToPtr(fn_i64, ptr_ty, "pipe_closure_fn");
                auto* fn_type = llvm::FunctionType::get(i64_ty, {ptr_ty, arg.val->getType()}, false);
                Value* raw = builder_->CreateCall(fn_type, fn_ptr, {fn.val, arg.val}, "pipe_call");
                if (ret_llvm->isPointerTy())
                    raw = builder_->CreateIntToPtr(raw, ret_llvm);
                else if (ret_llvm->isIntegerTy() && raw->getType() != ret_llvm)
                    raw = builder_->CreateZExtOrTrunc(raw, ret_llvm);
                return {raw, ret_ct};
            }
            auto* fn_type = llvm::FunctionType::get(ret_llvm, {arg.val->getType()}, false);
            return {builder_->CreateCall(fn_type, fn.val, {arg.val}, "pipe_call"), ret_ct};
        }
        case AST_PIPE_LEFT_EXPR: {
            // f <| x  →  f(x) — same logic, swapped sides
            auto* pe = static_cast<PipeLeftExpr*>(node);
            auto arg = codegen(pe->right);
            if (!arg) return {};
            std::string fn_name;
            if (pe->left->get_type() == AST_IDENTIFIER_EXPR)
                fn_name = static_cast<IdentifierExpr*>(pe->left)->name->value;
            if (!fn_name.empty()) {
                std::vector<TypedValue> all_args = {arg};
                auto cf_it = compiled_functions_.find(fn_name);
                if (cf_it == compiled_functions_.end()) {
                    auto def_it = deferred_functions_.find(fn_name);
                    if (def_it != deferred_functions_.end()) {
                        compile_function(fn_name, def_it->second, all_args);
                        cf_it = compiled_functions_.find(fn_name);
                    }
                }
                if (cf_it != compiled_functions_.end())
                    return {builder_->CreateCall(cf_it->second.fn, {arg.val}), cf_it->second.return_type};
                auto ext_it = imports_.extern_functions.find(fn_name);
                if (ext_it != imports_.extern_functions.end()) {
                    auto* ext_fn = module_->getFunction(ext_it->second);
                    if (!ext_fn) {
                        auto fn_type = llvm::FunctionType::get(arg.val->getType(), {arg.val->getType()}, false);
                        ext_fn = Function::Create(fn_type, Function::ExternalLinkage, ext_it->second, module_.get());
                    }
                    return {builder_->CreateCall(ext_fn, {arg.val}), CType::INT};
                }
                auto nv_it = named_values_.find(fn_name);
                if (nv_it != named_values_.end() && nv_it->second.type == CType::FUNCTION && nv_it->second.val) {
                    auto fn_type = llvm::FunctionType::get(arg.val->getType(), {arg.val->getType()}, false);
                    return {builder_->CreateCall(fn_type, nv_it->second.val, {arg.val}), arg.type};
                }
            }
            report_error(pe->source_context, "pipe: left side must be a function");
            return {};
        }
        case AST_LET_EXPR:        return codegen_let(static_cast<LetExpr*>(node));
        case AST_IF_EXPR:         return codegen_if(static_cast<IfExpr*>(node));
        case AST_CASE_EXPR:       return codegen_case(static_cast<CaseExpr*>(node));
        case AST_DO_EXPR:         return codegen_do(static_cast<DoExpr*>(node));
        case AST_RAISE_EXPR:      return codegen_raise(static_cast<RaiseExpr*>(node));
        case AST_TRY_CATCH_EXPR:  return codegen_try_catch(static_cast<TryCatchExpr*>(node));
        case AST_WITH_EXPR:      return codegen_with(static_cast<WithExpr*>(node));
        case AST_PERFORM_EXPR:   return codegen_perform(static_cast<PerformExpr*>(node));
        case AST_HANDLE_EXPR:    return codegen_handle(static_cast<HandleExpr*>(node));
        case AST_IDENTIFIER_EXPR: return codegen_identifier(static_cast<IdentifierExpr*>(node));
        case AST_FUNCTION_EXPR:   return codegen_function_def(static_cast<FunctionExpr*>(node), "");
        case AST_APPLY_EXPR:      return codegen_apply(static_cast<ApplyExpr*>(node));
        case AST_LAMBDA_ALIAS:    return codegen_lambda_alias(static_cast<LambdaAlias*>(node));
        case AST_IMPORT_EXPR:     return codegen_import(static_cast<ImportExpr*>(node));
        case AST_EXTERN_DECL:    return codegen_extern_decl(static_cast<ExternDeclExpr*>(node));
        case AST_FIELD_UPDATE_EXPR: {
            auto* fu = static_cast<FieldUpdateExpr*>(node);
            auto obj = codegen(fu->identifier);
            if (!obj || obj.type != CType::ADT) {
                report_error(fu->source_context, "field update requires ADT value");
                return {};
            }
            // Find the constructor with the matching field names
            for (auto& [ctor_name, info] : types_.adt_constructors) {
                if (info.field_names.empty()) continue;
                // Copy the struct, replace updated fields
                Value* result = obj.val;
                for (auto& [name_expr, val_expr] : fu->updates) {
                    auto new_val = codegen(val_expr);
                    if (!new_val) return {};
                    for (size_t fi = 0; fi < info.field_names.size(); fi++) {
                        if (info.field_names[fi] == name_expr->value) {
                            if (info.is_recursive) {
                                // Heap ADT: create new node, copy all fields, replace one
                                // For simplicity, not supported yet for recursive types
                                report_error(fu->source_context, "field update on recursive ADT not supported");
                                return {};
                            }
                            Value* store_val = new_val.val;
                            if (store_val->getType() != LType::getInt64Ty(*context_)) {
                                if (store_val->getType()->isPointerTy())
                                    store_val = builder_->CreatePtrToInt(store_val, LType::getInt64Ty(*context_));
                            }
                            result = builder_->CreateInsertValue(result, store_val, {(unsigned)(fi + 1)});
                            break;
                        }
                    }
                }
                return {result, CType::ADT};
            }
            report_error(fu->source_context, "no ADT constructor found for field update");
            return {};
        }
        case AST_FIELD_ACCESS_EXPR: {
            auto* fa = static_cast<FieldAccessExpr*>(node);
            auto obj = codegen(fa->identifier);
            if (!obj) return {};
            std::string field_name = fa->name->value;
            if (obj.type == CType::ADT) {
                for (auto& [ctor_name, info] : types_.adt_constructors) {
                    if (!obj.adt_type_name.empty() && info.type_name != obj.adt_type_name)
                        continue;
                    for (size_t fi = 0; fi < info.field_names.size(); fi++) {
                        if (info.field_names[fi] == field_name) {
                            CType ftype = (fi < info.field_types.size()) ? info.field_types[fi] : CType::INT;
                            bool use_heap_layout = info.is_recursive ||
                                (obj.val && obj.val->getType()->isPointerTy()) ||
                                (obj.val && obj.val->getType()->isIntegerTy());
                            if (use_heap_layout) {
                                Value* obj_ptr = obj.val;
                                if (obj_ptr->getType()->isIntegerTy())
                                    obj_ptr = builder_->CreateIntToPtr(obj_ptr, PointerType::get(*context_, 0));
                                auto val = builder_->CreateCall(rt_.adt_get_field_,
                                    {obj_ptr, ConstantInt::get(LType::getInt64Ty(*context_), fi)});
                                if (ftype == CType::STRING || ftype == CType::SEQ)
                                    return {builder_->CreateIntToPtr(val, PointerType::get(*context_, 0)), ftype};
                                return {val, ftype};
                            } else {
                                auto val = builder_->CreateExtractValue(obj.val, {(unsigned)(fi + 1)});
                                // Cast i64 to ptr if field type is pointer-based
                                if (ftype == CType::STRING || ftype == CType::SEQ ||
                                    ftype == CType::SET || ftype == CType::DICT ||
                                    ftype == CType::FUNCTION) {
                                    val = builder_->CreateIntToPtr(val, PointerType::get(*context_, 0));
                                }
                                return {val, ftype};
                            }
                        }
                    }
                }
            }
            // Record field access: look up field index from the record's field map
            if (obj.type == CType::RECORD && !obj.record_fields.empty()) {
                for (size_t fi = 0; fi < obj.record_fields.size(); fi++) {
                    if (obj.record_fields[fi] == field_name) {
                        Value* tuple_ptr = obj.val;
                        if (tuple_ptr->getType()->isIntegerTy())
                            tuple_ptr = builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0));
                        auto* gep = builder_->CreateGEP(LType::getInt64Ty(*context_), tuple_ptr,
                            {ConstantInt::get(LType::getInt64Ty(*context_), fi + 2)}, "rec_field_gep");
                        auto* val = builder_->CreateLoad(LType::getInt64Ty(*context_), gep, "rec_field");
                        CType ftype = (fi < obj.subtypes.size()) ? obj.subtypes[fi] : CType::INT;
                        if (ftype == CType::STRING || ftype == CType::FUNCTION ||
                            ftype == CType::BYTE_ARRAY || ftype == CType::PROMISE)
                            return {builder_->CreateIntToPtr(val, PointerType::get(*context_, 0)), ftype};
                        if (ftype == CType::SEQ || ftype == CType::SET || ftype == CType::DICT)
                            return {builder_->CreateIntToPtr(val, PointerType::get(LType::getInt64Ty(*context_), 0)), ftype};
                        if (ftype == CType::FLOAT)
                            return {builder_->CreateBitCast(val, LType::getDoubleTy(*context_)), ftype};
                        if (ftype == CType::BOOL)
                            return {builder_->CreateTrunc(val, LType::getInt1Ty(*context_)), ftype};
                        return {val, ftype};
                    }
                }
            }
            report_error(fa->source_context, "unknown field '" + field_name + "'");
            return {};
        }
        case AST_RECORD_LITERAL_EXPR: {
            auto* rec = static_cast<RecordLiteralExpr*>(node);
            set_debug_loc(rec->source_context);
            auto i64_ty = LType::getInt64Ty(*context_);

            // Compile each field value
            std::vector<Value*> elems;
            std::vector<CType> field_ctypes;
            std::vector<std::string> field_names;
            for (auto& [name, expr] : rec->fields) {
                auto tv = codegen(expr);
                if (!tv) return {};
                Value* i64_val = tv.val;
                if (i64_val->getType()->isPointerTy())
                    i64_val = builder_->CreatePtrToInt(i64_val, i64_ty);
                else if (i64_val->getType()->isDoubleTy())
                    i64_val = builder_->CreateBitCast(i64_val, i64_ty);
                else if (i64_val->getType()->isIntegerTy() && i64_val->getType() != i64_ty)
                    i64_val = builder_->CreateZExtOrTrunc(i64_val, i64_ty);
                elems.push_back(i64_val);
                field_ctypes.push_back(tv.type);
                field_names.push_back(name);
            }

            // Allocate as tuple
            auto* tuple_ptr = builder_->CreateCall(rt_.tuple_alloc_,
                {ConstantInt::get(i64_ty, elems.size())}, "record");
            int64_t heap_mask = 0;
            for (size_t i = 0; i < elems.size(); i++) {
                builder_->CreateCall(rt_.tuple_set_,
                    {tuple_ptr, ConstantInt::get(i64_ty, i), elems[i]});
                // Only set heap_mask for non-constant heap values (constants aren't RC-managed)
                if (is_heap_type(field_ctypes[i]) && i < 64 && !llvm::isa<llvm::Constant>(elems[i]))
                    heap_mask |= ((int64_t)1 << i);
            }
            if (heap_mask != 0)
                builder_->CreateCall(rt_.tuple_set_heap_mask_,
                    {tuple_ptr, ConstantInt::get(i64_ty, heap_mask)});
            auto* rec_i64 = builder_->CreatePtrToInt(tuple_ptr, i64_ty, "record_i64");

            TypedValue result = {rec_i64, CType::RECORD, field_ctypes};
            result.record_fields = field_names;
            return result;
        }
        case AST_TUPLE_EXPR:      return codegen_tuple(static_cast<TupleExpr*>(node));
        case AST_VALUES_SEQUENCE_EXPR: return codegen_seq(static_cast<ValuesSequenceExpr*>(node));
        case AST_SET_EXPR:        return codegen_set(static_cast<SetExpr*>(node));
        case AST_DICT_EXPR:       return codegen_dict(static_cast<DictExpr*>(node));
        case AST_CONS_LEFT_EXPR:  return codegen_cons(static_cast<ConsLeftExpr*>(node));
        case AST_JOIN_EXPR:       return codegen_join(static_cast<JoinExpr*>(node));
        case AST_SEQ_GENERATOR_EXPR: return codegen_seq_generator(static_cast<SeqGeneratorExpr*>(node));
        case AST_SET_GENERATOR_EXPR: return codegen_set_generator(static_cast<SetGeneratorExpr*>(node));
        case AST_DICT_GENERATOR_EXPR: return codegen_dict_generator(static_cast<DictGeneratorExpr*>(node));
        case AST_ADT_DECL:       return {}; // handled at module level
        case AST_ADT_CONSTRUCTOR: return {};
        case AST_CONSTRUCTOR_PATTERN: return {};
        default:
            report_error(node->source_context, "unsupported expression type");
            return {};
    }
}

// ===== Symbol interning =====

int64_t Codegen::intern_symbol(const std::string& name) {
    auto it = symbols_.ids.find(name);
    if (it != symbols_.ids.end()) return it->second;
    int64_t id = static_cast<int64_t>(symbols_.strings.size());
    symbols_.ids[name] = id;
    symbols_.strings.push_back(builder_->CreateGlobalStringPtr(name, "sym." + name));
    return id;
}

// ===== CFFI =====

void Codegen::register_cffi_signatures() {
    // TODO: Register known C library function signatures
}

bool Codegen::is_cffi_import(const std::string& mod_fqn) {
    return mod_fqn.size() >= 2 && mod_fqn[0] == 'C' && mod_fqn[1] == '\\';
}

// ===== Type annotation helpers (local to this TU for compile_module) =====

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

// Decompose a curried function type (Int -> Int -> Int) into param types + return type
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
