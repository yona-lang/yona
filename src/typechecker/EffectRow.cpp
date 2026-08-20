/// Effect-row helpers for Hindley-Milner arrows (GitHub #8).

#include "typechecker/InferType.h"
#include "typechecker/UnionFind.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace yona::compiler::typechecker {

std::vector<std::string> normalize_effect_labels(std::vector<std::string> labels) {
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    return labels;
}

static std::string pretty_rest(MonoTypePtr rest) {
    if (!rest) return "";
    if (rest->tag == MonoType::Var)
        return std::to_string(rest->var_id); // caller pretty_print replaces via ctx
    if (rest->tag == MonoType::MEffectRow)
        return pretty_effect_row(rest->effect_labels, rest->effect_rest);
    return "?";
}

std::string pretty_effect_row(const std::vector<std::string>& labels, MonoTypePtr rest) {
    std::ostringstream os;
    os << "!{";
    for (size_t i = 0; i < labels.size(); i++) {
        if (i) os << ",";
        os << labels[i];
    }
    if (rest) {
        if (!labels.empty()) os << "|";
        else os << "|";
        // Var ids are rewritten by arrow pretty_print; raw fallback:
        if (rest->tag == MonoType::Var)
            os << "r" << rest->var_id;
        else
            os << pretty_rest(rest);
    }
    os << "}";
    return os.str();
}

static MonoTypePtr resolve_row(MonoTypePtr type, UnionFind* uf) {
    if (!type || !uf) return type;
    if (type->tag == MonoType::Var) {
        auto bound = uf->find(type->var_id);
        if (bound) return resolve_row(bound, uf);
    }
    return type;
}

static void collect_row_parts_rec(MonoTypePtr type, UnionFind* uf,
                                  std::optional<TypeId> skip_var,
                                  std::set<std::string>& labels,
                                  std::vector<MonoTypePtr>& open_rests,
                                  std::set<TypeId>& seen_vars) {
    type = resolve_row(type, uf);
    if (!type) return;
    if (type->tag == MonoType::Var) {
        if (skip_var && type->var_id == *skip_var) return;
        if (!seen_vars.insert(type->var_id).second) return;
        open_rests.push_back(type);
        return;
    }
    if (type->tag == MonoType::Arrow || type->tag == MonoType::MEffectRow) {
        for (auto& l : type->effect_labels) labels.insert(l);
        collect_row_parts_rec(type->effect_rest, uf, skip_var, labels, open_rests, seen_vars);
        if (type->tag == MonoType::MEffectRow) {
            for (auto* extra : type->args)
                collect_row_parts_rec(extra, uf, skip_var, labels, open_rests, seen_vars);
        }
    }
}

void collect_effect_row_parts(MonoTypePtr row, UnionFind* uf,
                              std::optional<TypeId> skip_var,
                              std::vector<std::string>& labels,
                              std::vector<MonoTypePtr>& open_rests) {
    std::set<std::string> lab_set(labels.begin(), labels.end());
    std::set<TypeId> seen;
    for (auto* r : open_rests) {
        if (r && r->tag == MonoType::Var) seen.insert(r->var_id);
    }
    collect_row_parts_rec(row, uf, skip_var, lab_set, open_rests, seen);
    labels.assign(lab_set.begin(), lab_set.end());
}

static void collect_labels_rec(MonoTypePtr type, UnionFind* uf, std::set<std::string>& out,
                               std::set<TypeId>& seen_vars) {
    if (!type) return;
    if (uf && type->tag == MonoType::Var) {
        auto bound = uf->find(type->var_id);
        if (bound) {
            collect_labels_rec(bound, uf, out, seen_vars);
            return;
        }
        if (!seen_vars.insert(type->var_id).second) return;
        return; // open rest — no concrete labels
    }
    if (type->tag == MonoType::Arrow || type->tag == MonoType::MEffectRow) {
        for (auto& l : type->effect_labels) out.insert(l);
        collect_labels_rec(type->effect_rest, uf, out, seen_vars);
        if (type->tag == MonoType::MEffectRow) {
            for (auto* extra : type->args)
                collect_labels_rec(extra, uf, out, seen_vars);
        }
    }
}

std::vector<std::string> collect_effect_labels(MonoTypePtr type, UnionFind* uf) {
    std::set<std::string> out;
    std::set<TypeId> seen;
    collect_labels_rec(type, uf, out, seen);
    return std::vector<std::string>(out.begin(), out.end());
}

void merge_effect_origins(std::unordered_map<std::string, SourceLocation>& dst,
                          const std::unordered_map<std::string, SourceLocation>& src) {
    for (auto& [label, loc] : src) {
        if (!loc.is_valid()) continue;
        dst.emplace(label, loc);
    }
}

static void collect_origins_rec(MonoTypePtr type, UnionFind* uf,
                                std::unordered_map<std::string, SourceLocation>& origins,
                                std::set<TypeId>& seen_vars) {
    type = resolve_row(type, uf);
    if (!type) return;
    if (type->tag == MonoType::Var) {
        if (!seen_vars.insert(type->var_id).second) return;
        return;
    }
    if (type->tag == MonoType::Arrow || type->tag == MonoType::MEffectRow) {
        merge_effect_origins(origins, type->effect_origins);
        collect_origins_rec(type->effect_rest, uf, origins, seen_vars);
        if (type->tag == MonoType::MEffectRow) {
            for (auto* extra : type->args)
                collect_origins_rec(extra, uf, origins, seen_vars);
        }
    }
}

void collect_effect_origins(MonoTypePtr type, UnionFind* uf,
                            std::unordered_map<std::string, SourceLocation>& origins) {
    std::set<TypeId> seen;
    collect_origins_rec(type, uf, origins, seen);
}

static std::string trim_sv(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return std::string(s.substr(b, e - b));
}

static bool parse_nonneg_int(std::string_view tok, int& out, int max_value) {
    if (tok.empty()) return false;
    unsigned long n = 0;
    for (char ch : tok) {
        auto c = static_cast<unsigned char>(ch);
        if (!std::isdigit(c)) return false;
        n = n * 10 + static_cast<unsigned>(c - '0');
        if (n > static_cast<unsigned long>(max_value)) return false;
    }
    out = static_cast<int>(n);
    return true;
}

static bool parse_rest_id(std::string_view tok, int& id) {
    if (tok.empty() || tok[0] != 'r') return false;
    if (tok.size() == 1) {
        id = 0;
        return true;
    }
    return parse_nonneg_int(tok.substr(1), id, 100000);
}

static bool parse_serialized_row(std::string_view spec, SerializedEffectRow& out) {
    out = {};
    std::string s = trim_sv(spec);
    if (s.empty()) return true;
    size_t pipe = s.find('|');
    std::string labels_part = pipe == std::string::npos ? s : s.substr(0, pipe);
    std::string rests_part = pipe == std::string::npos ? std::string() : s.substr(pipe);
    if (!labels_part.empty()) {
        size_t start = 0;
        while (start < labels_part.size()) {
            auto comma = labels_part.find(',', start);
            std::string lab = comma == std::string::npos
                ? labels_part.substr(start)
                : labels_part.substr(start, comma - start);
            lab = trim_sv(lab);
            if (!lab.empty()) out.labels.push_back(std::move(lab));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        out.labels = normalize_effect_labels(std::move(out.labels));
    }
    size_t i = 0;
    while (i < rests_part.size()) {
        if (rests_part[i] != '|') break;
        size_t j = i + 1;
        while (j < rests_part.size() && rests_part[j] != '|') j++;
        int id = 0;
        if (!parse_rest_id(std::string_view(rests_part).substr(i + 1, j - i - 1), id))
            return false;
        out.rest_ids.push_back(id);
        i = j;
    }
    return true;
}

std::string format_serialized_row(const SerializedEffectRow& row) {
    std::ostringstream os;
    for (size_t i = 0; i < row.labels.size(); i++) {
        if (i) os << ',';
        os << row.labels[i];
    }
    for (int id : row.rest_ids)
        os << "|r" << id;
    return os.str();
}

std::string format_fn_effects(const SerializedFnEffects& fx) {
    if (fx.empty()) return "";
    std::ostringstream os;
    os << format_serialized_row(fx.result);
    for (auto& [idx, row] : fx.params)
        os << ' ' << idx << ':' << format_serialized_row(row);
    return os.str();
}

static SerializedEffectRow row_from_arrow(MonoTypePtr arrow, UnionFind* uf,
                                          std::unordered_map<TypeId, int>& rest_ids) {
    SerializedEffectRow row;
    if (!arrow) return row;
    std::vector<MonoTypePtr> opens;
    row.labels = arrow->effect_labels;
    collect_effect_row_parts(arrow->effect_rest, uf, std::nullopt, row.labels, opens);
    if (arrow->tag == MonoType::MEffectRow) {
        for (auto* extra : arrow->args)
            collect_effect_row_parts(extra, uf, std::nullopt, row.labels, opens);
    }
    row.labels = normalize_effect_labels(std::move(row.labels));
    for (auto* rest : opens) {
        rest = resolve_row(rest, uf);
        if (!rest || rest->tag != MonoType::Var) continue;
        auto [it, inserted] = rest_ids.emplace(rest->var_id, static_cast<int>(rest_ids.size()));
        row.rest_ids.push_back(it->second);
    }
    return row;
}

SerializedFnEffects serialized_effects_from_arrow(MonoTypePtr ty, int arity, UnionFind* uf) {
    SerializedFnEffects fx;
    std::vector<MonoTypePtr> arrows;
    MonoTypePtr cur = ty;
    for (int i = 0; i < arity && cur; i++) {
        cur = resolve_row(cur, uf);
        if (!cur || cur->tag != MonoType::Arrow) break;
        arrows.push_back(cur);
        cur = resolve_row(cur->return_type, uf);
    }
    if (arrows.empty()) return fx;
    std::unordered_map<TypeId, int> rest_ids;
    fx.result = row_from_arrow(arrows.back(), uf, rest_ids);
    for (size_t i = 0; i < arrows.size(); i++) {
        MonoTypePtr param = resolve_row(arrows[i]->param_type, uf);
        if (!param || param->tag != MonoType::Arrow) continue;
        auto prow = row_from_arrow(param, uf, rest_ids);
        if (prow.labels.empty() && prow.rest_ids.empty()) continue;
        fx.params.emplace_back(static_cast<int>(i), std::move(prow));
    }
    return fx;
}

bool parse_fn_effects(std::string_view spec, SerializedFnEffects& out) {
    out = {};
    std::string s = trim_sv(spec);
    if (s.empty()) return true;
    std::istringstream iss(s);
    std::string tok;
    bool have_result = false;
    while (iss >> tok) {
        auto colon = tok.find(':');
        if (colon != std::string::npos && colon > 0 &&
            std::all_of(tok.begin(), tok.begin() + static_cast<std::ptrdiff_t>(colon),
                        [](unsigned char c) { return std::isdigit(c); })) {
            int idx = 0;
            if (!parse_nonneg_int(std::string_view(tok).substr(0, colon), idx, 10000))
                return false;
            SerializedEffectRow row;
            if (!parse_serialized_row(tok.substr(colon + 1), row))
                return false;
            out.params.emplace_back(idx, std::move(row));
        } else {
            if (have_result) return false;
            if (!parse_serialized_row(tok, out.result))
                return false;
            have_result = true;
        }
    }
    return true;
}

} // namespace yona::compiler::typechecker
