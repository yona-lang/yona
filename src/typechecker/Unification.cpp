/// Type unification for Hindley-Milner inference.
///
/// Core algorithm:
///  1. Resolve both types via union-find
///  2. If identical → success
///  3. If one is a variable → occurs check → bind
///  4. If both constructors → match constructor, unify args
///  5. Error otherwise

#include "typechecker/Unification.h"
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace yona::compiler::typechecker {

Unifier::Unifier(TypeArena& arena, UnionFind& uf, DiagnosticEngine& diag)
    : arena_(arena), uf_(uf), diag_(diag) {}

MonoTypePtr Unifier::resolve(MonoTypePtr type) {
    if (!type) return nullptr;
    if (type->tag == MonoType::Var) {
        auto bound = uf_.find(type->var_id);
        if (bound) return resolve(bound);
    }
    return type;
}

bool Unifier::unify(MonoTypePtr a, MonoTypePtr b, const SourceLocation& loc,
                     const std::string& context) {
    return unify_inner(resolve(a), resolve(b), loc, context);
}

bool Unifier::unify_inner(MonoTypePtr a, MonoTypePtr b, const SourceLocation& loc,
                           const std::string& context) {
    if (a == b) return true;
    if (!a || !b) return false;

    // Var on left: bind
    if (a->tag == MonoType::Var) {
        if (b->tag == MonoType::Var && a->var_id == b->var_id) return true;
        if (occurs_in(a->var_id, b)) {
            diag_.error(loc, ErrorCode::E0101, "infinite type: cannot construct " +
                        pretty_print(a) + " ~ " + pretty_print(b) +
                        (context.empty() ? "" : " " + context));
            return false;
        }
        adjust_levels(b, uf_.level(a->var_id));
        uf_.bind(a->var_id, b);
        return true;
    }

    // Var on right: bind
    if (b->tag == MonoType::Var) {
        if (occurs_in(b->var_id, a)) {
            diag_.error(loc, ErrorCode::E0101, "infinite type: cannot construct " +
                        pretty_print(b) + " ~ " + pretty_print(a) +
                        (context.empty() ? "" : " " + context));
            return false;
        }
        adjust_levels(a, uf_.level(b->var_id));
        uf_.bind(b->var_id, a);
        return true;
    }

    // Both concrete: must match structurally
    if (a->tag != b->tag) {
        diag_.error(loc, ErrorCode::E0100, "type mismatch: expected " + pretty_print(a) +
                    " but found " + pretty_print(b) +
                    (context.empty() ? "" : " " + context));
        return false;
    }

    switch (a->tag) {
        case MonoType::Con:
            if (a->con != b->con) {
                diag_.error(loc, ErrorCode::E0100, "type mismatch: expected " + pretty_print(a) +
                            " but found " + pretty_print(b) +
                            (context.empty() ? "" : " " + context));
                return false;
            }
            return true;

        case MonoType::Arrow:
            return unify(a->param_type, b->param_type, loc, context) &&
                   unify(a->return_type, b->return_type, loc, context) &&
                   unify_effect_rows(a->arrow_effects, a->effect_rest,
                                     b->arrow_effects, b->effect_rest, loc, context);

        case MonoType::ERow:
            return unify_effect_rows(a->arrow_effects, a->effect_rest,
                                     b->arrow_effects, b->effect_rest, loc, context);

        case MonoType::App:
            if (a->type_name != b->type_name || a->args.size() != b->args.size()) {
                diag_.error(loc, ErrorCode::E0100, "type mismatch: " + pretty_print(a) +
                            " vs " + pretty_print(b) +
                            (context.empty() ? "" : " " + context));
                return false;
            }
            for (size_t i = 0; i < a->args.size(); i++) {
                if (!unify(a->args[i], b->args[i], loc, context)) return false;
            }
            return true;

        case MonoType::MTuple:
            if (a->elements.size() != b->elements.size()) {
                diag_.error(loc, ErrorCode::E0102, "tuple size mismatch: " +
                            std::to_string(a->elements.size()) + " vs " +
                            std::to_string(b->elements.size()) +
                            (context.empty() ? "" : " " + context));
                return false;
            }
            for (size_t i = 0; i < a->elements.size(); i++) {
                if (!unify(a->elements[i], b->elements[i], loc, context)) return false;
            }
            return true;

        case MonoType::MRecord: {
            // Row unification: match common fields, propagate extras
            // Collect fields from both sides
            std::unordered_map<std::string, MonoTypePtr> a_fields, b_fields;
            for (auto& [name, type] : a->record_fields) a_fields[name] = type;
            for (auto& [name, type] : b->record_fields) b_fields[name] = type;

            // Unify common fields
            for (auto& [name, a_type] : a_fields) {
                auto it = b_fields.find(name);
                if (it != b_fields.end()) {
                    if (!unify(a_type, it->second, loc, context)) return false;
                }
            }

            // Extra fields in a but not b: b must have an open row to absorb them
            std::vector<std::pair<std::string, MonoTypePtr>> a_extras, b_extras;
            for (auto& [name, type] : a_fields)
                if (b_fields.find(name) == b_fields.end()) a_extras.push_back({name, type});
            for (auto& [name, type] : b_fields)
                if (a_fields.find(name) == a_fields.end()) b_extras.push_back({name, type});

            // If a has extras, b must have an open row variable to absorb them
            if (!a_extras.empty() && b->row_rest) {
                auto* extra_record = arena_.make_record(a_extras, a->row_rest);
                if (!unify(b->row_rest, extra_record, loc, context)) return false;
            } else if (!a_extras.empty() && !b->row_rest) {
                diag_.error(loc, ErrorCode::E0100, "record has extra field(s) not expected" +
                    (context.empty() ? "" : " " + context));
                return false;
            }

            if (!b_extras.empty() && a->row_rest) {
                auto* extra_record = arena_.make_record(b_extras, b->row_rest);
                if (!unify(a->row_rest, extra_record, loc, context)) return false;
            } else if (!b_extras.empty() && !a->row_rest) {
                diag_.error(loc, ErrorCode::E0100, "record missing field(s)" +
                    (context.empty() ? "" : " " + context));
                return false;
            }

            // If both have row rest, unify them
            if (a->row_rest && b->row_rest && a_extras.empty() && b_extras.empty())
                unify(a->row_rest, b->row_rest, loc, context);

            return true;
        }

        default:
            return false;
    }
}

void Unifier::flatten_effect_row(MonoTypePtr rest, std::vector<LatentEffect>& labs,
                                  MonoTypePtr& out_rest) {
    rest = resolve(rest);
    int guard = 0;
    while (rest && rest->tag == MonoType::ERow && guard++ < 64) {
        for (auto& e : rest->arrow_effects) {
            bool seen = false;
            for (auto& l : labs)
                if (l.op_key == e.op_key) { seen = true; break; }
            if (!seen) labs.push_back(e);
        }
        rest = resolve(rest->effect_rest);
    }
    out_rest = (rest && rest->tag == MonoType::Var) ? rest : nullptr;
}

bool Unifier::unify_effect_rows(const std::vector<LatentEffect>& a_labs_in, MonoTypePtr a_rest,
                                 const std::vector<LatentEffect>& b_labs_in, MonoTypePtr b_rest,
                                 const SourceLocation& loc, const std::string& context) {
    std::vector<LatentEffect> a_labs = a_labs_in;
    std::vector<LatentEffect> b_labs = b_labs_in;
    flatten_effect_row(a_rest, a_labs, a_rest);
    flatten_effect_row(b_rest, b_labs, b_rest);

    std::unordered_map<std::string, LatentEffect> a_map, b_map;
    for (auto& e : a_labs) a_map[e.op_key] = e;
    for (auto& e : b_labs) b_map[e.op_key] = e;

    std::vector<LatentEffect> a_extras, b_extras;
    for (auto& [k, e] : a_map)
        if (b_map.find(k) == b_map.end()) a_extras.push_back(e);
    for (auto& [k, e] : b_map)
        if (a_map.find(k) == a_map.end()) b_extras.push_back(e);

    if (!a_extras.empty()) {
        if (b_rest) {
            if (!unify(b_rest, arena_.make_erow(a_extras, a_rest), loc, context))
                return false;
        } else {
            diag_.error(loc, ErrorCode::E0100,
                        "incompatible effect rows" + (context.empty() ? "" : " " + context));
            return false;
        }
    }
    if (!b_extras.empty()) {
        if (a_rest) {
            if (!unify(a_rest, arena_.make_erow(b_extras, b_rest), loc, context))
                return false;
        } else {
            diag_.error(loc, ErrorCode::E0100,
                        "incompatible effect rows" + (context.empty() ? "" : " " + context));
            return false;
        }
    }
    if (a_extras.empty() && b_extras.empty() && a_rest && b_rest)
        return unify(a_rest, b_rest, loc, context);
    return true;
}

bool Unifier::occurs_in(TypeId var_id, MonoTypePtr type) {
    type = resolve(type);
    if (!type) return false;
    if (type->tag == MonoType::Var) return type->var_id == var_id;
    if (type->tag == MonoType::Arrow)
        return occurs_in(var_id, type->param_type) || occurs_in(var_id, type->return_type)
            || occurs_in(var_id, type->effect_rest);
    if (type->tag == MonoType::ERow)
        return occurs_in(var_id, type->effect_rest);
    if (type->tag == MonoType::App) {
        for (auto* a : type->args) if (occurs_in(var_id, a)) return true;
        return false;
    }
    if (type->tag == MonoType::MTuple) {
        for (auto* e : type->elements) if (occurs_in(var_id, e)) return true;
        return false;
    }
    if (type->tag == MonoType::MRecord) {
        for (auto& [_, ft] : type->record_fields) if (occurs_in(var_id, ft)) return true;
        if (type->row_rest && occurs_in(var_id, type->row_rest)) return true;
        return false;
    }
    return false;
}

void Unifier::adjust_levels(MonoTypePtr type, int level) {
    type = resolve(type);
    if (!type) return;
    if (type->tag == MonoType::Var) {
        if (uf_.level(type->var_id) > level)
            uf_.set_level(type->var_id, level);
        return;
    }
    if (type->tag == MonoType::Arrow) {
        adjust_levels(type->param_type, level);
        adjust_levels(type->return_type, level);
        adjust_levels(type->effect_rest, level);
    }
    if (type->tag == MonoType::ERow)
        adjust_levels(type->effect_rest, level);
    if (type->tag == MonoType::App)
        for (auto* a : type->args) adjust_levels(a, level);
    if (type->tag == MonoType::MTuple)
        for (auto* e : type->elements) adjust_levels(e, level);
    if (type->tag == MonoType::MRecord) {
        for (auto& [_, ft] : type->record_fields) adjust_levels(ft, level);
        if (type->row_rest) adjust_levels(type->row_rest, level);
    }
}

// Convert a var_id to a user-facing name: a, b, ..., z, a1, b1, ...
// The id → name map is unique per top-level pretty_print call so the same
// variable renders the same way within one error message. Internal
// counters (which can be large / non-contiguous) never leak into
// diagnostics.
namespace {
struct PrintCtx {
    std::unordered_map<int, std::string> names;
    std::string name_for(int var_id) {
        auto it = names.find(var_id);
        if (it != names.end()) return it->second;
        int n = static_cast<int>(names.size());
        std::string nm;
        nm += static_cast<char>('a' + (n % 26));
        if (n >= 26) nm += std::to_string(n / 26);
        names[var_id] = nm;
        return nm;
    }
};

std::string pretty_print_rec(MonoTypePtr type, PrintCtx& ctx) {
    if (!type) return "?";
    switch (type->tag) {
        case MonoType::Var: return ctx.name_for(type->var_id);
        case MonoType::Con: {
            switch (type->con) {
                case TyCon::Int: return "Int";
                case TyCon::Float: return "Float";
                case TyCon::Bool: return "Bool";
                case TyCon::String: return "String";
                case TyCon::Char: return "Char";
                case TyCon::Byte: return "Byte";
                case TyCon::Symbol: return "Symbol";
                case TyCon::Unit: return "()";
                case TyCon::Seq: return "Seq";
                case TyCon::Set: return "Set";
                case TyCon::Dict: return "Dict";
                case TyCon::Tuple: return "Tuple";
                case TyCon::Function: return "Function";
                case TyCon::Promise: return "Promise";
                case TyCon::ByteArray: return "ByteArray";
            }
            return "?";
        }
        case MonoType::Arrow: {
            std::vector<std::string> labels;
            for (auto& e : type->arrow_effects) labels.push_back(e.op_key);
            MonoTypePtr rest = type->effect_rest;
            int guard = 0;
            while (rest && rest->tag == MonoType::ERow && guard++ < 64) {
                for (auto& e : rest->arrow_effects) labels.push_back(e.op_key);
                rest = rest->effect_rest;
            }
            std::sort(labels.begin(), labels.end());
            labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
            std::string row;
            if (!labels.empty() || (rest && rest->tag == MonoType::Var)) {
                row = "!{";
                for (size_t i = 0; i < labels.size(); i++) {
                    if (i) row += ",";
                    row += labels[i];
                }
                if (rest && rest->tag == MonoType::Var) {
                    if (!labels.empty()) row += " | ";
                    else row += "|";
                    row += pretty_print_rec(rest, ctx);
                }
                row += "} ";
            }
            return "(" + pretty_print_rec(type->param_type, ctx) + " -> " + row +
                   pretty_print_rec(type->return_type, ctx) + ")";
        }
        case MonoType::ERow: {
            std::string s = "!{";
            for (size_t i = 0; i < type->arrow_effects.size(); i++) {
                if (i) s += ",";
                s += type->arrow_effects[i].op_key;
            }
            if (type->effect_rest) {
                if (!type->arrow_effects.empty()) s += " | ";
                s += pretty_print_rec(type->effect_rest, ctx);
            }
            return s + "}";
        }
        case MonoType::App: {
            std::string s = type->type_name;
            for (auto* a : type->args) s += " " + pretty_print_rec(a, ctx);
            return s;
        }
        case MonoType::MTuple: {
            std::string s = "(";
            for (size_t i = 0; i < type->elements.size(); i++) {
                if (i > 0) s += ", ";
                s += pretty_print_rec(type->elements[i], ctx);
            }
            return s + ")";
        }
        case MonoType::MRecord: {
            std::string s = "{ ";
            for (size_t i = 0; i < type->record_fields.size(); i++) {
                if (i > 0) s += ", ";
                s += type->record_fields[i].first + " : " + pretty_print_rec(type->record_fields[i].second, ctx);
            }
            if (type->row_rest) {
                if (!type->record_fields.empty()) s += " | ";
                s += pretty_print_rec(type->row_rest, ctx);
            }
            return s + " }";
        }
        default: return "?";
    }
}
} // namespace

// Pretty-print a type for error messages
std::string pretty_print(MonoTypePtr type) {
    PrintCtx ctx;
    return pretty_print_rec(type, ctx);
}

} // namespace yona::compiler::typechecker
