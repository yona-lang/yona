#include "yona/Model/TypeArena.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <list>
#include <set>
#include <sstream>
#include <stdexcept>

namespace yona::compiler::typechecker {
namespace {

std::atomic<EffectAtomCatalogDomain> NextEffectAtomCatalogDomain{1};

std::string effectKey(const MonoType &Arrow) {
  if (!Arrow.effect_solver)
    return "invalid";
  try {
    const auto Summary = Arrow.effect_solver->summarize(Arrow.arrow_effect);
    std::string Result =
        std::to_string(Arrow.effect_solver->atomDomain()) + ':';
    for (const auto Atom : Summary.known_atoms)
      Result += "a" + std::to_string(Atom) + ';';
    for (const auto &Tail : Summary.tails) {
      Result += Tail.opaque ? "o" : "f";
      Result += std::to_string(Tail.variable) + '[';
      for (const auto Atom : Tail.excluded_atoms)
        Result += std::to_string(Atom) + ',';
      Result += "];";
    }
    return Result;
  } catch (const std::invalid_argument &) {
    return "staged:" + std::to_string(Arrow.arrow_effect.owner) + ':' +
           std::to_string(Arrow.arrow_effect.index);
  }
}

std::string typeKey(MonoTypePtr Type) {
  if (!Type)
    return "?";
  std::string Result = std::to_string(static_cast<unsigned>(Type->tag)) + ':';
  switch (Type->tag) {
  case MonoType::Var:
    return Result + std::to_string(Type->var_id);
  case MonoType::Con:
    return Result + std::to_string(static_cast<unsigned>(Type->con));
  case MonoType::App:
    if (Type->resolved_key)
      Result +=
          Type->resolved_key->Module.fqn() + "\\" + Type->resolved_key->Name;
    else
      Result += Type->type_name;
    for (const auto *Argument : Type->args)
      Result += '(' + typeKey(Argument) + ')';
    return Result;
  case MonoType::Arrow:
    return Result + typeKey(Type->param_type) + "->" +
           typeKey(Type->return_type) + '!' + effectKey(*Type);
  case MonoType::MTuple:
    for (const auto *Element : Type->elements)
      Result += '(' + typeKey(Element) + ')';
    return Result;
  case MonoType::MRecord:
    for (const auto &[Name, Field] : Type->record_fields)
      Result += '(' + Name + ':' + typeKey(Field) + ')';
    if (Type->row_rest)
      Result += "|" + typeKey(Type->row_rest);
    return Result;
  case MonoType::Sum:
    for (const auto *Alternative : Type->sum_alternatives)
      Result += '(' + typeKey(Alternative) + ')';
    return Result;
  }
  return Result;
}

bool sameType(MonoTypePtr Left, MonoTypePtr Right,
              std::set<std::pair<MonoTypePtr, MonoTypePtr>> &Seen) {
  if (Left == Right)
    return true;
  if (!Left || !Right || Left->tag != Right->tag)
    return false;
  if (!Seen.emplace(Left, Right).second)
    return true;
  switch (Left->tag) {
  case MonoType::Var:
    return Left->var_id == Right->var_id;
  case MonoType::Con:
    return Left->con == Right->con;
  case MonoType::App:
    if ((Left->resolved_key || Right->resolved_key)
            ? (!Left->resolved_key || !Right->resolved_key ||
               *Left->resolved_key != *Right->resolved_key)
            : Left->type_name != Right->type_name)
      return false;
    if (Left->args.size() != Right->args.size())
      return false;
    for (std::size_t Index = 0; Index < Left->args.size(); ++Index)
      if (!sameType(Left->args[Index], Right->args[Index], Seen))
        return false;
    return true;
  case MonoType::Arrow: {
    if (Left->effect_solver != Right->effect_solver || !Left->effect_solver)
      return false;
    bool SameEffects = Left->arrow_effect == Right->arrow_effect;
    if (!SameEffects) {
      try {
        SameEffects = Left->effect_solver->summarize(Left->arrow_effect) ==
                      Right->effect_solver->summarize(Right->arrow_effect);
      } catch (const std::invalid_argument &) {
        // Coordinated scheme instantiation gives catalog-staged type
        // arguments private solver references. They are valid only after the
        // matching solver commit, so unequal staged references cannot yet be
        // proven equivalent by catalog interning.
        return false;
      }
    }
    return SameEffects && sameType(Left->param_type, Right->param_type, Seen) &&
           sameType(Left->return_type, Right->return_type, Seen);
  }
  case MonoType::MTuple:
    if (Left->elements.size() != Right->elements.size())
      return false;
    for (std::size_t Index = 0; Index < Left->elements.size(); ++Index)
      if (!sameType(Left->elements[Index], Right->elements[Index], Seen))
        return false;
    return true;
  case MonoType::MRecord:
    if (Left->record_fields.size() != Right->record_fields.size() ||
        static_cast<bool>(Left->row_rest) != static_cast<bool>(Right->row_rest))
      return false;
    for (std::size_t Index = 0; Index < Left->record_fields.size(); ++Index)
      if (Left->record_fields[Index].first !=
              Right->record_fields[Index].first ||
          !sameType(Left->record_fields[Index].second,
                    Right->record_fields[Index].second, Seen))
        return false;
    return !Left->row_rest || sameType(Left->row_rest, Right->row_rest, Seen);
  case MonoType::Sum:
    if (Left->sum_alternatives.size() != Right->sum_alternatives.size())
      return false;
    for (std::size_t Index = 0; Index < Left->sum_alternatives.size(); ++Index)
      if (!sameType(Left->sum_alternatives[Index],
                    Right->sum_alternatives[Index], Seen))
        return false;
    return true;
  }
  return false;
}

bool sameAtom(const SolvedEffectAtom &Left, const SolvedEffectAtom &Right) {
  if (Left.index() != Right.index())
    return false;
  const auto *LeftOperation = std::get_if<OperationEffectAtom>(&Left);
  const auto *RightOperation = std::get_if<OperationEffectAtom>(&Right);
  if (!LeftOperation || !RightOperation)
    return true;
  if (LeftOperation->Key != RightOperation->Key ||
      LeftOperation->TypeArguments.size() !=
          RightOperation->TypeArguments.size() ||
      LeftOperation->EffectArguments != RightOperation->EffectArguments)
    return false;
  std::set<std::pair<MonoTypePtr, MonoTypePtr>> Seen;
  for (std::size_t Index = 0; Index < LeftOperation->TypeArguments.size();
       ++Index)
    if (!sameType(LeftOperation->TypeArguments[Index],
                  RightOperation->TypeArguments[Index], Seen))
      return false;
  return true;
}

} // namespace

struct EffectAtomCatalog::Impl {
  EffectAtomCatalogDomain Domain =
      NextEffectAtomCatalogDomain.fetch_add(1, std::memory_order_relaxed);
  std::list<SolvedEffectAtom> Atoms;
  std::vector<const SolvedEffectAtom *> Index;
  std::uint64_t Generation = 0;

  Impl() {
    if (Domain == InvalidEffectAtomCatalogDomain)
      throw std::overflow_error("effect atom catalog domain exhausted");
  }
};

struct EffectAtomCatalog::AppendTransaction::State {
  EffectAtomCatalog *Catalog = nullptr;
  std::uint64_t Generation = 0;
  std::list<SolvedEffectAtom> Atoms;
  std::vector<const SolvedEffectAtom *> Index;
  bool Prepared = false;
  bool Committed = false;
};

EffectAtomCatalog::EffectAtomCatalog() : Impl_(std::make_unique<Impl>()) {}
EffectAtomCatalog::EffectAtomCatalog(EffectAtomCatalog &&) noexcept = default;
EffectAtomCatalog &
EffectAtomCatalog::operator=(EffectAtomCatalog &&) noexcept = default;
EffectAtomCatalog::~EffectAtomCatalog() = default;

EffectAtomCatalogDomain EffectAtomCatalog::domain() const noexcept {
  return Impl_->Domain;
}

EffectAtomCatalog::AppendTransaction EffectAtomCatalog::beginAppend() {
  auto State = std::make_unique<AppendTransaction::State>();
  State->Catalog = this;
  State->Generation = Impl_->Generation;
  return AppendTransaction(std::move(State));
}

bool EffectAtomCatalog::valid(EffectAtomId Atom) const noexcept {
  return Atom < Impl_->Index.size();
}

const SolvedEffectAtom &EffectAtomCatalog::at(EffectAtomId Atom) const {
  if (!valid(Atom))
    throw std::out_of_range("effect atom is not published in this catalog");
  return *Impl_->Index[Atom];
}

std::string EffectAtomCatalog::format(EffectAtomId Atom) const {
  return std::visit(
      [&](const auto &Value) -> std::string {
        using ValueType = std::decay_t<decltype(Value)>;
        if constexpr (std::is_same_v<ValueType, MayRaiseEffectAtom>) {
          return "raise";
        } else if constexpr (std::is_same_v<ValueType, MayCancelEffectAtom>) {
          return "cancel";
        } else {
          std::string Result;
          const auto Module = Value.Key.Effect.Module.fqn();
          if (Module != "Main" && Module != Value.Key.Effect.Name)
            Result += Module + "\\";
          Result += Value.Key.Effect.Name + "." + Value.Key.Operation;
          if (!Value.TypeArguments.empty() || !Value.EffectArguments.empty()) {
            Result += '<';
            bool First = true;
            for (const auto *Type : Value.TypeArguments) {
              if (!First)
                Result += ',';
              First = false;
              Result += typeKey(Type);
            }
            for (const auto Effect : Value.EffectArguments) {
              if (!First)
                Result += ',';
              First = false;
              Result += "!" + std::to_string(Effect.owner) + ':' +
                        std::to_string(Effect.index);
            }
            Result += '>';
          }
          return Result;
        }
      },
      at(Atom));
}

std::size_t EffectAtomCatalog::size() const noexcept {
  return Impl_->Index.size();
}

EffectAtomCatalog::AppendTransaction::AppendTransaction(
    std::unique_ptr<State> State)
    : State_(std::move(State)) {}
EffectAtomCatalog::AppendTransaction::AppendTransaction(
    AppendTransaction &&) noexcept = default;
EffectAtomCatalog::AppendTransaction &
EffectAtomCatalog::AppendTransaction::operator=(AppendTransaction &&) noexcept =
    default;
EffectAtomCatalog::AppendTransaction::~AppendTransaction() = default;

EffectAtomId
EffectAtomCatalog::AppendTransaction::intern(SolvedEffectAtom Atom) {
  if (!State_ || !State_->Catalog || State_->Prepared || State_->Committed)
    throw std::logic_error("effect atom transaction is not writable");
  const auto &Catalog = *State_->Catalog->Impl_;
  for (std::size_t Index = 0; Index < Catalog.Index.size(); ++Index)
    if (sameAtom(*Catalog.Index[Index], Atom))
      return static_cast<EffectAtomId>(Index);
  for (std::size_t Index = 0; Index < State_->Index.size(); ++Index)
    if (sameAtom(*State_->Index[Index], Atom))
      return static_cast<EffectAtomId>(Catalog.Index.size() + Index);
  State_->Atoms.push_back(std::move(Atom));
  State_->Index.push_back(&State_->Atoms.back());
  return static_cast<EffectAtomId>(Catalog.Index.size() + State_->Index.size() -
                                   1);
}

bool EffectAtomCatalog::AppendTransaction::valid(
    EffectAtomId Atom) const noexcept {
  if (!State_ || !State_->Catalog)
    return false;
  return Atom < State_->Catalog->Impl_->Index.size() + State_->Index.size();
}

bool EffectAtomCatalog::AppendTransaction::validForMask(
    EffectAtomId Atom) const noexcept {
  if (!valid(Atom))
    return false;
  const auto Published = State_->Catalog->Impl_->Index.size();
  const auto *Value = Atom < Published ? State_->Catalog->Impl_->Index[Atom]
                                       : State_->Index[Atom - Published];
  return std::holds_alternative<OperationEffectAtom>(*Value);
}

std::expected<void, std::string>
EffectAtomCatalog::AppendTransaction::prepareCommit() {
  if (!State_ || !State_->Catalog)
    return std::unexpected("effect atom transaction is empty");
  if (State_->Prepared || State_->Committed)
    return std::unexpected("effect atom transaction already prepared");
  auto &Catalog = *State_->Catalog->Impl_;
  if (Catalog.Generation != State_->Generation)
    return std::unexpected("effect atom catalog changed during append");
  for (const auto *Atom : State_->Index) {
    if (const auto *Operation = std::get_if<OperationEffectAtom>(Atom)) {
      if (Operation->Key.Operation.empty())
        return std::unexpected("effect operation atom has an empty name");
      if (std::ranges::any_of(Operation->TypeArguments,
                              [](MonoTypePtr Type) { return Type == nullptr; }))
        return std::unexpected(
            "effect operation atom has a null type argument");
      if (std::ranges::any_of(Operation->EffectArguments,
                              [](EffectRef Effect) { return !Effect.valid(); }))
        return std::unexpected(
            "effect operation atom has an invalid effect argument");
    }
  }
  try {
    Catalog.Index.reserve(Catalog.Index.size() + State_->Index.size());
  } catch (const std::bad_alloc &) {
    return std::unexpected("unable to reserve effect atom catalog storage");
  }
  State_->Prepared = true;
  return {};
}

void EffectAtomCatalog::AppendTransaction::commitPrepared() noexcept {
  if (!State_ || !State_->Prepared || State_->Committed)
    std::terminate();
  auto &Catalog = *State_->Catalog->Impl_;
  Catalog.Atoms.splice(Catalog.Atoms.end(), State_->Atoms);
  for (const auto *Atom : State_->Index)
    Catalog.Index.push_back(Atom);
  ++Catalog.Generation;
  State_->Committed = true;
}

TypeArena::TypeArena()
    : effect_atoms_(),
      effect_solver_(effect_atoms_.domain(), [this](EffectAtomId Atom) {
        if (!effect_atoms_.valid(Atom))
          return false;
        return std::holds_alternative<OperationEffectAtom>(
            effect_atoms_.at(Atom));
      }) {}

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
  return alloc(MonoType::make_arrow(param, ret, effect, &effect_solver_,
                                    &effect_atoms_));
}

MonoTypePtr TypeArena::make_app(const std::string &name,
                                std::vector<MonoTypePtr> args) {
  return alloc(MonoType::make_app(name, std::move(args)));
}

MonoTypePtr TypeArena::make_app(model::NominalTypeKey Key,
                                std::vector<MonoTypePtr> Arguments,
                                std::string DisplayName) {
  return alloc(MonoType::make_app(std::move(Key), std::move(Arguments),
                                  std::move(DisplayName)));
}

MonoTypePtr TypeArena::make_tuple(std::vector<MonoTypePtr> elems) {
  return alloc(MonoType::make_tuple(std::move(elems)));
}

MonoTypePtr TypeArena::make_sum(std::vector<MonoTypePtr> Alternatives) {
  std::vector<MonoTypePtr> Flattened;
  std::function<void(MonoTypePtr)> Append = [&](MonoTypePtr Alternative) {
    if (!Alternative)
      throw std::invalid_argument("sum alternatives must not be null");
    if (Alternative->tag == MonoType::Sum) {
      for (const auto *Nested : Alternative->sum_alternatives)
        Append(Nested);
      return;
    }
    Flattened.push_back(Alternative);
  };
  for (const auto *Alternative : Alternatives)
    Append(Alternative);
  if (Flattened.empty())
    throw std::invalid_argument("sum types require at least one alternative");
  std::vector<MonoTypePtr> Unique;
  Unique.reserve(Flattened.size());
  for (const auto *Alternative : Flattened) {
    const auto Duplicate =
        std::ranges::any_of(Unique, [&](MonoTypePtr Existing) {
          std::set<std::pair<MonoTypePtr, MonoTypePtr>> Seen;
          return sameType(Existing, Alternative, Seen);
        });
    if (!Duplicate)
      Unique.push_back(Alternative);
  }
  Flattened = std::move(Unique);
  std::ranges::sort(Flattened, {}, typeKey);
  if (Flattened.size() == 1)
    return Flattened.front();
  return alloc(MonoType::make_sum(std::move(Flattened)));
}

MonoTypePtr
TypeArena::make_record(std::vector<std::pair<std::string, MonoTypePtr>> fields,
                       MonoTypePtr row_rest) {
  return alloc(MonoType::make_record(std::move(fields), row_rest));
}

} // namespace yona::compiler::typechecker
