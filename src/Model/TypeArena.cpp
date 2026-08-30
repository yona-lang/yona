#include "yona/Model/TypeArena.h"

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

MonoTypePtr TypeArena::make_arrow(MonoTypePtr param, MonoTypePtr ret) {
  return make_arrow(param, ret, effect_solver_.empty());
}

MonoTypePtr TypeArena::make_arrow(MonoTypePtr param, MonoTypePtr ret,
                                  EffectRef effect) {
  // Validation is intentional: an EffectRef is solver-local, so accepting a
  // foreign handle here would leave an Arrow that cannot be summarized.
  (void)effect_solver_.summarize(effect);
  return alloc(MonoType::make_arrow(param, ret, effect, &effect_solver_));
}

MonoTypePtr TypeArena::make_app(const std::string &name,
                                std::vector<MonoTypePtr> args) {
  return alloc(MonoType::make_app(name, std::move(args)));
}

MonoTypePtr TypeArena::make_tuple(std::vector<MonoTypePtr> elems) {
  return alloc(MonoType::make_tuple(std::move(elems)));
}

MonoTypePtr
TypeArena::make_record(std::vector<std::pair<std::string, MonoTypePtr>> fields,
                       MonoTypePtr row_rest) {
  return alloc(MonoType::make_record(std::move(fields), row_rest));
}

} // namespace yona::compiler::typechecker
