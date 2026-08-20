#ifndef YONA_TYPECHECKER_INFER_TYPE_H
#define YONA_TYPECHECKER_INFER_TYPE_H

/// Type representations for Hindley-Milner inference.
///
/// MonoType — monomorphic type (may contain unification variables).
/// TypeScheme — polymorphic type (forall a b. constraints => body).
/// All MonoType nodes are arena-allocated via TypeArena.

#include "SourceLocation.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yona::compiler::typechecker {

using TypeId = uint32_t;

/// Built-in type constructors.
enum class TyCon {
    Int, Float, Bool, String, Char, Byte, Symbol, Unit,
    Seq, Set, Dict, Tuple, Function, Promise, ByteArray
};

/// A monomorphic type node in the inference system.
/// Allocated by TypeArena, referenced by pointer (stable, never moved).
struct MonoType {
    enum Tag {
        Var,        ///< Unification variable
        Con,        ///< Built-in type constructor
        App,        ///< Named type application: App("Option", [Int])
        Arrow,      ///< Function type: Arrow(param, ret) with latent effect row
        MTuple,     ///< Product type: Tuple([Int, String])
        MRecord,    ///< Record type: { name : String, age : Int | r }
        MEffectRow, ///< Effect row value used when binding open effect rests
    } tag;

    // Var
    TypeId var_id = 0;
    int level = 0;

    // Con
    TyCon con = TyCon::Int;

    // App
    std::string type_name;
    std::vector<const MonoType*> args;

    // Arrow
    const MonoType* param_type = nullptr;
    const MonoType* return_type = nullptr;

    // MTuple
    std::vector<const MonoType*> elements;

    // MRecord: sorted (name, type) pairs + optional row rest variable
    std::vector<std::pair<std::string, const MonoType*>> record_fields;
    const MonoType* row_rest = nullptr; // row variable (Var) or nullptr (closed row)

    // Arrow + MEffectRow: sorted unique "Effect.op" labels; effect_rest is
    // Var / MEffectRow / nullptr (closed). Empty closed row = pure.
    std::vector<std::string> effect_labels;
    const MonoType* effect_rest = nullptr;
    /// Introducing `perform` (or first join) for each concrete label.
    std::unordered_map<std::string, SourceLocation> effect_origins;

    /// Create a Var type
    static MonoType make_var(TypeId id, int lvl) {
        MonoType t; t.tag = Var; t.var_id = id; t.level = lvl; return t;
    }
    /// Create a Con type
    static MonoType make_con(TyCon c) {
        MonoType t; t.tag = Con; t.con = c; return t;
    }
    /// Create an Arrow type (optional latent effect row)
    static MonoType make_arrow(const MonoType* p, const MonoType* r,
                               std::vector<std::string> effects = {},
                               const MonoType* effect_rest = nullptr,
                               std::unordered_map<std::string, SourceLocation> origins = {}) {
        MonoType t;
        t.tag = Arrow;
        t.param_type = p;
        t.return_type = r;
        t.effect_labels = std::move(effects);
        t.effect_rest = effect_rest;
        t.effect_origins = std::move(origins);
        return t;
    }
    /// Create an App type
    static MonoType make_app(const std::string& name, std::vector<const MonoType*> a) {
        MonoType t; t.tag = App; t.type_name = name; t.args = std::move(a); return t;
    }
    /// Create a Tuple type
    static MonoType make_tuple(std::vector<const MonoType*> elems) {
        MonoType t; t.tag = MTuple; t.elements = std::move(elems); return t;
    }
    /// Create a Record type (closed or open row)
    static MonoType make_record(std::vector<std::pair<std::string, const MonoType*>> fields,
                                 const MonoType* rest = nullptr) {
        MonoType t; t.tag = MRecord; t.record_fields = std::move(fields); t.row_rest = rest; return t;
    }
    /// Create an effect-row binder (for open-row unification)
    static MonoType make_effect_row(std::vector<std::string> labels,
                                    const MonoType* rest = nullptr,
                                    std::unordered_map<std::string, SourceLocation> origins = {}) {
        MonoType t;
        t.tag = MEffectRow;
        t.effect_labels = std::move(labels);
        t.effect_rest = rest;
        t.effect_origins = std::move(origins);
        return t;
    }
};

using MonoTypePtr = const MonoType*;

/// Sort + unique effect operation labels (`Effect.op`).
std::vector<std::string> normalize_effect_labels(std::vector<std::string> labels);

/// Pretty-print an effect row: `!{}`, `!{State.get}`, `!{Gpu.oom|a}`.
std::string pretty_effect_row(const std::vector<std::string>& labels, MonoTypePtr rest);

/// Flatten concrete labels from an arrow / effect-row (resolve open rests).
std::vector<std::string> collect_effect_labels(MonoTypePtr type, class UnionFind* uf = nullptr);

/// Split a row / rest chain into concrete labels and unbound rest variables.
/// `skip_var` is omitted (used for effect-row occurs / least fixed point).
void collect_effect_row_parts(MonoTypePtr row, class UnionFind* uf,
                              std::optional<TypeId> skip_var,
                              std::vector<std::string>& labels,
                              std::vector<MonoTypePtr>& open_rests);

/// Merge introducing locations (first write wins).
void merge_effect_origins(std::unordered_map<std::string, SourceLocation>& dst,
                          const std::unordered_map<std::string, SourceLocation>& src);

/// Collect introducing locations from an arrow / effect-row (follow open rests).
void collect_effect_origins(MonoTypePtr type, class UnionFind* uf,
                            std::unordered_map<std::string, SourceLocation>& origins);

/// One `.yonai` effect row: labels plus optional open rest ids (`|r0`).
struct SerializedEffectRow {
    std::vector<std::string> labels;
    std::vector<int> rest_ids;
    bool open() const { return !rest_ids.empty(); }
};

/// Function-level `effects` trailer: result row plus per-parameter rows.
struct SerializedFnEffects {
    SerializedEffectRow result;
    std::vector<std::pair<int, SerializedEffectRow>> params;
    bool empty() const {
        return result.labels.empty() && result.rest_ids.empty() && params.empty();
    }
};

std::string format_serialized_row(const SerializedEffectRow& row);
std::string format_fn_effects(const SerializedFnEffects& fx);
bool parse_fn_effects(std::string_view spec, SerializedFnEffects& out);

/// Encode the curried arrow's result row and function-typed parameter rows.
/// `ty` should already be zonked. Walks exactly `arity` arrows.
SerializedFnEffects serialized_effects_from_arrow(MonoTypePtr ty, int arity,
                                                  class UnionFind* uf = nullptr);

/// A trait constraint: e.g., Num a
struct Constraint {
    std::string trait_name;
    MonoTypePtr type;
};

/// Polymorphic type scheme: forall vars. constraints => body
struct TypeScheme {
    std::vector<TypeId> quantified_vars;
    std::vector<Constraint> constraints;
    MonoTypePtr body = nullptr;

    /// Monomorphic scheme (no quantification)
    explicit TypeScheme(MonoTypePtr t) : body(t) {}
    TypeScheme() = default;
    TypeScheme(std::vector<TypeId> qv, MonoTypePtr b)
        : quantified_vars(std::move(qv)), body(b) {}
    TypeScheme(std::vector<TypeId> qv, std::vector<Constraint> c, MonoTypePtr b)
        : quantified_vars(std::move(qv)), constraints(std::move(c)), body(b) {}
};

/// Pretty-print a monotype for error messages
std::string pretty_print(MonoTypePtr type);

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_INFER_TYPE_H
