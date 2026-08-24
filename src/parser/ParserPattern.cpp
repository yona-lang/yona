//
// ParserPattern.cpp — Pattern parsing for case expressions and function parameters.
//

#include "ParserImpl.h"

namespace yona::parser {

unique_ptr<PatternNode> ParserImpl::parse_pattern() {
    return parse_pattern_or();
}

unique_ptr<PatternNode> ParserImpl::parse_pattern_no_or() {
    return parse_pattern_primary();
}

unique_ptr<PatternNode> ParserImpl::parse_pattern_or() {
    auto left = parse_pattern_primary();

    vector<unique_ptr<PatternNode>> patterns;
    patterns.push_back(std::move(left));

    while (match(TokenType::YPIPE)) {
        auto right = parse_pattern_primary();
        patterns.push_back(std::move(right));
    }

    if (patterns.size() == 1) {
        return std::move(patterns[0]);
    }

    return make_unique<OrPattern>(peek().location, std::move(patterns));
}

unique_ptr<PatternNode> ParserImpl::parse_pattern_primary() {
    SourceLocation loc = current_location();

    // Underscore pattern
    if (match(TokenType::YUNDERSCORE)) {
        return make_unique<UnderscorePattern>(loc);
    }

    // Symbol pattern
    if (check(TokenType::YSYMBOL)) {
        auto token = advance();
        string symbol_name(token.lexeme);
        if (!symbol_name.empty() && symbol_name[0] == ':') {
            symbol_name = symbol_name.substr(1);
        }
        auto symbol_expr = new SymbolExpr(loc, symbol_name);
        return make_unique<PatternValue>(loc, symbol_expr);
    }

    // Integer literal pattern
    if (check(TokenType::YINTEGER)) {
        auto token = advance();
        auto value = get<int64_t>(token.value);
        auto literal_expr = new IntegerExpr(loc, value);
        return make_unique<PatternValue>(loc, reinterpret_cast<LiteralExpr<void*>*>(literal_expr));
    }

    // Byte literal pattern
    if (check(TokenType::YBYTE)) {
        auto token = advance();
        auto value = get<uint8_t>(token.value);
        auto byte_expr = new ByteExpr(loc, value);
        return make_unique<PatternValue>(loc, reinterpret_cast<LiteralExpr<void*>*>(byte_expr));
    }

    // Float literal pattern
    if (check(TokenType::YFLOAT)) {
        auto token = advance();
        auto value = get<double>(token.value);
        auto float_expr = new FloatExpr(loc, value);
        return make_unique<PatternValue>(loc, reinterpret_cast<LiteralExpr<void*>*>(float_expr));
    }

    // String literal pattern
    if (check(TokenType::YSTRING)) {
        auto token = advance();
        auto value = get<string>(token.value);
        auto string_expr = new StringExpr(loc, value);
        return make_unique<PatternValue>(loc, reinterpret_cast<LiteralExpr<void*>*>(string_expr));
    }

    // Character literal pattern
    if (check(TokenType::YCHARACTER)) {
        auto token = advance();
        auto value = get<char32_t>(token.value);
        auto char_expr = new CharacterExpr(loc, static_cast<char>(value));
        return make_unique<PatternValue>(loc, reinterpret_cast<LiteralExpr<void*>*>(char_expr));
    }

    // Boolean literal patterns
    if (check(TokenType::YTRUE)) {
        advance();
        auto true_expr = new TrueLiteralExpr(loc);
        return make_unique<PatternValue>(loc, reinterpret_cast<LiteralExpr<void*>*>(true_expr));
    }

    if (check(TokenType::YFALSE)) {
        advance();
        auto false_expr = new FalseLiteralExpr(loc);
        return make_unique<PatternValue>(loc, reinterpret_cast<LiteralExpr<void*>*>(false_expr));
    }

    // Tuple pattern or typed pattern (name : Type)
    if (match(TokenType::YLPAREN)) {
        // Check for typed pattern: (name : Type)
        // Lookahead: IDENT COLON IDENT RPAREN
        if (check(TokenType::YIDENTIFIER)) {
            size_t saved_pos = current_;
            string name(current().lexeme);
            advance();
            if (check(TokenType::YCOLON)) {
                advance(); // consume ':'
                if (check(TokenType::YIDENTIFIER)) {
                    string type_name(advance().lexeme);
                    if (check(TokenType::YRPAREN)) {
                        advance(); // consume ')'
                        return make_unique<TypedPattern>(loc, name, type_name);
                    }
                }
            }
            // Not a typed pattern — restore position
            current_ = saved_pos;
        }

        vector<PatternNode*> elements;

        if (!check(TokenType::YRPAREN)) {
            do {
                auto elem = parse_pattern();
                if (elem) elements.push_back(elem.release());
            } while (match(TokenType::YCOMMA));
        }

        expect(TokenType::YRPAREN, "Expected ')' after tuple pattern");
        // Parentheses group a pattern; only a comma creates a tuple.  Keeping
        // a single grouped constructor as a TuplePattern prevents normal
        // function-parameter destructuring from seeing its bound fields.
        if (elements.size() == 1)
            return unique_ptr<PatternNode>(elements.front());
        return make_unique<TuplePattern>(loc, elements);
    }

    // List pattern
    if (match(TokenType::YLBRACKET)) {
        vector<PatternNode*> elements;

        if (!check(TokenType::YRBRACKET)) {
            while (true) {
                auto elem = parse_pattern_no_or();
                if (elem) elements.push_back(elem.release());

                // Check for pipe (|) for head|tail pattern
                if (check(TokenType::YPIPE)) {
                    advance(); // consume |

                    auto tail = parse_pattern();
                    expect(TokenType::YRBRACKET, "Expected ']' after tail pattern");

                    vector<PatternWithoutSequence*> heads;
                    for (auto e : elements) {
                        heads.push_back(e);
                    }

                    if (!heads.empty()) {
                        return make_unique<HeadTailsPattern>(loc, heads, tail.release());
                    } else {
                        return std::move(tail);
                    }
                }

                if (!match(TokenType::YCOMMA)) break;
            }
        }

        expect(TokenType::YRBRACKET, "Expected ']' after list pattern");
        return make_unique<SeqPattern>(loc, elements);
    }

    // Record pattern
    if (match(TokenType::YLBRACE)) {
        string record_type = "";
        vector<pair<NameExpr*, Pattern*>> fields;

        while (!check(TokenType::YRBRACE) && !is_at_end()) {
            if (check(TokenType::YSYMBOL)) {
                string field_name = string(advance().lexeme).substr(1);
                expect(TokenType::YCOLON, "Expected ':' after field name");

                auto pattern = parse_pattern();
                if (pattern) {
                    auto name_expr = new NameExpr(loc, field_name);
                    fields.push_back({name_expr, pattern.release()});
                }
            } else {
                error(ParseError::Type::INVALID_PATTERN,
                      "Expected field name in record pattern");
                synchronize();
                break;
            }

            if (!match(TokenType::YCOMMA)) break;
        }

        expect(TokenType::YRBRACE, "Expected '}' after record pattern");
        return make_unique<RecordPattern>(loc, record_type, fields);
    }

    // Variable pattern or constructor pattern
    if (check(TokenType::YIDENTIFIER)) {
        // Capitalized names are constructors (prelude, same-module, or imported).
        // Unknown arity (not yet in the registry) consumes primary patterns
        // until a non-pattern token so `import … from Std\Json in case j of
        // JsonObject pairs -> …` parses before codegen loads the .yonai.
        string peek_name(current().lexeme);
        if (!peek_name.empty() && isupper(peek_name[0])) {
            auto ctor_it = constructor_registry_.find(peek_name);
            advance(); // consume constructor name
            // Named field pattern: Person { name = n, age = a }
            if (check(TokenType::YLBRACE)) {
                advance(); // consume {
                vector<pair<NameExpr*, Pattern*>> named_fields;
                while (!check(TokenType::YRBRACE) && !is_at_end()) {
                    skip_newlines();
                    if (!check(TokenType::YIDENTIFIER)) break;
                    string fname(advance().lexeme);
                    expect(TokenType::YASSIGN, "Expected '=' after field name in pattern");
                    auto pat = parse_pattern();
                    if (pat) named_fields.push_back({new NameExpr(loc, fname), pat.release()});
                    if (!match(TokenType::YCOMMA)) break;
                    skip_newlines();
                }
                expect(TokenType::YRBRACE, "Expected '}' after named field pattern");
                return make_unique<RecordPattern>(loc, peek_name, named_fields);
            }
            // Parenthesized constructor: Person(x, y)
            if (check(TokenType::YLPAREN)) {
                advance(); // consume '('
                vector<PatternNode*> sub_pats;
                if (!check(TokenType::YRPAREN)) {
                    do {
                        if (check(TokenType::YIDENTIFIER) && peek(1).type == TokenType::YASSIGN) {
                            advance(); // consume field name
                            advance(); // consume '='
                            auto pattern = parse_pattern();
                            if (pattern)
                                sub_pats.push_back(pattern.release());
                        } else {
                            auto pattern = parse_pattern();
                            if (pattern)
                                sub_pats.push_back(pattern.release());
                        }
                    } while (match(TokenType::YCOMMA));
                }
                expect(TokenType::YRPAREN, "Expected ')' after constructor pattern arguments");
                return make_unique<ConstructorPattern>(loc, peek_name, sub_pats);
            }
            // Positional juxtaposition: Some x, Cons h t, JsonObject pairs
            vector<PatternNode*> sub_pats;
            auto pattern_start = [&]() {
                if (is_at_end()) return false;
                switch (current().type) {
                case TokenType::YIDENTIFIER:
                case TokenType::YUNDERSCORE:
                case TokenType::YSYMBOL:
                case TokenType::YINTEGER:
                case TokenType::YBYTE:
                case TokenType::YFLOAT:
                case TokenType::YSTRING:
                case TokenType::YLBRACKET:
                case TokenType::YLPAREN:
                case TokenType::YTRUE:
                case TokenType::YFALSE:
                    return true;
                default:
                    return false;
                }
            };
            if (ctor_it != constructor_registry_.end()) {
                for (int j = 0; j < ctor_it->second.arity; j++) {
                    auto sub = parse_pattern_primary();
                    if (sub) sub_pats.push_back(sub.release());
                }
            } else {
                while (pattern_start()) {
                    auto sub = parse_pattern_primary();
                    if (!sub) break;
                    sub_pats.push_back(sub.release());
                }
            }
            return make_unique<ConstructorPattern>(loc, peek_name, sub_pats);
        }

        string name(advance().lexeme);

        // Check for constructor pattern (e.g., Person(x, y))
        if (check(TokenType::YLPAREN) && !name.empty() && isupper(name[0])) {
            advance(); // consume '('
            vector<PatternNode*> sub_pats;

            if (!check(TokenType::YRPAREN)) {
                do {
                    if (check(TokenType::YIDENTIFIER) && peek(1).type == TokenType::YASSIGN) {
                        advance(); // consume field name
                        advance(); // consume '='
                        auto pattern = parse_pattern();
                        if (pattern) {
                            sub_pats.push_back(pattern.release());
                        }
                    } else {
                        auto pattern = parse_pattern();
                        if (pattern) {
                            sub_pats.push_back(pattern.release());
                        }
                    }
                } while (match(TokenType::YCOMMA));
            }

            expect(TokenType::YRPAREN, "Expected ')' after constructor pattern arguments");

            return make_unique<ConstructorPattern>(loc, name, sub_pats);
        }

        // Check for @ pattern
        if (match(TokenType::YAT)) {
            auto pattern = parse_pattern();
            auto id_expr = new IdentifierExpr(loc, new NameExpr(loc, name));
            return make_unique<AsDataStructurePattern>(loc, id_expr, pattern.release());
        }

        // Check for cons pattern
        if (match(TokenType::YCONS)) {
            auto tail = parse_pattern();
            auto id_expr = new IdentifierExpr(loc, new NameExpr(loc, name));
            auto head_pattern = new PatternValue(loc, id_expr);
            vector<PatternWithoutSequence*> heads = {head_pattern};
            return make_unique<HeadTailsPattern>(loc,
                heads,
                tail.release());
        }

        auto id_expr = new IdentifierExpr(loc, new NameExpr(loc, name));
        return make_unique<PatternValue>(loc, id_expr);
    }

    error(ParseError::Type::INVALID_PATTERN, "Invalid pattern");
    return nullptr;
}

} // namespace yona::parser
