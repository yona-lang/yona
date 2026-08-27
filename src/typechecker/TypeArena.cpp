#include "typechecker/TypeArena.h"

namespace yona::compiler::typechecker {

MonoTypePtr TypeArena::alloc(MonoType t) {
  storage_.push_back(std::move(t));
  return &storage_.back();
}

MonoTypePtr TypeArena::fresh_var(int level) { return alloc(MonoType::make_var(next_id_++, level)); }

MonoTypePtr TypeArena::make_con(TyCon con) { return alloc(MonoType::make_con(con)); }

MonoTypePtr TypeArena::make_arrow(MonoTypePtr param, MonoTypePtr ret, std::vector<LatentEffect> effects, MonoTypePtr rest) {
  const auto effect = make_legacy_effect(effects, rest);
  return alloc(MonoType::make_arrow(param, ret, effect, &effect_solver_, true,
                                    std::move(effects), rest));
}

MonoTypePtr TypeArena::make_arrow_with_effect(MonoTypePtr param, MonoTypePtr ret, EffectRef effect) {
  // Validation is intentional: an EffectRef is solver-local, so accepting a
  // foreign handle here would leave an Arrow that cannot be summarized.
  (void)effect_solver_.summarize(effect);
  return alloc(MonoType::make_arrow(param, ret, effect, &effect_solver_, false));
}

MonoTypePtr TypeArena::make_erow(std::vector<LatentEffect> effects, MonoTypePtr rest) {
  const auto effect = make_legacy_effect(effects, rest);
  return alloc(MonoType::make_erow(std::move(effects), rest, effect, &effect_solver_));
}

EffectRef TypeArena::make_legacy_effect(const std::vector<LatentEffect> &effects, MonoTypePtr rest) {
  std::vector<std::string> labels;
  labels.reserve(effects.size());
  for (const auto &effect : effects)
    labels.push_back(effect.op_key);

  std::vector<EffectRef> sources;
  if (!labels.empty())
    sources.push_back(effect_solver_.labels(std::move(labels)));
  if (const auto tail = legacy_effect_ref(rest))
    sources.push_back(*tail);
  return effect_solver_.join(std::move(sources));
}

std::optional<EffectRef> TypeArena::legacy_effect_ref(MonoTypePtr type) {
  if (!type)
    return std::nullopt;
  if (type->tag == MonoType::ERow || type->tag == MonoType::Arrow) {
    if (type->effect_solver != &effect_solver_ || !type->arrow_effect.valid())
      return std::nullopt;
    return type->arrow_effect;
  }
  if (type->tag != MonoType::Var)
    return std::nullopt;
  const auto found = legacy_effect_vars_.find(type->var_id);
  if (found != legacy_effect_vars_.end())
    return found->second;
  const auto fresh = effect_solver_.flexible();
  legacy_effect_vars_.emplace(type->var_id, fresh);
  return fresh;
}

MonoTypePtr TypeArena::make_app(const std::string &name, std::vector<MonoTypePtr> args) { return alloc(MonoType::make_app(name, std::move(args))); }

MonoTypePtr TypeArena::make_tuple(std::vector<MonoTypePtr> elems) { return alloc(MonoType::make_tuple(std::move(elems))); }

MonoTypePtr TypeArena::make_record(std::vector<std::pair<std::string, MonoTypePtr>> fields, MonoTypePtr row_rest) {
  return alloc(MonoType::make_record(std::move(fields), row_rest));
}

} // namespace yona::compiler::typechecker
