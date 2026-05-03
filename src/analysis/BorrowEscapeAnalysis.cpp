#include "analysis/BorrowEscapeAnalysis.h"

#include "ast.h"

#include <variant>

namespace yona::compiler::analysis {
using namespace yona::ast;

int count_identifier_refs(AstNode* node, const std::string& name) {
    if (!node) return 0;
    auto ty = node->get_type();

    if (ty == AST_IDENTIFIER_EXPR)
        return static_cast<IdentifierExpr*>(node)->name->value == name ? 1 : 0;

    if (dynamic_cast<BinaryOpExpr*>(static_cast<AstNode*>(node))) {
        auto* b = static_cast<BinaryOpExpr*>(node);
        return count_identifier_refs(b->left, name) + count_identifier_refs(b->right, name);
    }

    if (ty == AST_IF_EXPR) {
        auto* e = static_cast<IfExpr*>(node);
        return count_identifier_refs(e->condition, name) + count_identifier_refs(e->thenExpr, name)
            + count_identifier_refs(e->elseExpr, name);
    }
    if (ty == AST_LET_EXPR) {
        auto* e = static_cast<LetExpr*>(node);
        int c = 0;
        for (auto* a : e->aliases) {
            if (auto* va = dynamic_cast<ValueAlias*>(a)) {
                c += count_identifier_refs(va->expr, name);
                if (va->identifier->name->value == name) return c;
            } else if (auto* la = dynamic_cast<LambdaAlias*>(a)) {
                c += count_identifier_refs(la->lambda, name);
                if (la->name->value == name) return c;
            }
        }
        return c + count_identifier_refs(e->expr, name);
    }
    if (ty == AST_CASE_EXPR) {
        auto* e = static_cast<CaseExpr*>(node);
        int c = count_identifier_refs(e->expr, name);
        for (auto* clause : e->clauses)
            c += count_identifier_refs(clause->body, name);
        return c;
    }
    if (ty == AST_APPLY_EXPR) {
        auto* e = static_cast<ApplyExpr*>(node);
        int c = 0;
        if (auto* nc = dynamic_cast<NameCall*>(e->call)) {
            c += (nc->name->value == name) ? 1 : 0;
        } else if (auto* ec = dynamic_cast<ExprCall*>(e->call)) {
            if (ec->expr) c += count_identifier_refs(ec->expr, name);
        }
        for (auto& arg : e->args) {
            if (std::holds_alternative<ExprNode*>(arg))
                c += count_identifier_refs(std::get<ExprNode*>(arg), name);
            else
                c += count_identifier_refs(std::get<ValueExpr*>(arg), name);
        }
        return c;
    }
    if (ty == AST_TUPLE_EXPR) {
        auto* e = static_cast<TupleExpr*>(node);
        int c = 0;
        for (auto* v : e->values) c += count_identifier_refs(v, name);
        return c;
    }
    if (ty == AST_VALUES_SEQUENCE_EXPR) {
        auto* e = static_cast<ValuesSequenceExpr*>(node);
        int c = 0;
        for (auto* v : e->values) c += count_identifier_refs(v, name);
        return c;
    }
    if (ty == AST_SEQ_GENERATOR_EXPR) {
        auto* g = static_cast<SeqGeneratorExpr*>(node);
        auto* ext = static_cast<ValueCollectionExtractorExpr*>(g->collectionExtractor);
        int c = count_identifier_refs(ext->collection, name);
        std::string var;
        if (auto* id = std::get_if<IdentifierExpr*>(&ext->expr))
            var = (*id)->name->value;
        if (var != name) {
            c += count_identifier_refs(g->reducerExpr, name);
            if (ext->condition) c += count_identifier_refs(ext->condition, name);
        }
        return c;
    }
    if (ty == AST_FUNCTION_EXPR) {
        auto* f = static_cast<FunctionExpr*>(node);
        for (auto& p : f->patterns) {
            if (p->get_type() == AST_PATTERN_VALUE) {
                auto* pv = static_cast<PatternValue*>(p);
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                    if ((*id)->name->value == name) return 0;
            }
        }
        int c = 0;
        for (auto* body : f->bodies)
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
                c += count_identifier_refs(bwg->expr, name);
        return c;
    }
    if (ty == AST_DO_EXPR) {
        auto* e = static_cast<DoExpr*>(node);
        int c = 0;
        for (auto* s : e->steps) c += count_identifier_refs(s, name);
        return c;
    }
    return 0;
}

bool heap_param_may_escape(AstNode* node, const std::string& name, bool is_return_position) {
    if (!node) return false;
    auto ty = node->get_type();

    if (ty == AST_IDENTIFIER_EXPR) {
        if (static_cast<IdentifierExpr*>(node)->name->value == name)
            return is_return_position;
        return false;
    }

    if (ty == AST_VALUES_SEQUENCE_EXPR) {
        auto* e = static_cast<ValuesSequenceExpr*>(node);
        for (auto* v : e->values)
            if (count_identifier_refs(v, name) > 0) return true;
        return false;
    }
    if (ty == AST_TUPLE_EXPR) {
        auto* e = static_cast<TupleExpr*>(node);
        for (auto* v : e->values)
            if (count_identifier_refs(v, name) > 0) return true;
        return false;
    }

    if (ty == AST_CONS_LEFT_EXPR) {
        auto* e = static_cast<ConsLeftExpr*>(node);
        if (count_identifier_refs(e->left, name) > 0) return true;
        if (count_identifier_refs(e->right, name) > 0) return true;
        return false;
    }

    if (ty == AST_FUNCTION_EXPR) {
        auto* f = static_cast<FunctionExpr*>(node);
        for (auto* pat : f->patterns) {
            if (auto* pv = dynamic_cast<PatternValue*>(pat)) {
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                    if ((*id)->name->value == name) return false;
            }
        }
        for (auto* body : f->bodies)
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
                if (count_identifier_refs(bwg->expr, name) > 0) return true;
        return false;
    }

    if (dynamic_cast<BinaryOpExpr*>(node)) {
        auto* b = static_cast<BinaryOpExpr*>(node);
        return heap_param_may_escape(b->left, name, false) || heap_param_may_escape(b->right, name, false);
    }

    if (ty == AST_IF_EXPR) {
        auto* e = static_cast<IfExpr*>(node);
        return heap_param_may_escape(e->condition, name, false)
            || heap_param_may_escape(e->thenExpr, name, is_return_position)
            || heap_param_may_escape(e->elseExpr, name, is_return_position);
    }

    if (ty == AST_LET_EXPR) {
        auto* e = static_cast<LetExpr*>(node);
        for (auto* a : e->aliases) {
            if (auto* va = dynamic_cast<ValueAlias*>(a)) {
                if (heap_param_may_escape(va->expr, name, false)) return true;
                if (va->identifier->name->value == name) return false;
            } else if (auto* la = dynamic_cast<LambdaAlias*>(a)) {
                if (heap_param_may_escape(la->lambda, name, false)) return true;
                if (la->name->value == name) return false;
            }
        }
        return heap_param_may_escape(e->expr, name, is_return_position);
    }

    if (ty == AST_CASE_EXPR) {
        auto* e = static_cast<CaseExpr*>(node);
        if (count_identifier_refs(e->expr, name) > 0) return true;
        for (auto* clause : e->clauses)
            if (heap_param_may_escape(clause->body, name, is_return_position)) return true;
        return false;
    }

    if (ty == AST_APPLY_EXPR) {
        return false;
    }

    if (ty == AST_DO_EXPR) {
        auto* e = static_cast<DoExpr*>(node);
        for (size_t i = 0; i < e->steps.size(); i++) {
            bool last = (i == e->steps.size() - 1);
            if (heap_param_may_escape(e->steps[i], name, last && is_return_position)) return true;
        }
        return false;
    }

    return count_identifier_refs(node, name) > 0;
}

} // namespace yona::compiler::analysis
