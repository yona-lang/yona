/// DeriveEngine — registry and dispatcher for auto-derive strategies.
///
/// Each derivable trait registers itself via a static initializer at the
/// bottom of this file. Adding a new derivable trait is self-contained:
/// write the generator function and add one register_strategy() call.

#include "DeriveEngine.h"
#include <sstream>

namespace yona::compiler::codegen {

// ===== Registry =====

std::unordered_map<std::string, DeriveStrategyInfo>& DeriveEngine::registry() {
    static std::unordered_map<std::string, DeriveStrategyInfo> reg;
    return reg;
}

int DeriveEngine::register_strategy(const std::string& trait_name,
                                     std::vector<std::string> method_names,
                                     DeriveGeneratorFn generator) {
    registry()[trait_name] = {trait_name, std::move(method_names), std::move(generator)};
    return 0;
}

bool DeriveEngine::is_derivable(const std::string& trait_name) {
    return registry().count(trait_name) > 0;
}

const DeriveStrategyInfo* DeriveEngine::get_strategy(const std::string& trait_name) {
    auto it = registry().find(trait_name);
    return (it != registry().end()) ? &it->second : nullptr;
}

std::vector<std::string> DeriveEngine::all_derivable_traits() {
    std::vector<std::string> result;
    for (auto& [name, _] : registry()) result.push_back(name);
    return result;
}

// ===== Strategy: Show =====

static std::string derive_show(const DeriveAdtInfo& adt) {
    std::ostringstream os;
    os << "show @borrow x = case x of\n";

    for (auto& ctor : adt.constructors) {
        if (ctor.arity == 0) {
            os << "    " << ctor.name << " -> \"" << ctor.name << "\"\n";
        } else {
            os << "    " << ctor.name;
            for (int i = 0; i < ctor.arity; i++)
                os << " _f" << i;
            os << " -> \"" << ctor.name << "(\"";
            for (int i = 0; i < ctor.arity; i++) {
                if (i > 0) os << " ++ \", \"";
                os << " ++ show _f" << i;
            }
            os << " ++ \")\"\n";
        }
    }

    os << "end\n";
    return os.str();
}

static auto _reg_show = DeriveEngine::register_strategy("Show", {"show"}, derive_show);

// ===== Strategy: Eq =====

static std::string derive_eq(const DeriveAdtInfo& adt) {
    std::ostringstream os;
    os << "eq @borrow _a @borrow _b = case _a of\n";

    for (auto& ctor : adt.constructors) {
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
                if (i > 0) os << " && ";
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

static auto _reg_eq = DeriveEngine::register_strategy("Eq", {"eq"}, derive_eq);

// ===== Strategy: Ord =====

static std::string derive_ord(const DeriveAdtInfo& adt) {
    std::ostringstream os;
    os << "compare @borrow _a @borrow _b = case _a of\n";
    for (size_t ci = 0; ci < adt.constructors.size(); ci++) {
        auto& ctor = adt.constructors[ci];
        os << "    " << ctor.name;
        for (int i = 0; i < ctor.arity; i++) os << " _x" << i;
        os << " -> case _b of\n";

        for (size_t bi = 0; bi < adt.constructors.size(); ++bi) {
            const auto& other = adt.constructors[bi];
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
                for (int i = 0; i < ctor.arity; ++i) os << "\n        end";
            }
            os << "\n";
        }
        os << "    end\n";
    }
    os << "end\n";

    return os.str();
}

static auto _reg_ord = DeriveEngine::register_strategy("Ord", {"compare"}, derive_ord);

// ===== Strategy: Hash =====

static std::string derive_hash(const DeriveAdtInfo& adt) {
    std::ostringstream os;
    os << "hash @borrow x = case x of\n";

    for (auto& ctor : adt.constructors) {
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
            for (int i = 0; i < ctor.arity; ++i) os << "(";
            os << ctor.tag;
            for (int i = 0; i < ctor.arity; ++i)
                os << " * 31 + hash _f" << i << ")";
        }
        os << "\n";
    }

    os << "end\n";
    return os.str();
}

static auto _reg_hash = DeriveEngine::register_strategy("Hash", {"hash"}, derive_hash);

} // namespace yona::compiler::codegen
