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
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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
    if (a->tag == MonoType::Var)
        return bind_var(a, b, loc, context);

    // Var on right: bind
    if (b->tag == MonoType::Var)
        return bind_var(b, a, loc, context);

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
                   unify_effect_rows(a, b, loc, context);

        case MonoType::MEffectRow:
            return unify_effect_rows(a, b, loc, context);

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

bool Unifier::bind_var(MonoTypePtr var, MonoTypePtr type, const SourceLocation& loc,
                       const std::string& context) {
    if (type->tag == MonoType::Var && var->var_id == type->var_id) return true;
    if (occurs_in(var->var_id, type)) {
        // Least fixed point for effect rows: r ~ !{L | r}  ⇒  r := !{L}
        // (and r ~ !{L | r, ρ} ⇒ r := !{L | ρ}). Value-position occurs stay E0101.
        if (!occurs_in_value(var->var_id, type) &&
            (type->tag == MonoType::MEffectRow || type->tag == MonoType::Var)) {
            auto* closed = close_effect_occurs(var->var_id, type);
            adjust_levels(closed, uf_.level(var->var_id));
            uf_.bind(var->var_id, closed);
            return true;
        }
        diag_.error(loc, ErrorCode::E0101, "infinite type: cannot construct " +
                    pretty_print(var) + " ~ " + pretty_print(type) +
                    (context.empty() ? "" : " " + context));
        return false;
    }
    adjust_levels(type, uf_.level(var->var_id));
    uf_.bind(var->var_id, type);
    return true;
}

MonoTypePtr Unifier::close_effect_occurs(TypeId var_id, MonoTypePtr row) {
    std::vector<std::string> labels;
    std::vector<MonoTypePtr> rests;
    collect_effect_row_parts(row, &uf_, var_id, labels, rests);
    std::unordered_map<std::string, SourceLocation> origins;
    collect_effect_origins(row, &uf_, origins);
    return arena_.make_effect_row(std::move(labels), arena_.pack_effect_rest(rests), {},
                                  std::move(origins));
}

bool Unifier::unify_effect_rows(MonoTypePtr a, MonoTypePtr b,
                                const SourceLocation& loc, const std::string& context) {
    std::vector<std::string> a_all = a ? a->effect_labels : std::vector<std::string>{};
    std::vector<std::string> b_all = b ? b->effect_labels : std::vector<std::string>{};
    MonoTypePtr a_rest = a ? a->effect_rest : nullptr;
    MonoTypePtr b_rest = b ? b->effect_rest : nullptr;
    std::vector<MonoTypePtr> a_open, b_open;
    collect_effect_row_parts(a_rest, &uf_, std::nullopt, a_all, a_open);
    collect_effect_row_parts(b_rest, &uf_, std::nullopt, b_all, b_open);
    if (a && a->tag == MonoType::MEffectRow) {
        for (auto* extra : a->args)
            collect_effect_row_parts(extra, &uf_, std::nullopt, a_all, a_open);
    }
    if (b && b->tag == MonoType::MEffectRow) {
        for (auto* extra : b->args)
            collect_effect_row_parts(extra, &uf_, std::nullopt, b_all, b_open);
    }
    a_rest = arena_.pack_effect_rest(a_open);
    b_rest = arena_.pack_effect_rest(b_open);

    std::unordered_map<std::string, SourceLocation> a_origins;
    std::unordered_map<std::string, SourceLocation> b_origins;
    collect_effect_origins(a, &uf_, a_origins);
    collect_effect_origins(b, &uf_, b_origins);

    std::unordered_set<std::string> a_set(a_all.begin(), a_all.end());
    std::unordered_set<std::string> b_set(b_all.begin(), b_all.end());

    std::vector<std::string> a_only, b_only;
    for (auto& l : a_all)
        if (!b_set.count(l)) a_only.push_back(l);
    for (auto& l : b_all)
        if (!a_set.count(l)) b_only.push_back(l);

    auto origins_for = [](const std::vector<std::string>& labels,
                          const std::unordered_map<std::string, SourceLocation>& src) {
        std::unordered_map<std::string, SourceLocation> out;
        for (auto& l : labels) {
            auto it = src.find(l);
            if (it != src.end()) out.emplace(l, it->second);
        }
        return out;
    };

    if (!a_only.empty()) {
        if (b_rest) {
            auto* extra = arena_.make_effect_row(a_only, a_rest, {}, origins_for(a_only, a_origins));
            if (!unify(b_rest, extra, loc, context.empty() ? "in effect row" : context))
                return false;
        } else {
            diag_.error(loc, ErrorCode::E0100,
                        "effect row mismatch: extra " + pretty_effect_row(a_only, nullptr) +
                        (context.empty() ? "" : " " + context));
            return false;
        }
    }
    if (!b_only.empty()) {
        if (a_rest) {
            auto* extra = arena_.make_effect_row(b_only, b_rest, {}, origins_for(b_only, b_origins));
            if (!unify(a_rest, extra, loc, context.empty() ? "in effect row" : context))
                return false;
        } else {
            diag_.error(loc, ErrorCode::E0100,
                        "effect row mismatch: extra " + pretty_effect_row(b_only, nullptr) +
                        (context.empty() ? "" : " " + context));
            return false;
        }
    }
    if (a_only.empty() && b_only.empty() && a_rest && b_rest)
        return unify(a_rest, b_rest, loc, context.empty() ? "in effect row rest" : context);
    return true;
}

bool Unifier::occurs_in(TypeId var_id, MonoTypePtr type) {
    type = resolve(type);
    if (!type) return false;
    if (type->tag == MonoType::Var) return type->var_id == var_id;
    if (type->tag == MonoType::Arrow)
        return occurs_in(var_id, type->param_type) || occurs_in(var_id, type->return_type) ||
               occurs_in(var_id, type->effect_rest);
    if (type->tag == MonoType::MEffectRow) {
        if (occurs_in(var_id, type->effect_rest)) return true;
        for (auto* extra : type->args)
            if (occurs_in(var_id, extra)) return true;
        return false;
    }
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

bool Unifier::occurs_in_value(TypeId var_id, MonoTypePtr type) {
    type = resolve(type);
    if (!type) return false;
    if (type->tag == MonoType::Var) return type->var_id == var_id;
    if (type->tag == MonoType::MEffectRow) return false;
    if (type->tag == MonoType::Arrow)
        return occurs_in_value(var_id, type->param_type) ||
               occurs_in_value(var_id, type->return_type);
    if (type->tag == MonoType::App) {
        for (auto* a : type->args) if (occurs_in_value(var_id, a)) return true;
        return false;
    }
    if (type->tag == MonoType::MTuple) {
        for (auto* e : type->elements) if (occurs_in_value(var_id, e)) return true;
        return false;
    }
    if (type->tag == MonoType::MRecord) {
        for (auto& [_, ft] : type->record_fields)
            if (occurs_in_value(var_id, ft)) return true;
        if (type->row_rest && occurs_in_value(var_id, type->row_rest)) return true;
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
        if (type->effect_rest) adjust_levels(type->effect_rest, level);
    }
    if (type->tag == MonoType::MEffectRow) {
        if (type->effect_rest) adjust_levels(type->effect_rest, level);
        for (auto* extra : type->args) adjust_levels(extra, level);
    }
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
            std::string s = "(" + pretty_print_rec(type->param_type, ctx) + " -> ";
            bool has_fx = !type->effect_labels.empty() || type->effect_rest;
            if (has_fx) {
                s += "!{";
                for (size_t i = 0; i < type->effect_labels.size(); i++) {
                    if (i) s += ",";
                    s += type->effect_labels[i];
                }
                if (type->effect_rest) {
                    s += "|";
                    s += pretty_print_rec(type->effect_rest, ctx);
                }
                s += "} ";
            }
            s += pretty_print_rec(type->return_type, ctx) + ")";
            return s;
        }
        case MonoType::MEffectRow: {
            std::string s = "!{";
            for (size_t i = 0; i < type->effect_labels.size(); i++) {
                if (i) s += ",";
                s += type->effect_labels[i];
            }
            if (type->effect_rest || !type->args.empty()) {
                s += "|";
                bool first = true;
                if (type->effect_rest) {
                    s += pretty_print_rec(type->effect_rest, ctx);
                    first = false;
                }
                for (auto* extra : type->args) {
                    if (!first) s += ",";
                    s += pretty_print_rec(extra, ctx);
                    first = false;
                }
            }
            s += "}";
            return s;
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
