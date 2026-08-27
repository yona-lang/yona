#include "PatternAnalysis.h"

namespace yona::compiler::pattern_analysis {
using namespace yona::ast;

bool covers(PatternNode* cover, PatternNode* candidate) {
    if (!cover || !candidate) return false;
    if (cover->get_type() == AST_UNDERSCORE_PATTERN || cover->get_type() == AST_UNDERSCORE_NODE) return true;
    if (cover->get_type() == AST_PATTERN_VALUE) {
        auto* value = static_cast<PatternValue*>(cover);
        if (std::get_if<IdentifierExpr*>(&value->expr)) return true;
        if (candidate->get_type() != AST_PATTERN_VALUE) return false;
        auto* other = static_cast<PatternValue*>(candidate);
        if (auto* left = std::get_if<LiteralExpr<nullptr_t>*>(&value->expr)) {
            auto* right = std::get_if<LiteralExpr<nullptr_t>*>(&other->expr);
            return right && reinterpret_cast<AstNode*>(*left)->get_type() == reinterpret_cast<AstNode*>(*right)->get_type();
        }
        if (auto* left = std::get_if<LiteralExpr<void*>*>(&value->expr)) {
            auto* right = std::get_if<LiteralExpr<void*>*>(&other->expr);
            if (!right) return false;
            auto* lhs = reinterpret_cast<AstNode*>(*left);
            auto* rhs = reinterpret_cast<AstNode*>(*right);
            if (lhs->get_type() != rhs->get_type()) return false;
            return lhs->get_type() != AST_INTEGER_EXPR || static_cast<IntegerExpr*>(lhs)->value == static_cast<IntegerExpr*>(rhs)->value;
        }
        if (auto* left = std::get_if<SymbolExpr*>(&value->expr)) {
            auto* right = std::get_if<SymbolExpr*>(&other->expr);
            return right && (*left)->value == (*right)->value;
        }
        return false;
    }
    if (cover->get_type() == AST_OR_PATTERN) {
        for (const auto& alternative : static_cast<OrPattern*>(cover)->patterns)
            if (covers(alternative.get(), candidate)) return true;
        return false;
    }
    if (cover->get_type() == AST_CONSTRUCTOR_PATTERN && candidate->get_type() == AST_CONSTRUCTOR_PATTERN) {
        auto* left = static_cast<ConstructorPattern*>(cover);
        auto* right = static_cast<ConstructorPattern*>(candidate);
        if (left->constructor_name != right->constructor_name || left->sub_patterns.size() != right->sub_patterns.size()) return false;
        for (size_t i = 0; i < left->sub_patterns.size(); ++i) if (!covers(left->sub_patterns[i], right->sub_patterns[i])) return false;
        return true;
    }
    auto covers_product = [&](const auto& left, const auto& right) {
        if (left->patterns.size() != right->patterns.size()) return false;
        for (size_t i = 0; i < left->patterns.size(); ++i) if (!covers(left->patterns[i], right->patterns[i])) return false;
        return true;
    };
    if (cover->get_type() == AST_TUPLE_PATTERN && candidate->get_type() == AST_TUPLE_PATTERN)
        return covers_product(static_cast<TuplePattern*>(cover), static_cast<TuplePattern*>(candidate));
    if (cover->get_type() == AST_SEQ_PATTERN && candidate->get_type() == AST_SEQ_PATTERN)
        return covers_product(static_cast<SeqPattern*>(cover), static_cast<SeqPattern*>(candidate));
    return false;
}
} // namespace yona::compiler::pattern_analysis
