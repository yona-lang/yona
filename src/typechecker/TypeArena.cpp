#include "typechecker/TypeArena.h"
#include <set>

namespace yona::compiler::typechecker {

MonoTypePtr TypeArena::alloc(MonoType t) {
    storage_.push_back(std::move(t));
    return &storage_.back();
}

MonoTypePtr TypeArena::fresh_var(int level) {
    return alloc(MonoType::make_var(next_id_++, level));
}

MonoTypePtr TypeArena::make_con(TyCon con) {
    return alloc(MonoType::make_con(con));
}

MonoTypePtr TypeArena::make_arrow(MonoTypePtr param, MonoTypePtr ret,
                                  std::vector<std::string> effect_labels,
                                  MonoTypePtr effect_rest,
                                  std::unordered_map<std::string, SourceLocation> origins) {
    return alloc(MonoType::make_arrow(param, ret, normalize_effect_labels(std::move(effect_labels)),
                                      effect_rest, std::move(origins)));
}

MonoTypePtr TypeArena::make_effect_row(std::vector<std::string> labels, MonoTypePtr rest,
                                       std::vector<MonoTypePtr> extra_rests,
                                       std::unordered_map<std::string, SourceLocation> origins) {
    MonoType t = MonoType::make_effect_row(normalize_effect_labels(std::move(labels)), rest,
                                           std::move(origins));
    t.args = std::move(extra_rests);
    return alloc(std::move(t));
}

MonoTypePtr TypeArena::pack_effect_rest(const std::vector<MonoTypePtr>& open_rests) {
    std::vector<MonoTypePtr> unique;
    std::set<TypeId> seen;
    for (auto* r : open_rests) {
        if (!r) continue;
        if (r->tag == MonoType::Var) {
            if (!seen.insert(r->var_id).second) continue;
        }
        unique.push_back(r);
    }
    if (unique.empty()) return nullptr;
    if (unique.size() == 1) return unique[0];
    return make_effect_row({}, unique[0],
                           std::vector<MonoTypePtr>(unique.begin() + 1, unique.end()));
}

MonoTypePtr TypeArena::make_app(const std::string& name, std::vector<MonoTypePtr> args) {
    return alloc(MonoType::make_app(name, std::move(args)));
}

MonoTypePtr TypeArena::make_tuple(std::vector<MonoTypePtr> elems) {
    return alloc(MonoType::make_tuple(std::move(elems)));
}

MonoTypePtr TypeArena::make_record(std::vector<std::pair<std::string, MonoTypePtr>> fields,
                                    MonoTypePtr row_rest) {
    return alloc(MonoType::make_record(std::move(fields), row_rest));
}

} // namespace yona::compiler::typechecker
