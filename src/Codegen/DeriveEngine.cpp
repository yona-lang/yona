/// DeriveEngine — registry and dispatcher for auto-derive strategies.
///
/// Built-in strategies are installed into each CodegenSession's engine.
/// Additional strategies are registered explicitly on that session.

#include "yona/Codegen/DeriveEngine.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace yona::compiler::codegen {
namespace {

std::string deriveShow(const DeriveAdtInfo &Adt);
std::string deriveEq(const DeriveAdtInfo &Adt);
std::string deriveOrd(const DeriveAdtInfo &Adt);
std::string deriveHash(const DeriveAdtInfo &Adt);

} // namespace

// ===== Registry =====

DeriveEngine::DeriveEngine() {
  registerStrategy("Show", {"show"}, deriveShow);
  registerStrategy("Eq", {"eq"}, deriveEq);
  registerStrategy("Ord", {"compare"}, deriveOrd);
  registerStrategy("Hash", {"hash"}, deriveHash);
}

void DeriveEngine::registerStrategy(std::string TraitName,
                                    std::vector<std::string> MethodNames,
                                    DeriveGeneratorFn Generator) {
  const auto Key = TraitName;
  Strategies[Key] = {std::move(TraitName), std::move(MethodNames),
                     std::move(Generator)};
}

bool DeriveEngine::isDerivable(const std::string &TraitName) const {
  return Strategies.contains(TraitName);
}

const DeriveStrategyInfo *
DeriveEngine::getStrategy(const std::string &TraitName) const {
  const auto It = Strategies.find(TraitName);
  return It != Strategies.end() ? &It->second : nullptr;
}

std::vector<std::string> DeriveEngine::allDerivableTraits() const {
  std::vector<std::string> Result;
  Result.reserve(Strategies.size());
  for (const auto &[Name, _] : Strategies)
    Result.push_back(Name);
  std::sort(Result.begin(), Result.end());
  return Result;
}

// ===== Strategy: Show =====

namespace {

std::string deriveShow(const DeriveAdtInfo &adt) {
  std::ostringstream os;
  os << "show @borrow x = case x of\n";

  for (auto &ctor : adt.constructors) {
    if (ctor.arity == 0) {
      os << "    " << ctor.name << " -> \"" << ctor.name << "\"\n";
    } else {
      os << "    " << ctor.name;
      for (int i = 0; i < ctor.arity; i++)
        os << " _f" << i;
      os << " -> \"" << ctor.name << "(\"";
      for (int i = 0; i < ctor.arity; i++) {
        if (i > 0)
          os << " ++ \", \"";
        os << " ++ show _f" << i;
      }
      os << " ++ \")\"\n";
    }
  }

  os << "end\n";
  return os.str();
}

// ===== Strategy: Eq =====

std::string deriveEq(const DeriveAdtInfo &adt) {
  std::ostringstream os;
  os << "eq @borrow _a @borrow _b = case _a of\n";

  for (auto &ctor : adt.constructors) {
    os << "    " << ctor.name;
    for (int i = 0; i < ctor.arity; i++)
      os << " _x" << i;

    os << " -> case _b of\n";
    os << "        " << ctor.name;
    for (int i = 0; i < ctor.arity; i++)
      os << " _y" << i;
    os << " -> ";

    if (ctor.arity == 0) {
      os << "true";
    } else {
      for (int i = 0; i < ctor.arity; i++) {
        if (i > 0)
          os << " && ";
        os << "eq _x" << i << " _y" << i;
      }
    }
    os << "\n";
    os << "        _ -> false\n";
    os << "    end\n";
  }

  os << "end\n";
  return os.str();
}

// ===== Strategy: Ord =====

std::string deriveOrd(const DeriveAdtInfo &adt) {
  std::ostringstream os;
  os << "compare @borrow _a @borrow _b = case _a of\n";
  for (size_t ci = 0; ci < adt.constructors.size(); ci++) {
    auto &ctor = adt.constructors[ci];
    os << "    " << ctor.name;
    for (int i = 0; i < ctor.arity; i++)
      os << " _x" << i;
    os << " -> case _b of\n";

    for (size_t bi = 0; bi < adt.constructors.size(); ++bi) {
      const auto &other = adt.constructors[bi];
      os << "        " << other.name;
      for (int i = 0; i < other.arity; ++i)
        os << (bi == ci ? " _y" : " _ignored") << i;
      os << " -> ";
      if (bi < ci) {
        os << "Greater";
      } else if (bi > ci) {
        os << "Less";
      } else if (ctor.arity == 0) {
        os << "Equal";
      } else {
        for (int i = 0; i < ctor.arity; ++i) {
          os << "case compare _x" << i << " _y" << i << " of\n"
             << "            Less -> Less\n"
             << "            Greater -> Greater\n"
             << "            Equal -> ";
        }
        os << "Equal";
        for (int i = 0; i < ctor.arity; ++i)
          os << "\n        end";
      }
      os << "\n";
    }
    os << "    end\n";
  }
  os << "end\n";

  return os.str();
}

// ===== Strategy: Hash =====

std::string deriveHash(const DeriveAdtInfo &adt) {
  std::ostringstream os;
  os << "hash @borrow x = case x of\n";

  for (auto &ctor : adt.constructors) {
    os << "    " << ctor.name;
    for (int i = 0; i < ctor.arity; i++)
      os << " _f" << i;
    os << " -> ";

    if (ctor.arity == 0) {
      os << ctor.tag;
    } else {
      // A Yona multi-binding `let` is parallel, so a repeated `_h`
      // alias cannot express a sequential hash fold. Emit the fold as
      // one nested expression instead.
      for (int i = 0; i < ctor.arity; ++i)
        os << "(";
      os << ctor.tag;
      for (int i = 0; i < ctor.arity; ++i)
        os << " * 31 + hash _f" << i << ")";
    }
    os << "\n";
  }

  os << "end\n";
  return os.str();
}

} // namespace

} // namespace yona::compiler::codegen
