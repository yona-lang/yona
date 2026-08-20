#include "lsp/Analysis.h"

#include "Codegen.h"
#include "Diagnostic.h"
#include "Parser.h"
#include "typechecker/LinearityChecker.h"
#include "typechecker/RefinementChecker.h"
#include "typechecker/TypeChecker.h"
#include "typechecker/InferType.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace yona::lsp {
namespace {

bool looks_like_module(std::string_view src) {
    // Match lexer / yonac `is_module_source`: `#` starts a line comment
    // through newline (same as `##` docs). Stdlib modules often begin with
    // doc lines, so the first token is `module` only after those comments.
    std::size_t i = 0;
    const std::size_t n = src.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '#') {
            while (i < n && src[i] != '\n' && src[i] != '\r')
                ++i;
            continue;
        }
        break;
    }
    if (i + 6 > n)
        return false;
    return src.substr(i, 6) == "module" &&
           (i + 6 >= n || !std::isalnum(static_cast<unsigned char>(src[i + 6])));
}

bool is_ident_char(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '\'';
}

bool contains(const Range& r, Position p) {
    if (p.line < r.start.line || p.line > r.end.line)
        return false;
    if (p.line == r.start.line && p.character < r.start.character)
        return false;
    if (p.line == r.end.line && p.character > r.end.character)
        return false;
    return true;
}

int span_size(const Range& r) {
    if (r.end.line == r.start.line)
        return static_cast<int>(r.end.character - r.start.character);
    return 1000 + static_cast<int>(r.end.line - r.start.line);
}

Json range_json(const Range& r) {
    Json start;
    start["line"] = static_cast<int>(r.start.line);
    start["character"] = static_cast<int>(r.start.character);
    Json end;
    end["line"] = static_cast<int>(r.end.line);
    end["character"] = static_cast<int>(r.end.character);
    Json o;
    o["start"] = start;
    o["end"] = end;
    return o;
}

} // namespace

struct Occurrence {
    std::string name;
    Range range;
    bool is_def = false;
    std::string kind;
    std::string type;
};

struct Analysis::Impl {
    compiler::DiagnosticEngine diag;
    std::vector<std::string> module_paths;
    std::unique_ptr<parser::Parser> parser;
    std::unique_ptr<compiler::codegen::Codegen> codegen;
    std::unique_ptr<compiler::typechecker::TypeChecker> checker;
    std::shared_ptr<ast::AstNode> root;
    std::vector<Occurrence> occs;
    std::vector<SymbolInfo> symbols;

    void reset() {
        diag = compiler::DiagnosticEngine();
        parser = std::make_unique<parser::Parser>();
        if (!codegen)
            codegen = std::make_unique<compiler::codegen::Codegen>("yls", &diag);
        else
            codegen->module_paths_.clear();
        checker = std::make_unique<compiler::typechecker::TypeChecker>(diag);
        root.reset();
        occs.clear();
        symbols.clear();
    }

    void add_occ(const std::string& name, const SourceLocation& loc, std::string_view text,
                 bool is_def, const std::string& kind, compiler::typechecker::TypeChecker* tc,
                 ast::AstNode* typed) {
        if (name.empty())
            return;
        Occurrence o;
        o.name = name;
        o.range = source_to_range(text, loc);
        o.is_def = is_def;
        o.kind = kind;
        if (tc && typed) {
            if (auto* ty = tc->type_of(typed))
                o.type = compiler::typechecker::pretty_print(tc->zonk(ty));
        }
        occs.push_back(std::move(o));
        if (is_def) {
            SymbolInfo s;
            s.name = name;
            s.kind = kind;
            s.range = o.range;
            s.selection = o.range;
            s.type = o.type;
            symbols.push_back(std::move(s));
        }
    }

    void walk(ast::AstNode* node, std::string_view text, compiler::typechecker::TypeChecker* tc,
              bool in_pattern = false);
};

void Analysis::Impl::walk(ast::AstNode* node, std::string_view text, compiler::typechecker::TypeChecker* tc,
                          bool in_pattern) {
    if (!node)
        return;
    switch (node->get_type()) {
    case ast::AST_IDENTIFIER_EXPR: {
        auto* id = static_cast<ast::IdentifierExpr*>(node);
        if (id->name)
            add_occ(id->name->value, id->source_context, text, in_pattern, "variable", tc, node);
        return;
    }
    case ast::AST_NAME_EXPR: {
        auto* n = static_cast<ast::NameExpr*>(node);
        add_occ(n->value, n->source_context, text, in_pattern, "variable", tc, node);
        return;
    }
    case ast::AST_PATTERN_VALUE: {
        auto* pv = static_cast<ast::PatternValue*>(node);
        if (auto* id = std::get_if<ast::IdentifierExpr*>(&pv->expr)) {
            if (*id && (*id)->name)
                add_occ((*id)->name->value, (*id)->source_context, text, true, "variable", tc, *id);
        }
        return;
    }
    case ast::AST_AS_DATA_STRUCTURE_PATTERN: {
        auto* as = static_cast<ast::AsDataStructurePattern*>(node);
        if (as->identifier && as->identifier->name)
            add_occ(as->identifier->name->value, as->identifier->source_context, text, true, "variable",
                    tc, as->identifier);
        walk(as->pattern, text, tc, true);
        return;
    }
    case ast::AST_TUPLE_PATTERN: {
        auto* tp = static_cast<ast::TuplePattern*>(node);
        for (auto* p : tp->patterns)
            walk(p, text, tc, true);
        return;
    }
    case ast::AST_SEQ_PATTERN: {
        auto* sp = static_cast<ast::SeqPattern*>(node);
        for (auto* p : sp->patterns)
            walk(p, text, tc, true);
        return;
    }
    case ast::AST_HEAD_TAILS_PATTERN: {
        auto* ht = static_cast<ast::HeadTailsPattern*>(node);
        for (auto* h : ht->heads)
            walk(h, text, tc, true);
        walk(ht->tail, text, tc, true);
        return;
    }
    case ast::AST_TAILS_HEAD_PATTERN: {
        auto* th = static_cast<ast::TailsHeadPattern*>(node);
        walk(th->tail, text, tc, true);
        for (auto* h : th->heads)
            walk(h, text, tc, true);
        return;
    }
    case ast::AST_HEAD_TAILS_HEAD_PATTERN: {
        auto* hth = static_cast<ast::HeadTailsHeadPattern*>(node);
        for (auto* h : hth->left)
            walk(h, text, tc, true);
        walk(hth->tail, text, tc, true);
        for (auto* h : hth->right)
            walk(h, text, tc, true);
        return;
    }
    case ast::AST_DICT_PATTERN: {
        auto* dp = static_cast<ast::DictPattern*>(node);
        for (auto& kv : dp->keyValuePairs) {
            walk(kv.first, text, tc, true);
            walk(kv.second, text, tc, true);
        }
        return;
    }
    case ast::AST_RECORD_PATTERN: {
        auto* rp = static_cast<ast::RecordPattern*>(node);
        for (auto& item : rp->items) {
            walk(item.first, text, tc, false);
            walk(item.second, text, tc, true);
        }
        return;
    }
    case ast::AST_OR_PATTERN: {
        auto* op = static_cast<ast::OrPattern*>(node);
        for (auto& p : op->patterns)
            walk(p.get(), text, tc, true);
        return;
    }
    case ast::AST_CONSTRUCTOR_PATTERN: {
        auto* cp = static_cast<ast::ConstructorPattern*>(node);
        add_occ(cp->constructor_name, cp->source_context, text, false, "function", tc, node);
        for (auto* p : cp->sub_patterns)
            walk(p, text, tc, true);
        return;
    }
    case ast::AST_TYPED_PATTERN: {
        auto* tp = static_cast<ast::TypedPattern*>(node);
        add_occ(tp->binding_name, tp->source_context, text, true, "variable", tc, node);
        return;
    }
    case ast::AST_FUNCTION_EXPR: {
        auto* fn = static_cast<ast::FunctionExpr*>(node);
        if (!fn->name.empty())
            add_occ(fn->name, fn->source_context, text, true, "function", tc, node);
        for (auto* p : fn->patterns)
            walk(p, text, tc, true);
        for (auto* b : fn->bodies) {
            if (!b)
                continue;
            if (auto* wg = dynamic_cast<ast::BodyWithGuards*>(b)) {
                walk(wg->guard, text, tc);
                walk(wg->expr, text, tc);
            } else if (auto* ng = dynamic_cast<ast::BodyWithoutGuards*>(b)) {
                walk(ng->expr, text, tc);
            }
        }
        return;
    }
    case ast::AST_MODULE_DECL: {
        auto* m = static_cast<ast::ModuleDecl*>(node);
        if (m->fqn && m->fqn->moduleName)
            add_occ(m->fqn->moduleName->value, m->fqn->source_context, text, true, "namespace", tc, m);
        for (auto* fn : m->functions)
            walk(fn, text, tc);
        for (auto* adt : m->adt_declarations)
            walk(adt, text, tc);
        for (auto* tr : m->trait_declarations)
            walk(tr, text, tc);
        for (auto* inst : m->instance_declarations)
            walk(inst, text, tc);
        for (auto* ext : m->extern_declarations)
            walk(ext, text, tc);
        return;
    }
    case ast::AST_ADT_DECL: {
        auto* adt = static_cast<ast::AdtDeclNode*>(node);
        add_occ(adt->name, adt->source_context, text, true, "type", tc, node);
        for (auto* v : adt->variants) {
            if (v)
                add_occ(v->name, v->source_context, text, true, "function", tc, v);
        }
        return;
    }
    case ast::AST_TRAIT_DECL: {
        auto* tr = static_cast<ast::TraitDeclNode*>(node);
        add_occ(tr->name, tr->source_context, text, true, "interface", tc, node);
        return;
    }
    case ast::AST_INSTANCE_DECL: {
        auto* inst = static_cast<ast::InstanceDeclNode*>(node);
        add_occ(inst->trait_name, inst->source_context, text, false, "interface", tc, node);
        for (auto* fn : inst->methods)
            walk(fn, text, tc);
        return;
    }
    case ast::AST_EXTERN_DECL: {
        auto* ext = static_cast<ast::ExternDeclExpr*>(node);
        add_occ(ext->name, ext->source_context, text, true, "function", tc, node);
        walk(ext->body, text, tc);
        return;
    }
    case ast::AST_LET_EXPR: {
        auto* let = static_cast<ast::LetExpr*>(node);
        for (auto* a : let->aliases) {
            if (!a)
                continue;
            if (auto* va = dynamic_cast<ast::ValueAlias*>(a)) {
                if (va->identifier && va->identifier->name)
                    add_occ(va->identifier->name->value, va->identifier->source_context, text, true,
                            "variable", tc, va->identifier);
                walk(va->expr, text, tc);
            } else if (auto* la = dynamic_cast<ast::LambdaAlias*>(a)) {
                if (la->name)
                    add_occ(la->name->value, la->name->source_context, text, true, "function", tc, la->lambda);
                walk(la->lambda, text, tc);
            } else if (auto* pa = dynamic_cast<ast::PatternAlias*>(a)) {
                walk(pa->pattern, text, tc, true);
                walk(pa->expr, text, tc);
            }
        }
        walk(let->expr, text, tc);
        return;
    }
    case ast::AST_APPLY_EXPR: {
        auto* ap = static_cast<ast::ApplyExpr*>(node);
        walk(ap->call, text, tc);
        for (auto& a : ap->args) {
            if (auto* e = std::get_if<ast::ExprNode*>(&a))
                walk(*e, text, tc);
            else if (auto* v = std::get_if<ast::ValueExpr*>(&a))
                walk(*v, text, tc);
        }
        return;
    }
    case ast::AST_IF_EXPR: {
        auto* iff = static_cast<ast::IfExpr*>(node);
        walk(iff->condition, text, tc);
        walk(iff->thenExpr, text, tc);
        walk(iff->elseExpr, text, tc);
        return;
    }
    case ast::AST_CASE_EXPR: {
        auto* c = static_cast<ast::CaseExpr*>(node);
        walk(c->expr, text, tc);
        for (auto* cl : c->clauses) {
            if (!cl)
                continue;
            walk(cl->pattern, text, tc, true);
            walk(cl->guard, text, tc);
            walk(cl->body, text, tc);
        }
        return;
    }
    case ast::AST_DO_EXPR: {
        auto* d = static_cast<ast::DoExpr*>(node);
        for (auto* s : d->steps)
            walk(s, text, tc);
        return;
    }
    case ast::AST_WITH_EXPR: {
        auto* w = static_cast<ast::WithExpr*>(node);
        walk(w->contextExpr, text, tc);
        if (w->name)
            add_occ(w->name->value, w->name->source_context, text, true, "variable", tc, w->name);
        walk(w->bodyExpr, text, tc);
        return;
    }
    case ast::AST_TRY_CATCH_EXPR: {
        auto* tcch = static_cast<ast::TryCatchExpr*>(node);
        walk(tcch->tryExpr, text, tc);
        walk(tcch->catchExpr, text, tc);
        return;
    }
    case ast::AST_CATCH_EXPR: {
        auto* c = static_cast<ast::CatchExpr*>(node);
        for (auto* p : c->patterns)
            walk(p, text, tc);
        return;
    }
    case ast::AST_CATCH_PATTERN_EXPR: {
        auto* cp = static_cast<ast::CatchPatternExpr*>(node);
        walk(cp->matchPattern, text, tc, true);
        if (auto* ng = std::get_if<ast::PatternWithoutGuards*>(&cp->pattern)) {
            if (*ng)
                walk((*ng)->expr, text, tc);
        } else if (auto* gs = std::get_if<std::vector<ast::PatternWithGuards*>>(&cp->pattern)) {
            for (auto* g : *gs) {
                if (!g)
                    continue;
                walk(g->guard, text, tc);
                walk(g->expr, text, tc);
            }
        }
        return;
    }
    case ast::AST_IMPORT_EXPR: {
        auto* im = static_cast<ast::ImportExpr*>(node);
        for (auto* cl : im->clauses)
            walk(cl, text, tc);
        walk(im->expr, text, tc);
        return;
    }
    case ast::AST_FUNCTIONS_IMPORT: {
        auto* fi = static_cast<ast::FunctionsImport*>(node);
        for (auto* al : fi->aliases) {
            if (!al)
                continue;
            if (al->alias)
                add_occ(al->alias->value, al->alias->source_context, text, true, "function", tc, al);
            else if (al->name)
                add_occ(al->name->value, al->name->source_context, text, true, "function", tc, al);
        }
        return;
    }
    case ast::AST_MODULE_IMPORT: {
        auto* mi = static_cast<ast::ModuleImport*>(node);
        if (mi->name)
            add_occ(mi->name->value, mi->name->source_context, text, true, "namespace", tc, mi);
        return;
    }
    case ast::AST_TUPLE_EXPR: {
        auto* t = static_cast<ast::TupleExpr*>(node);
        for (auto* v : t->values)
            walk(v, text, tc);
        return;
    }
    case ast::AST_VALUES_SEQUENCE_EXPR: {
        auto* s = static_cast<ast::ValuesSequenceExpr*>(node);
        for (auto* v : s->values)
            walk(v, text, tc);
        return;
    }
    case ast::AST_RANGE_SEQUENCE_EXPR: {
        auto* r = static_cast<ast::RangeSequenceExpr*>(node);
        walk(r->start, text, tc);
        walk(r->end, text, tc);
        walk(r->step, text, tc);
        return;
    }
    case ast::AST_SET_EXPR: {
        auto* s = static_cast<ast::SetExpr*>(node);
        for (auto* v : s->values)
            walk(v, text, tc);
        return;
    }
    case ast::AST_DICT_EXPR: {
        auto* d = static_cast<ast::DictExpr*>(node);
        for (auto& kv : d->values) {
            walk(kv.first, text, tc);
            walk(kv.second, text, tc);
        }
        return;
    }
    case ast::AST_RECORD_INSTANCE_EXPR: {
        auto* r = static_cast<ast::RecordInstanceExpr*>(node);
        walk(r->recordType, text, tc);
        for (auto& item : r->items) {
            walk(item.first, text, tc);
            walk(item.second, text, tc);
        }
        return;
    }
    case ast::AST_RECORD_LITERAL_EXPR: {
        auto* r = static_cast<ast::RecordLiteralExpr*>(node);
        for (auto& f : r->fields)
            walk(f.second, text, tc);
        return;
    }
    case ast::AST_SEQ_GENERATOR_EXPR: {
        auto* g = static_cast<ast::SeqGeneratorExpr*>(node);
        walk(g->collectionExtractor, text, tc);
        walk(g->reducerExpr, text, tc);
        walk(g->stepExpression, text, tc);
        return;
    }
    case ast::AST_SET_GENERATOR_EXPR: {
        auto* g = static_cast<ast::SetGeneratorExpr*>(node);
        walk(g->collectionExtractor, text, tc);
        walk(g->reducerExpr, text, tc);
        walk(g->stepExpression, text, tc);
        return;
    }
    case ast::AST_DICT_GENERATOR_EXPR: {
        auto* g = static_cast<ast::DictGeneratorExpr*>(node);
        walk(g->collectionExtractor, text, tc);
        walk(g->reducerExpr, text, tc);
        walk(g->stepExpression, text, tc);
        return;
    }
    case ast::AST_VALUE_COLLECTION_EXTRACTOR_EXPR: {
        auto* ex = static_cast<ast::ValueCollectionExtractorExpr*>(node);
        if (auto* id = std::get_if<ast::IdentifierExpr*>(&ex->expr)) {
            if (*id && (*id)->name)
                add_occ((*id)->name->value, (*id)->source_context, text, true, "variable", tc, *id);
        }
        walk(ex->collection, text, tc);
        walk(ex->condition, text, tc);
        return;
    }
    case ast::AST_KEY_VALUE_COLLECTION_EXTRACTOR_EXPR: {
        auto* ex = static_cast<ast::KeyValueCollectionExtractorExpr*>(node);
        auto bind = [&](ast::IdentifierOrUnderscore& v) {
            if (auto* id = std::get_if<ast::IdentifierExpr*>(&v)) {
                if (*id && (*id)->name)
                    add_occ((*id)->name->value, (*id)->source_context, text, true, "variable", tc, *id);
            }
        };
        bind(ex->keyExpr);
        bind(ex->valueExpr);
        walk(ex->collection, text, tc);
        walk(ex->condition, text, tc);
        return;
    }
    case ast::AST_PERFORM_EXPR: {
        auto* p = static_cast<ast::PerformExpr*>(node);
        add_occ(p->operation_name, p->source_context, text, false, "function", tc, node);
        for (auto* a : p->args)
            walk(a, text, tc);
        return;
    }
    case ast::AST_HANDLE_EXPR: {
        auto* h = static_cast<ast::HandleExpr*>(node);
        walk(h->body, text, tc);
        for (auto* cl : h->clauses) {
            if (!cl)
                continue;
            for (const auto& n : cl->arg_names)
                add_occ(n, cl->source_context, text, true, "variable", tc, cl);
            if (!cl->resume_name.empty())
                add_occ(cl->resume_name, cl->source_context, text, true, "function", tc, cl);
            if (!cl->return_binding.empty())
                add_occ(cl->return_binding, cl->source_context, text, true, "variable", tc, cl);
            walk(cl->body, text, tc);
        }
        return;
    }
    case ast::AST_RAISE_EXPR: {
        auto* r = static_cast<ast::RaiseExpr*>(node);
        walk(r->value, text, tc);
        return;
    }
    case ast::AST_LOGICAL_NOT_OP_EXPR: {
        walk(static_cast<ast::LogicalNotOpExpr*>(node)->expr, text, tc);
        return;
    }
    case ast::AST_BINARY_NOT_OP_EXPR: {
        walk(static_cast<ast::BinaryNotOpExpr*>(node)->expr, text, tc);
        return;
    }
    case ast::AST_FIELD_ACCESS_EXPR: {
        auto* f = static_cast<ast::FieldAccessExpr*>(node);
        walk(f->identifier, text, tc);
        walk(f->name, text, tc);
        return;
    }
    case ast::AST_FIELD_UPDATE_EXPR: {
        auto* f = static_cast<ast::FieldUpdateExpr*>(node);
        walk(f->identifier, text, tc);
        for (auto& u : f->updates) {
            walk(u.first, text, tc);
            walk(u.second, text, tc);
        }
        return;
    }
    case ast::AST_MODULE_CALL: {
        auto* mc = static_cast<ast::ModuleCall*>(node);
        if (mc->funName)
            add_occ(mc->funName->value, mc->funName->source_context, text, false, "function", tc, node);
        if (auto* e = std::get_if<ast::ExprNode*>(&mc->fqn))
            walk(*e, text, tc);
        return;
    }
    case ast::AST_NAME_CALL: {
        auto* nc = static_cast<ast::NameCall*>(node);
        if (nc->name)
            add_occ(nc->name->value, nc->name->source_context, text, false, "function", tc, node);
        return;
    }
    case ast::AST_EXPR_CALL: {
        walk(static_cast<ast::ExprCall*>(node)->expr, text, tc);
        return;
    }
    case ast::AST_BINARY_OP_EXPR:
    case ast::AST_ADD_EXPR:
    case ast::AST_SUBTRACT_EXPR:
    case ast::AST_MULTIPLY_EXPR:
    case ast::AST_DIVIDE_EXPR:
    case ast::AST_MODULO_EXPR:
    case ast::AST_POWER_EXPR:
    case ast::AST_EQ_EXPR:
    case ast::AST_NEQ_EXPR:
    case ast::AST_LT_EXPR:
    case ast::AST_LTE_EXPR:
    case ast::AST_GT_EXPR:
    case ast::AST_GTE_EXPR:
    case ast::AST_LOGICAL_AND_EXPR:
    case ast::AST_LOGICAL_OR_EXPR:
    case ast::AST_PIPE_RIGHT_EXPR:
    case ast::AST_PIPE_LEFT_EXPR:
    case ast::AST_IN_EXPR:
    case ast::AST_CONS_LEFT_EXPR:
    case ast::AST_CONS_RIGHT_EXPR:
    case ast::AST_JOIN_EXPR:
    case ast::AST_REMOVE_EXPR:
    case ast::AST_LEFT_SHIFT_EXPR:
    case ast::AST_RIGHT_SHIFT_EXPR:
    case ast::AST_ZEROFILL_RIGHT_SHIFT_EXPR:
    case ast::AST_BITWISE_AND_EXPR:
    case ast::AST_BITWISE_OR_EXPR:
    case ast::AST_BITWISE_XOR_EXPR: {
        auto* b = static_cast<ast::BinaryOpExpr*>(node);
        walk(b->left, text, tc);
        walk(b->right, text, tc);
        return;
    }
    default:
        break;
    }
}

Analysis::Analysis() : impl_(std::make_unique<Impl>()) {}
Analysis::~Analysis() = default;
Analysis::Analysis(Analysis&&) noexcept = default;
Analysis& Analysis::operator=(Analysis&&) noexcept = default;

void Analysis::set_module_paths(std::vector<std::string> paths) {
    impl_->module_paths = std::move(paths);
}

void Analysis::analyze(std::string uri, std::string text) {
    uri_ = std::move(uri);
    text_ = std::move(text);
    impl_->reset();
    impl_->diag.set_source(text_, uri_);
    for (const auto& p : impl_->module_paths) {
        impl_->checker->add_module_path(p);
        impl_->codegen->module_paths_.push_back(p);
    }
    impl_->codegen->load_prelude(impl_->parser.get(), impl_->checker.get());
    impl_->checker->set_import_type_source(&impl_->codegen->import_types_);

    const bool is_mod = looks_like_module(text_);
    ast::AstNode* root = nullptr;
    if (is_mod) {
        auto result = impl_->parser->parse_module(text_, uri_);
        if (!result.has_value()) {
            for (auto& e : result.error())
                impl_->diag.error(e.location, compiler::ErrorCode::E0301, e.message);
        } else {
            impl_->root = std::shared_ptr<ast::AstNode>(result.value().release());
            root = impl_->root.get();
            impl_->checker->check_module(static_cast<ast::ModuleDecl*>(root));
        }
    } else {
        auto result = impl_->parser->parse_expression(text_, uri_);
        if (!result.has_value()) {
            for (auto& e : result.error())
                impl_->diag.error(e.location, compiler::ErrorCode::E0301, e.message);
        } else {
            impl_->root = std::shared_ptr<ast::AstNode>(result.value().release());
            root = impl_->root.get();
            impl_->checker->check(root);
        }
    }
    if (root) {
        compiler::typechecker::RefinementChecker refine(impl_->diag, impl_->checker.get());
        refine.check(root);
        compiler::typechecker::LinearityChecker lin(impl_->diag, impl_->checker.get());
        lin.check(root);
        impl_->walk(root, text_, impl_->checker.get());
    }
}

std::vector<LspDiagnostic> Analysis::diagnostics() const {
    std::vector<LspDiagnostic> out;
    for (const auto& rec : impl_->diag.records()) {
        LspDiagnostic d;
        d.range = source_to_range(text_, rec.loc);
        d.severity = rec.level == compiler::DiagLevel::Error ? 1
                     : rec.level == compiler::DiagLevel::Warning ? 2
                                                                 : 3;
        if (rec.code)
            d.code = compiler::error_code_str(*rec.code);
        d.message = rec.message;
        out.push_back(std::move(d));
    }
    return out;
}

const Occurrence* find_at(const std::vector<Occurrence>& occs, Position pos) {
    const Occurrence* best = nullptr;
    int best_sz = 1 << 30;
    for (const auto& o : occs) {
        if (!contains(o.range, pos))
            continue;
        int sz = span_size(o.range);
        if (sz < best_sz) {
            best_sz = sz;
            best = &o;
        }
    }
    return best;
}

std::optional<HoverInfo> Analysis::hover(Position pos) const {
    auto* o = find_at(impl_->occs, pos);
    if (!o)
        return std::nullopt;
    HoverInfo h;
    h.range = o->range;
    std::ostringstream os;
    os << o->name;
    if (!o->type.empty())
        os << " : " << o->type;
    h.contents = os.str();
    return h;
}

std::vector<Range> Analysis::definition(Position pos) const {
    auto* o = find_at(impl_->occs, pos);
    if (!o)
        return {};
    std::vector<Range> out;
    for (const auto& c : impl_->occs) {
        if (c.is_def && c.name == o->name)
            out.push_back(c.range);
    }
    return out;
}

std::vector<Range> Analysis::references(Position pos, bool include_decl) const {
    auto* o = find_at(impl_->occs, pos);
    if (!o)
        return {};
    std::vector<Range> out;
    for (const auto& c : impl_->occs) {
        if (c.name != o->name)
            continue;
        if (!include_decl && c.is_def)
            continue;
        out.push_back(c.range);
    }
    return out;
}

std::vector<SymbolInfo> Analysis::document_symbols() const {
    return impl_->symbols;
}

std::vector<Json> Analysis::completions(Position pos) const {
    (void)pos;
    static const char* keywords[] = {
        "module", "import", "from", "as", "export", "let", "in", "if", "then", "else",
        "case",   "of",     "do",   "end", "try",   "catch", "raise", "with", "extern",
        "for",    "effect", "perform", "handle", "fun", "lambda", "record", "type",
        "trait",  "instance", "deriving", "true", "false", "async", "native", "io", "resume"};
    std::unordered_set<std::string> seen;
    std::vector<Json> items;
    auto add = [&](const std::string& name, const std::string& kind, const std::string& detail) {
        if (!seen.insert(name).second)
            return;
        Json it;
        it["label"] = name;
        it["kind"] = kind == "function" ? 3 : kind == "keyword" ? 14 : 6;
        if (!detail.empty())
            it["detail"] = detail;
        items.push_back(std::move(it));
    };
    for (auto* k : keywords)
        add(k, "keyword", "");
    for (const auto& s : impl_->symbols)
        add(s.name, s.kind, s.type);
    for (const auto& o : impl_->occs)
        add(o.name, o.kind, o.type);
    return items;
}

std::vector<std::uint32_t> Analysis::semantic_tokens() const {
    std::vector<Occurrence> sorted = impl_->occs;
    std::sort(sorted.begin(), sorted.end(), [](const Occurrence& a, const Occurrence& b) {
        if (a.range.start.line != b.range.start.line)
            return a.range.start.line < b.range.start.line;
        return a.range.start.character < b.range.start.character;
    });
    std::vector<std::uint32_t> data;
    std::size_t prev_line = 0, prev_col = 0;
    for (const auto& o : sorted) {
        std::uint32_t type = 8; // variable
        if (o.kind == "function")
            type = 0;
        else if (o.kind == "type" || o.kind == "class")
            type = 1;
        else if (o.kind == "namespace")
            type = 2;
        else if (o.kind == "interface")
            type = 1;
        auto line = o.range.start.line;
        auto col = o.range.start.character;
        auto delta_line = static_cast<std::uint32_t>(line - prev_line);
        auto delta_col = static_cast<std::uint32_t>(line == prev_line ? col - prev_col : col);
        auto len = static_cast<std::uint32_t>(
            o.range.end.line == o.range.start.line
                ? o.range.end.character - o.range.start.character
                : o.name.size());
        std::uint32_t mods = o.is_def ? 1u : 0u;
        data.push_back(delta_line);
        data.push_back(delta_col);
        data.push_back(len);
        data.push_back(type);
        data.push_back(mods);
        prev_line = line;
        prev_col = col;
    }
    return data;
}

std::optional<std::string> Analysis::rename(Position pos, std::string_view new_name, Json& edits) const {
    auto* o = find_at(impl_->occs, pos);
    if (!o)
        return std::nullopt;
    Json::Array changes;
    for (const auto& c : impl_->occs) {
        if (c.name != o->name)
            continue;
        Json edit;
        edit["range"] = range_json(c.range);
        edit["newText"] = std::string(new_name);
        changes.push_back(std::move(edit));
    }
    Json file;
    file["textDocument"] = Json::Object{{"uri", Json(uri_)}};
    file["edits"] = changes;
    edits = Json::Array{file};
    return o->name;
}

std::optional<Json> Analysis::signature_help(Position pos) const {
    auto make_help = [](const Occurrence& o) -> std::optional<Json> {
        Json sig;
        sig["label"] = o.type.empty() ? o.name : o.name + " : " + o.type;
        Json help;
        help["signatures"] = Json::Array{sig};
        help["activeSignature"] = 0;
        help["activeParameter"] = 0;
        return help;
    };
    if (auto* o = find_at(impl_->occs, pos))
        return make_help(*o);
    // Juxtaposition `f x`: the space trigger leaves the cursor after `f`,
    // so scan left for the applied name.
    auto off = position_to_offset(text_, pos);
    if (off > 0)
        --off;
    while (off > 0 && std::isspace(static_cast<unsigned char>(text_[off])))
        --off;
    while (off > 0 && is_ident_char(static_cast<unsigned char>(text_[off - 1])))
        --off;
    if (off >= text_.size() || !is_ident_char(static_cast<unsigned char>(text_[off])))
        return std::nullopt;
    auto* o = find_at(impl_->occs, offset_to_position(text_, off));
    return o ? make_help(*o) : std::nullopt;
}

std::vector<Json> Analysis::inlay_hints(Range range) const {
    std::vector<Json> out;
    for (const auto& s : impl_->symbols) {
        if (!contains(range, s.range.end) && !contains(range, s.range.start))
            continue;
        if (s.type.empty())
            continue;
        Json h;
        h["position"] = Json::Object{
            {"line", Json(static_cast<int>(s.range.end.line))},
            {"character", Json(static_cast<int>(s.range.end.character))},
        };
        h["label"] = " : " + s.type;
        h["kind"] = 1;
        out.push_back(std::move(h));
    }
    return out;
}

std::optional<Json> Analysis::prepare_call_hierarchy(Position pos) const {
    auto* o = find_at(impl_->occs, pos);
    if (!o)
        return std::nullopt;
    Json item;
    item["name"] = o->name;
    item["kind"] = 12;
    item["uri"] = uri_;
    item["range"] = range_json(o->range);
    item["selectionRange"] = range_json(o->range);
    return item;
}

std::vector<Json> Analysis::incoming_calls(std::string_view name) const {
    (void)name;
    return {};
}

std::vector<Json> Analysis::outgoing_calls(std::string_view name) const {
    (void)name;
    return {};
}

std::vector<Json> Analysis::code_actions(Range range) const {
    std::vector<Json> out;
    for (const auto& d : diagnostics()) {
        if (!contains(d.range, range.start) && !contains(range, d.range.start))
            continue;
        if (d.code.empty())
            continue;
        Json act;
        act["title"] = "Explain " + d.code;
        act["kind"] = "quickfix";
        act["command"] = Json::Object{
            {"title", Json("Explain " + d.code)},
            {"command", Json("yona.explain")},
            {"arguments", Json::Array{Json(d.code)}},
        };
        out.push_back(std::move(act));
    }
    return out;
}

std::vector<SymbolInfo> Analysis::workspace_symbols(std::string_view query) const {
    std::vector<SymbolInfo> out;
    for (const auto& s : impl_->symbols) {
        if (query.empty() || s.name.find(query) != std::string::npos)
            out.push_back(s);
    }
    return out;
}

std::vector<std::string> default_module_paths(std::string_view document_path,
                                              const std::vector<std::string>& workspace_roots) {
    std::vector<std::string> paths;
    std::unordered_set<std::string> seen;
    auto add = [&](const std::filesystem::path& p) {
        std::error_code ec;
        auto c = std::filesystem::weakly_canonical(p, ec);
        if (ec)
            c = p;
        auto s = c.string();
        if (seen.insert(s).second)
            paths.push_back(s);
    };
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    if (const char* yp = std::getenv("YONA_PATH"); yp && *yp) {
        std::string cur;
        for (const char* c = yp; *c; ++c) {
            if (*c == sep) {
                if (!cur.empty())
                    add(cur);
                cur.clear();
            } else {
                cur.push_back(*c);
            }
        }
        if (!cur.empty())
            add(cur);
    }
    for (const auto& root : workspace_roots) {
        if (!root.empty())
            add(root);
    }
    if (!document_path.empty()) {
        auto parent = std::filesystem::path(std::string(document_path)).parent_path();
        if (!parent.empty())
            add(parent);
    }
    add(".");
    if (const char* home = std::getenv("YONA_HOME"); home && *home) {
        add(std::filesystem::path(home) / "lib");
        add(std::filesystem::path(home) / "share" / "yona" / "lib");
    }
    for (auto* cand : {"lib", "../lib", "../../lib", "../../../lib"}) {
        if (std::filesystem::exists(std::filesystem::path(cand) / "Prelude.yonai")) {
            add(cand);
            break;
        }
    }
    return paths;
}

} // namespace yona::lsp
