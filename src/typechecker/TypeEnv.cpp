/// TypeEnv — lexical scope chain for type bindings.
///
/// Supports nested scopes (let, function bodies, case arms) via parent
/// pointers. Builtin operators and types are registered in the root env.

#include "typechecker/TypeEnv.h"
#include "typechecker/TypeArena.h"

namespace yona::compiler::typechecker {

TypeEnv::TypeEnv(std::shared_ptr<TypeEnv> parent) : parent_(std::move(parent)) {}

std::optional<TypeScheme> TypeEnv::lookup(const std::string& name) const {
    auto it = bindings_.find(name);
    if (it != bindings_.end()) return it->second;
    if (parent_) return parent_->lookup(name);
    return std::nullopt;
}

void TypeEnv::bind(const std::string& name, MonoTypePtr type) {
    bindings_[name] = TypeScheme(type);
}

void TypeEnv::bind_scheme(const std::string& name, TypeScheme scheme) {
    bindings_[name] = std::move(scheme);
}

bool TypeEnv::bound_through(const std::string& name,
                            const TypeEnv* ancestor) const {
    for (const TypeEnv* scope = this; scope; scope = scope->parent_.get()) {
        if (scope->bindings_.count(name)) return true;
        if (scope == ancestor) break;
    }
    return false;
}

std::shared_ptr<TypeEnv> TypeEnv::child() const {
    // const_cast is safe: parent_ is only read, never mutated via child
    auto self = std::const_pointer_cast<TypeEnv>(shared_from_this());
    return std::make_shared<TypeEnv>(self);
}

// ===== Builtin Registration =====

/// Helper: create a polymorphic binary operator scheme.
/// For `a -> a -> a` (arithmetic), `a -> a -> Bool` (comparison), etc.
static TypeScheme make_binop_scheme(TypeArena& arena, MonoTypePtr arg_type, MonoTypePtr ret_type) {
    auto* fn = arena.make_arrow(arg_type, arena.make_arrow(arg_type, ret_type));
    return TypeScheme(fn);
}

void register_builtins(TypeEnv& env, TypeArena& arena) {
    auto* int_t   = arena.make_con(TyCon::Int);
    auto* float_t = arena.make_con(TyCon::Float);
    auto* bool_t  = arena.make_con(TyCon::Bool);
    auto* str_t   = arena.make_con(TyCon::String);
    auto* unit_t  = arena.make_con(TyCon::Unit);

    // Arithmetic: a -> a -> a (polymorphic — works for Int and Float)
    {
        auto* a = arena.fresh_var(0);
        auto* fn = arena.make_arrow(a, arena.make_arrow(a, a));
        TypeScheme arith_scheme({a->var_id}, fn);
        for (auto& op : {"+", "-", "*", "/", "%"})
            env.bind_scheme(op, arith_scheme);
    }

    // Comparison: a -> a -> Bool (polymorphic)
    {
        auto* a = arena.fresh_var(0);
        auto* fn = arena.make_arrow(a, arena.make_arrow(a, bool_t));
        TypeScheme cmp_scheme({a->var_id}, fn);
        for (auto& op : {"==", "!=", "<", ">", "<=", ">="})
            env.bind_scheme(op, cmp_scheme);
    }

    // Logical: Bool -> Bool -> Bool
    for (auto& op : {"&&", "||"})
        env.bind_scheme(op, make_binop_scheme(arena, bool_t, bool_t));

    // String concat: String -> String -> String
    env.bind_scheme("++", make_binop_scheme(arena, str_t, str_t));

    // Cons: a -> [a] -> [a]
    {
        auto* a = arena.fresh_var(0);
        auto* seq_a = arena.make_app("Seq", {a});
        auto* fn = arena.make_arrow(a, arena.make_arrow(seq_a, seq_a));
        env.bind_scheme("::", TypeScheme({a->var_id}, fn));
    }

    // Pipe right: a -> (a -> b) -> b
    {
        auto* a = arena.fresh_var(0);
        auto* b = arena.fresh_var(0);
        auto* fn = arena.make_arrow(a, arena.make_arrow(arena.make_arrow(a, b), b));
        env.bind_scheme("|>", TypeScheme({a->var_id, b->var_id}, fn));
    }
    // Pipe left: (a -> b) -> a -> b
    {
        auto* a = arena.fresh_var(0);
        auto* b = arena.fresh_var(0);
        auto* fn = arena.make_arrow(arena.make_arrow(a, b), arena.make_arrow(a, b));
        env.bind_scheme("<|", TypeScheme({a->var_id, b->var_id}, fn));
    }

    // Negate: Int -> Int
    env.bind_scheme("negate", TypeScheme(arena.make_arrow(int_t, int_t)));

    // not: Bool -> Bool
    env.bind_scheme("not", TypeScheme(arena.make_arrow(bool_t, bool_t)));

    // Type names (for annotations / lookups)
    env.bind("Int", int_t);
    env.bind("Float", float_t);
    env.bind("Bool", bool_t);
    env.bind("String", str_t);
    env.bind("Unit", unit_t);

    // true/false as Bool
    env.bind("true", bool_t);
    env.bind("false", bool_t);
}

std::vector<std::string> TypeEnv::all_names() const {
    std::vector<std::string> result;
    for (auto& [name, _] : bindings_)
        result.push_back(name);
    if (parent_) {
        auto parent_names = parent_->all_names();
        result.insert(result.end(), parent_names.begin(), parent_names.end());
    }
    return result;
}

} // namespace yona::compiler::typechecker
