#include "typed_core/abi.h"

#include "Codegen.h"
#include "Diagnostic.h"
#include "ModuleSource.h"
#include "Parser.h"
#include "typechecker/InferType.h"
#include "typechecker/LinearityChecker.h"
#include "typechecker/RefinementChecker.h"
#include "typechecker/TypeChecker.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using yona::SourceLocation;
using yona::compiler::DiagnosticEngine;
using yona::compiler::ErrorCode;
using yona::compiler::codegen::Codegen;
using yona::compiler::typechecker::LinearityChecker;
using yona::compiler::typechecker::MonoTypePtr;
using yona::compiler::typechecker::RefinementChecker;
using yona::compiler::typechecker::TypeChecker;

struct Node {
    YonaTcKind kind = YONA_TC_KIND_UNSUPPORTED;
    YonaTcRange span{};
    std::string name;
    std::string module;
    std::string type;
    std::string effects;
    std::string linearity;
    std::string detail;
    std::vector<Node> children;
};

YonaTcPosition position_at(std::string_view src, std::size_t offset) {
    YonaTcPosition p{0, 0};
    for (std::size_t i = 0; i < offset && i < src.size(); ++i) {
        if (src[i] == '\n') {
            p.line += 1;
            p.character = 0;
        } else {
            p.character += 1;
        }
    }
    return p;
}

YonaTcRange range_of(std::string_view src, const SourceLocation& loc) {
    YonaTcRange r{};
    r.start = position_at(src, loc.offset);
    const std::size_t end = loc.length == 0 ? loc.offset : loc.offset + loc.length;
    r.end = position_at(src, end);
    if (r.end.line < r.start.line ||
        (r.end.line == r.start.line && r.end.character < r.start.character))
        r.end = r.start;
    return r;
}

char* dup_cstr(const std::string& s) {
    if (s.empty())
        return nullptr;
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (!p)
        return nullptr;
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

std::string type_str(TypeChecker* tc, yona::ast::AstNode* node) {
    if (!tc || !node)
        return {};
    if (auto* ty = tc->type_of(node))
        return yona::compiler::typechecker::pretty_print(tc->zonk(ty));
    return {};
}

std::string effects_str(TypeChecker* tc, yona::ast::AstNode* node) {
    if (!tc || !node)
        return "{}";
    auto* ty = tc->type_of(node);
    if (!ty)
        return "{}";
    auto info = tc->effect_row_info(tc->zonk(ty));
    std::string s = "{";
    for (std::size_t i = 0; i < info.ops.size(); ++i) {
        if (i)
            s += ",";
        s += info.ops[i];
    }
    if (info.open_rest) {
        if (!info.ops.empty())
            s += " | ";
        else
            s += "|";
        s += "r";
    }
    s += "}";
    return s;
}

std::string linearity_of(std::string_view type) {
    if (type == "Linear" || type.starts_with("Linear ") ||
        type.find(" Linear") != std::string_view::npos)
        return "linear";
    return "unrestricted";
}

void fill_typed(Node& n, TypeChecker* tc, yona::ast::AstNode* node, bool typed_kind) {
    n.type = type_str(tc, node);
    n.effects = effects_str(tc, node);
    if (typed_kind)
        n.linearity = linearity_of(n.type);
}

std::string ident_name(yona::ast::IdentifierExpr* id) {
    if (id && id->name)
        return id->name->value;
    return {};
}

std::string pattern_summary(yona::ast::PatternNode* p);

std::string pattern_summary(yona::ast::PatternNode* p) {
    if (!p)
        return {};
    switch (p->get_type()) {
    case yona::ast::AST_UNDERSCORE_PATTERN:
        return "_";
    case yona::ast::AST_PATTERN_VALUE: {
        auto* pv = static_cast<yona::ast::PatternValue*>(p);
        if (auto* id = std::get_if<yona::ast::IdentifierExpr*>(&pv->expr))
            return ident_name(*id);
        if (auto* sym = std::get_if<yona::ast::SymbolExpr*>(&pv->expr))
            return *sym ? (*sym)->value : std::string();
        return {};
    }
    case yona::ast::AST_CONSTRUCTOR_PATTERN: {
        auto* cp = static_cast<yona::ast::ConstructorPattern*>(p);
        std::string s = cp->constructor_name;
        for (auto* sub : cp->sub_patterns) {
            auto part = pattern_summary(sub);
            if (!part.empty()) {
                s += ' ';
                s += part;
            }
        }
        return s;
    }
    case yona::ast::AST_TUPLE_PATTERN: {
        auto* tp = static_cast<yona::ast::TuplePattern*>(p);
        std::string s = "(";
        for (std::size_t i = 0; i < tp->patterns.size(); ++i) {
            if (i)
                s += ", ";
            s += pattern_summary(tp->patterns[i]);
        }
        s += ")";
        return s;
    }
    case yona::ast::AST_AS_DATA_STRUCTURE_PATTERN: {
        auto* as = static_cast<yona::ast::AsDataStructurePattern*>(p);
        return ident_name(as->identifier);
    }
    default:
        return {};
    }
}

void collect_pattern_bindings(yona::ast::PatternNode* p, std::string_view src, TypeChecker* tc,
                              std::vector<Node>& out) {
    if (!p)
        return;
    switch (p->get_type()) {
    case yona::ast::AST_PATTERN_VALUE: {
        auto* pv = static_cast<yona::ast::PatternValue*>(p);
        if (auto* id = std::get_if<yona::ast::IdentifierExpr*>(&pv->expr)) {
            Node b;
            b.kind = YONA_TC_KIND_BINDING;
            b.span = range_of(src, (*id) ? (*id)->source_context : p->source_context);
            b.name = ident_name(*id);
            fill_typed(b, tc, *id, true);
            if (!b.name.empty())
                out.push_back(std::move(b));
        }
        return;
    }
    case yona::ast::AST_CONSTRUCTOR_PATTERN: {
        auto* cp = static_cast<yona::ast::ConstructorPattern*>(p);
        for (auto* sub : cp->sub_patterns)
            collect_pattern_bindings(sub, src, tc, out);
        return;
    }
    case yona::ast::AST_TUPLE_PATTERN: {
        auto* tp = static_cast<yona::ast::TuplePattern*>(p);
        for (auto* sub : tp->patterns)
            collect_pattern_bindings(sub, src, tc, out);
        return;
    }
    case yona::ast::AST_SEQ_PATTERN: {
        auto* sp = static_cast<yona::ast::SeqPattern*>(p);
        for (auto* sub : sp->patterns)
            collect_pattern_bindings(sub, src, tc, out);
        return;
    }
    case yona::ast::AST_HEAD_TAILS_PATTERN: {
        auto* ht = static_cast<yona::ast::HeadTailsPattern*>(p);
        for (auto* h : ht->heads)
            collect_pattern_bindings(h, src, tc, out);
        collect_pattern_bindings(ht->tail, src, tc, out);
        return;
    }
    case yona::ast::AST_AS_DATA_STRUCTURE_PATTERN: {
        auto* as = static_cast<yona::ast::AsDataStructurePattern*>(p);
        Node b;
        b.kind = YONA_TC_KIND_BINDING;
        b.span = range_of(src, as->identifier ? as->identifier->source_context : p->source_context);
        b.name = ident_name(as->identifier);
        fill_typed(b, tc, as->identifier, true);
        if (!b.name.empty())
            out.push_back(std::move(b));
        collect_pattern_bindings(as->pattern, src, tc, out);
        return;
    }
    case yona::ast::AST_TYPED_PATTERN: {
        auto* tp = static_cast<yona::ast::TypedPattern*>(p);
        Node b;
        b.kind = YONA_TC_KIND_BINDING;
        b.span = range_of(src, tp->source_context);
        b.name = tp->binding_name;
        fill_typed(b, tc, tp, true);
        if (!b.name.empty())
            out.push_back(std::move(b));
        return;
    }
    default:
        return;
    }
}

struct Analyzer {
    std::string_view src;
    TypeChecker* tc = nullptr;
    std::vector<Node> roots;

    void walk(yona::ast::AstNode* node);

    Node make(YonaTcKind kind, const SourceLocation& loc, std::string name) {
        Node n;
        n.kind = kind;
        n.span = range_of(src, loc);
        n.name = std::move(name);
        return n;
    }

    void walk_expr_variant(std::variant<yona::ast::ExprNode*, yona::ast::ValueExpr*>& v) {
        if (auto* e = std::get_if<yona::ast::ExprNode*>(&v))
            walk(*e);
        else if (auto* val = std::get_if<yona::ast::ValueExpr*>(&v))
            walk(*val);
    }

    void walk_function(yona::ast::FunctionExpr* fn, const std::string& override_name) {
        if (!fn)
            return;
        Node n = make(YONA_TC_KIND_FUNCTION, fn->source_context,
                      override_name.empty() ? fn->name : override_name);
        fill_typed(n, tc, fn, true);
        for (auto* p : fn->patterns)
            collect_pattern_bindings(p, src, tc, n.children);
        if (tc) {
            if (auto* ty = tc->type_of(fn)) {
                ty = tc->zonk(ty);
                std::size_t i = 0;
                while (ty && ty->tag == yona::compiler::typechecker::MonoType::Arrow &&
                       i < n.children.size()) {
                    if (n.children[i].kind == YONA_TC_KIND_BINDING) {
                        auto* pt = tc->zonk(ty->param_type);
                        n.children[i].type = yona::compiler::typechecker::pretty_print(pt);
                        n.children[i].effects = "{}";
                        n.children[i].linearity = linearity_of(n.children[i].type);
                    }
                    ty = tc->zonk(ty->return_type);
                    ++i;
                }
            }
        }
        for (auto* b : fn->bodies) {
            if (!b)
                continue;
            if (auto* wg = dynamic_cast<yona::ast::BodyWithGuards*>(b)) {
                walk(wg->guard);
                walk(wg->expr);
            } else if (auto* ng = dynamic_cast<yona::ast::BodyWithoutGuards*>(b)) {
                walk(ng->expr);
            }
        }
        // Nested facts collected by walk() go to roots; keep params on the function.
        roots.push_back(std::move(n));
    }
};

void Analyzer::walk(yona::ast::AstNode* node) {
    if (!node)
        return;
    switch (node->get_type()) {
    case yona::ast::AST_MAIN:
        walk(static_cast<yona::ast::MainNode*>(node)->node);
        return;
    case yona::ast::AST_MODULE_DECL: {
        auto* m = static_cast<yona::ast::ModuleDecl*>(node);
        for (auto* fn : m->functions)
            walk(fn);
        for (auto* adt : m->adt_declarations)
            walk(adt);
        for (auto* tr : m->trait_declarations)
            walk(tr);
        for (auto* inst : m->instance_declarations)
            walk(inst);
        for (auto* ext : m->extern_declarations)
            walk(ext);
        return;
    }
    case yona::ast::AST_FUNCTION_EXPR:
        walk_function(static_cast<yona::ast::FunctionExpr*>(node), {});
        return;
    case yona::ast::AST_ADT_DECL: {
        auto* adt = static_cast<yona::ast::AdtDeclNode*>(node);
        Node n = make(YONA_TC_KIND_ADT, adt->source_context, adt->name);
        for (std::size_t i = 0; i < adt->type_params.size(); ++i) {
            if (i)
                n.detail += ' ';
            n.detail += adt->type_params[i];
        }
        for (auto* v : adt->variants) {
            if (!v)
                continue;
            Node c = make(YONA_TC_KIND_CONSTRUCTOR, v->source_context, v->name);
            fill_typed(c, tc, v, true);
            std::string fields;
            for (std::size_t i = 0; i < v->field_type_names.size(); ++i) {
                if (i)
                    fields += " -> ";
                fields += v->field_type_names[i].to_string();
            }
            c.detail = std::move(fields);
            n.children.push_back(std::move(c));
        }
        roots.push_back(std::move(n));
        return;
    }
    case yona::ast::AST_LET_EXPR: {
        auto* let = static_cast<yona::ast::LetExpr*>(node);
        for (auto* a : let->aliases) {
            if (!a)
                continue;
            if (auto* va = dynamic_cast<yona::ast::ValueAlias*>(a)) {
                Node b = make(YONA_TC_KIND_BINDING,
                              va->identifier ? va->identifier->source_context : va->source_context,
                              ident_name(va->identifier));
                fill_typed(b, tc, va->expr ? static_cast<yona::ast::AstNode*>(va->expr)
                                           : static_cast<yona::ast::AstNode*>(va->identifier),
                           true);
                if (b.type.empty())
                    fill_typed(b, tc, va->identifier, true);
                roots.push_back(std::move(b));
                walk(va->expr);
            } else if (auto* la = dynamic_cast<yona::ast::LambdaAlias*>(a)) {
                walk_function(la->lambda, la->name ? la->name->value : std::string());
            } else if (auto* pa = dynamic_cast<yona::ast::PatternAlias*>(a)) {
                collect_pattern_bindings(pa->pattern, src, tc, roots);
                walk(pa->expr);
            }
        }
        walk(let->expr);
        return;
    }
    case yona::ast::AST_IMPORT_EXPR: {
        auto* im = static_cast<yona::ast::ImportExpr*>(node);
        for (auto* cl : im->clauses)
            walk(cl);
        walk(im->expr);
        return;
    }
    case yona::ast::AST_FUNCTIONS_IMPORT: {
        auto* fi = static_cast<yona::ast::FunctionsImport*>(node);
        const std::string mod = fi->fromFqn ? fi->fromFqn->to_string() : std::string();
        for (auto* al : fi->aliases) {
            if (!al)
                continue;
            std::string nm;
            if (al->alias)
                nm = al->alias->value;
            if (nm.empty() && al->name)
                nm = al->name->value;
            Node n = make(YONA_TC_KIND_IMPORT, al->source_context, nm);
            n.module = mod;
            fill_typed(n, tc, al, true);
            roots.push_back(std::move(n));
        }
        return;
    }
    case yona::ast::AST_MODULE_IMPORT: {
        auto* mi = static_cast<yona::ast::ModuleImport*>(node);
        Node n = make(YONA_TC_KIND_IMPORT, mi->source_context,
                      mi->name ? mi->name->value : std::string());
        n.module = mi->fqn ? mi->fqn->to_string() : std::string();
        roots.push_back(std::move(n));
        return;
    }
    case yona::ast::AST_CASE_EXPR: {
        auto* cse = static_cast<yona::ast::CaseExpr*>(node);
        Node n = make(YONA_TC_KIND_CASE, cse->source_context, {});
        fill_typed(n, tc, cse, true);
        walk(cse->expr);
        for (auto* cl : cse->clauses) {
            if (!cl)
                continue;
            Node pat = make(YONA_TC_KIND_PATTERN, cl->source_context, {});
            if (cl->pattern) {
                if (auto* cp = dynamic_cast<yona::ast::ConstructorPattern*>(cl->pattern))
                    pat.name = cp->constructor_name;
                pat.detail = pattern_summary(cl->pattern);
                if (pat.name.empty())
                    pat.name = pat.detail;
                collect_pattern_bindings(cl->pattern, src, tc, pat.children);
            }
            n.children.push_back(std::move(pat));
            walk(cl->guard);
            walk(cl->body);
        }
        roots.push_back(std::move(n));
        return;
    }
    case yona::ast::AST_PERFORM_EXPR: {
        auto* p = static_cast<yona::ast::PerformExpr*>(node);
        std::string op = p->effect_name;
        if (!op.empty() && !p->operation_name.empty())
            op += '.';
        op += p->operation_name;
        Node n = make(YONA_TC_KIND_EFFECT, p->source_context, op);
        fill_typed(n, tc, p, true);
        roots.push_back(std::move(n));
        for (auto* a : p->args)
            walk(a);
        return;
    }
    case yona::ast::AST_TRY_CATCH_EXPR: {
        auto* t = static_cast<yona::ast::TryCatchExpr*>(node);
        Node n = make(YONA_TC_KIND_UNSUPPORTED, t->source_context, "try/catch");
        n.detail = "try/catch is not in typed-core v1";
        roots.push_back(std::move(n));
        walk(t->tryExpr);
        walk(t->catchExpr);
        return;
    }
    case yona::ast::AST_CATCH_EXPR: {
        auto* c = static_cast<yona::ast::CatchExpr*>(node);
        for (auto* p : c->patterns)
            walk(p);
        return;
    }
    case yona::ast::AST_TRAIT_DECL: {
        auto* tr = static_cast<yona::ast::TraitDeclNode*>(node);
        Node n = make(YONA_TC_KIND_UNSUPPORTED, tr->source_context, tr->name);
        n.detail = "trait declaration is not in typed-core v1";
        roots.push_back(std::move(n));
        return;
    }
    case yona::ast::AST_INSTANCE_DECL: {
        auto* inst = static_cast<yona::ast::InstanceDeclNode*>(node);
        Node n = make(YONA_TC_KIND_UNSUPPORTED, inst->source_context, inst->trait_name);
        n.detail = "trait instance is not in typed-core v1";
        roots.push_back(std::move(n));
        for (auto* fn : inst->methods)
            walk(fn);
        return;
    }
    case yona::ast::AST_EXTERN_DECL: {
        auto* ext = static_cast<yona::ast::ExternDeclExpr*>(node);
        Node n = make(YONA_TC_KIND_FUNCTION, ext->source_context, ext->name);
        n.detail = "extern";
        fill_typed(n, tc, ext, true);
        roots.push_back(std::move(n));
        walk(ext->body);
        return;
    }
    case yona::ast::AST_APPLY_EXPR: {
        auto* ap = static_cast<yona::ast::ApplyExpr*>(node);
        walk(ap->call);
        for (auto& a : ap->args)
            walk_expr_variant(a);
        return;
    }
    case yona::ast::AST_IF_EXPR: {
        auto* iff = static_cast<yona::ast::IfExpr*>(node);
        walk(iff->condition);
        walk(iff->thenExpr);
        walk(iff->elseExpr);
        return;
    }
    case yona::ast::AST_DO_EXPR: {
        for (auto* s : static_cast<yona::ast::DoExpr*>(node)->steps)
            walk(s);
        return;
    }
    case yona::ast::AST_WITH_EXPR: {
        auto* w = static_cast<yona::ast::WithExpr*>(node);
        walk(w->contextExpr);
        walk(w->bodyExpr);
        return;
    }
    case yona::ast::AST_HANDLE_EXPR: {
        auto* h = static_cast<yona::ast::HandleExpr*>(node);
        walk(h->body);
        for (auto* cl : h->clauses) {
            if (cl)
                walk(cl->body);
        }
        return;
    }
    case yona::ast::AST_RAISE_EXPR:
        walk(static_cast<yona::ast::RaiseExpr*>(node)->value);
        return;
    case yona::ast::AST_BINARY_OP_EXPR:
    case yona::ast::AST_ADD_EXPR:
    case yona::ast::AST_SUBTRACT_EXPR:
    case yona::ast::AST_MULTIPLY_EXPR:
    case yona::ast::AST_DIVIDE_EXPR:
    case yona::ast::AST_MODULO_EXPR:
    case yona::ast::AST_POWER_EXPR:
    case yona::ast::AST_EQ_EXPR:
    case yona::ast::AST_NEQ_EXPR:
    case yona::ast::AST_LT_EXPR:
    case yona::ast::AST_LTE_EXPR:
    case yona::ast::AST_GT_EXPR:
    case yona::ast::AST_GTE_EXPR:
    case yona::ast::AST_LOGICAL_AND_EXPR:
    case yona::ast::AST_LOGICAL_OR_EXPR:
    case yona::ast::AST_PIPE_RIGHT_EXPR:
    case yona::ast::AST_PIPE_LEFT_EXPR:
    case yona::ast::AST_IN_EXPR:
    case yona::ast::AST_CONS_LEFT_EXPR:
    case yona::ast::AST_CONS_RIGHT_EXPR:
    case yona::ast::AST_JOIN_EXPR:
    case yona::ast::AST_REMOVE_EXPR:
    case yona::ast::AST_LEFT_SHIFT_EXPR:
    case yona::ast::AST_RIGHT_SHIFT_EXPR:
    case yona::ast::AST_ZEROFILL_RIGHT_SHIFT_EXPR:
    case yona::ast::AST_BITWISE_AND_EXPR:
    case yona::ast::AST_BITWISE_OR_EXPR:
    case yona::ast::AST_BITWISE_XOR_EXPR: {
        auto* b = static_cast<yona::ast::BinaryOpExpr*>(node);
        walk(b->left);
        walk(b->right);
        return;
    }
    case yona::ast::AST_LOGICAL_NOT_OP_EXPR:
        walk(static_cast<yona::ast::LogicalNotOpExpr*>(node)->expr);
        return;
    case yona::ast::AST_BINARY_NOT_OP_EXPR:
        walk(static_cast<yona::ast::BinaryNotOpExpr*>(node)->expr);
        return;
    default:
        return;
    }
}

void free_node_contents(YonaTcNode* n) {
    if (!n)
        return;
    std::free(const_cast<char*>(n->name));
    std::free(const_cast<char*>(n->module));
    std::free(const_cast<char*>(n->type));
    std::free(const_cast<char*>(n->effects));
    std::free(const_cast<char*>(n->linearity));
    std::free(const_cast<char*>(n->detail));
    if (n->children) {
        auto* kids = const_cast<YonaTcNode*>(n->children);
        for (uint32_t i = 0; i < n->child_count; ++i)
            free_node_contents(&kids[i]);
        std::free(kids);
    }
}

YonaTcNode freeze_node(const Node& n);

YonaTcNode* freeze_nodes(const std::vector<Node>& nodes) {
    if (nodes.empty())
        return nullptr;
    auto* out = static_cast<YonaTcNode*>(std::calloc(nodes.size(), sizeof(YonaTcNode)));
    if (!out)
        return nullptr;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        out[i] = freeze_node(nodes[i]);
    return out;
}

YonaTcNode freeze_node(const Node& n) {
    YonaTcNode c{};
    c.kind = n.kind;
    c.span = n.span;
    c.name = dup_cstr(n.name);
    c.module = dup_cstr(n.module);
    c.type = dup_cstr(n.type);
    c.effects = dup_cstr(n.effects);
    c.linearity = dup_cstr(n.linearity);
    c.detail = dup_cstr(n.detail);
    c.child_count = static_cast<uint32_t>(n.children.size());
    c.children = freeze_nodes(n.children);
    return c;
}

std::string diag_code(const DiagnosticEngine::Record& rec) {
    if (!rec.code)
        return {};
    return yona::compiler::error_code_str(*rec.code);
}

} // namespace

extern "C" {

uint32_t yona_tc_abi_version(void) { return YONA_TYPED_CORE_ABI_VERSION; }

YonaTcModule* yona_tc_analyze(const char* source, const char* filename,
                              const char* const* include_paths, size_t include_path_count) {
    if (!source)
        return nullptr;

    const char* file = filename && filename[0] ? filename : "-";
    DiagnosticEngine diag;
    diag.set_source(source, file);

    yona::parser::Parser parser;
    Codegen codegen("typed_core", &diag);
    TypeChecker checker(diag);

    for (size_t i = 0; i < include_path_count; ++i) {
        if (!include_paths || !include_paths[i] || !include_paths[i][0])
            continue;
        checker.add_module_path(include_paths[i]);
        codegen.module_paths_.emplace_back(include_paths[i]);
    }
    codegen.load_prelude(&parser, &checker);
    checker.set_import_type_source(&codegen.import_types_);

    std::shared_ptr<yona::ast::AstNode> root;
    std::string module_name = "-";
    const bool is_mod = yona::is_module_source(source);
    if (is_mod) {
        auto result = parser.parse_module(source, file);
        if (!result.has_value()) {
            for (auto& e : result.error())
                diag.error(e.location, ErrorCode::E0301, e.message);
        } else {
            root = std::shared_ptr<yona::ast::AstNode>(result.value().release());
            auto* mod = static_cast<yona::ast::ModuleDecl*>(root.get());
            if (mod && mod->fqn)
                module_name = mod->fqn->to_string();
            checker.check_module(mod);
        }
    } else {
        auto result = parser.parse_expression(source, file);
        if (!result.has_value()) {
            for (auto& e : result.error())
                diag.error(e.location, ErrorCode::E0301, e.message);
        } else {
            root = std::shared_ptr<yona::ast::AstNode>(result.value().release());
            checker.check(root.get());
        }
    }

    if (root) {
        RefinementChecker refine(diag, &checker);
        refine.check(root.get());
        LinearityChecker lin(diag, &checker);
        lin.check(root.get());
    }

    Analyzer az;
    az.src = source;
    az.tc = &checker;
    az.walk(root.get());

    auto* mod = static_cast<YonaTcModule*>(std::calloc(1, sizeof(YonaTcModule)));
    if (!mod)
        return nullptr;
    mod->abi_version = YONA_TYPED_CORE_ABI_VERSION;
    mod->filename = dup_cstr(file);
    mod->module_name = dup_cstr(module_name);
    mod->node_count = static_cast<uint32_t>(az.roots.size());
    mod->nodes = freeze_nodes(az.roots);

    const auto& recs = diag.records();
    if (!recs.empty()) {
        auto* ds = static_cast<YonaTcDiagnostic*>(
            std::calloc(recs.size(), sizeof(YonaTcDiagnostic)));
        if (ds) {
            for (std::size_t i = 0; i < recs.size(); ++i) {
                ds[i].range = range_of(source, recs[i].loc);
                ds[i].severity = recs[i].level == yona::compiler::DiagLevel::Error ? 1 : 2;
                ds[i].code = dup_cstr(diag_code(recs[i]));
                ds[i].message = dup_cstr(recs[i].message);
            }
            mod->diagnostics = ds;
            mod->diagnostic_count = static_cast<uint32_t>(recs.size());
        }
    }
    return mod;
}

void yona_tc_module_free(YonaTcModule* module) {
    if (!module)
        return;
    std::free(const_cast<char*>(module->filename));
    std::free(const_cast<char*>(module->module_name));
    if (module->nodes) {
        auto* nodes = const_cast<YonaTcNode*>(module->nodes);
        for (uint32_t i = 0; i < module->node_count; ++i)
            free_node_contents(&nodes[i]);
        std::free(nodes);
    }
    if (module->diagnostics) {
        auto* ds = const_cast<YonaTcDiagnostic*>(module->diagnostics);
        for (uint32_t i = 0; i < module->diagnostic_count; ++i) {
            std::free(const_cast<char*>(ds[i].code));
            std::free(const_cast<char*>(ds[i].message));
        }
        std::free(ds);
    }
    std::free(module);
}

} // extern "C"
