//
// ParserModule.cpp — Module, function, and ADT declaration parsing.
//

#include "ParserImpl.h"

namespace yona::parser {

bool ParserImpl::try_consume_borrow_annotation() {
    if (!check(TokenType::YAT) || is_at_end()) return false;
    const Token& t1 = peek(1);
    if (t1.type != TokenType::YIDENTIFIER || string(t1.lexeme) != "borrow")
        return false;
    advance(); // @
    advance(); // borrow
    return true;
}

unique_ptr<ModuleDecl> ParserImpl::parse_module_internal() {
    SourceLocation start_loc = current_location();

    if (!expect(TokenType::YMODULE, "Expected 'module' at start of file")) {
        return nullptr;
    }

    auto name = parse_module_name();
    if (!name) return nullptr;

    skip_newlines();

    vector<string> exports;
    vector<string> exported_types;
    vector<string> opaque_exported_types;
    vector<string> exported_traits;
    vector<ReExport> re_exports;
    vector<FunctionDeclaration*> function_declarations;
    vector<FunctionExpr*> functions;
    vector<ExternDeclExpr*> extern_declarations;
    vector<AdtDeclNode*> adt_declarations;
    vector<TraitDeclNode*> trait_declarations;
    vector<InstanceDeclNode*> instance_declarations;

    while (!is_at_end()) {
        skip_newlines();
        if (is_at_end()) break;

        if (match(TokenType::YEXPORT)) {
            // Check for "export type TypeName"
            if (match(TokenType::YTYPE)) {
                if (!check(TokenType::YIDENTIFIER)) {
                    error(ParseError::Type::INVALID_SYNTAX, "Expected type name after 'export type'");
                } else {
                    string type_name(advance().lexeme);
                    bool opaque = check(TokenType::YIDENTIFIER) && current().lexeme == "opaque";
                    if (opaque) advance();
                    if (std::find(exported_types.begin(), exported_types.end(), type_name) != exported_types.end()) {
                        error(ParseError::Type::INVALID_SYNTAX,
                              "Type '" + type_name + "' is exported more than once");
                    } else {
                        exported_types.push_back(type_name);
                        if (opaque) opaque_exported_types.push_back(type_name);
                    }
                }
            } else if (match(TokenType::YTRAIT)) {
                // export trait TraitName
                if (!check(TokenType::YIDENTIFIER)) {
                    error(ParseError::Type::INVALID_SYNTAX, "Expected trait name after 'export trait'");
                } else {
                    exported_traits.push_back(string(advance().lexeme));
                }
            } else {
                parse_export_list(exports, re_exports);
            }
        } else if (match(TokenType::YTRAIT)) {
            if (auto trait = parse_trait_declaration()) {
                trait_declarations.push_back(trait.release());
            }
        } else if (match(TokenType::YINSTANCE)) {
            if (auto inst = parse_instance_declaration()) {
                instance_declarations.push_back(inst.release());
            }
        } else if (match(TokenType::YTYPE)) {
            if (auto adt = parse_adt_declaration()) {
                // Register constructors for pattern parsing
                for (size_t ci = 0; ci < adt->variants.size(); ci++) {
                    auto* ctor = adt->variants[ci];
                    constructor_registry_[ctor->name] = {adt->name, static_cast<int>(ci),
                        static_cast<int>(ctor->field_type_names.size()), ctor->field_names};
                }
                adt_declarations.push_back(adt.release());
            }
        } else if (match(TokenType::YEXTERN)) {
            // Module-level extern: extern [async|io|native] name : Type [= "CSYMBOL"]
            //   `async` — thread-pool async (returns Promise)
            //   `io`    — io_uring submit-and-return (returns Promise)
            //   `native` — C returns yona_promise_t*; await via yona_rt_async_await (GPU, etc.)
            //   neither — synchronous C call
            ast::ExternPromiseKind ext_promise = ast::ExternPromiseKind::Sync;
            if (check(TokenType::YIDENTIFIER) && current().lexeme == "async") {
                ext_promise = ast::ExternPromiseKind::ThreadPool;
                advance();
            } else if (check(TokenType::YIDENTIFIER) && current().lexeme == "io") {
                ext_promise = ast::ExternPromiseKind::IoUring;
                advance();
            } else if (check(TokenType::YIDENTIFIER) && current().lexeme == "native") {
                ext_promise = ast::ExternPromiseKind::NativePtr;
                advance();
            }
            skip_newlines();
            if (!check(TokenType::YIDENTIFIER)) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected function name after 'extern'");
            } else {
                SourceLocation eloc = current_location();
                string ename(advance().lexeme);
                expect(TokenType::YCOLON, "Expected ':' after extern function name");
                skip_newlines();
                auto etype = parse_type();
                if (etype) {
                    auto* decl = new ExternDeclExpr(eloc, ename, *etype, nullptr, ext_promise);
                    // Optional `= "C_SYMBOL"` to bind a Yona-friendly name to a
                    // mangled C ABI symbol. Without it, ename IS the C symbol.
                    if (match(TokenType::YASSIGN)) {
                        skip_newlines();
                        if (check(TokenType::YSTRING)) {
                            auto tok = advance();
                            decl->c_symbol = std::get<std::string>(tok.value);
                        } else {
                            error(ParseError::Type::INVALID_SYNTAX,
                                "Expected string literal after '=' in extern declaration");
                        }
                    }
                    extern_declarations.push_back(decl);
                }
            }
        } else if (check(TokenType::YIDENTIFIER)) {
            if (auto func = parse_function()) {
                functions.push_back(func.release());
            }
        } else {
            // Skip unexpected tokens
            advance();
        }

        if (errors_.size() > 10 && config_.enable_error_recovery) {
            synchronize();
        }
    }

    // Convert PackageNameExpr to FqnExpr
    if (name->parts.empty()) {
        error(ParseError::Type::INVALID_SYNTAX, "Module name cannot be empty");
        return nullptr;
    }

    auto source_ctx = name->source_context;

    // If there's only one part, it's just the module name
    if (name->parts.size() == 1) {
        auto module_name = name->parts[0];
        name->parts.clear();
        delete name.release();
        auto fqn = std::unique_ptr<FqnExpr>(new FqnExpr(source_ctx, nullopt, module_name));
        return make_unique<ModuleDecl>(
            start_loc,
            fqn.release(),
            exports,
            exported_types,
            opaque_exported_types,
            exported_traits,
            re_exports,
            functions,
            function_declarations,
            adt_declarations,
            trait_declarations,
            instance_declarations,
            extern_declarations
        );
    }

    // Multiple parts: split into package and module
    auto module_name_node = name->parts.back();
    name->parts.pop_back();

    auto module_name = new NameExpr(module_name_node->source_context, module_name_node->value);
    auto fqn = std::unique_ptr<FqnExpr>(new FqnExpr(source_ctx, name.release(), module_name));

    return make_unique<ModuleDecl>(
        start_loc,
        fqn.release(),
        exports,
        exported_types,
        opaque_exported_types,
        exported_traits,
        re_exports,
        functions,
        function_declarations,
        adt_declarations,
        trait_declarations,
        instance_declarations,
        extern_declarations
    );
}

unique_ptr<PackageNameExpr> ParserImpl::parse_module_name() {
    SourceLocation start_loc = current_location();
    vector<NameExpr*> parts;

    do {
        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX,
                  "Expected identifier in module name");
            return nullptr;
        }
        auto token = advance();
        auto name_expr = make_unique<NameExpr>(token_location(token), string(token.lexeme));
        parts.push_back(name_expr.release());
    } while (match(TokenType::YBACKSLASH));

    return make_unique<PackageNameExpr>(start_loc, parts);
}

void ParserImpl::parse_export_list(vector<string>& exports, vector<ReExport>& re_exports) {
    while (check(TokenType::YIDENTIFIER)) {
        vector<string> names;
        names.push_back(string(advance().lexeme));
        while (match(TokenType::YCOMMA)) {
            skip_newlines();
            if (!check(TokenType::YIDENTIFIER)) break;
            names.push_back(string(advance().lexeme));
        }

        if (match(TokenType::YFROM)) {
            string mod_fqn;
            if (!check(TokenType::YIDENTIFIER)) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected module name after 'from'");
                break;
            }
            mod_fqn = string(advance().lexeme);
            while (match(TokenType::YBACKSLASH)) {
                if (!check(TokenType::YIDENTIFIER)) break;
                mod_fqn += "\\";
                mod_fqn += string(advance().lexeme);
            }
            re_exports.push_back({std::move(names), std::move(mod_fqn)});

            if (!match(TokenType::YCOMMA)) break;
            skip_newlines();
        } else {
            for (auto& n : names) exports.push_back(std::move(n));
            break;
        }
    }
}

unique_ptr<FunctionExpr> ParserImpl::parse_function() {
    SourceLocation start_loc = current_location();
    size_t src_start = start_loc.offset;

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected function name");
        return nullptr;
    }
    string name(advance().lexeme);

    // Type signature (optional)
    unique_ptr<compiler::types::Type> type_signature;
    if (match(TokenType::YCOLON)) {
        type_signature = parse_type();
        if (!type_signature) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected type after ':'");
            return nullptr;
        }
    }

    vector<PatternNode*> patterns;
    vector<bool> param_borrow_flags;
    vector<FunctionBody*> function_bodies;

    if (type_signature) {
        skip_newlines();
        auto first_clause = parse_function_clause_with_patterns(name);
        if (first_clause.patterns.empty() && first_clause.bodies.empty())
            return nullptr;
        patterns = std::move(first_clause.patterns);
        param_borrow_flags = std::move(first_clause.param_borrow);
        for (auto& body : first_clause.bodies)
            function_bodies.push_back(body.release());
    } else {
        auto first_clause = parse_function_clause_patterns_and_body();
        if (first_clause.patterns.empty() && first_clause.bodies.empty())
            return nullptr;
        patterns = std::move(first_clause.patterns);
        param_borrow_flags = std::move(first_clause.param_borrow);
        for (auto& body : first_clause.bodies)
            function_bodies.push_back(body.release());
    }

    // Parse additional clauses
    skip_newlines();
    while (check(TokenType::YIDENTIFIER) &&
           peek().lexeme == name &&
           !is_at_end()) {
        auto clause = parse_function_clause(name);
        for (auto& body : clause) {
            function_bodies.push_back(body.release());
        }
    }

    auto func = make_unique<FunctionExpr>(
        start_loc,
        name,
        patterns,
        function_bodies,
        type_signature ? std::optional<compiler::types::Type>(*type_signature) : std::nullopt
    );
    func->param_borrow = std::move(param_borrow_flags);
    func->param_borrow.resize(func->patterns.size(), false);

    // Capture original source text for cross-module monomorphization
    size_t src_end = previous_location().offset + previous_location().length;
    if (!source_.empty() && src_end > src_start && src_end <= source_.size()) {
        func->source_text = std::string(source_.substr(src_start, src_end - src_start));
    }

    return func;
}

ParsedFunctionClause ParserImpl::parse_function_clause_with_patterns(const string& expected_name) {
    SourceLocation start_loc = current_location();

    if (!check(TokenType::YIDENTIFIER) || peek().lexeme != expected_name) {
        error(ParseError::Type::INVALID_SYNTAX,
              "Expected function name '" + expected_name + "'");
        return {};
    }
    advance();

    vector<PatternNode*> patterns;
    vector<bool> param_borrow;
    while (!check(TokenType::YASSIGN) && !check(TokenType::YIF) && !is_at_end()) {
        bool borrow = try_consume_borrow_annotation();
        auto pattern = parse_pattern();
        if (pattern) {
            patterns.push_back(pattern.release());
            param_borrow.push_back(borrow);
        }
    }

    vector<unique_ptr<FunctionBody>> bodies;

    if (match(TokenType::YIF)) {
        do {
            auto guard_expr = parse_expr();
            expect(TokenType::YASSIGN, "Expected '=' after guard");
            auto body_expr = parse_expr();

            if (guard_expr && body_expr) {
                bodies.push_back(make_unique<BodyWithGuards>(
                    start_loc,
                    guard_expr.release(),
                    body_expr.release()
                ));
            }
        } while (match(TokenType::YIF));
    } else {
        expect(TokenType::YASSIGN, "Expected '=' after patterns");
        auto body = parse_expr();

        if (body) {
            bodies.push_back(make_unique<BodyWithoutGuards>(
                start_loc,
                body.release()
            ));
        }
    }

    return {std::move(patterns), std::move(param_borrow), std::move(bodies)};
}

ParsedFunctionClause ParserImpl::parse_function_clause_patterns_and_body() {
    SourceLocation start_loc = current_location();

    vector<PatternNode*> patterns;
    vector<bool> param_borrow;
    while (!check(TokenType::YASSIGN) && !is_at_end()) {
        bool borrow = try_consume_borrow_annotation();
        auto pattern = parse_pattern();
        if (pattern) {
            patterns.push_back(pattern.release());
            param_borrow.push_back(borrow);
        } else {
            break;
        }
    }

    expect(TokenType::YASSIGN, "Expected '=' after function patterns");

    vector<unique_ptr<FunctionBody>> bodies;
    // Distinguish guard from if-expression body
    bool is_guard = false;
    if (check(TokenType::YIF)) {
        for (size_t off = 1; off < 20 && !is_at_end(); off++) {
            auto t = peek(off).type;
            if (t == TokenType::YARROW) { is_guard = true; break; }
            if (t == TokenType::YTHEN) break;
            if (t == TokenType::YEOF_TOKEN || t == TokenType::YEND) break;
        }
    }
    if (is_guard) {
        while (match(TokenType::YIF)) {
            auto guard = parse_expr();
            if (!guard) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected guard expression after 'if'");
                break;
            }

            expect(TokenType::YARROW, "Expected '->' after guard");

            auto body = parse_expr();

            if (body) {
                bodies.push_back(make_unique<BodyWithGuards>(
                    start_loc,
                    guard.release(),
                    body.release()
                ));
            }
        }
    } else {
        auto body = parse_expr();

        if (body) {
            bodies.push_back(make_unique<BodyWithoutGuards>(
                start_loc,
                body.release()
            ));
        }
    }

    return {std::move(patterns), std::move(param_borrow), std::move(bodies)};
}

vector<unique_ptr<FunctionBody>> ParserImpl::parse_function_clause(const string& expected_name) {
    auto result = parse_function_clause_with_patterns(expected_name);
    // Clean up patterns since we don't need them for subsequent clauses
    for (auto* p : result.patterns) delete p;
    return std::move(result.bodies);
}

unique_ptr<AdtDeclNode> ParserImpl::parse_adt_declaration() {
    // YTYPE was already consumed by the caller (parse_module_internal)
    SourceLocation loc = previous_location();

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected type name after 'type'");
        return nullptr;
    }

    string type_name(current().lexeme);
    advance();

    // Parse type parameters (optional, lowercase identifiers before '=')
    vector<string> type_params;
    while (check(TokenType::YIDENTIFIER) && !check(TokenType::YASSIGN)) {
        string param(current().lexeme);
        if (islower(param[0])) {
            type_params.push_back(param);
            advance();
        } else {
            break;
        }
    }

    if (!expect(TokenType::YASSIGN, "Expected '=' in type definition")) {
        return nullptr;
    }

    skip_newlines();

    // Parse constructor variants separated by |
    vector<AdtConstructor*> variants;
    do {
        skip_newlines();

        if (!check(TokenType::YIDENTIFIER)) break;

        string ctor_name(current().lexeme);
        if (!isupper(ctor_name[0])) break;
        advance();

        vector<FieldType> field_types;
        vector<string> field_names;

        // Parse a single field type. Handles bare identifiers, parenthesized
        // groups (tuples and function types), and parameterized type
        // applications like `Option a`, `Map String Int`, `Option (a, Stream a)`.
        // Only the head identifier is preserved in the resulting FieldType;
        // the codegen uses that to pick a CType, and the type arguments are
        // consumed so the rest of the constructor parses correctly.
        std::function<FieldType()> parse_field_type = [&]() -> FieldType {
            // Helper: consume one "atom" — a primary type without further
            // application. Used for arguments to a parameterized head type.
            std::function<void()> consume_type_atom = [&]() {
                if (check(TokenType::YLPAREN)) {
                    advance();  // (
                    if (check(TokenType::YRPAREN)) { advance(); return; }
                    // Recursively consume one or more comma/arrow-separated
                    // sub-types until the matching ')'.
                    int depth = 1;
                    while (!is_at_end() && depth > 0) {
                        if (check(TokenType::YLPAREN)) { depth++; advance(); }
                        else if (check(TokenType::YRPAREN)) { depth--; advance(); }
                        else advance();
                    }
                } else if (check(TokenType::YLBRACKET)) {
                    advance();
                    int depth = 1;
                    while (!is_at_end() && depth > 0) {
                        if (check(TokenType::YLBRACKET)) { depth++; advance(); }
                        else if (check(TokenType::YRBRACKET)) { depth--; advance(); }
                        else advance();
                    }
                } else if (check(TokenType::YIDENTIFIER)) {
                    advance();
                }
            };

            if (check(TokenType::YLPAREN)) {
                advance(); // consume (

                vector<FieldType> types;

                if (check(TokenType::YRPAREN)) {
                    types.push_back(FieldType::simple("()"));
                    advance();
                } else if (check(TokenType::YLPAREN)) {
                    types.push_back(parse_field_type());
                } else if (check(TokenType::YIDENTIFIER)) {
                    string tname(current().lexeme);
                    advance();
                    while ((check(TokenType::YIDENTIFIER) && islower(current().lexeme[0])
                            && !check(TokenType::YARROW)) ||
                           (check(TokenType::YLPAREN) && !check(TokenType::YARROW))) {
                        if (check(TokenType::YLPAREN)) {
                            // Type application argument: `Option (a, Stream a)`.
                            // Consume the parenthesized group; we don't model
                            // type arguments structurally — only the head name
                            // matters for codegen.
                            consume_type_atom();
                        } else {
                            tname += " ";
                            tname += string(current().lexeme);
                            advance();
                        }
                    }
                    types.push_back(FieldType::simple(tname));
                }

                // Tuple inside parentheses: `(a, Stream a)`. We don't preserve
                // tuple structure in FieldType (the codegen doesn't use it),
                // but we MUST consume the trailing types so the constructor
                // parser advances past the closing ')'.
                while (match(TokenType::YCOMMA)) {
                    if (check(TokenType::YLPAREN) || check(TokenType::YIDENTIFIER)) {
                        (void)parse_field_type();  // discard — only head name matters
                    } else {
                        break;
                    }
                }

                while (match(TokenType::YARROW)) {
                    if (check(TokenType::YLPAREN)) {
                        types.push_back(parse_field_type());
                    } else if (check(TokenType::YIDENTIFIER)) {
                        string tname(current().lexeme);
                        advance();
                        while ((check(TokenType::YIDENTIFIER) && islower(current().lexeme[0])
                                && !check(TokenType::YARROW) && !check(TokenType::YRPAREN)) ||
                               (check(TokenType::YLPAREN) && !check(TokenType::YARROW))) {
                            if (check(TokenType::YLPAREN)) {
                                consume_type_atom();
                            } else {
                                tname += " ";
                                tname += string(current().lexeme);
                                advance();
                            }
                        }
                        types.push_back(FieldType::simple(tname));
                    }
                }

                if (check(TokenType::YRPAREN)) advance();

                if (types.size() <= 1) {
                    return types.empty() ? FieldType::simple("()") : types[0];
                }

                FieldType ft;
                ft.name = "Fn";
                ft.is_function_type = true;
                ft.return_types.push_back(types.back());
                types.pop_back();
                ft.param_types = std::move(types);
                return ft;
            } else if (check(TokenType::YIDENTIFIER)) {
                string tname(current().lexeme);
                bool head_is_ctor = !tname.empty() && isupper(tname[0]);
                advance();
                // Type application: `Stream a`, `Option (Step a)`, ...
                // Only valid when the head is a type constructor (uppercase).
                // Lowercase heads are type variables and never take arguments.
                // We must NOT consume a following `(` after a lowercase head,
                // because that paren begins a separate field type.
                while (head_is_ctor &&
                       ((check(TokenType::YIDENTIFIER) && islower(current().lexeme[0])
                         && !check(TokenType::YARROW)) ||
                        check(TokenType::YLPAREN))) {
                    if (check(TokenType::YLPAREN)) {
                        consume_type_atom();
                    } else {
                        advance();  // consume the lowercase type variable
                    }
                }
                return FieldType::simple(tname);
            }
            return FieldType::simple("Int");
        };

        if (match(TokenType::YLBRACE)) {
            // Named fields
            while (!check(TokenType::YRBRACE) && !is_at_end()) {
                skip_newlines();
                if (!check(TokenType::YIDENTIFIER)) break;
                string fname(advance().lexeme);
                expect(TokenType::YCOLON, "Expected ':' after field name");
                auto ftype = parse_field_type();
                field_names.push_back(fname);
                field_types.push_back(ftype);
                if (!match(TokenType::YCOMMA)) break;
                skip_newlines();
            }
            expect(TokenType::YRBRACE, "Expected '}' after named fields");
        } else {
            // Positional fields
            while (!check(TokenType::YPIPE) && !check(TokenType::YNEWLINE) &&
                   !is_at_end() && !check(TokenType::YEND)) {
                if (check(TokenType::YIDENTIFIER) || check(TokenType::YLPAREN)) {
                    field_types.push_back(parse_field_type());
                } else {
                    break;
                }
            }
        }

        variants.push_back(new AdtConstructor(loc, ctor_name, field_types, field_names));

        skip_newlines();
    } while (match(TokenType::YPIPE));

    if (variants.empty()) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected at least one constructor in ADT declaration");
        return nullptr;
    }

    // Parse optional deriving clause:
    //   type Color = Red | Green | Blue deriving Show, Eq
    //   type Color = Red | Green | Blue
    //       deriving Show, Eq
    vector<string> derive_traits;
    skip_newlines();
    if (match(TokenType::YDERIVING)) {
        // Parse comma-separated trait names
        if (match(TokenType::YLPAREN)) {
            // Parenthesized: deriving (Show, Eq, Ord)
            do {
                skip_newlines();
                if (!check(TokenType::YIDENTIFIER)) break;
                derive_traits.push_back(string(current().lexeme));
                advance();
            } while (match(TokenType::YCOMMA));
            expect(TokenType::YRPAREN, "Expected ')' after deriving list");
        } else {
            // Unparenthesized: deriving Show, Eq, Ord
            do {
                if (!check(TokenType::YIDENTIFIER)) break;
                derive_traits.push_back(string(current().lexeme));
                advance();
            } while (match(TokenType::YCOMMA));
        }
    }

    return make_unique<AdtDeclNode>(loc, type_name, type_params, variants, derive_traits);
}

unique_ptr<TraitDeclNode> ParserImpl::parse_trait_declaration() {
    // YTRAIT was already consumed by the caller
    SourceLocation loc = previous_location();

    // Phase 3: Check for superclass constraints: "Eq a => Ord a"
    // Look ahead to see if there's a "=>" before the trait body
    vector<pair<string, string>> superclasses;
    {
        // Save position for backtracking
        size_t saved_pos = current_;
        bool found_fat_arrow = false;

        // Scan ahead: ClassName typevar => ...
        if (check(TokenType::YIDENTIFIER)) {
            string first_name(peek().lexeme);
            // Check if this could be "ConstraintName typevar =>"
            size_t lookahead = 0;
            while (lookahead < 20) {
                auto t = peek(lookahead).type;
                if (t == TokenType::YFAT_ARROW) { found_fat_arrow = true; break; }
                if (t == TokenType::YEND || t == TokenType::YEOF_TOKEN || t == TokenType::YNEWLINE) break;
                lookahead++;
            }
        }

        if (found_fat_arrow) {
            // Parse superclass constraints
            do {
                if (!check(TokenType::YIDENTIFIER)) break;
                string sc_name(advance().lexeme);
                if (!check(TokenType::YIDENTIFIER)) {
                    error(ParseError::Type::INVALID_SYNTAX, "Expected type variable after superclass name");
                    break;
                }
                string sc_param(advance().lexeme);
                superclasses.push_back({sc_name, sc_param});
            } while (match(TokenType::YCOMMA));

            if (!expect(TokenType::YFAT_ARROW, "Expected '=>' after superclass constraints")) {
                return nullptr;
            }
        } else {
            // No fat arrow found, restore position
            current_ = saved_pos;
        }
    }

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected trait name after 'trait'");
        return nullptr;
    }
    string trait_name(advance().lexeme);

    // Parse type parameters (one or more lowercase identifiers)
    string type_param;
    vector<string> type_params;
    while (check(TokenType::YIDENTIFIER) && islower(peek().lexeme[0])) {
        string tp(advance().lexeme);
        if (type_param.empty()) type_param = tp;
        type_params.push_back(tp);
    }
    if (type_params.empty()) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected type parameter after trait name");
        return nullptr;
    }

    skip_newlines();

    // Parse method signatures (with optional default implementations) until "end"
    vector<TraitMethodSig> methods;
    while (!check(TokenType::YEND) && !is_at_end()) {
        skip_newlines();
        if (check(TokenType::YEND)) break;

        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected method name in trait declaration");
            break;
        }
        string method_name(advance().lexeme);

        // Check if this is a type signature (has ':') or a default implementation
        if (match(TokenType::YCOLON)) {
            auto type_sig = parse_type();
            if (!type_sig) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected type signature after ':'");
                break;
            }

            methods.push_back({method_name, *type_sig, nullptr});
        } else {
            // Phase 3: This is a default method implementation (no ':' → it's a function def)
            // We need to find the method signature already declared and attach the default impl
            // Back up: the method_name was consumed, now parse patterns and body
            // Construct a FunctionExpr for the default implementation

            vector<PatternNode*> patterns;
            vector<bool> param_borrow;
            while (!check(TokenType::YASSIGN) && !is_at_end() && !check(TokenType::YNEWLINE)) {
                bool borrow = try_consume_borrow_annotation();
                auto pattern = parse_pattern();
                if (pattern) {
                    patterns.push_back(pattern.release());
                    param_borrow.push_back(borrow);
                } else {
                    break;
                }
            }

            expect(TokenType::YASSIGN, "Expected '=' in default method implementation");

            auto body = parse_expr();
            if (!body) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected body for default method");
                break;
            }

            vector<FunctionBody*> bodies;
            bodies.push_back(new BodyWithoutGuards(loc, body.release()));

            auto default_fn = make_unique<FunctionExpr>(loc, method_name, patterns, bodies, std::nullopt);
            default_fn->param_borrow = std::move(param_borrow);
            default_fn->param_borrow.resize(default_fn->patterns.size(), false);

            // Find the matching method signature and attach default
            bool found = false;
            for (auto& m : methods) {
                if (m.name == method_name) {
                    m.default_impl = default_fn.release();
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Default impl before signature — store with a dummy type
                // This allows trait declarations where default comes after sig in same block
                compiler::types::Type dummy = compiler::types::BuiltinType::Unit;
                methods.push_back({method_name, dummy, default_fn.release()});
            }
        }
        skip_newlines();
    }

    if (!expect(TokenType::YEND, "Expected 'end' to close trait declaration")) {
        return nullptr;
    }

    auto node = make_unique<TraitDeclNode>(loc, trait_name, type_param, std::move(methods), std::move(superclasses));
    node->type_params = std::move(type_params);
    return node;
}

unique_ptr<InstanceDeclNode> ParserImpl::parse_instance_declaration() {
    // YINSTANCE was already consumed by the caller
    SourceLocation loc = previous_location();

    // Phase 2: Check for constraints: "Show a => Show (Option a)"
    // Look ahead for "=>" to determine if there are constraints
    vector<pair<string, string>> constraints;
    {
        size_t saved_pos = current_;
        bool found_fat_arrow = false;

        // Scan ahead for =>
        size_t lookahead = 0;
        while (lookahead < 30) {
            auto t = peek(lookahead).type;
            if (t == TokenType::YFAT_ARROW) { found_fat_arrow = true; break; }
            if (t == TokenType::YEND || t == TokenType::YEOF_TOKEN || t == TokenType::YNEWLINE) break;
            lookahead++;
        }

        if (found_fat_arrow) {
            // Parse constraints: "Show a" or "Show a, Eq a"
            do {
                if (!check(TokenType::YIDENTIFIER)) break;
                string c_trait(advance().lexeme);
                if (!check(TokenType::YIDENTIFIER)) {
                    error(ParseError::Type::INVALID_SYNTAX, "Expected type variable after constraint trait name");
                    break;
                }
                string c_var(advance().lexeme);
                constraints.push_back({c_trait, c_var});
            } while (match(TokenType::YCOMMA));

            if (!expect(TokenType::YFAT_ARROW, "Expected '=>' after constraints")) {
                return nullptr;
            }
        } else {
            current_ = saved_pos;
        }
    }

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected trait name after 'instance'");
        return nullptr;
    }
    string trait_name(advance().lexeme);

    // Phase 2: Handle parenthesized type names like "(Option a)"
    string type_name;
    vector<string> type_params;
    if (match(TokenType::YLPAREN)) {
        // Parenthesized type: (TypeName param1 param2 ...)
        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected type name inside parentheses");
            return nullptr;
        }
        type_name = string(advance().lexeme);
        while (check(TokenType::YIDENTIFIER) && islower(peek().lexeme[0])) {
            type_params.push_back(string(advance().lexeme));
        }
        if (!expect(TokenType::YRPAREN, "Expected ')' after parameterized type")) {
            return nullptr;
        }
    } else {
        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected type name after trait name in instance");
            return nullptr;
        }
        type_name = string(advance().lexeme);
        // Multi-param: collect additional uppercase type names
        while (check(TokenType::YIDENTIFIER) && isupper(peek().lexeme[0])) {
            // Additional type arguments for multi-param traits
            // (don't consume lowercase — those are type_params for parameterized types)
            break; // For now, additional types handled below
        }
    }

    // Multi-param trait: collect remaining type names (uppercase identifiers before newline)
    vector<string> type_names = {type_name};
    while (check(TokenType::YIDENTIFIER) && isupper(peek().lexeme[0])) {
        type_names.push_back(string(advance().lexeme));
    }

    skip_newlines();

    // Parse method implementations until "end"
    vector<FunctionExpr*> methods;
    while (!check(TokenType::YEND) && !is_at_end()) {
        skip_newlines();
        if (check(TokenType::YEND)) break;

        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected method implementation in instance declaration");
            break;
        }

        auto func = parse_function();
        if (func) {
            methods.push_back(func.release());
        }
        skip_newlines();
    }

    if (!expect(TokenType::YEND, "Expected 'end' to close instance declaration")) {
        return nullptr;
    }

    auto node = make_unique<InstanceDeclNode>(loc, trait_name, type_name, std::move(methods),
                                          std::move(constraints), std::move(type_params));
    node->type_names = std::move(type_names);
    return node;
}

unique_ptr<RecordNode> ParserImpl::parse_record_definition() {
    SourceLocation loc = previous().location;

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected record name after 'record'");
        return nullptr;
    }

    string record_name(peek().lexeme);
    advance();

    if (!expect(TokenType::YLPAREN, "Expected '(' after record name")) {
        return nullptr;
    }

    vector<pair<IdentifierExpr*, TypeDefinition*>> fields;

    if (!check(TokenType::YRPAREN)) {
        do {
            if (!check(TokenType::YIDENTIFIER)) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected field name");
                return nullptr;
            }

            SourceLocation field_loc = current_location();
            string field_name(current().lexeme);
            advance();

            auto field_id = new IdentifierExpr(field_loc, new NameExpr(field_loc, field_name));

            TypeDefinition* field_type = nullptr;
            if (expect(TokenType::YCOLON, "Expected ':' after field name")) {
                auto type_node = parse_type_name_node();
                if (!type_node) {
                    error(ParseError::Type::INVALID_SYNTAX, "Expected type after ':'");
                    return nullptr;
                }
                vector<TypeNameNode*> empty_types;
                field_type = new TypeDefinition(field_loc, type_node.release(), empty_types);
            }

            fields.push_back({field_id, field_type});

        } while (match(TokenType::YCOMMA));
    }

    if (!expect(TokenType::YRPAREN, "Expected ')' after record fields")) {
        return nullptr;
    }

    auto record_name_expr = new NameExpr(loc, record_name);
    return make_unique<RecordNode>(loc, record_name_expr, fields);
}

} // namespace yona::parser
