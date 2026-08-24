#ifndef YONA_TYPECHECKER_REFINEMENT_CHECKER_H
#define YONA_TYPECHECKER_REFINEMENT_CHECKER_H

/// Refinement type checker for Yona.
///
/// A flow-sensitive analysis that tracks known facts (integer intervals,
/// collection non-emptiness) through the program and verifies that
/// refinement predicates are satisfied at call sites.
///
/// Built-in refined functions:
///   - head/tail: require non-empty first argument
///   - /: requires non-zero second argument
///
/// Facts are established by:
///   - Integer literals in let bindings
///   - Sequence literals and cons (::) expressions
///   - Head-tail [h|t] and empty [] pattern matches
///   - Wildcard _ patterns (complement of prior matched values)
///   - Guard expressions (n | n > 0)
///   - If conditions (>, <, >=, <=, ==, !=)
///   - Arithmetic (+, -, *) on known intervals

#include "Diagnostic.h"
#include "ast.h"
#include "types.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <optional>
#include <limits>

namespace yona::compiler::typechecker {

class TypeChecker;

/// An integer interval [lo, hi].
struct Interval {
    int64_t lo = std::numeric_limits<int64_t>::min();
    int64_t hi = std::numeric_limits<int64_t>::max();

    static Interval exact(int64_t v) { return {v, v}; }
    static Interval above(int64_t lo) { return {lo, std::numeric_limits<int64_t>::max()}; }
    static Interval below(int64_t hi) { return {std::numeric_limits<int64_t>::min(), hi}; }
    static Interval full() { return {}; }

    bool contains(int64_t v) const { return v >= lo && v <= hi; }
    bool is_positive() const { return lo > 0; }
    bool excludes(int64_t v) const { return v < lo || v > hi; }

    /// Interval arithmetic
    Interval add(int64_t c) const;
    Interval sub(int64_t c) const;
};

/// Facts known to be true at a program point.
struct FactEnv {
    /// Integer bounds: variable name -> interval
    std::unordered_map<std::string, Interval> int_bounds;

    /// Variables known to refer to non-empty sequences
    std::unordered_set<std::string> non_empty_seqs;

    /// Integer values known to be excluded for a variable
    std::unordered_map<std::string, std::unordered_set<int64_t>> excluded_values;

    /// Check if a refinement predicate is satisfied by current facts.
    /// The predicate's var_name is substituted with actual_var.
    bool satisfies(const types::RefinePredicate& pred, const std::string& actual_var) const;

    /// Check with original var names (no substitution).
    bool satisfies(const types::RefinePredicate& pred) const;

    /// Create copies with additional facts.
    FactEnv with_int_bound(const std::string& var, Interval interval) const;
    FactEnv with_non_empty(const std::string& var) const;
    FactEnv with_excluded(const std::string& var, int64_t value) const;
};

/// Registered refined type: name -> (base type, predicate)
struct RefinedTypeInfo {
    std::string name;
    types::Type base_type;
    std::shared_ptr<types::RefinePredicate> predicate;
};

/// A built-in function with a refinement requirement on one of its arguments.
struct BuiltinRefinement {
    int arg_index;   ///< Which argument (0-based) must satisfy the predicate
    enum Kind { NonEmpty, NonZero } kind;
};

class RefinementChecker {
public:
    /// \p tc optional; when set, warns on discarded algebraic values (see \c warn_if_discarded_adt).
    explicit RefinementChecker(DiagnosticEngine& diag, TypeChecker* tc = nullptr);

    /// Register a refined type alias.
    void register_refined_type(const std::string& name,
                                const types::Type& base_type,
                                std::shared_ptr<types::RefinePredicate> predicate);

    /// Look up a refined type by name (returns nullptr if not refined).
    const RefinedTypeInfo* lookup(const std::string& name) const;

    /// Check an AST tree for refinement violations.
    void check(ast::AstNode* node);

    bool has_errors() const { return error_count_ > 0; }

private:
    void check_node(ast::AstNode* node, FactEnv& facts);
    void check_let(ast::LetExpr* node, FactEnv& facts);
    void check_case(ast::CaseExpr* node, FactEnv& facts);
    void check_apply(ast::ApplyExpr* node, const FactEnv& facts);
    void check_if(ast::IfExpr* node, FactEnv& facts);
    void check_function(ast::FunctionExpr* node, FactEnv& facts);
    void check_module(ast::ModuleDecl* node, FactEnv& facts);

    /// If \p tc was provided and \p expr has a named-sum type (App other than Seq/Set/Dict),
    /// emit \c WarningFlag::UnmatchedAdt (enable with \c -Wall or \c enable_warning).
    void warn_if_discarded_adt(ast::AstNode* expr);

    /// Extract the variable name from an argument expression (if it's an identifier).
    static std::string arg_var_name(ast::AstNode* node);

    /// Narrow a FactEnv from a comparison condition node.
    static void narrow_from_condition(ast::AstNode* cond, FactEnv& then_facts, FactEnv& else_facts);

    /// Establish facts from a let-bound expression.
    void establish_let_facts(const std::string& name, ast::AstNode* expr, FactEnv& facts);

    DiagnosticEngine& diag_;
    TypeChecker* tc_ = nullptr;
    int error_count_ = 0;

    /// Registry of refined type aliases.
    std::unordered_map<std::string, RefinedTypeInfo> refined_types_;

    /// Built-in function refinement requirements.
    std::unordered_map<std::string, BuiltinRefinement> builtin_refinements_;
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_REFINEMENT_CHECKER_H
