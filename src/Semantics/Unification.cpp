/// Type unification for Hindley-Milner inference.
///
/// Core algorithm:
///  1. Resolve both types via union-find
///  2. If identical → success
///  3. If one is a variable → occurs check → bind
///  4. If both constructors → match constructor, unify args
///  5. Error otherwise

#include "yona/Semantics/Unification.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace yona::compiler::typechecker {

Unifier::Unifier(TypeArena &arena, UnionFind &uf, DiagnosticEngine &diag)
    : arena_(arena), uf_(uf), diag_(diag) {}

MonoTypePtr Unifier::resolve(MonoTypePtr type) {
  if (!type)
    return nullptr;
  if (type->tag == MonoType::Var) {
    auto bound = uf_.find(type->var_id);
    if (bound)
      return resolve(bound);
  }
  return type;
}

bool Unifier::unify(MonoTypePtr a, MonoTypePtr b, const SourceRange &loc,
                    const std::string &context) {
  if (!unify_inner(resolve(a), resolve(b), loc, context))
    return false;
  return reconcile_effect_atoms(loc, context);
}

bool Unifier::solved_types_equal(
    MonoTypePtr Left, MonoTypePtr Right,
    std::set<std::pair<MonoTypePtr, MonoTypePtr>> &Seen) {
  Left = resolve(Left);
  Right = resolve(Right);
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
  case MonoType::App: {
    const bool SameIdentity =
        (Left->resolved_key || Right->resolved_key)
            ? Left->resolved_key && Right->resolved_key &&
                  *Left->resolved_key == *Right->resolved_key
            : Left->type_name == Right->type_name;
    if (!SameIdentity || Left->args.size() != Right->args.size())
      return false;
    for (std::size_t Index = 0; Index < Left->args.size(); ++Index)
      if (!solved_types_equal(Left->args[Index], Right->args[Index], Seen))
        return false;
    return true;
  }
  case MonoType::Arrow:
    return solved_types_equal(Left->param_type, Right->param_type, Seen) &&
           solved_types_equal(Left->return_type, Right->return_type, Seen) &&
           arena_.effect_solver().summarize(Left->arrow_effect) ==
               arena_.effect_solver().summarize(Right->arrow_effect);
  case MonoType::MTuple:
    if (Left->elements.size() != Right->elements.size())
      return false;
    for (std::size_t Index = 0; Index < Left->elements.size(); ++Index)
      if (!solved_types_equal(Left->elements[Index], Right->elements[Index],
                              Seen))
        return false;
    return true;
  case MonoType::MRecord:
    if (Left->record_fields.size() != Right->record_fields.size() ||
        static_cast<bool>(Left->row_rest) != static_cast<bool>(Right->row_rest))
      return false;
    for (std::size_t Index = 0; Index < Left->record_fields.size(); ++Index) {
      if (Left->record_fields[Index].first !=
              Right->record_fields[Index].first ||
          !solved_types_equal(Left->record_fields[Index].second,
                              Right->record_fields[Index].second, Seen))
        return false;
    }
    return !Left->row_rest ||
           solved_types_equal(Left->row_rest, Right->row_rest, Seen);
  case MonoType::Sum: {
    if (Left->sum_alternatives.size() != Right->sum_alternatives.size())
      return false;
    std::vector<bool> Matched(Right->sum_alternatives.size());
    for (const auto *LeftAlternative : Left->sum_alternatives) {
      bool Found = false;
      for (std::size_t Index = 0; Index < Right->sum_alternatives.size();
           ++Index) {
        if (Matched[Index])
          continue;
        auto CandidateSeen = Seen;
        if (!solved_types_equal(LeftAlternative, Right->sum_alternatives[Index],
                                CandidateSeen))
          continue;
        Seen = std::move(CandidateSeen);
        Matched[Index] = true;
        Found = true;
        break;
      }
      if (!Found)
        return false;
    }
    return true;
  }
  }
  return false;
}

bool Unifier::reconcile_effect_atoms(const SourceRange &loc,
                                     const std::string &context) {
  auto &Effects = arena_.effect_solver();
  const auto &Catalog = arena_.effect_atoms();
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (EffectAtomId Left = 0; Left < Catalog.size(); ++Left) {
      const auto *LeftOperation =
          std::get_if<OperationEffectAtom>(&Catalog.at(Left));
      if (!LeftOperation)
        continue;
      for (EffectAtomId Right = Left + 1; Right < Catalog.size(); ++Right) {
        if (Effects.canonicalAtom(Left) == Effects.canonicalAtom(Right))
          continue;
        const auto *RightOperation =
            std::get_if<OperationEffectAtom>(&Catalog.at(Right));
        if (!RightOperation || LeftOperation->Key != RightOperation->Key ||
            LeftOperation->TypeArguments.size() !=
                RightOperation->TypeArguments.size() ||
            LeftOperation->EffectArguments.size() !=
                RightOperation->EffectArguments.size())
          continue;

        std::set<std::pair<MonoTypePtr, MonoTypePtr>> Seen;
        bool Same = true;
        for (std::size_t Index = 0;
             Same && Index < LeftOperation->TypeArguments.size(); ++Index)
          Same = solved_types_equal(LeftOperation->TypeArguments[Index],
                                    RightOperation->TypeArguments[Index], Seen);
        for (std::size_t Index = 0;
             Same && Index < LeftOperation->EffectArguments.size(); ++Index)
          Same = Effects.summarize(LeftOperation->EffectArguments[Index]) ==
                 Effects.summarize(RightOperation->EffectArguments[Index]);
        if (!Same)
          continue;
        if (Effects.equateAtoms(Left, Right) ==
            EffectConstraintResult::Conflict) {
          diag_.error(loc, ErrorCode::E0100,
                      "incompatible effect rows after solving operation "
                      "arguments" +
                          (context.empty() ? "" : " " + context));
          return false;
        }
        Changed = true;
      }
    }
  }
  return true;
}

bool Unifier::unify_inner(MonoTypePtr a, MonoTypePtr b, const SourceRange &loc,
                          const std::string &context) {
  if (a == b)
    return true;
  if (!a || !b)
    return false;

  // Var on left: bind
  if (a->tag == MonoType::Var) {
    if (b->tag == MonoType::Var && a->var_id == b->var_id)
      return true;
    if (occurs_in(a->var_id, b)) {
      diag_.error(loc, ErrorCode::E0101,
                  "infinite type: cannot construct " + pretty_print(a) + " ~ " +
                      pretty_print(b) + (context.empty() ? "" : " " + context));
      return false;
    }
    adjust_levels(b, uf_.level(a->var_id));
    uf_.bind(a->var_id, b);
    return true;
  }

  // Var on right: bind
  if (b->tag == MonoType::Var) {
    if (occurs_in(b->var_id, a)) {
      diag_.error(loc, ErrorCode::E0101,
                  "infinite type: cannot construct " + pretty_print(b) + " ~ " +
                      pretty_print(a) + (context.empty() ? "" : " " + context));
      return false;
    }
    adjust_levels(a, uf_.level(b->var_id));
    uf_.bind(b->var_id, a);
    return true;
  }

  // Both concrete: must match structurally
  if (a->tag != b->tag) {
    diag_.error(loc, ErrorCode::E0100,
                "type mismatch: expected " + pretty_print(a) + " but found " +
                    pretty_print(b) + (context.empty() ? "" : " " + context));
    return false;
  }

  switch (a->tag) {
  case MonoType::Con:
    if (a->con != b->con) {
      diag_.error(loc, ErrorCode::E0100,
                  "type mismatch: expected " + pretty_print(a) + " but found " +
                      pretty_print(b) + (context.empty() ? "" : " " + context));
      return false;
    }
    return true;

  case MonoType::Arrow:
    return unify(a->param_type, b->param_type, loc, context) &&
           unify(a->return_type, b->return_type, loc, context) &&
           unify_effects(a->arrow_effect, b->arrow_effect, loc, context);

  case MonoType::App: {
    // `.yonai` writes every algebraic type as ADT. That wildcard must
    // unify with a named ADT (Stream, Option, FileMode) but not with
    // Seq/Set/Dict — those have their own tags.
    auto is_collection = [](MonoTypePtr t) {
      return t->type_name == "Seq" || t->type_name == "Set" ||
             t->type_name == "Dict";
    };
    if (a->type_name == "ADT" && b->type_name != "ADT") {
      if (is_collection(b)) {
        diag_.error(loc, ErrorCode::E0100,
                    "type mismatch: " + pretty_print(a) + " vs " +
                        pretty_print(b) +
                        (context.empty() ? "" : " " + context));
        return false;
      }
      size_t n = std::min(a->args.size(), b->args.size());
      for (size_t i = 0; i < n; i++) {
        if (!unify(a->args[i], b->args[i], loc, context))
          return false;
      }
      return true;
    }
    if (b->type_name == "ADT" && a->type_name != "ADT") {
      if (is_collection(a)) {
        diag_.error(loc, ErrorCode::E0100,
                    "type mismatch: " + pretty_print(a) + " vs " +
                        pretty_print(b) +
                        (context.empty() ? "" : " " + context));
        return false;
      }
      size_t n = std::min(a->args.size(), b->args.size());
      for (size_t i = 0; i < n; i++) {
        if (!unify(a->args[i], b->args[i], loc, context))
          return false;
      }
      return true;
    }
    const bool IdentityMismatch = (a->resolved_key || b->resolved_key)
                                      ? (!a->resolved_key || !b->resolved_key ||
                                         *a->resolved_key != *b->resolved_key)
                                      : a->type_name != b->type_name;
    if (IdentityMismatch || a->args.size() != b->args.size()) {
      diag_.error(loc, ErrorCode::E0100,
                  "type mismatch: " + pretty_print(a) + " vs " +
                      pretty_print(b) + (context.empty() ? "" : " " + context));
      return false;
    }
    for (size_t i = 0; i < a->args.size(); i++) {
      if (!unify(a->args[i], b->args[i], loc, context))
        return false;
    }
    if (a->type_name == "Promise" &&
        (a->promise_result_contract || b->promise_result_contract)) {
      if (!a->promise_result_contract || !b->promise_result_contract ||
          *a->promise_result_contract != *b->promise_result_contract ||
          !a->promise_effects.valid() || !b->promise_effects.valid()) {
        diag_.error(loc, ErrorCode::E0100,
                    "Promise result contracts do not match" +
                        (context.empty() ? "" : " " + context));
        return false;
      }
      if (!unify_effects(a->promise_effects, b->promise_effects, loc, context))
        return false;
    }
    return true;
  }

  case MonoType::MTuple:
    if (a->elements.size() != b->elements.size()) {
      diag_.error(loc, ErrorCode::E0102,
                  "tuple size mismatch: " + std::to_string(a->elements.size()) +
                      " vs " + std::to_string(b->elements.size()) +
                      (context.empty() ? "" : " " + context));
      return false;
    }
    for (size_t i = 0; i < a->elements.size(); i++) {
      if (!unify(a->elements[i], b->elements[i], loc, context))
        return false;
    }
    return true;

  case MonoType::MRecord: {
    // Row unification: match common fields, propagate extras
    // Collect fields from both sides
    std::unordered_map<std::string, MonoTypePtr> a_fields, b_fields;
    for (auto &[name, type] : a->record_fields)
      a_fields[name] = type;
    for (auto &[name, type] : b->record_fields)
      b_fields[name] = type;

    // Unify common fields
    for (auto &[name, a_type] : a_fields) {
      auto it = b_fields.find(name);
      if (it != b_fields.end()) {
        if (!unify(a_type, it->second, loc, context))
          return false;
      }
    }

    // Extra fields in a but not b: b must have an open row to absorb them
    std::vector<std::pair<std::string, MonoTypePtr>> a_extras, b_extras;
    for (auto &[name, type] : a_fields)
      if (b_fields.find(name) == b_fields.end())
        a_extras.push_back({name, type});
    for (auto &[name, type] : b_fields)
      if (a_fields.find(name) == a_fields.end())
        b_extras.push_back({name, type});

    // If a has extras, b must have an open row variable to absorb them
    if (!a_extras.empty() && b->row_rest) {
      auto *extra_record = arena_.make_record(a_extras, a->row_rest);
      if (!unify(b->row_rest, extra_record, loc, context))
        return false;
    } else if (!a_extras.empty() && !b->row_rest) {
      diag_.error(loc, ErrorCode::E0100,
                  "record has extra field(s) not expected" +
                      (context.empty() ? "" : " " + context));
      return false;
    }

    if (!b_extras.empty() && a->row_rest) {
      auto *extra_record = arena_.make_record(b_extras, b->row_rest);
      if (!unify(a->row_rest, extra_record, loc, context))
        return false;
    } else if (!b_extras.empty() && !a->row_rest) {
      diag_.error(loc, ErrorCode::E0100,
                  "record missing field(s)" +
                      (context.empty() ? "" : " " + context));
      return false;
    }

    // If both have row rest, unify them
    if (a->row_rest && b->row_rest && a_extras.empty() && b_extras.empty())
      unify(a->row_rest, b->row_rest, loc, context);

    return true;
  }

  case MonoType::Sum: {
    const auto normalize = [&](MonoTypePtr Sum) {
      std::vector<MonoTypePtr> Alternatives;
      const auto append = [&](const auto &Self,
                              MonoTypePtr Alternative) -> void {
        Alternative = resolve(Alternative);
        if (Alternative && Alternative->tag == MonoType::Sum) {
          for (const auto *Nested : Alternative->sum_alternatives)
            Self(Self, Nested);
        } else if (Alternative) {
          Alternatives.push_back(Alternative);
        }
      };
      for (const auto *Alternative : Sum->sum_alternatives)
        append(append, Alternative);
      std::ranges::sort(Alternatives, {}, [](MonoTypePtr Alternative) {
        if (Alternative->tag == MonoType::Var)
          return "~" + std::to_string(Alternative->var_id);
        return pretty_print(Alternative);
      });
      std::vector<MonoTypePtr> Unique;
      for (const auto *Alternative : Alternatives) {
        const bool Duplicate =
            std::ranges::any_of(Unique, [&](MonoTypePtr Existing) {
              std::set<std::pair<MonoTypePtr, MonoTypePtr>> Seen;
              return solved_types_equal(Existing, Alternative, Seen);
            });
        if (!Duplicate)
          Unique.push_back(Alternative);
      }
      Alternatives = std::move(Unique);
      return Alternatives;
    };
    auto Left = normalize(a);
    auto Right = normalize(b);
    if (Left.size() != Right.size()) {
      diag_.error(loc, ErrorCode::E0100,
                  "sum alternatives differ: " + pretty_print(a) + " vs " +
                      pretty_print(b) + (context.empty() ? "" : " " + context));
      return false;
    }

    // Consume already-equal alternatives before allowing type variables to
    // bind. This preserves exact members such as `Int` in `a | Int` and
    // avoids pairing `a` with `Int` merely because variable IDs sort first.
    for (auto LeftIt = Left.begin(); LeftIt != Left.end();) {
      const auto RightIt =
          std::ranges::find_if(Right, [&](MonoTypePtr Candidate) {
            std::set<std::pair<MonoTypePtr, MonoTypePtr>> Seen;
            return solved_types_equal(*LeftIt, Candidate, Seen);
          });
      if (RightIt == Right.end()) {
        ++LeftIt;
        continue;
      }
      LeftIt = Left.erase(LeftIt);
      Right.erase(RightIt);
    }
    const auto ShapeKey = [](MonoTypePtr Type) {
      return Type->tag == MonoType::Var ? std::string{"~"} : pretty_print(Type);
    };
    std::ranges::sort(Left, {}, ShapeKey);
    std::ranges::sort(Right, {}, ShapeKey);
    for (std::size_t Index = 0; Index < Left.size(); ++Index)
      if (!unify(Left[Index], Right[Index], loc, context))
        return false;
    return true;
  }

  default:
    return false;
  }
}

bool Unifier::unify_effects(EffectRef left, EffectRef right,
                            const SourceRange &loc,
                            const std::string &context) {
  const auto result = arena_.effect_solver().equate(left, right);
  if (result != EffectConstraintResult::Conflict)
    return true;
  diag_.error(loc, ErrorCode::E0100,
              "incompatible effect rows" +
                  (context.empty() ? "" : " " + context));
  return false;
}

bool Unifier::occurs_in(TypeId var_id, MonoTypePtr type) {
  type = resolve(type);
  if (!type)
    return false;
  if (type->tag == MonoType::Var)
    return type->var_id == var_id;
  if (type->tag == MonoType::Arrow)
    return occurs_in(var_id, type->param_type) ||
           occurs_in(var_id, type->return_type);
  if (type->tag == MonoType::App) {
    for (auto *a : type->args)
      if (occurs_in(var_id, a))
        return true;
    return false;
  }
  if (type->tag == MonoType::MTuple) {
    for (auto *e : type->elements)
      if (occurs_in(var_id, e))
        return true;
    return false;
  }
  if (type->tag == MonoType::MRecord) {
    for (auto &[_, ft] : type->record_fields)
      if (occurs_in(var_id, ft))
        return true;
    if (type->row_rest && occurs_in(var_id, type->row_rest))
      return true;
    return false;
  }
  if (type->tag == MonoType::Sum) {
    for (auto *Alternative : type->sum_alternatives)
      if (occurs_in(var_id, Alternative))
        return true;
    return false;
  }
  return false;
}

void Unifier::adjust_levels(MonoTypePtr type, int level) {
  type = resolve(type);
  if (!type)
    return;
  if (type->tag == MonoType::Var) {
    if (uf_.level(type->var_id) > level)
      uf_.set_level(type->var_id, level);
    return;
  }
  if (type->tag == MonoType::Arrow) {
    adjust_levels(type->param_type, level);
    adjust_levels(type->return_type, level);
  }
  if (type->tag == MonoType::App)
    for (auto *a : type->args)
      adjust_levels(a, level);
  if (type->tag == MonoType::MTuple)
    for (auto *e : type->elements)
      adjust_levels(e, level);
  if (type->tag == MonoType::MRecord) {
    for (auto &[_, ft] : type->record_fields)
      adjust_levels(ft, level);
    if (type->row_rest)
      adjust_levels(type->row_rest, level);
  }
  if (type->tag == MonoType::Sum)
    for (auto *Alternative : type->sum_alternatives)
      adjust_levels(Alternative, level);
}

// Convert a var_id to a user-facing name: a, b, ..., z, a1, b1, ...
// The id → name map is unique per top-level pretty_print call so the same
// variable renders the same way within one error message. Internal
// counters (which can be large / non-contiguous) never leak into
// diagnostics.
namespace {
struct PrintCtx {
  std::unordered_map<int, std::string> names;
  std::unordered_map<EffectVarId, std::string> effect_names;
  std::string name_for(int var_id) {
    auto it = names.find(var_id);
    if (it != names.end())
      return it->second;
    int n = static_cast<int>(names.size());
    std::string nm;
    nm += static_cast<char>('a' + (n % 26));
    if (n >= 26)
      nm += std::to_string(n / 26);
    names[var_id] = nm;
    return nm;
  }
  std::string effect_name_for(EffectVarId var_id) {
    auto it = effect_names.find(var_id);
    if (it != effect_names.end())
      return it->second;
    auto name = "e" + std::to_string(effect_names.size());
    effect_names[var_id] = name;
    return name;
  }
};

std::string pretty_print_effect(const EffectNormalForm &effect, PrintCtx &ctx,
                                const EffectAtomCatalog *Atoms) {
  if (effect.empty())
    return "";
  std::string row = "!{";
  std::vector<std::string> Known;
  Known.reserve(effect.known_atoms.size());
  for (const auto Atom : effect.known_atoms)
    Known.push_back(Atoms && Atoms->valid(Atom)
                        ? Atoms->format(Atom)
                        : "atom(" + std::to_string(Atom) + ")");
  std::ranges::sort(Known);
  for (size_t i = 0; i < Known.size(); ++i) {
    if (i)
      row += ",";
    row += Known[i];
  }
  if (!effect.tails.empty()) {
    if (!effect.known_atoms.empty())
      row += " | ";
    else
      row += "|";
    for (size_t i = 0; i < effect.tails.size(); ++i) {
      if (i)
        row += " + ";
      const auto &tail = effect.tails[i];
      if (tail.opaque)
        row += "opaque(";
      row += ctx.effect_name_for(tail.variable);
      if (tail.opaque)
        row += ")";
      if (!tail.excluded_atoms.empty()) {
        row += " \\ {";
        std::vector<std::string> Excluded;
        Excluded.reserve(tail.excluded_atoms.size());
        for (const auto Atom : tail.excluded_atoms)
          Excluded.push_back(Atoms && Atoms->valid(Atom)
                                 ? Atoms->format(Atom)
                                 : "atom(" + std::to_string(Atom) + ")");
        std::ranges::sort(Excluded);
        for (size_t j = 0; j < Excluded.size(); ++j) {
          if (j)
            row += ",";
          row += Excluded[j];
        }
        row += "}";
      }
    }
  }
  return row + "} ";
}

std::string pretty_print_rec(MonoTypePtr type, PrintCtx &ctx) {
  if (!type)
    return "?";
  switch (type->tag) {
  case MonoType::Var:
    return ctx.name_for(type->var_id);
  case MonoType::Con: {
    switch (type->con) {
    case TyCon::Int:
      return "Int";
    case TyCon::Float:
      return "Float";
    case TyCon::Bool:
      return "Bool";
    case TyCon::String:
      return "String";
    case TyCon::Char:
      return "Char";
    case TyCon::Byte:
      return "Byte";
    case TyCon::Symbol:
      return "Symbol";
    case TyCon::Unit:
      return "()";
    case TyCon::Seq:
      return "Seq";
    case TyCon::Set:
      return "Set";
    case TyCon::Dict:
      return "Dict";
    case TyCon::Tuple:
      return "Tuple";
    case TyCon::Function:
      return "Function";
    case TyCon::Promise:
      return "Promise";
    case TyCon::ByteArray:
      return "ByteArray";
    }
    return "?";
  }
  case MonoType::Arrow: {
    const auto row =
        pretty_print_effect(type->effect_solver->summarize(type->arrow_effect),
                            ctx, type->effect_atoms);
    return "(" + pretty_print_rec(type->param_type, ctx) + " -> " + row +
           pretty_print_rec(type->return_type, ctx) + ")";
  }
  case MonoType::App: {
    std::string s = type->type_name;
    for (auto *a : type->args)
      s += " " + pretty_print_rec(a, ctx);
    return s;
  }
  case MonoType::MTuple: {
    std::string s = "(";
    for (size_t i = 0; i < type->elements.size(); i++) {
      if (i > 0)
        s += ", ";
      s += pretty_print_rec(type->elements[i], ctx);
    }
    return s + ")";
  }
  case MonoType::MRecord: {
    std::string s = "{ ";
    for (size_t i = 0; i < type->record_fields.size(); i++) {
      if (i > 0)
        s += ", ";
      s += type->record_fields[i].first + " : " +
           pretty_print_rec(type->record_fields[i].second, ctx);
    }
    if (type->row_rest) {
      if (!type->record_fields.empty())
        s += " | ";
      s += pretty_print_rec(type->row_rest, ctx);
    }
    return s + " }";
  }
  case MonoType::Sum: {
    std::string Result;
    for (std::size_t Index = 0; Index < type->sum_alternatives.size();
         ++Index) {
      if (Index)
        Result += " | ";
      Result += pretty_print_rec(type->sum_alternatives[Index], ctx);
    }
    return Result;
  }
  default:
    return "?";
  }
}
} // namespace

// Pretty-print a type for error messages
std::string pretty_print(MonoTypePtr type) {
  PrintCtx ctx;
  return pretty_print_rec(type, ctx);
}

} // namespace yona::compiler::typechecker
