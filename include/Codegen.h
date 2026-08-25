//
// LLVM Code Generation for Yona
// =============================
//
// Generates LLVM IR from a type-checked AST. Since Yona uses Hindley-Milner
// type inference, all types are known at compile time and primitives are unboxed.
//
// Every expression produces a TypedValue — an LLVM Value paired with its
// codegen type tag (CType). Types propagate structurally through expressions,
// enabling type-directed code generation without relying on the TypeChecker.
//
// The Codegen class carries several parallel state machines that a reader
// should understand before making changes:
//
//   1. Deferred compilation cache
//      - deferred_functions_: name → AST, awaiting a concrete type at a call
//        site (monomorphization).
//      - compiled_functions_: (name + arg types) → LLVM Function. Result of
//        specializing a deferred function for a specific call signature.
//
//   2. Scope/binding state
//      - named_values_: identifier → TypedValue for the currently visible
//        lexical scope. Saved/restored around let and function bodies.
//      - arm_drop_stack_: per-case-arm list of (Value*, CType) pairs that
//        must be rc_dec'd before the arm branches to the merge block.
//
//   3. Perceus-linear ownership (see docs/memory-management.md)
//      - "Last use" is decided at call sites via count_identifier_refs()
//        over current_fn_body_ (the AST of the enclosing function). A
//        single textual occurrence means the argument may transfer.
//      - transferred_values_: Value* -> transfer-domain flags, currently
//        SEQ and MAP (SET/DICT). Storage is unified, but semantics stay
//        explicit via domain-specific helpers. TransferScope remains SEQ-only.
//      - TransferScope stack: flow-sensitive transfer tracking across
//        if/case. Invariant (critical): transfer_scope_enter() must run
//        BEFORE any branch BasicBlocks are created so the pre_blocks
//        snapshot excludes them; otherwise cross-branch droppability
//        misclassifies values defined inside a branch.
//
//   4. Effects (algebraic handlers)
//      - handler_stack_: active `handle ... with` handlers for perform
//        resolution. Lexically nested handle frames push/pop here.
//      - compiling_unhandled_perform_ok_: module-export compile may emit a
//        runtime `:UnhandledEffect` raise so GENFN bodies that `perform`
//        can be precompiled; call-site remonomorphization inside `handle`
//        binds the caller's clauses (effect-row-directed, not a name list).
//
//   5. Closure devirtualization
//      - closure_known_fn_: Value* → Function*. When a known lambda is
//        wrapped in a closure env, we remember the underlying fn so
//        indirect calls through the env can be rewritten to direct calls.
//
//   6. Module & import state
//      - module_paths_: search roots for .yonai interface files.
//      - Interface files carry GENFN source bodies so call sites can
//        re-compile generics locally when their arg types differ.
//
// Files in src/codegen/ split the implementation by syntactic category:
//   CodegenExpr.cpp        — literals, let, if, arithmetic, sequences
//   CodegenCase.cpp        — case/pattern matching
//   CodegenFunction.cpp    — function defs, apply, closures, last-use scan
//   CodegenCollections.cpp — comprehensions, stream fusion
//   CodegenEffects.cpp     — perform/handle, effect op dispatch
//   CodegenModule.cpp      — module decls, imports, exports
//

//
// Reader notes
// ------------
// TypedValue flow: every codegen_* helper returns a TypedValue so the caller
// knows both the LLVM value and the CType tag. Functions that consume a
// TypedValue (emit_rc_inc/dec, emit_direct_call, pattern matchers) switch
// on the tag to generate type-correct IR — do not drop it.
//

#pragma once

#include <filesystem>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/Target/TargetMachine.h>

#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>
#include <vector>

#include "ast.h"
#include "types.h"
#include "Diagnostic.h"
#include "typechecker/TypeChecker.h"
#include "AcceleratorLowering.h"

namespace yona::parser { class Parser; }

namespace yona::compiler::codegen {

using namespace yona::ast;

// Codegen type tag — tracks what kind of value an expression produces.
// Propagates through all expressions so the codegen always knows the
// correct LLVM type to use.
enum class CType { INT, FLOAT, BOOL, STRING, SEQ, TUPLE, UNIT, FUNCTION, SYMBOL, PROMISE, SET, DICT, ADT, BYTE_ARRAY, SUM, RECORD, INT_ARRAY, FLOAT_ARRAY, CHANNEL };

/// How `auto_await` completes a `PROMISE`-typed LLVM value at the runtime boundary.
enum class PromiseAwaitPath : uint8_t {
    AsyncPtr, ///< `yona_rt_async_await` — thread pool or `extern native` pointer
    IoUring,   ///< `yona_rt_io_await` — io_uring user_data cookie (`extern io`)
};

// A typed value: LLVM value + its codegen type + optional subtype info
struct TypedValue {
    llvm::Value* val = nullptr;
    CType type = CType::INT;
    std::vector<CType> subtypes; // tuple: element types; SEQ/SET: {elem_type}; DICT: {key_type, val_type}
    std::string adt_type_name;   // For CType::ADT: the ADT type name (e.g., "Option")
    /// True when `val` is a heap pointer stored as INT (Result/Option payload
    /// from a C ABI, or a field loaded from a heap ADT). Reused call sites
    /// must DUP even though `type` is not `CType::ADT`.
    bool boxed_heap = false;
    std::vector<std::string> record_fields; // For CType::RECORD: sorted field names (index = tuple position)
    PromiseAwaitPath promise_await = PromiseAwaitPath::AsyncPtr;

    TypedValue() = default;
    TypedValue(llvm::Value* v, CType t) : val(v), type(t) {}
    TypedValue(llvm::Value* v, CType t, std::vector<CType> st)
        : val(v), type(t), subtypes(std::move(st)) {}

    explicit operator bool() const { return val != nullptr; }
};

// Deferred function — stored AST, compiled at call site with known arg types
struct DeferredFunction {
    FunctionExpr* ast;
    std::vector<std::string> param_names;
    std::vector<std::string> free_vars;
};

class Codegen {
public:
    Codegen(const std::string& module_name = "yona_module",
            compiler::DiagnosticEngine* diag = nullptr);
    ~Codegen();

    // Set optional type checker for type-aware codegen.
    // When set, the codegen queries type_checker_->type_of(node) instead of guessing.
    void set_type_checker(typechecker::TypeChecker* tc) { type_checker_ = tc; }

    // Compile a single expression (wraps in main())
    llvm::Module* compile(AstNode* node);

    // Compile a module (exports functions with external linkage)
    llvm::Module* compile_module(ModuleDecl* module);

    bool emit_object_file(const std::string& output_path);
    bool emit_interface_file(const std::string& output_path);

    struct FiniteCaseCoverage {
        std::string adt_name;
        std::vector<std::string> missing;
    };

    struct CasePatternAnalysis {
        std::vector<size_t> unreachable_clauses;
        std::optional<FiniteCaseCoverage> incomplete;
    };

    /// Missing constructors for a case over a registered finite ADT. A
    /// wildcard/identifier arm, a complete constructor set, or a non-ADT case
    /// returns std::nullopt. Guarded arms do not establish coverage.
    std::optional<FiniteCaseCoverage> finite_case_coverage(ast::CaseExpr* node) const;
    CasePatternAnalysis analyze_case_patterns(ast::CaseExpr* node) const;

    /// After `compile_module`, type-check the module as a unit (`check_module`)
    /// so exported wrappers see private siblings, then copy inferred latent
    /// effect rows onto FN metadata for `.yonai` emission.
    void populate_interface_effect_rows(ast::ModuleDecl* mod,
                                        typechecker::TypeChecker& tc);
    bool load_interface_file(const std::string& path);
    std::string emit_ir();

    // Enable DWARF debug info emission
    void set_debug_info(bool enabled, const std::string& filename = "");

    /// Load the Prelude module interface. Call after module_paths_ is set.
    /// Single entry point: registers ADTs, functions, constructors, and types
    /// across codegen, parser, and type checker from the .yonai file.
    /// Falls back to programmatic registration if Prelude.yonai is not found.
    void load_prelude(parser::Parser* parser = nullptr,
                       typechecker::TypeChecker* type_checker = nullptr);
    void set_opt_level(int level) { opt_level_ = (level < 0) ? 0 : (level > 3) ? 3 : level; }
    /// Rewrite IntArray/FloatArray map/filter/foldl whose lambdas are in the
    /// Std\GPU kernel library into the columnar runtime ABI. Default on;
    /// GPU vs CPU remains a runtime decision (`YONA_GPU_VULKAN_MIN_LEN`, device).
    void set_accelerator_lowering(bool enabled) { accelerator_lowering_enabled_ = enabled; }
    void set_strict_accelerator(bool enabled) { strict_accelerator_ = enabled; }

    // Module search paths for resolving imports
    std::vector<std::string> module_paths_;

    /// `.yonai` signatures for the type checker (`set_import_type_source`).
    class ImportTypes final : public typechecker::ImportTypeSource {
    public:
        explicit ImportTypes(Codegen* cg) : cg_(cg) {}
        std::optional<typechecker::ImportedFnSig> imported_function_sig(
            const std::string& module_fqn, const std::string& name) override;
        std::vector<std::string> imported_module_exports(
            const std::string& module_fqn) override;
    private:
        Codegen* cg_;
    };
    ImportTypes import_types_{this};

    // Run LLVM optimization passes
    void optimize();

    // Link runtime bitcode for cross-module inlining (LTO).
    // If the bitcode file exists, loads and merges it into the module
    // before optimization, enabling LLVM to inline runtime functions.
    bool link_runtime_bitcode(const std::string& bc_path);

    // Apply fastcc to internal functions whose address is never taken
    void apply_fastcc();

    // Mangle a module function name for export
    static std::string mangle_name(const std::string& module_fqn, const std::string& func_name);

private:
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;
    llvm::TargetMachine* target_machine_ = nullptr;
    typechecker::TypeChecker* type_checker_ = nullptr;
    /// ADTs exported by the module currently being emitted. Interface files
    /// never expose private ADTs; opaque exports omit their constructor rows.
    std::unordered_set<std::string> interface_exported_types_;
    std::unordered_set<std::string> interface_opaque_types_;
    bool interface_export_filter_active_ = false;

    // Scope: variable name → typed value
    std::unordered_map<std::string, TypedValue> named_values_;

    // Deferred functions: name → AST (compiled at call site)
    std::unordered_map<std::string, DeferredFunction> deferred_functions_;

    // Stream fusion: deferred single-use generator bindings.
    // When a let-bound seq generator has exactly one use, we skip its codegen
    // and fuse it into the consuming generator at codegen time.
    std::unordered_map<std::string, ast::SeqGeneratorExpr*> deferred_generators_;

    // Compiled function cache: name + arg types → LLVM function + return type
    struct CompiledFunction {
        llvm::Function* fn;
        CType return_type;
        std::vector<CType> param_types;
        std::vector<std::string> capture_names;
        ast::ExternPromiseKind extern_promise = ast::ExternPromiseKind::Sync;
        CType promise_inner_type = CType::INT; // awaited value when return_type is PROMISE
        llvm::Value* closure_env = nullptr;
        std::string return_adt_name;
        std::vector<CType> return_subtypes;
        // Borrow inference: borrowed_params[i] == true means param i is
        // read-only (not returned, not stored). Call sites skip rc_inc;
        // function exit skips rc_dec. Empty vector = all params owned.
        std::vector<bool> borrowed_params;
        /// Type-checker overlay: C ABI is still `return_type` / `param_types`.
        bool return_linear = false;
        std::vector<char> tuple_elem_linear;
        std::vector<char> param_linear;
        std::vector<std::string> effect_ops;
        /// The exporter inferred an effect row, including a closed empty row.
        bool effect_row_known = false;
        bool effect_open_rest = false;
        bool effect_hof = false;
    };

    // Escape analysis: returns true if `name` appears in a "storing"
    // position in the AST — returned, captured in a closure, inserted
    // into a collection, or passed as an ADT constructor field.
    struct BorrowInferenceContext {
        std::string function_name;
        const std::vector<bool>* recursive_borrowed_params = nullptr;
    };
    bool has_escaping_use(ast::AstNode* node, const std::string& name,
                          bool is_return_position = false,
                          const BorrowInferenceContext* context = nullptr);
    std::vector<bool> infer_borrowed_params(const std::string& function_name,
                                            const DeferredFunction& def,
                                            const std::vector<CType>& param_ctypes);
    std::unordered_map<std::string, CompiledFunction> compiled_functions_;

    int lambda_counter_ = 0;
    std::string last_lambda_name_;

    // Case-arm-scoped seq bindings to drop before the arm branches to the
    // merge block. A head-tail pattern binds `rest = seq_tail(scrutinee)`,
    // which allocates a fresh seq when the scrutinee isn't unique. Without
    // an explicit rc_dec at arm exit, that seq leaks — see the queens
    // benchmark investigation. Each inner vector is the drops for one
    // active case arm; codegen_case pushes a new frame on entry to the
    // arm and drops + pops on exit.
    std::vector<std::vector<std::pair<llvm::Value*, CType>>> arm_drop_stack_;

    // ===== Perceus linear: seq ownership tracking =====
    //
    // "Last use" is decided on demand at call sites via
    // count_identifier_refs() over the enclosing function body
    // (current_fn_body_). A single-use occurrence is treated as a
    // transfer; codegen marks the corresponding Value* transfer-domain in
    // transferred_values_ so function-exit and
    // let-scope cleanup skip the rc_dec.
    enum class TransferDomain : uint8_t {
        Seq = 1u << 0, // branch-scoped Perceus transfer tracking
        Map = 1u << 1, // SET/DICT callee-owns suppression at cleanup/exit
    };
    using TransferMask = uint8_t;
    std::unordered_map<llvm::Value*, TransferMask> transferred_values_;
    void mark_transferred(llvm::Value* val, TransferDomain domain);
    bool is_transferred(llvm::Value* val, TransferDomain domain) const;
    void clear_transferred(TransferDomain domain);
    std::unordered_set<llvm::Value*> snapshot_transferred(TransferDomain domain) const;
    void restore_transferred(TransferDomain domain,
                             const std::unordered_set<llvm::Value*>& snapshot);

    // Runtime-decided "consumed by closure call" flags. When a heap-typed
    // arg is passed to a closure and the closure returns a different ptr,
    // the callee chain freed the arg. The function-exit dec must check
    // this flag at runtime to avoid double-free.
    std::unordered_map<llvm::Value*, llvm::Value*> closure_consumed_flags_;

    // Perceus phase 3: stack-allocated yona_frame_t for the function
    // currently being compiled (nullptr when the fn has no heap params).
    // Transfer sites emit yona_rt_frame_transfer(ptr) to NULL the drop
    // slot so a raise-unwind doesn't double-dec something a callee now
    // owns. Saved/restored around nested compile_function calls.
    llvm::Value* current_frame_alloca_ = nullptr;
    llvm::StructType* yona_frame_ty_ = nullptr;
    llvm::StructType* get_frame_type();
    void emit_frame_transfer(llvm::Value* ptr);

    // The body AST of the function currently being compiled. Used by
    // codegen_pattern_headtail for single-use scrutinee detection (the
    // Perceus-linear owned-scrutinee fast path).
    ast::AstNode* current_fn_body_ = nullptr;

    // Flow-sensitive transfer tracking across branching constructs.
    //
    // A "transfer scope" wraps a multi-way branch (if-then-else, case
    // arms) where each branch may transfer a different set of seqs via
    // emit_direct_call or pattern consume. Without per-branch tracking,
    // flow-insensitive seq transfer tracking would be "poisoned" whenever
    // ANY branch's codegen transferred a value — causing leaks when the
    // actual runtime path was a non-transfer branch whose scope cleanup
    // skipped the drop.
    //
    // Protocol:
    //   transfer_scope_enter()          — snapshot seq transfers + pre-scope BB ordinal watermark
    //   for each branch:
    //     transfer_branch_begin()       — reset seq transfers to snapshot
    //     codegen(branch)               — populates seq transfers
    //     transfer_branch_end(exit_bb)  — record this branch's set
    //   transfer_scope_exit()           — compute asymmetric transfers,
    //                                      emit rc_dec before each
    //                                      non-transferring branch's
    //                                      terminator, union into
    //                                      seq transfers
    //
    // INVARIANT (load-bearing, do not break without updating this doc):
    //   transfer_scope_enter() MUST run before any branch BasicBlock is
    //   created. pre_scope_block_ordinal captures the function's BB
    //   ordinals at entry;
    //   is_cross_branch_droppable() treats an Instruction as droppable
    //   from a sibling branch iff its parent BB ordinal is <= snapshot. A
    //   branch BB created between enter() and first branch codegen
    //   would be (incorrectly) classified as pre-scope, and values
    //   defined inside it would get rc_dec'd from sibling branches
    //   that never reach them — leading to pool UAF.
    //
    //   The fix for that bug in codegen_if: moved transfer_scope_enter()
    //   up above BasicBlock::Create calls. See CodegenExpr.cpp.
    struct TransferScope {
        std::unordered_set<llvm::Value*> entry_snapshot;
        uint64_t pre_scope_block_ordinal = 0;
        struct Branch {
            llvm::BasicBlock* exit_bb;   // nullptr if terminated (ret/raise)
            std::unordered_set<llvm::Value*> transfers;
        };
        std::vector<Branch> branches;
    };
    std::vector<TransferScope> transfer_scope_stack_;

    void transfer_scope_enter();
    void transfer_branch_begin();
    void transfer_branch_end(llvm::BasicBlock* exit_bb);
    void transfer_scope_exit();
    bool is_cross_branch_droppable(llvm::Value* v, uint64_t pre_scope_block_ordinal);
    void refresh_transfer_block_ordinals(llvm::Function* fn);

    llvm::Function* transfer_block_ordinal_fn_ = nullptr;
    uint64_t transfer_block_ordinal_next_ = 0;
    std::unordered_map<llvm::BasicBlock*, uint64_t> transfer_block_ordinals_;

    // Closure devirtualization: map closure Value* → underlying Function*
    // When a known lambda is wrapped in a closure, we remember the mapping
    // so indirect closure calls can be replaced with direct calls.
    std::unordered_map<llvm::Value*, llvm::Function*> closure_known_fn_;

    // Escape analysis: variables whose values don't escape the current scope
    std::unordered_set<std::string> non_escaping_vars_;
    // Current arena pointer (nullptr if no arena active)
    llvm::Value* current_arena_ = nullptr;

    // ===== Type Registry — ADTs, traits, CFFI =====
    struct TraitInfo {
        std::string name;
        std::string type_param;            // first param (backward compat)
        std::vector<std::string> type_params; // all params for multi-param traits
        std::vector<std::string> method_names;
        std::vector<std::pair<std::string, std::string>> superclasses;
        std::unordered_map<std::string, FunctionExpr*> default_impls;
    };
    struct TraitInstanceInfo {
        std::string trait_name;
        std::string type_name;             // first type arg (backward compat)
        std::vector<std::string> type_names; // all type args for multi-param
        std::unordered_map<std::string, std::string> method_mangled_names;
        std::vector<std::pair<std::string, std::string>> constraints;
    };
    struct AdtInfo {
        struct FieldShape {
            CType type = CType::INT;
            std::vector<FieldShape> tuple_elements;
            CType function_return_type = CType::INT;
            std::string function_return_adt_name;
        };

        std::string type_name;
        int tag, arity, total_variants, max_arity;
        bool is_recursive = false;
        std::vector<std::string> field_names;
        std::vector<CType> field_types;
        // For function-typed fields like `(() -> Stream a)`, the return
        // type's CType. Used by pattern-extracted callable fields so the
        // call site knows what the closure produces. Empty CType (INT) if
        // the field is not a function or the return type is unknown.
        std::vector<CType> field_fn_return_types;
        // For function-typed fields whose return type is an ADT, the
        // type name (e.g. "Stream"). Same length as field_fn_return_types.
        std::vector<std::string> field_fn_return_adt_names;
        // Full recursive shapes for fields declared as tuples. `field_types`
        // is the ABI view; this retains element types for pattern binding.
        std::vector<FieldShape> field_shapes;
    };

    static AdtInfo::FieldShape field_shape_from_field_type(const ast::FieldType& field_type);
    static std::string encode_field_shape(const AdtInfo::FieldShape& shape);
    static AdtInfo::FieldShape decode_field_shape(const std::string& text);
    struct CFFISignature {
        CType return_type;
        std::vector<CType> param_types;
    };
    struct ImportedFunctionSource {
        std::string source_text, local_name;
    };
    struct ModuleFunctionMeta {
        std::vector<CType> param_types;
        /// Lossless `.yonai` source descriptors. CType remains the C ABI view.
        std::vector<std::string> param_type_descriptors;
        CType return_type;
        std::string return_type_descriptor;
        ast::ExternPromiseKind extern_promise = ast::ExternPromiseKind::Sync;
        CType promise_inner_type = CType::INT; // inner `B` for `Promise B` / AFN IO NAT rows
        std::string return_adt_name;  // for ADT returns from extern decls: the type name
        // Inferred borrow contract loaded from .yonai. borrowed_params[i]
        // means parameter i is read-only/non-escaping, so call sites can
        // avoid defensive ownership bumps across module boundaries.
        std::vector<bool> borrowed_params;
        /// Type-checker overlay. `LINEAR` in `.yonai` does not change CType
        /// (C still returns fd/int); LinearityChecker reads this via ImportTypeSource.
        bool return_linear = false;
        std::vector<char> tuple_elem_linear;
        std::vector<char> param_linear;
        /// Closed latent effect ops from the exporter (`Fs.read`). Empty if none.
        std::vector<std::string> effect_ops;
        bool effect_row_known = false;
        /// Open rest var (`effects |` / `effects Fs.read|`). Distinct from a missing field.
        bool effect_open_rest = false;
        /// First parameter is a function that shares this row (`effects … hof`).
        bool effect_hof = false;
    };
    ModuleFunctionMeta module_meta_from_compiled(const CompiledFunction& cf) const;
    CompiledFunction compiled_function_from_meta(llvm::Function* fn,
                                                 const ModuleFunctionMeta& meta,
                                                 CType return_type) const;

    struct EffectInfo {
        std::string name, type_param;
        struct OpSig { std::string name; int arg_count; };
        std::vector<OpSig> operations;
    };

    struct TypeRegistry {
        std::unordered_map<std::string, TraitInfo> traits;
        std::unordered_map<std::string, TraitInstanceInfo> trait_instances;
        std::unordered_map<std::string, AdtInfo> adt_constructors;
        std::unordered_map<std::string, llvm::StructType*> adt_struct_types;
        std::unordered_map<std::string, CFFISignature> cffi_signatures;
        std::unordered_map<std::string, EffectInfo> effects;
    } types_;

    // ===== Module System — imports, exports, cross-module =====
    struct ImportState {
        std::unordered_map<std::string, std::string> extern_functions;       // local → mangled
        std::unordered_map<std::string, std::string> function_source;        // mangled → source
        std::unordered_set<std::string> interface_symbols;                   // symbols owned by current module
        /// Unexported helpers referenced by exported GENFN bodies. Emitted as
        /// GENFN (not FN) so remonomorphization can compile them locally
        /// without making them public imports.
        std::unordered_set<std::string> private_genfn_symbols;
        std::unordered_map<std::string, ImportedFunctionSource> imported_sources;
        /// Constructor metadata needed only while recompiling an exported
        /// generic function. Opaque ADT constructors never enter the public
        /// constructor registry.
        std::unordered_map<std::string, std::vector<std::pair<std::string, AdtInfo>>> private_genfn_ctors;
        std::vector<std::unique_ptr<ast::FunctionExpr>> imported_ast_nodes;  // ownership
        std::unordered_map<std::string, ModuleFunctionMeta> meta;            // function metadata
    } imports_;

    // ===== Symbol Interning =====
    struct SymbolTable {
        std::unordered_map<std::string, int64_t> ids;
        std::vector<llvm::Constant*> strings;
    } symbols_;

    // ===== Debug Info =====
    struct DebugState {
        std::unique_ptr<llvm::DIBuilder> builder;
        llvm::DICompileUnit* cu = nullptr;
        llvm::DIFile* file = nullptr;
        llvm::DIScope* scope = nullptr;
        bool enabled = false;
    } debug_;

    std::string resolve_trait_method(const std::string& method_name, CType arg_type,
                                     const std::string& adt_type_name = "");
    static std::string ctype_to_type_name(CType ct);
    void register_cffi_signatures();
    static bool is_cffi_import(const std::string& mod_fqn);

    // All runtime function declarations grouped in a struct
    struct RuntimeDecls {
        // Printing
        llvm::Function *set_process_args_ = nullptr;
        llvm::Function *print_int_ = nullptr, *print_float_ = nullptr,
            *print_string_ = nullptr, *print_bool_ = nullptr, *print_newline_ = nullptr,
            *print_seq_ = nullptr, *print_symbol_ = nullptr, *print_set_ = nullptr,
            *print_dict_ = nullptr, *print_byte_array_ = nullptr,
            *print_int_array_ = nullptr, *print_float_array_ = nullptr;
        // Strings
        llvm::Function* string_concat_ = nullptr;
        llvm::Function* string_eq_ = nullptr;
        // Sequences
        llvm::Function *seq_alloc_ = nullptr, *seq_set_ = nullptr, *seq_get_ = nullptr,
            *seq_set_heap_ = nullptr,
            *seq_length_ = nullptr, *seq_cons_ = nullptr, *seq_join_ = nullptr,
            *seq_head_ = nullptr, *seq_tail_ = nullptr, *seq_tail_consume_ = nullptr,
            *seq_is_empty_ = nullptr, *seq_snoc_ = nullptr,
            *seq_contains_ = nullptr, *seq_difference_ = nullptr;
        // Sets
        llvm::Function *set_alloc_ = nullptr, *set_put_ = nullptr, *set_insert_ = nullptr,
            *set_set_heap_ = nullptr,
            *set_contains_ = nullptr, *set_size_ = nullptr, *set_elements_ = nullptr,
            *set_union_ = nullptr, *set_intersection_ = nullptr, *set_difference_ = nullptr;
        // Dicts
        llvm::Function *dict_alloc_ = nullptr, *dict_set_ = nullptr, *dict_put_ = nullptr,
            *dict_set_heap_ = nullptr,
            *dict_get_ = nullptr, *dict_size_ = nullptr, *dict_contains_ = nullptr,
            *dict_keys_ = nullptr;
        // ADTs
        llvm::Function *adt_alloc_ = nullptr, *adt_get_tag_ = nullptr,
            *adt_get_field_ = nullptr, *adt_set_field_ = nullptr, *adt_set_heap_mask_ = nullptr;
        // Async
        llvm::Function *async_call_ = nullptr, *async_call_thunk_ = nullptr,
            *async_await_ = nullptr, *async_await_keep_ = nullptr, *io_await_ = nullptr;
        // Task groups (structured concurrency)
        llvm::Function *group_begin_ = nullptr, *group_register_ = nullptr,
            *group_register_io_ = nullptr, *group_await_all_ = nullptr,
            *group_end_ = nullptr, *group_cancel_ = nullptr, *group_is_cancelled_ = nullptr,
            *async_call_grouped_ = nullptr, *async_call_thunk_grouped_ = nullptr,
            *group_attach_arena_ = nullptr,
            *group_arena_bind_push_ = nullptr, *group_arena_bind_pop_ = nullptr;
        // Exceptions
        llvm::Function *try_begin_ = nullptr, *try_end_ = nullptr, *raise_ = nullptr,
            *get_exc_sym_ = nullptr, *get_exc_msg_ = nullptr;
        // Perceus phase 3: frame-scoped heap cleanup on raise unwind
        llvm::Function *frame_push_ = nullptr, *frame_pop_ = nullptr,
            *frame_transfer_ = nullptr, *try_depth_ = nullptr;
        // Closures
        llvm::Function *closure_create_ = nullptr, *closure_set_cap_ = nullptr,
            *closure_get_cap_ = nullptr, *closure_set_heap_mask_ = nullptr,
            *closure2_create_ = nullptr, *closure2_apply_ = nullptr;
        // Tuples
        llvm::Function *tuple_alloc_ = nullptr, *tuple_set_ = nullptr,
            *tuple_set_heap_mask_ = nullptr;
        // Reference counting
        llvm::Function *rc_inc_ = nullptr, *rc_dec_ = nullptr;
        // Bytes
        llvm::Function *byte_array_alloc_ = nullptr, *byte_array_length_ = nullptr,
            *byte_array_get_ = nullptr, *byte_array_set_ = nullptr, *byte_array_concat_ = nullptr,
            *byte_array_slice_ = nullptr, *byte_array_from_string_ = nullptr, *byte_array_to_string_ = nullptr,
            *byte_array_from_seq_ = nullptr, *byte_array_to_seq_ = nullptr;
        // IntArray / FloatArray
        llvm::Function *int_array_alloc_ = nullptr, *int_array_length_ = nullptr,
            *int_array_get_ = nullptr, *int_array_set_ = nullptr,
            *int_array_head_ = nullptr, *int_array_tail_ = nullptr,
            *int_array_cons_ = nullptr, *int_array_join_ = nullptr;
        llvm::Function *float_array_alloc_ = nullptr, *float_array_length_ = nullptr,
            *float_array_get_ = nullptr, *float_array_set_ = nullptr,
            *float_array_head_ = nullptr, *float_array_tail_ = nullptr,
            *float_array_cons_ = nullptr, *float_array_join_ = nullptr;
        // Channels
        llvm::Function *channel_new_ = nullptr, *channel_send_ = nullptr,
            *channel_recv_ = nullptr, *channel_try_recv_ = nullptr,
            *channel_close_ = nullptr, *channel_is_closed_ = nullptr,
            *channel_length_ = nullptr, *channel_capacity_ = nullptr;
        // Misc
        llvm::Function *box_ = nullptr, *close_ = nullptr;
        // Arena
        llvm::Function *arena_create_ = nullptr, *arena_alloc_ = nullptr,
            *arena_destroy_ = nullptr;
    } rt_;

    int64_t intern_symbol(const std::string& name);
    int opt_level_ = 2;

    // Debug info helpers
    void init_debug_info(const std::string& filename);
    void finalize_debug_info();
    void set_debug_loc(const SourceLocation& loc);
    llvm::DIType* di_type_for(CType ct);
    llvm::DISubroutineType* di_func_type(const std::vector<CType>& param_types, CType ret_type);

    void init_target();
    void declare_runtime();

    // Get LLVM type for a CType
    llvm::Type* llvm_type(CType ct);

    // Top-level: wrap expression in main()
    llvm::Function* codegen_main(AstNode* node);

    // Core codegen — returns TypedValue
    TypedValue codegen(AstNode* node);

    // Literals
    TypedValue codegen_integer(IntegerExpr* node);
    TypedValue codegen_float(FloatExpr* node);
    TypedValue codegen_bool_true(TrueLiteralExpr* node);
    TypedValue codegen_bool_false(FalseLiteralExpr* node);
    TypedValue codegen_string(StringExpr* node);
    TypedValue codegen_unit(UnitExpr* node);
    TypedValue codegen_symbol(SymbolExpr* node);

    // Arithmetic (type-directed: int vs float dispatch)
    TypedValue codegen_binary(AstNode* left_node, AstNode* right_node,
                               const std::string& op);
    TypedValue codegen_comparison(AstNode* left_node, AstNode* right_node,
                                   const std::string& op);

    // Control flow
    TypedValue codegen_let(LetExpr* node);
    // codegen_let helpers
    std::unordered_set<std::string> analyze_let_escaping(LetExpr* node);
    llvm::Value* setup_let_arena(const std::unordered_set<std::string>& non_escaping);
    void codegen_let_aliases(LetExpr* node, llvm::Value* arena,
                              const std::unordered_set<std::string>& non_escaping,
                              std::vector<TypedValue>& scope_bindings,
                              std::vector<bool>& binding_is_arena);
    void cleanup_let_scope(const std::vector<TypedValue>& scope_bindings,
                            const std::vector<bool>& binding_is_arena,
                            const TypedValue& result, llvm::Value* arena,
                            bool destroy_arena_at_end = true);

    TypedValue codegen_if(IfExpr* node);
    TypedValue codegen_case(CaseExpr* node);
    // codegen_case pattern helpers — return true if body codegen was inlined
    bool codegen_pattern_value(PatternValue* pat, const TypedValue& scrutinee,
                                llvm::BasicBlock* body_bb, llvm::BasicBlock* next_bb);
    bool codegen_pattern_headtail(HeadTailsPattern* pat, CaseExpr* node,
                                   CaseClause* clause, const TypedValue& scrutinee,
                                   llvm::Value* seq_ptr,
                                   llvm::BasicBlock* body_bb, llvm::BasicBlock* next_bb);
    bool codegen_pattern_seq(SeqPattern* pat, const TypedValue& scrutinee,
                              llvm::BasicBlock* body_bb, llvm::BasicBlock* next_bb);
    bool codegen_pattern_tuple(TuplePattern* pat, const TypedValue& scrutinee,
                                llvm::BasicBlock* body_bb, llvm::BasicBlock* next_bb);
    bool codegen_pattern_constructor(ConstructorPattern* pat, const TypedValue& scrutinee,
                                      llvm::BasicBlock* body_bb, llvm::BasicBlock* next_bb);
    bool codegen_pattern_typed(TypedPattern* pat, const TypedValue& scrutinee,
                                llvm::BasicBlock* body_bb, llvm::BasicBlock* next_bb);
    /// Bind fields from a function parameter pattern after the caller has
    /// selected the matching function clause.
    void bind_parameter_pattern(PatternNode* pat, const TypedValue& value);

    /// Box a value as a sum type: creates a 2-tuple (type_tag, value).
    TypedValue box_as_sum(const TypedValue& value);

    /// Convert a type name string ("Int", "String", etc.) to a CType tag integer.
    static int ctype_tag(CType ct);
    /// Convert a type name string to CType.
    static CType type_name_to_ctype(const std::string& name);

    TypedValue codegen_do(DoExpr* node);
    TypedValue codegen_raise(RaiseExpr* node);
    TypedValue codegen_try_catch(TryCatchExpr* node);
    TypedValue codegen_with(WithExpr* node);

    // Algebraic effects
    TypedValue codegen_perform(PerformExpr* node);
    TypedValue codegen_handle(HandleExpr* node);

    /// Effect handler context — maps "Effect.op" to handler function pointers.
    struct HandlerContext {
        std::unordered_map<std::string, llvm::Value*> handler_closures;
    };
    std::vector<HandlerContext> handler_stack_;
    /// When true, `perform` without a lexical handler emits `yona_rt_raise`
    /// (`:UnhandledEffect`) instead of a compile error. Set while compiling
    /// exported module functions so effectful GENFN (e.g. `Std\GPU.raiseGpu`)
    /// can be precompiled; importers remonomorphize inside `handle`.
    bool compiling_unhandled_perform_ok_ = false;
    llvm::Value* current_group_ = nullptr; ///< Active task group for structured concurrency
    std::unordered_set<std::string> effect_resume_names_; ///< Names of resume fn ptr params

    // TCO: self-recursive tail call optimization
    std::string tco_fn_name_;                  ///< Current function name (if self-recursive)
    std::vector<std::string> tco_param_names_; ///< Parameter names for RC cleanup
    std::vector<CType> tco_param_ctypes_;      ///< Parameter CTypes
    std::vector<bool> tco_borrowed_params_;    ///< Borrowed params excluded from TCO cleanup
    bool tco_cleanup_done_ = false;            ///< Pre-tail-call cleanup already emitted

    // Identifiers
    TypedValue codegen_identifier(IdentifierExpr* node);
    TypedValue codegen_main_node(MainNode* node);

    // Functions (deferred compilation)
    TypedValue codegen_function_def(FunctionExpr* node, const std::string& name);
    TypedValue codegen_apply(ApplyExpr* node);
    TypedValue codegen_lambda_alias(LambdaAlias* node);
    TypedValue emit_accelerator_kernel(const yona::compiler::AccelMatch& match);
    bool accelerator_lowering_enabled_ = true;
    bool strict_accelerator_ = false;

    // codegen_apply helpers (extracted for readability)
    struct ApplyChain {
        std::string fn_name;
        std::string module_fqn;
        std::vector<ApplyExpr*> chain;
    };
    ApplyChain flatten_apply_chain(ApplyExpr* node);

    struct EvaluatedArgs {
        std::vector<TypedValue> all_args;
        std::vector<std::string> arg_lambda_names;
    };
    EvaluatedArgs evaluate_apply_args(const std::vector<ApplyExpr*>& chain);
    void precompile_function_args(EvaluatedArgs& args);
    void wrap_function_args_in_closures(std::vector<TypedValue>& all_args);

    TypedValue codegen_adt_construct(const std::string& fn_name, const std::vector<TypedValue>& all_args);
    std::unordered_map<std::string, CompiledFunction>::iterator
        resolve_apply_function(const std::string& fn_name, const std::vector<TypedValue>& all_args);
    TypedValue codegen_higher_order_call(const std::string& fn_name, const std::vector<TypedValue>& all_args);
    TypedValue codegen_extern_call(ApplyExpr* node, const std::string& fn_name,
                                    const std::vector<TypedValue>& all_args);
    TypedValue codegen_partial_apply(const std::string& fn_name, CompiledFunction& cf,
                                      const std::vector<TypedValue>& all_args);
    TypedValue codegen_curry_apply(const std::string& fn_name, CompiledFunction& cf,
                                    const std::vector<TypedValue>& all_args);
    void prepare_callee_owned_heap_args(const CompiledFunction& cf,
                                        const std::vector<TypedValue>& all_args);
    void cleanup_borrowed_temporary_args(const CompiledFunction& cf,
                                         const std::vector<TypedValue>& all_args);
    TypedValue emit_direct_call(const std::string& fn_name, CompiledFunction& cf,
                                 const std::vector<TypedValue>& all_args);

    // Compile a deferred function with known argument types
    CompiledFunction compile_function(const std::string& name,
                                       const DeferredFunction& def,
                                       const std::vector<TypedValue>& args);

    // Imports and extern declarations
    TypedValue codegen_import(ImportExpr* node);
    TypedValue codegen_extern_decl(ExternDeclExpr* node);
    std::pair<std::string, std::filesystem::path> build_fqn_path(FqnExpr* fqn);
    void load_module_interface(const std::filesystem::path& mod_path);
    void load_module_by_fqn(const std::string& mod_fqn);
    static typechecker::ImportedFnSig sig_from_meta(const ModuleFunctionMeta& meta);
    /// Parse a .yona stdlib source file and register its declarations into the
    /// current Codegen state. Functions become deferred and compile on demand
    /// at call sites in the importing program. Used as fallback when no
    /// pre-compiled .yonai interface exists.
    bool load_yona_module(const std::filesystem::path& yona_path);
    /// Register ADT/trait/instance/extern/function declarations from a parsed
    /// module into the current Codegen state. Does NOT emit IR or run verify
    /// (unlike compile_module). Functions are stored as deferred entries.
    void register_yona_module_decls(ast::ModuleDecl* mod);
    /// Loaded .yona stdlib modules — owned to keep AST nodes alive while
    /// deferred functions reference them.
    std::vector<std::unique_ptr<ast::ModuleDecl>> loaded_yona_modules_;
    /// Cache: which .yona files have already been loaded (absolute paths).
    std::unordered_set<std::string> loaded_yona_paths_;
    void register_import(const std::string& mod_fqn, const std::string& func_name, const std::string& import_name);
    void register_all_imports(const std::string& mod_fqn);
    /// Declare LLVM `Function*` for an imported AFN/IO/NAT symbol (sync FN returns nullptr).
    llvm::Function* declare_import_extern_fn(const std::string& mangled, const ModuleFunctionMeta& meta);
    /// Register `compiled_functions_` + `named_values_` for promise-style imports.
    void bind_imported_promise_cf(const std::string& logical_name, llvm::Function* fn,
                                   const ModuleFunctionMeta& meta);
    void register_trait_externs();
    /// Restrict name lookup to the defining module while remonomorphizing a
    /// GENFN. Importer aliases (`import length from Std\String`) must not
    /// shadow Prelude Array `length`/`get` used by `Std\Json.getPair`.
    struct GenfnNameIsolation {
        Codegen& cg;
        std::unordered_map<std::string, std::string> saved_externs;
        std::unordered_map<std::string, CompiledFunction> hidden_cfs;
        std::unordered_map<std::string, TypedValue> hidden_nvs;
        std::unordered_map<std::string, AdtInfo> saved_adt_constructors;
        std::vector<std::string> scoped_cafs;
        bool restored = false;
        /// `mangled` is taken by value: the constructor clears
        /// `extern_functions`, so a reference into that map would dangle.
        GenfnNameIsolation(Codegen& cg, std::string mangled);
        void restore();
        ~GenfnNameIsolation() { restore(); }
        GenfnNameIsolation(const GenfnNameIsolation&) = delete;
        GenfnNameIsolation& operator=(const GenfnNameIsolation&) = delete;
    };
    /// Register same-module GENFN siblings as deferred functions so a
    /// remonomorphized export can call unexported helpers.
    void register_sibling_genfns(const std::string& mangled);
    void install_private_genfn_ctors(const std::string& mangled);
    /// Build a first-class closure for an imported name used as a value.
    TypedValue materialize_imported_function_value(const std::string& name);
    TypedValue dummy_typed_value(CType ct);
    llvm::Type* adt_llvm_type(const std::string& type_name);
    std::unique_ptr<ast::ModuleDecl> reparse_genfn(const std::string& local_name, const std::string& source_text);

    // Collections
    TypedValue codegen_tuple(TupleExpr* node);
    TypedValue codegen_seq(ValuesSequenceExpr* node);
    TypedValue codegen_set(SetExpr* node);
    TypedValue codegen_dict(DictExpr* node);
    TypedValue codegen_cons(ConsLeftExpr* node);
    TypedValue codegen_cons_right(ConsRightExpr* node);
    TypedValue codegen_join(JoinExpr* node);
    TypedValue codegen_in(InExpr* node);
    TypedValue codegen_remove(RemoveExpr* node);

    // Generators / comprehensions
    TypedValue codegen_seq_generator(SeqGeneratorExpr* node);
    TypedValue codegen_fused_seq_generator(SeqGeneratorExpr* outer, SeqGeneratorExpr* inner);
    static int count_identifier_refs(ast::AstNode* node, const std::string& name);
    TypedValue codegen_set_generator(SetGeneratorExpr* node);
    TypedValue codegen_dict_generator(DictGeneratorExpr* node);

    // Auto-await: if a TypedValue is PROMISE, insert yona_rt_async_await
    TypedValue auto_await(TypedValue tv);

    // Wrap a Function* in a closure for uniform calling convention.
    // Generates an env-passing wrapper and creates a trivial closure {wrapper, ret_tag}.
    llvm::Value* wrap_in_closure(llvm::Function* fn, CType ret_type);

    // Free variable analysis
    static void collect_free_vars(AstNode* node,
                                   const std::unordered_set<std::string>& bound,
                                   std::unordered_set<std::string>& free_vars);

    // Infer parameter type from a function pattern node
    CType infer_type_from_pattern(PatternNode* pat);

    // Inferred parameter type with source pattern for struct layout
    struct InferredParamType {
        CType type = CType::INT;
        PatternNode* source_pattern = nullptr; // tuple/seq pattern for element types
        std::vector<CType> subtypes;
        std::vector<std::string> accessed_fields;
    };

    // Infer parameter types for a module function by analyzing patterns and body
    std::vector<InferredParamType> infer_param_types(FunctionExpr* func);

    // Reference counting helpers — emit rc_inc/rc_dec for heap-typed values
    static bool is_heap_type(CType ct);
    bool is_unboxed_enum_adt(const TypedValue& value) const;
    bool is_heap_value(const TypedValue& value) const;
    void emit_rc_inc(llvm::Value* val, CType type);
    void emit_rc_dec(llvm::Value* val, CType type);

    std::pair<llvm::Type*, CType> infer_return_type(ast::AstNode* body_expr);
    llvm::Value* emit_arena_alloc(int64_t type_tag, llvm::Value* payload_bytes);

    // Print a typed value (with newline) / value only (no newline)
    void codegen_print(const TypedValue& tv);
    void codegen_print_value(const TypedValue& tv);

    // Compile error reporting with source location
    void report_error(const SourceLocation& loc, const std::string& message);

    // "Did you mean?" suggestion for undefined names
    std::string suggest_similar(const std::string& name) const;

    compiler::DiagnosticEngine* diag_ = nullptr;        // external, not owned
    std::unique_ptr<compiler::DiagnosticEngine> owned_diag_; // fallback if none provided

public:
    int error_count_ = 0;
};

} // namespace yona::compiler::codegen
