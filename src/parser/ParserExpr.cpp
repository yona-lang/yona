//
// ParserExpr.cpp — Expression parsing (Pratt parser, control flow, generators, imports).
//

#include "ParserImpl.h"

namespace yona::parser {

bool ParserImpl::could_be_argument(TokenType type) const {
    switch (type) {
        case TokenType::YINTEGER:
        case TokenType::YFLOAT:
        case TokenType::YSTRING:
        case TokenType::YCHARACTER:
        case TokenType::YBYTE:
        case TokenType::YTRUE:
        case TokenType::YFALSE:
        case TokenType::YUNIT:
        case TokenType::YIDENTIFIER:
        case TokenType::YSYMBOL:
        case TokenType::YLPAREN:
        case TokenType::YLBRACKET:
        case TokenType::YLBRACE:
        case TokenType::YBACKSLASH:
        case TokenType::YUNDERSCORE:
            return true;
        default:
            return false;
    }
}

bool ParserImpl::could_start_expr(TokenType type) const {
    switch (type) {
        case TokenType::YINTEGER:
        case TokenType::YFLOAT:
        case TokenType::YSTRING:
        case TokenType::YCHARACTER:
        case TokenType::YTRUE:
        case TokenType::YFALSE:
        case TokenType::YUNIT:
        case TokenType::YIDENTIFIER:
        case TokenType::YSYMBOL:
        case TokenType::YLPAREN:
        case TokenType::YLBRACKET:
        case TokenType::YLBRACE:
        case TokenType::YBACKSLASH:
        case TokenType::YLET:
        case TokenType::YIF:
        case TokenType::YCASE:
        case TokenType::YTRY:
        case TokenType::YRAISE:
        case TokenType::YWITH:
        case TokenType::YIMPORT:
        case TokenType::YMINUS:
        case TokenType::YNOT:
        case TokenType::YBIT_NOT:
            return true;
        default:
            return false;
    }
}

unique_ptr<ExprNode> ParserImpl::parse_juxtaposition_apply(unique_ptr<ExprNode> func) {
    SourceLocation loc = current_location();

    auto arg = parse_expr(next_precedence(Precedence::CALL));
    if (!arg) {
        return func;
    }

    vector<variant<ExprNode*, ValueExpr*>> args;
    args.push_back(arg.release());

    if (auto id = dynamic_cast<IdentifierExpr*>(func.get())) {
        auto name_copy = new NameExpr(loc, id->name->value);
        auto name_call = new NameCall(loc, name_copy);
        return make_unique<ApplyExpr>(loc, name_call, args);
    } else {
        auto expr_call = new ExprCall(loc, func.release());
        return make_unique<ApplyExpr>(loc, expr_call, args);
    }
}

unique_ptr<ExprNode> ParserImpl::parse_expr(Precedence min_prec) {
    static int recursion_depth = 0;

    struct DepthGuard {
        int& depth;
        DepthGuard(int& d) : depth(d) { depth++; }
        ~DepthGuard() { depth--; }
    } guard(recursion_depth);

    if (recursion_depth > 50) {
        std::cerr << "parse_expr: Max recursion depth exceeded!" << std::endl;
        error(ParseError::Type::INVALID_SYNTAX, "Maximum recursion depth exceeded in parse_expr");
        return nullptr;
    }

    auto left = parse_prefix_expr();
    if (!left) {
        return nullptr;
    }

    int loop_count = 0;
    size_t last_position = current_;
    while (!is_at_end()) {
        if (++loop_count > 100) {
            std::cerr << "parse_expr: Infinite loop detected at position " << current_
                                    << "/" << tokens_.size()
                                    << ", token: " << (current_ < tokens_.size() ? peek().lexeme : "EOF")
                                    << ", type: " << (current_ < tokens_.size() ? static_cast<int>(peek().type) : -1) << std::endl;
            if (current_ > 0 && current_ - 1 < tokens_.size()) {
                std::cerr << "  Previous token: " << tokens_[current_ - 1].lexeme << std::endl;
            }
            if (current_ + 1 < tokens_.size()) {
                std::cerr << "  Next token: " << tokens_[current_ + 1].lexeme << std::endl;
            }
            error(ParseError::Type::INVALID_SYNTAX, "Infinite loop in parse_expr");
            return nullptr;
        }

        if (current_ == last_position && loop_count > 1) {
            break;
        }
        last_position = current_;

        // Field update: expr { field = value, ... }
        if (check(TokenType::YLBRACE) && peek(1).type == TokenType::YIDENTIFIER &&
            peek(2).type == TokenType::YASSIGN) {
            auto sloc = left->source_context;
            auto* id_expr = dynamic_cast<IdentifierExpr*>(left.release());
            if (!id_expr) { left.reset(); break; }
            advance(); // consume {
            vector<pair<NameExpr*, ExprNode*>> updates;
            while (!check(TokenType::YRBRACE) && !is_at_end()) {
                skip_newlines();
                if (!check(TokenType::YIDENTIFIER)) break;
                SourceLocation floc = current_location();
                string fname(advance().lexeme);
                expect(TokenType::YASSIGN, "Expected '=' after field name in update");
                auto val = parse_expr();
                if (val) updates.push_back({new NameExpr(floc, fname), val.release()});
                if (!match(TokenType::YCOMMA)) break;
                skip_newlines();
            }
            expect(TokenType::YRBRACE, "Expected '}' after field update");
            left = make_unique<FieldUpdateExpr>(sloc, id_expr, updates);
            continue;
        }

        // Check for function application by juxtaposition.
        // Skip if the preceding token was an `end`/`in` keyword that
        // syntactically closes a block expression (case/do/with/handle/let).
        // Without this, `case x of ... end <next>` would treat `<next>` as
        // a function argument applied to the case result, which then eats
        // the next case clause when the surrounding context is something
        // like `(\_ -> case x of ... end) followed-by-another-clause`.
        bool prev_closes_block = current_ > 0 &&
            (tokens_[current_ - 1].type == TokenType::YEND ||
             tokens_[current_ - 1].type == TokenType::YIN);
        if (!prev_closes_block && min_prec <= Precedence::CALL && could_be_argument(peek().type)) {
            auto prec = get_infix_precedence(peek().type);
            if (prec == Precedence::LOWEST) {
                left = parse_juxtaposition_apply(std::move(left));
                if (!left) return nullptr;
                continue;
            }
        }

        {
            auto prec = get_infix_precedence(peek().type);
            if (prec < min_prec) break;

            left = parse_infix_expr(std::move(left), prec);
            if (!left) {
                return nullptr;
            }
        }
    }

    return left;
}

unique_ptr<ExprNode> ParserImpl::parse_expr_until_pattern_end() {
    auto expr = parse_expr();
    skip_newlines();
    return expr;
}

unique_ptr<ExprNode> ParserImpl::parse_expr_until_in() {
    auto left = parse_prefix_expr_until_in();
    if (!left) return nullptr;

    int loop_count = 0;
    size_t last_position = current_;
    while (!is_at_end() && peek().type != TokenType::YIN) {
        if (++loop_count > 1000) {
            std::cerr << "parse_expr_until_in: Infinite loop detected!" << std::endl;
            error(ParseError::Type::INVALID_SYNTAX, "Infinite loop in parse_expr_until_in");
            return nullptr;
        }

        if (current_ == last_position && loop_count > 1) {
            break;
        }
        last_position = current_;

        if (could_be_argument(peek().type)) {
            auto prec = get_infix_precedence(peek().type);
            if (prec == Precedence::LOWEST) {
                left = parse_juxtaposition_apply(std::move(left));
                if (!left) return nullptr;
                continue;
            }
        }

        {
            auto prec = get_infix_precedence(peek().type);
            if (prec <= Precedence::LOWEST) break;

            left = parse_infix_expr(std::move(left), prec);
            if (!left) return nullptr;
        }
    }

    return left;
}

unique_ptr<ExprNode> ParserImpl::parse_prefix_expr_until_in() {
    SourceLocation loc = current_location();

    if (is_at_end()) {
        return nullptr;
    }

    if (peek().type == TokenType::YBACKSLASH) {
        return parse_lambda_expr(true);
    } else {
        return parse_prefix_expr();
    }
}

unique_ptr<ExprNode> ParserImpl::parse_prefix_expr() {
    SourceLocation loc = current_location();

    if (is_at_end()) {
        return nullptr;
    }

    switch (peek().type) {
        // Literals
        case TokenType::YINTEGER: {
            auto token = advance();
            auto value = get<int64_t>(token.value);
            return make_unique<IntegerExpr>(loc, value);
        }

        case TokenType::YFLOAT: {
            auto token = advance();
            auto value = get<double>(token.value);
            return make_unique<FloatExpr>(loc, value);
        }

        case TokenType::YSTRING: {
            auto token = advance();
            auto value = get<string>(token.value);
            return make_unique<StringExpr>(loc, value);
        }

        case TokenType::YSTRING_PART: {
            return parse_string_interpolation();
        }

        case TokenType::YCHARACTER: {
            auto token = advance();
            auto value = get<char32_t>(token.value);
            return make_unique<CharacterExpr>(loc, static_cast<wchar_t>(value));
        }

        case TokenType::YBYTE: {
            auto token = advance();
            auto value = get<uint8_t>(token.value);
            return make_unique<ByteExpr>(loc, value);
        }

        case TokenType::YTRUE:
            advance();
            return make_unique<TrueLiteralExpr>(loc);

        case TokenType::YFALSE:
            advance();
            return make_unique<FalseLiteralExpr>(loc);

        case TokenType::YUNIT:
            advance();
            return make_unique<UnitExpr>(loc);

        case TokenType::YSYMBOL: {
            auto token = advance();
            string symbol_name(token.lexeme);
            if (!symbol_name.empty() && symbol_name[0] == ':') {
                symbol_name = symbol_name.substr(1);
            }
            return make_unique<SymbolExpr>(loc, symbol_name);
        }

        case TokenType::YIDENTIFIER: {
            auto token = advance();
            string id_name(token.lexeme);

            // Named ADT construction: Person { name = "Alice", age = 30 }
            if (!id_name.empty() && isupper(id_name[0]) && check(TokenType::YLBRACE)) {
                auto ctor_it = constructor_registry_.find(id_name);
                if (ctor_it != constructor_registry_.end() && !ctor_it->second.field_names.empty()) {
                    advance(); // consume {
                    std::unordered_map<string, unique_ptr<ExprNode>> field_exprs;
                    while (!check(TokenType::YRBRACE) && !is_at_end()) {
                        skip_newlines();
                        if (!check(TokenType::YIDENTIFIER)) break;
                        string fname(advance().lexeme);
                        expect(TokenType::YASSIGN, "Expected '=' after field name");
                        auto expr = parse_expr();
                        if (expr) field_exprs[fname] = std::move(expr);
                        if (!match(TokenType::YCOMMA)) break;
                        skip_newlines();
                    }
                    expect(TokenType::YRBRACE, "Expected '}' after named construction");

                    auto& field_order = ctor_it->second.field_names;
                    auto call_name = new NameCall(loc, new NameExpr(loc, id_name));
                    vector<variant<ExprNode*, ValueExpr*>> args;
                    for (auto& fn : field_order) {
                        auto it = field_exprs.find(fn);
                        if (it != field_exprs.end())
                            args.push_back(it->second.release());
                        else
                            args.push_back(static_cast<ExprNode*>(new UnitExpr(loc)));
                    }
                    return make_unique<ApplyExpr>(loc, call_name, args);
                }
            }

            auto name_expr = new NameExpr(loc, id_name);
            return make_unique<IdentifierExpr>(loc, name_expr);
        }

        // Unary operators
        case TokenType::YNOT:
            advance();
            return make_unique<LogicalNotOpExpr>(loc,
                parse_expr(Precedence::UNARY).release());

        case TokenType::YBIT_NOT:
            advance();
            return make_unique<BinaryNotOpExpr>(loc,
                parse_expr(Precedence::UNARY).release());

        case TokenType::YMINUS: {
            advance();
            auto expr = parse_expr(Precedence::UNARY);
            if (auto int_expr = dynamic_cast<IntegerExpr*>(expr.get())) {
                return make_unique<IntegerExpr>(loc, -int_expr->value);
            }
            if (auto float_expr = dynamic_cast<FloatExpr*>(expr.get())) {
                return make_unique<FloatExpr>(loc, -float_expr->value);
            }
            return make_unique<SubtractExpr>(loc,
                make_unique<IntegerExpr>(loc, 0).release(),
                expr.release());
        }

        // Parenthesized expression or tuple
        case TokenType::YLPAREN: {
            advance();

            if (check(TokenType::YRPAREN)) {
                advance();
                return make_unique<UnitExpr>(loc);
            }

            vector<ExprNode*> elements;
            do {
                auto expr = parse_expr();
                if (expr) elements.push_back(expr.release());
            } while (match(TokenType::YCOMMA));

            expect(TokenType::YRPAREN, "Expected ')' after expression");

            if (elements.size() == 1) {
                return unique_ptr<ExprNode>(elements[0]);
            } else {
                return make_unique<TupleExpr>(loc, elements);
            }
        }

        // List/Sequence or parallel comprehension [| expr | var <- source |]
        case TokenType::YLBRACKET: {
            advance();

            // Parallel comprehension: [| expr for var = source ]
            // The [| opening distinguishes from regular comprehensions.
            // Closing ] is the same as regular comprehensions (no trailing |).
            if (match(TokenType::YPIPE)) {
                auto body = parse_expr();
                if (!body) { error(ParseError::Type::INVALID_SYNTAX, "Expected expression in parallel comprehension"); return nullptr; }
                if (!match(TokenType::YFOR)) {
                    error(ParseError::Type::INVALID_SYNTAX, "Expected 'for' in parallel comprehension [| expr for var = source ]");
                    return nullptr;
                }
                auto extractor = parse_collection_extractor();
                expect(TokenType::YRBRACKET, "Expected ']' after parallel comprehension");

                auto result = make_unique<SeqGeneratorExpr>(loc, body.release(), extractor.release(), nullptr);
                result->is_parallel = true;
                return result;
            }

            if (check(TokenType::YRBRACKET)) {
                advance();
                return make_unique<ValuesSequenceExpr>(loc, vector<ExprNode*>{});
            }

            auto first = parse_expr();

            if (match(TokenType::YFOR)) {
                return parse_sequence_generator(loc, std::move(first));
            } else if (match(TokenType::YDOTDOT)) {
                auto end = parse_range_bound();
                if (!end) {
                    error(ParseError::Type::INVALID_SYNTAX, "Expected numeric expression after '..' in range");
                    return nullptr;
                }

                ExprNode* step = nullptr;
                if (match(TokenType::YDOTDOT)) {
                    auto step_expr = parse_range_bound();
                    if (step_expr) {
                        step = step_expr.release();
                    } else {
                        error(ParseError::Type::INVALID_SYNTAX, "Expected numeric expression for range step");
                    }
                }

                expect(TokenType::YRBRACKET, "Expected ']' after range");
                auto range_expr = make_unique<RangeSequenceExpr>(loc,
                    first.release(), end.release(), step);
                return range_expr;
            } else {
                vector<ExprNode*> elements;
                elements.push_back(first.release());

                while (match(TokenType::YCOMMA)) {
                    auto elem = parse_expr();
                    if (elem) elements.push_back(elem.release());
                }

                expect(TokenType::YRBRACKET, "Expected ']' after list");
                return make_unique<ValuesSequenceExpr>(loc, elements);
            }
        }

        // Set
        case TokenType::YLBRACE: {
            advance();

            if (check(TokenType::YRBRACE)) {
                advance();
                return make_unique<SetExpr>(loc, vector<ExprNode*>{});
            }

            // Check for record literal: { ident = expr, ... }
            // Lookahead: IDENT ASSIGN
            if (check(TokenType::YIDENTIFIER)) {
                size_t saved_pos = current_;
                string first_name(current().lexeme);
                // Records have lowercase field names; uppercase would be a set of constructors
                if (!first_name.empty() && islower(first_name[0])) {
                    advance();
                    if (check(TokenType::YASSIGN)) {
                        advance(); // consume '='
                        // It's a record literal
                        vector<pair<string, ExprNode*>> fields;
                        auto first_val = parse_expr();
                        if (first_val) fields.push_back({first_name, first_val.release()});

                        while (match(TokenType::YCOMMA)) {
                            skip_newlines();
                            if (check(TokenType::YRBRACE)) break;
                            if (!check(TokenType::YIDENTIFIER)) {
                                error(ParseError::Type::INVALID_SYNTAX, "Expected field name in record");
                                break;
                            }
                            string fname(advance().lexeme);
                            if (!expect(TokenType::YASSIGN, "Expected '=' after field name")) break;
                            auto fval = parse_expr();
                            if (fval) fields.push_back({fname, fval.release()});
                        }
                        skip_newlines();
                        expect(TokenType::YRBRACE, "Expected '}' after record literal");

                        // Sort fields by name for deterministic layout
                        std::sort(fields.begin(), fields.end(),
                            [](auto& a, auto& b) { return a.first < b.first; });

                        return make_unique<RecordLiteralExpr>(loc, std::move(fields));
                    }
                }
                // Not a record — restore and fall through to set/dict parsing
                current_ = saved_pos;
            }

            auto first = parse_expr();

            if (match(TokenType::YCOLON)) {
                return parse_dict(loc, std::move(first));
            } else if (match(TokenType::YFOR)) {
                return parse_set_generator(loc, std::move(first));
            } else {
                vector<ExprNode*> elements;
                elements.push_back(first.release());

                while (match(TokenType::YCOMMA)) {
                    auto elem = parse_expr();
                    if (elem) elements.push_back(elem.release());
                }

                expect(TokenType::YRBRACE, "Expected '}' after set");
                return make_unique<SetExpr>(loc, elements);
            }
        }

        // Control flow
        case TokenType::YIF:
            return parse_if_expr();

        case TokenType::YLET:
            return parse_let_expr();

        case TokenType::YCASE:
            return parse_case_expr();

        case TokenType::YDO:
            return parse_do_expr();

        case TokenType::YTRY:
            return parse_try_expr();

        case TokenType::YRAISE:
            return parse_raise_expr();

        case TokenType::YPERFORM:
            return parse_perform_expr();

        case TokenType::YHANDLE:
            return parse_handle_expr();

        case TokenType::YWITH:
            return parse_with_expr();

        case TokenType::YBACKSLASH:
            return parse_lambda_expr();

        case TokenType::YIMPORT:
            return parse_import_expr();

        case TokenType::YEXTERN:
            return parse_extern_decl();

        case TokenType::YRECORD:
            return parse_record_expr();

        default:
            error(ParseError::Type::UNEXPECTED_TOKEN,
                  "Unexpected token in expression");
            return nullptr;
    }
}

unique_ptr<ExprNode> ParserImpl::parse_infix_expr(unique_ptr<ExprNode> left, Precedence prec) {
    SourceLocation loc = current_location();
    auto op = advance();

#define BINARY_OP(TokenType, ExprType, OpStr) \
    case TokenType: { \
        auto right = parse_expr(next_precedence(prec)); \
        if (!right) { \
            error(ParseError::Type::INVALID_SYNTAX, "Expected expression after '" OpStr "'"); \
            return nullptr; \
        } \
        return make_unique<ExprType>(loc, left.release(), right.release()); \
    }

#define BINARY_OP_RIGHT_ASSOC(TokenType, ExprType, OpStr) \
    case TokenType: { \
        auto right = parse_expr(prec); \
        if (!right) { \
            error(ParseError::Type::INVALID_SYNTAX, "Expected expression after '" OpStr "'"); \
            return nullptr; \
        } \
        return make_unique<ExprType>(loc, left.release(), right.release()); \
    }

    switch (op.type) {
        // Binary operators
        BINARY_OP(TokenType::YPLUS, AddExpr, "+")
        BINARY_OP(TokenType::YMINUS, SubtractExpr, "-")
        BINARY_OP(TokenType::YSTAR, MultiplyExpr, "*")
        BINARY_OP(TokenType::YSLASH, DivideExpr, "/")
        BINARY_OP(TokenType::YPERCENT, ModuloExpr, "%")
        BINARY_OP_RIGHT_ASSOC(TokenType::YPOWER, PowerExpr, "**")

        // Comparison
        BINARY_OP(TokenType::YEQ, EqExpr, "==")
        BINARY_OP(TokenType::YNEQ, NeqExpr, "!=")
        BINARY_OP(TokenType::YLT, LtExpr, "<")
        BINARY_OP(TokenType::YGT, GtExpr, ">")
        BINARY_OP(TokenType::YLTE, LteExpr, "<=")
        BINARY_OP(TokenType::YGTE, GteExpr, ">=")

        // Logical
        BINARY_OP(TokenType::YAND, LogicalAndExpr, "&&")
        BINARY_OP(TokenType::YOR, LogicalOrExpr, "||")

        // Bitwise
        BINARY_OP(TokenType::YBIT_AND, BitwiseAndExpr, "&")
        BINARY_OP(TokenType::YPIPE, BitwiseOrExpr, "|")
        BINARY_OP(TokenType::YBIT_XOR, BitwiseXorExpr, "^")
        BINARY_OP(TokenType::YLEFT_SHIFT, LeftShiftExpr, "<<")
        BINARY_OP(TokenType::YRIGHT_SHIFT, RightShiftExpr, ">>")
        BINARY_OP(TokenType::YZERO_FILL_RIGHT_SHIFT, ZerofillRightShiftExpr, ">>>")

        // Cons operators
        BINARY_OP_RIGHT_ASSOC(TokenType::YCONS, ConsLeftExpr, "::")
        BINARY_OP(TokenType::YCONS_RIGHT, ConsRightExpr, ":>")

        // Pipe operators
        BINARY_OP_RIGHT_ASSOC(TokenType::YPIPE_LEFT, PipeLeftExpr, "<|")
        BINARY_OP(TokenType::YPIPE_RIGHT, PipeRightExpr, "|>")
        BINARY_OP(TokenType::YJOIN, JoinExpr, "++")

        // Field access
        case TokenType::YDOT: {
            if (!check(TokenType::YIDENTIFIER)) {
                error(ParseError::Type::INVALID_SYNTAX,
                      "Expected field name after '.'");
                return left;
            }
            auto field_token = advance();
            string field_name(field_token.lexeme);

            if (left->get_type() == AST_IDENTIFIER_EXPR) {
                auto identifier = static_cast<IdentifierExpr*>(left.get());
                string id_name = identifier->name->value;

                auto name_expr = make_unique<NameExpr>(token_location(field_token), field_name);

                if (!id_name.empty() && isupper(id_name[0])) {
                    left = make_unique<ModuleCall>(
                        token_location(op),
                        static_cast<ExprNode*>(left.release()),
                        name_expr.release()
                    );
                } else {
                    left = make_unique<FieldAccessExpr>(
                        token_location(op),
                        static_cast<IdentifierExpr*>(left.release()),
                        name_expr.release()
                    );
                }
            } else {
                error(ParseError::Type::INVALID_SYNTAX,
                      "Field access on complex expressions not yet supported");
                return left;
            }
            break;
        }

        // Function call — parenthesized argument list. One argument, or
        // zero arguments (nullary call). A comma-separated list in this
        // position IS a tuple argument (ML / Haskell convention), not
        // a curried multi-arg call; `f(x, y)` means `f ((x, y))`. Use
        // juxtaposition (`f x y` or `f x (y, z)`) for curried calls.
        case TokenType::YLPAREN: {
            vector<ExprNode*> args;
            vector<pair<string, ExprNode*>> named_args;
            bool has_named_args = false;
            bool had_comma = false;

            if (!check(TokenType::YRPAREN)) {
                do {
                    if (check(TokenType::YIDENTIFIER) && peek(1).type == TokenType::YASSIGN) {
                        string arg_name(advance().lexeme);
                        advance(); // consume '='
                        auto value = parse_expr();
                        if (value) {
                            named_args.push_back({arg_name, value.release()});
                            has_named_args = true;
                        }
                    } else {
                        if (has_named_args) {
                            error(ParseError::Type::INVALID_SYNTAX, "Positional arguments cannot follow named arguments");
                        }
                        auto arg = parse_expr();
                        if (arg) args.push_back(arg.release());
                    }
                    if (check(TokenType::YCOMMA)) had_comma = true;
                } while (match(TokenType::YCOMMA));
            }

            expect(TokenType::YRPAREN, "Expected ')' after arguments");

            CallExpr* call_expr = nullptr;

            if (auto id = dynamic_cast<IdentifierExpr*>(left.get())) {
                auto name_copy = new NameExpr(loc, id->name->value);
                call_expr = new NameCall(loc, name_copy);
            } else {
                call_expr = new ExprCall(loc, left.release());
            }

            vector<variant<ExprNode*, ValueExpr*>> apply_args;
            if (had_comma && !has_named_args && args.size() > 1) {
                // `f(x, y, ...)` → `f ((x, y, ...))` — the paren list is a
                // tuple argument, not a curried call with N arguments.
                auto* tup = new TupleExpr(loc, args);
                apply_args.push_back(static_cast<ExprNode*>(tup));
            } else if (args.empty() && !has_named_args) {
                // `f()` — paren-form call with no args is an application to
                // unit, same as the juxtaposition form `f ()`. Without this
                // we'd build `ApplyExpr(f, [])` which is a 0-of-N partial
                // application (returns the function unchanged).
                apply_args.push_back(static_cast<ExprNode*>(new UnitExpr(loc)));
            } else {
                for (auto* arg : args) {
                    apply_args.push_back(arg);
                }
            }

            auto apply_expr = make_unique<ApplyExpr>(loc, call_expr, apply_args);

            if (has_named_args) {
                vector<pair<string, variant<ExprNode*, ValueExpr*>>> named_apply_args;
                for (const auto& [name, expr] : named_args) {
                    named_apply_args.push_back({name, expr});
                }
                apply_expr->named_args = named_apply_args;
            }

            return apply_expr;
        }

        default:
            current_--; // Put the token back
            return left;
    }
    // Unreachable - all switch cases return
    return left;

#undef BINARY_OP
#undef BINARY_OP_RIGHT_ASSOC
}

unique_ptr<ExprNode> ParserImpl::parse_range_bound() {
    SourceLocation loc = current_location();

    bool negative = false;
    if (check(TokenType::YMINUS)) {
        advance();
        negative = true;
    }

    if (check(TokenType::YINTEGER)) {
        auto token = advance();
        auto value = get<int64_t>(token.value);
        if (negative) value = -value;
        return make_unique<IntegerExpr>(loc, value);
    } else if (check(TokenType::YFLOAT)) {
        auto token = advance();
        auto value = get<double>(token.value);
        if (negative) value = -value;
        return make_unique<FloatExpr>(loc, value);
    }

    return nullptr;
}

Precedence ParserImpl::get_infix_precedence(TokenType type) const {
    switch (type) {
        case TokenType::YPIPE_LEFT:
        case TokenType::YPIPE_RIGHT:
            return Precedence::PIPE;

        case TokenType::YOR:
            return Precedence::LOGICAL_OR;

        case TokenType::YAND:
            return Precedence::LOGICAL_AND;

        case TokenType::YPIPE:
            return Precedence::BITWISE_OR;

        case TokenType::YBIT_XOR:
            return Precedence::BITWISE_XOR;

        case TokenType::YBIT_AND:
            return Precedence::BITWISE_AND;

        case TokenType::YEQ:
        case TokenType::YNEQ:
            return Precedence::EQUALITY;

        case TokenType::YLT:
        case TokenType::YGT:
        case TokenType::YLTE:
        case TokenType::YGTE:
            return Precedence::COMPARISON;

        case TokenType::YCONS:
            return Precedence::CONS;

        case TokenType::YJOIN:
            return Precedence::JOIN;

        case TokenType::YLEFT_SHIFT:
        case TokenType::YRIGHT_SHIFT:
        case TokenType::YZERO_FILL_RIGHT_SHIFT:
            return Precedence::SHIFT;

        case TokenType::YPLUS:
        case TokenType::YMINUS:
            return Precedence::ADDITIVE;

        case TokenType::YSTAR:
        case TokenType::YSLASH:
        case TokenType::YPERCENT:
            return Precedence::MULTIPLICATIVE;

        case TokenType::YPOWER:
            return Precedence::POWER;

        case TokenType::YLPAREN:
            return Precedence::CALL;

        case TokenType::YDOT:
            return Precedence::FIELD;

        default:
            return Precedence::LOWEST;
    }
}

Precedence ParserImpl::next_precedence(Precedence prec) const {
    return static_cast<Precedence>(static_cast<int>(prec) + 1);
}

// --- Control flow ---

unique_ptr<ExprNode> ParserImpl::parse_if_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'if'

    auto condition = parse_expr();
    skip_newlines();
    expect(TokenType::YTHEN, "Expected 'then' after if condition");
    skip_newlines();
    auto then_expr = parse_expr();
    skip_newlines();
    expect(TokenType::YELSE, "Expected 'else' after then expression");
    skip_newlines();
    auto else_expr = parse_expr();

    return make_unique<IfExpr>(loc,
        condition.release(),
        then_expr.release(),
        else_expr.release());
}

unique_ptr<ExprNode> ParserImpl::parse_let_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'let'
    skip_newlines();

    vector<AliasExpr*> aliases;

    do {
        auto alias = parse_alias();
        if (alias) {
            aliases.push_back(alias.release());
        } else {
            break;
        }
    } while (match(TokenType::YCOMMA) && !check(TokenType::YIN) && !is_at_end());

    skip_newlines();
    expect(TokenType::YIN, "Expected 'in' after let bindings");
    skip_newlines();
    auto body = parse_expr();

    if (!body) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after 'in'");
        return nullptr;
    }

    return make_unique<LetExpr>(loc, aliases, body.release());
}

unique_ptr<AliasExpr> ParserImpl::parse_alias() {
    SourceLocation loc = current_location();

    // Pattern destructuring
    if (check(TokenType::YLPAREN) || check(TokenType::YLBRACKET)) {
        auto pattern = parse_pattern();
        if (!pattern) return nullptr;
        expect(TokenType::YASSIGN, "Expected '=' after pattern in let binding");
        auto expr = parse_expr_until_in();
        if (!expr) return nullptr;
        return make_unique<PatternAlias>(loc, pattern.release(), expr.release());
    }

    if (!check(TokenType::YIDENTIFIER) && !check(TokenType::YUNDERSCORE)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected identifier in let binding");
        return nullptr;
    }

    string name(advance().lexeme);

    // Type annotation (optional)
    unique_ptr<compiler::types::Type> type_annotation;
    if (match(TokenType::YCOLON)) {
        type_annotation = parse_type();
        if (!type_annotation) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected type after ':' in let binding");
            return nullptr;
        }
        skip_newlines();
        if (check(TokenType::YIDENTIFIER) && string(peek().lexeme) == name) {
            advance();
        }
    }

    // Check for function-style let binding
    vector<string> params;
    vector<bool> param_borrow;
    while (check(TokenType::YAT) || check(TokenType::YIDENTIFIER) || check(TokenType::YUNDERSCORE)) {
        bool borrow = try_consume_borrow_annotation();
        if (!check(TokenType::YIDENTIFIER) && !check(TokenType::YUNDERSCORE)) {
            if (check(TokenType::YAT))
                error(ParseError::Type::INVALID_SYNTAX,
                      "Expected keyword `borrow` after `@` in parameter list");
            else
                error(ParseError::Type::INVALID_SYNTAX, "Expected parameter name after `@borrow`");
            return nullptr;
        }
        params.push_back(string(advance().lexeme));
        param_borrow.push_back(borrow);
    }

    expect(TokenType::YASSIGN, "Expected '=' in let binding");

    auto expr = parse_expr_until_in();

    if (!expr) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after '=' in let binding");
        return nullptr;
    }

    if (!params.empty()) {
        vector<PatternNode*> patterns;
        for (auto& p : params) {
            if (p == "_") {
                patterns.push_back(new UnderscorePattern(loc));
            } else {
                auto id_expr = new IdentifierExpr(loc, new NameExpr(loc, p));
                variant<LiteralExpr<nullptr_t>*, LiteralExpr<void*>*, SymbolExpr*, IdentifierExpr*> v = id_expr;
                patterns.push_back(new PatternValue(loc, v));
            }
        }
        vector<FunctionBody*> bodies;
        bodies.push_back(new BodyWithoutGuards(loc, expr.release()));
        auto type_sig = type_annotation
            ? std::optional<compiler::types::Type>(*type_annotation)
            : std::nullopt;
        auto func_expr = new FunctionExpr(loc, name, patterns, bodies, std::move(type_sig));
        func_expr->param_borrow = std::move(param_borrow);
        func_expr->param_borrow.resize(func_expr->patterns.size(), false);
        auto name_expr = new NameExpr(loc, name);
        return make_unique<LambdaAlias>(loc, name_expr, func_expr);
    }

    if (auto func_expr = dynamic_cast<FunctionExpr*>(expr.get())) {
        auto name_expr = new NameExpr(loc, name);
        expr.release();
        return make_unique<LambdaAlias>(loc, name_expr, func_expr);
    } else {
        auto id_expr = new IdentifierExpr(loc, new NameExpr(loc, name));
        return make_unique<ValueAlias>(loc, id_expr, expr.release());
    }
}

unique_ptr<ExprNode> ParserImpl::parse_case_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'case'

    auto expr = parse_expr();
    expect(TokenType::YOF, "Expected 'of' after case expression");
    skip_newlines();

    vector<CaseClause*> clauses;

    size_t last_pos = current_;
    while (!check(TokenType::YEND) && !is_at_end()) {
        skip_newlines();
        if (check(TokenType::YEND)) break;
        auto clause = parse_case_clause();
        if (clause) clauses.push_back(clause.release());
        // Guard against infinite loops: if no progress was made, advance and retry
        if (current_ == last_pos) {
            advance();
        }
        last_pos = current_;
    }

    expect(TokenType::YEND, "Expected 'end' after case patterns");

    return make_unique<CaseExpr>(loc, expr.release(), clauses);
}

unique_ptr<CaseClause> ParserImpl::parse_case_clause() {
    SourceLocation loc = current_location();

    auto pattern = parse_pattern();

    if (!pattern) {
        error(ParseError::Type::INVALID_PATTERN, "Expected pattern in case clause");
        return nullptr;
    }

    // Check for guard expression: pattern if guard_expr -> body
    ExprNode* guard = nullptr;
    if (check(TokenType::YIF) && !check_ahead(TokenType::YARROW)) {
        advance(); // consume 'if'
        auto guard_expr = parse_expr();
        if (guard_expr) guard = guard_expr.release();
    }

    expect(TokenType::YARROW, "Expected '->' after pattern");

    auto body = parse_expr_until_pattern_end();

    if (!body) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after '->'");
        return nullptr;
    }

    return make_unique<CaseClause>(loc, pattern.release(), body.release(), guard);
}

unique_ptr<ExprNode> ParserImpl::parse_do_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'do'
    skip_newlines();

    vector<ExprNode*> expressions;

    while (!check(TokenType::YEND) && !is_at_end()) {
        skip_newlines();
        if (check(TokenType::YEND)) break;

        // Check for binding: identifier = expr (or pattern = expr)
        // Look ahead: if we see IDENTIFIER followed by ASSIGN, it's a do-binding
        if (check(TokenType::YIDENTIFIER) && peek(1).type == TokenType::YASSIGN) {
            // Do-binding: name = expr — desugar to sequential let
            SourceLocation bind_loc = current_location();
            string name(advance().lexeme);
            advance(); // consume '='
            auto value = parse_expr();
            skip_newlines();

            // Parse remaining expressions as the body
            vector<ExprNode*> rest;
            while (!check(TokenType::YEND) && !is_at_end()) {
                skip_newlines();
                if (check(TokenType::YEND)) break;
                if (check(TokenType::YIDENTIFIER) && peek(1).type == TokenType::YASSIGN) {
                    // Another binding — recurse by building a nested do
                    auto inner_do = parse_do_remaining(rest);
                    if (inner_do) rest.push_back(inner_do.release());
                    break;
                }
                size_t pos_before = current_;
                auto expr = parse_expr();
                if (expr) rest.push_back(expr.release());
                else if (current_ == pos_before) { advance(); break; }
                skip_newlines();
            }

            // Build: let name = value in <rest as do body>
            auto* id = new IdentifierExpr(bind_loc, new NameExpr(bind_loc, name));
            auto* alias = new ValueAlias(bind_loc, id, value.release());
            ExprNode* body;
            if (rest.size() == 1) {
                body = rest[0];
            } else if (rest.empty()) {
                body = new UnitExpr(bind_loc);
            } else {
                body = new DoExpr(bind_loc, rest);
            }
            auto* let_expr = new LetExpr(bind_loc, {alias}, body);
            expressions.push_back(let_expr);

            // The let wraps the rest, so we're done with the do body
            break;
        }

        size_t pos_before = current_;
        auto expr = parse_expr();
        if (expr) {
            expressions.push_back(expr.release());
        } else if (current_ == pos_before) {
            advance();
            break;
        }
        skip_newlines();
    }

    expect(TokenType::YEND, "Expected 'end' after do block");

    return make_unique<DoExpr>(loc, expressions);
}

// Helper: parse remaining do-block entries when a binding is encountered mid-block
unique_ptr<ExprNode> ParserImpl::parse_do_remaining(vector<ExprNode*>& exprs) {
    while (!check(TokenType::YEND) && !is_at_end()) {
        skip_newlines();
        if (check(TokenType::YEND)) break;

        if (check(TokenType::YIDENTIFIER) && peek(1).type == TokenType::YASSIGN) {
            SourceLocation bind_loc = current_location();
            string name(advance().lexeme);
            advance(); // consume '='
            auto value = parse_expr();
            skip_newlines();

            vector<ExprNode*> rest;
            auto inner = parse_do_remaining(rest);
            if (inner) rest.push_back(inner.release());

            auto* id = new IdentifierExpr(bind_loc, new NameExpr(bind_loc, name));
            auto* alias = new ValueAlias(bind_loc, id, value.release());
            ExprNode* body;
            if (rest.size() == 1) body = rest[0];
            else if (rest.empty()) body = new UnitExpr(bind_loc);
            else body = new DoExpr(bind_loc, rest);
            return unique_ptr<ExprNode>(new LetExpr(bind_loc, {alias}, body));
        }

        size_t pos_before = current_;
        auto expr = parse_expr();
        if (expr) exprs.push_back(expr.release());
        else if (current_ == pos_before) { advance(); break; }
        skip_newlines();
    }
    return nullptr;
}

unique_ptr<ExprNode> ParserImpl::parse_try_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'try'
    skip_newlines();

    auto expr = parse_expr();
    skip_newlines();

    vector<CatchExpr*> catches;

    while (match(TokenType::YCATCH)) {
        skip_newlines();
        auto error_pattern = parse_pattern();
        expect(TokenType::YARROW, "Expected '->' after catch pattern");
        auto handler = parse_expr();
        skip_newlines();

        auto pwg = new PatternWithoutGuards(current_location(), handler.release());
        variant<PatternWithoutGuards*, vector<PatternWithGuards*>> var = pwg;
        auto catch_pattern = new CatchPatternExpr(current_location(), error_pattern.release(), var);
        vector<CatchPatternExpr*> patterns = {catch_pattern};
        catches.push_back(new CatchExpr(current_location(), patterns));
    }

    CatchExpr* catch_expr = catches.empty() ? nullptr : catches[0];
    return make_unique<TryCatchExpr>(loc, expr.release(), catch_expr);
}

unique_ptr<ExprNode> ParserImpl::parse_raise_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'raise'
    auto value = parse_expr();
    if (!value) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after 'raise'");
        return nullptr;
    }
    return make_unique<RaiseExpr>(loc, value.release());
}

unique_ptr<ExprNode> ParserImpl::parse_with_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'with'

    // Syntax: with <name> = <resource> in <body>
    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX,
              "Expected identifier after 'with'");
        return nullptr;
    }

    string name(advance().lexeme);

    expect(TokenType::YASSIGN, "Expected '=' after identifier in with expression");

    auto resource = parse_expr();

    expect(TokenType::YIN, "Expected 'in' after resource expression in with");

    auto body = parse_expr();

    auto name_expr = new NameExpr(loc, name);
    return make_unique<WithExpr>(loc, false, resource.release(), name_expr, body.release());
}

unique_ptr<ExprNode> ParserImpl::parse_record_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'record'

    auto record = parse_record_definition();
    if (!record) {
        return nullptr;
    }

    return make_unique<UnitExpr>(loc);
}

unique_ptr<ExprNode> ParserImpl::parse_string_interpolation() {
    SourceLocation loc = current_location();
    unique_ptr<ExprNode> result = nullptr;

    auto join = [&](unique_ptr<ExprNode> right) {
        if (!result) {
            result = std::move(right);
        } else {
            result = make_unique<JoinExpr>(loc, result.release(), right.release());
        }
    };

    while (true) {
        if (check(TokenType::YSTRING_PART)) {
            auto token = advance();
            auto value = get<string>(token.value);
            if (!value.empty()) {
                join(make_unique<StringExpr>(loc, value));
            }

            if (is_at_end()) break;

            if (!check(TokenType::YSTRING_PART) && !check(TokenType::YSTRING)) {
                auto expr = parse_prefix_expr();
                if (expr) {
                    join(std::move(expr));
                }
            }
        } else if (check(TokenType::YSTRING)) {
            auto token = advance();
            auto value = get<string>(token.value);
            if (!value.empty()) {
                join(make_unique<StringExpr>(loc, value));
            }
            break;
        } else {
            break;
        }
    }

    return result ? std::move(result) : make_unique<StringExpr>(loc, "");
}

unique_ptr<ExprNode> ParserImpl::parse_lambda_expr(bool stop_at_in) {
    SourceLocation loc = current_location();
    advance(); // consume '\\'

    vector<PatternNode*> params;
    vector<bool> param_borrow;

    if (check(TokenType::YLPAREN)) {
        advance();
        if (!check(TokenType::YRPAREN)) {
            do {
                bool borrow = try_consume_borrow_annotation();
                auto param = parse_pattern();
                if (param) {
                    params.push_back(param.release());
                    param_borrow.push_back(borrow);
                } else return nullptr;
            } while (match(TokenType::YCOMMA));
        }
        if (!expect(TokenType::YRPAREN, "Expected ')' after lambda parameters")) {
            return nullptr;
        }
    } else {
        while (!check(TokenType::YARROW) && !is_at_end()) {
            bool borrow = try_consume_borrow_annotation();
            auto param = parse_pattern();
            if (param) {
                params.push_back(param.release());
                param_borrow.push_back(borrow);
            } else return nullptr;
            match(TokenType::YCOMMA);
        }
    }

    if (!expect(TokenType::YARROW, "Expected '->' in lambda expression")) {
        return nullptr;
    }

    auto body = stop_at_in ? parse_expr_until_in() : parse_expr();

    if (!body) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after '->' in lambda");
        return nullptr;
    }

    vector<FunctionBody*> bodies;
    bodies.push_back(new BodyWithoutGuards(loc, body.release()));

    auto func_expr = new FunctionExpr(loc, "", params, bodies);
    func_expr->param_borrow = std::move(param_borrow);
    func_expr->param_borrow.resize(func_expr->patterns.size(), false);

    return unique_ptr<ExprNode>(func_expr);
}

// --- Generators ---

unique_ptr<ExprNode> ParserImpl::parse_sequence_generator(SourceLocation loc, unique_ptr<ExprNode> expr) {
    auto extractor = parse_collection_extractor();

    expect(TokenType::YRBRACKET, "Expected ']' after sequence generator");

    return make_unique<SeqGeneratorExpr>(loc, expr.release(), extractor.release(), nullptr);
}

unique_ptr<ExprNode> ParserImpl::parse_set_generator(SourceLocation loc, unique_ptr<ExprNode> expr) {
    auto extractor = parse_collection_extractor();

    expect(TokenType::YRBRACE, "Expected '}' after set generator");

    return make_unique<SetGeneratorExpr>(loc, expr.release(), extractor.release(), nullptr);
}

unique_ptr<CollectionExtractorExpr> ParserImpl::parse_collection_extractor() {
    SourceLocation loc = current_location();

    auto pattern = parse_pattern();
    expect(TokenType::YASSIGN, "Expected '<-' in generator");
    auto collection = parse_expr();

    ExprNode* condition = nullptr;
    if (match(TokenType::YCOMMA) && check(TokenType::YIF)) {
        advance(); // consume 'if'
        condition = parse_expr().release();
    }

    ExprNode* coll = collection.release();

    if (auto underscore = dynamic_cast<UnderscoreNode*>(pattern.get())) {
        pattern.release();
        return make_unique<ValueCollectionExtractorExpr>(loc, underscore, coll, condition);
    } else if (auto pat_val = dynamic_cast<PatternValue*>(pattern.get())) {
        if (auto id = get_if<IdentifierExpr*>(&pat_val->expr)) {
            pattern.release();
            return make_unique<ValueCollectionExtractorExpr>(loc, *id, coll, condition);
        }
    }
    return make_unique<ValueCollectionExtractorExpr>(loc, new UnderscoreNode(loc), coll, condition);
}

unique_ptr<ExprNode> ParserImpl::parse_dict(SourceLocation loc, unique_ptr<ExprNode> first_key) {
    auto first_value = parse_expr();

    if (match(TokenType::YFOR)) {
        auto extractor = parse_collection_extractor();

        expect(TokenType::YRBRACE, "Expected '}' after dict generator");

        return make_unique<DictGeneratorExpr>(loc,
            new DictGeneratorReducer(loc, first_key.release(), first_value.release()),
            extractor.release(), nullptr);
    }

    vector<pair<ExprNode*, ExprNode*>> entries;
    entries.push_back({first_key.release(), first_value.release()});

    while (match(TokenType::YCOMMA)) {
        auto key = parse_expr();
        expect(TokenType::YCOLON, "Expected ':' in dictionary entry");
        auto value = parse_expr();

        entries.push_back({key.release(), value.release()});
    }

    expect(TokenType::YRBRACE, "Expected '}' after dictionary");

    return make_unique<DictExpr>(loc, entries);
}

// --- Import / extern ---

unique_ptr<ExprNode> ParserImpl::parse_extern_decl() {
    SourceLocation loc = current_location();
    advance(); // consume 'extern'
    skip_newlines();

    ast::ExternPromiseKind ext_promise = ast::ExternPromiseKind::Sync;
    if (check(TokenType::YIDENTIFIER) && current().lexeme == "async") {
        ext_promise = ast::ExternPromiseKind::ThreadPool;
        advance();
        skip_newlines();
    } else if (check(TokenType::YIDENTIFIER) && current().lexeme == "io") {
        ext_promise = ast::ExternPromiseKind::IoUring;
        advance();
        skip_newlines();
    } else if (check(TokenType::YIDENTIFIER) && current().lexeme == "native") {
        ext_promise = ast::ExternPromiseKind::NativePtr;
        advance();
        skip_newlines();
    }

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected function name after 'extern'");
        return nullptr;
    }
    string name(advance().lexeme);

    expect(TokenType::YCOLON, "Expected ':' after extern function name");
    skip_newlines();

    auto type_ann = parse_type();
    if (!type_ann) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected type annotation after ':'");
        return nullptr;
    }

    skip_newlines();
    expect(TokenType::YIN, "Expected 'in' after extern type annotation");
    skip_newlines();

    auto body = parse_expr();
    if (!body) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after 'in' in extern");
        return nullptr;
    }

    return make_unique<ExternDeclExpr>(loc, name, *type_ann, body.release(), ext_promise);
}

unique_ptr<ExprNode> ParserImpl::parse_import_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'import'

    vector<ImportClauseExpr*> clauses;

    int loop_guard = 0;
    while (!check(TokenType::YIN) && !is_at_end()) {
        if (++loop_guard > 100) {
            error(ParseError::Type::INVALID_SYNTAX, "Too many import clauses");
            return nullptr;
        }

        auto old_pos = current_;
        if (auto clause = parse_import_clause()) {
            clauses.push_back(clause.release());
        } else {
            break;
        }

        if (current_ == old_pos) {
            error(ParseError::Type::INVALID_SYNTAX, "Import clause parsing made no progress");
            return nullptr;
        }

        if (!match(TokenType::YCOMMA)) {
            while (match(TokenType::YNEWLINE)) {}
        }
    }

    if (!expect(TokenType::YIN, "Expected 'in' after import clauses")) {
        return nullptr;
    }

    while (match(TokenType::YNEWLINE)) {}

    auto expr = parse_expr();
    if (!expr) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after 'in'");
        return nullptr;
    }

    return make_unique<ImportExpr>(loc, clauses, expr.release());
}

unique_ptr<ImportClauseExpr> ParserImpl::parse_import_clause() {
    SourceLocation loc = current_location();

    if (check_fqn_start()) {
        auto fqn = parse_fqn();
        if (!fqn) return nullptr;

        if (match(TokenType::YAS)) {
            auto alias = parse_name();
            if (!alias) return nullptr;
            return make_unique<ModuleImport>(loc, fqn.release(), alias.release());
        }

        return make_unique<ModuleImport>(loc, fqn.release(), nullptr);
    }

    return parse_functions_import();
}

unique_ptr<FunctionsImport> ParserImpl::parse_functions_import() {
    SourceLocation loc = current_location();
    vector<FunctionAlias*> aliases;

    do {
        auto name = parse_name();
        if (!name) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected function name");
            return nullptr;
        }

        NameExpr* alias = nullptr;
        if (match(TokenType::YAS)) {
            auto alias_name = parse_name();
            if (!alias_name) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected alias name after 'as'");
                return nullptr;
            }
            alias = alias_name.release();
        }

        aliases.push_back(new FunctionAlias(loc, name.release(), alias));
    } while (match(TokenType::YCOMMA));


    if (!expect(TokenType::YFROM, "Expected 'from' after function list")) {
        return nullptr;
    }

    auto fqn = parse_fqn();
    if (!fqn) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected module name after 'from'");
        return nullptr;
    }

    return make_unique<FunctionsImport>(loc, aliases, fqn.release());
}

bool ParserImpl::check_fqn_start() {
    int lookahead = current_;
    int depth = 0;

    while (lookahead < static_cast<int>(tokens_.size()) && depth < 10) {
        TokenType type = tokens_[lookahead].type;

        if (type == TokenType::YFROM) {
            return false;
        }

        if (type == TokenType::YIN) {
            break;
        }

        if (type == TokenType::YIDENTIFIER || type == TokenType::YBACKSLASH ||
            type == TokenType::YAS || type == TokenType::YCOMMA) {
            lookahead++;
            depth++;
        } else {
            break;
        }
    }

    if (!check(TokenType::YIDENTIFIER)) return false;

    if (current_ + 1 < tokens_.size()) {
        return tokens_[current_ + 1].type == TokenType::YBACKSLASH;
    }

    return true;
}

unique_ptr<FqnExpr> ParserImpl::parse_fqn() {
    SourceLocation loc = current_location();

    vector<NameExpr*> parts;

    int loop_count = 0;
    while (true) {
        if (++loop_count > 100) {
            error(ParseError::Type::INVALID_SYNTAX, "Module name too long");
            return nullptr;
        }

        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected identifier in module name");
            return nullptr;
        }

        auto name = parse_name();
        if (!name) return nullptr;

        if (match(TokenType::YBACKSLASH)) {
            parts.push_back(name.release());
        } else {
            optional<PackageNameExpr*> package;
            if (!parts.empty()) {
                package = new PackageNameExpr(loc, parts);
            }
            return make_unique<FqnExpr>(loc, package, name.release());
        }
    }
}

unique_ptr<NameExpr> ParserImpl::parse_name() {
    if (!check(TokenType::YIDENTIFIER)) {
        return nullptr;
    }

    auto token = advance();
    return make_unique<NameExpr>(token_location(token), string(token.lexeme));
}

// ===== Algebraic Effects =====

/// Parse `perform Effect.op args`
unique_ptr<ExprNode> ParserImpl::parse_perform_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'perform'
    skip_newlines();

    // Parse Effect.op
    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected effect name after 'perform'");
        return nullptr;
    }
    string effect_name(advance().lexeme);

    if (!match(TokenType::YDOT)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected '.' after effect name in perform");
        return nullptr;
    }

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected operation name after '.'");
        return nullptr;
    }
    string op_name(advance().lexeme);

    // Parse arguments (0 or more expressions)
    vector<ExprNode*> args;
    while (!is_at_end() && !check(TokenType::YNEWLINE) && !check(TokenType::YSEMICOLON) &&
           !check(TokenType::YIN) && !check(TokenType::YEND) && !check(TokenType::YWITH) &&
           !check(TokenType::YELSE) && !check(TokenType::YTHEN) && !check(TokenType::YRBRACKET) &&
           !check(TokenType::YRPAREN) && !check(TokenType::YCOMMA)) {
        auto arg = parse_expr();
        if (!arg) break;
        args.push_back(arg.release());
    }

    return make_unique<PerformExpr>(loc, effect_name, op_name, args);
}

/// Parse `handle <body> with <clauses> end`
unique_ptr<ExprNode> ParserImpl::parse_handle_expr() {
    SourceLocation loc = current_location();
    advance(); // consume 'handle'
    skip_newlines();

    // Parse body expression (everything until 'with')
    auto body = parse_expr();
    if (!body) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected expression after 'handle'");
        return nullptr;
    }

    skip_newlines();
    expect(TokenType::YWITH, "Expected 'with' after handle body");
    skip_newlines();

    // Parse handler clauses until 'end'
    vector<HandlerClause*> clauses;
    while (!is_at_end() && !check(TokenType::YEND)) {
        skip_newlines();
        if (check(TokenType::YEND)) break;

        SourceLocation clause_loc = current_location();

        // Check for return clause: `return val -> body`
        if (check(TokenType::YIDENTIFIER) && current().lexeme == "return") {
            advance(); // consume 'return'
            skip_newlines();
            if (!check(TokenType::YIDENTIFIER)) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected binding name after 'return'");
                break;
            }
            string binding(advance().lexeme);
            skip_newlines();
            expect(TokenType::YARROW, "Expected '->' in return handler");
            skip_newlines();
            auto ret_body = parse_expr();
            if (!ret_body) break;
            clauses.push_back(new HandlerClause(clause_loc, binding, ret_body.release()));
            skip_newlines();
            continue;
        }

        // Parse operation clause: Effect.op arg_names resume -> body
        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected effect name or 'return' in handler clause");
            break;
        }
        string effect_name(advance().lexeme);
        if (!match(TokenType::YDOT)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected '.' after effect name in handler");
            break;
        }
        if (!check(TokenType::YIDENTIFIER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected operation name after '.'");
            break;
        }
        string op_name(advance().lexeme);
        skip_newlines();

        // Parse arg names and resume name (identifiers before '->')
        vector<string> arg_names;
        string resume_name;
        while (check(TokenType::YIDENTIFIER) || check(TokenType::YLPAREN)) {
            if (check(TokenType::YLPAREN)) {
                advance(); // consume '('
                if (check(TokenType::YRPAREN)) {
                    advance(); // consume ')' — unit arg ()
                    continue;
                }
                // Named arg inside parens
                if (check(TokenType::YIDENTIFIER)) {
                    arg_names.push_back(string(advance().lexeme));
                }
                expect(TokenType::YRPAREN, "Expected ')'");
                continue;
            }
            // Check if next token after this identifier is '->' (makes this the resume name)
            string name(advance().lexeme);
            skip_newlines();
            if (check(TokenType::YARROW)) {
                resume_name = name;
                break;
            }
            arg_names.push_back(name);
        }

        expect(TokenType::YARROW, "Expected '->' in handler clause");
        skip_newlines();
        auto clause_body = parse_expr();
        if (!clause_body) break;

        clauses.push_back(new HandlerClause(clause_loc, effect_name, op_name,
                                            arg_names, resume_name, clause_body.release()));
        skip_newlines();
    }

    expect(TokenType::YEND, "Expected 'end' after handler clauses");
    return make_unique<HandleExpr>(loc, body.release(), clauses);
}

} // namespace yona::parser
