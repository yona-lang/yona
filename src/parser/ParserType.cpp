//
// ParserType.cpp — Type annotation and type definition parsing.
//

#include "ParserImpl.h"

namespace yona::parser {

unique_ptr<compiler::types::Type> ParserImpl::parse_type() {
    return parse_function_type();
}

unique_ptr<compiler::types::Type> ParserImpl::parse_function_type() {
    auto arg_type = parse_sum_type();
    if (!arg_type) return nullptr;

    if (match(TokenType::YARROW)) {
        auto return_type = parse_function_type();
        if (!return_type) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected return type after '->'");
            return nullptr;
        }

        auto func_type = make_shared<compiler::types::FunctionType>();
        func_type->argumentType = *arg_type;
        func_type->returnType = *return_type;
        return make_unique<compiler::types::Type>(func_type);
    }

    return arg_type;
}

unique_ptr<compiler::types::Type> ParserImpl::parse_sum_type() {
    auto first_type = parse_product_type();
    if (!first_type) return nullptr;

    unordered_set<compiler::types::Type> types;
    types.insert(*first_type);

    while (match(TokenType::YPIPE)) {
        auto next_type = parse_product_type();
        if (!next_type) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected type after '|'");
            return nullptr;
        }
        types.insert(*next_type);
    }

    if (types.size() == 1) {
        return first_type;
    }

    auto sum_type = make_shared<compiler::types::SumType>();
    sum_type->types = types;
    return make_unique<compiler::types::Type>(sum_type);
}

unique_ptr<compiler::types::Type> ParserImpl::parse_product_type() {
    if (check(TokenType::YLPAREN)) {
        advance();

        vector<compiler::types::Type> types;

        if (check(TokenType::YRPAREN)) {
            advance();
            return make_unique<compiler::types::Type>(compiler::types::Unit);
        }

        auto first_type = parse_type();
        if (!first_type) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected type in tuple");
            return nullptr;
        }
        types.push_back(*first_type);

        while (match(TokenType::YCOMMA)) {
            auto next_type = parse_type();
            if (!next_type) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected type after ','");
                return nullptr;
            }
            types.push_back(*next_type);
        }

        if (!expect(TokenType::YRPAREN, "Expected ')' after tuple types")) {
            return nullptr;
        }

        if (types.size() == 1) {
            return make_unique<compiler::types::Type>(types[0]);
        }

        auto product_type = make_shared<compiler::types::ProductType>();
        product_type->types = types;
        return make_unique<compiler::types::Type>(product_type);
    }

    return parse_collection_type();
}

unique_ptr<compiler::types::Type> ParserImpl::parse_collection_type() {
    if (check(TokenType::YLBRACKET)) {
        advance();

        auto element_type = parse_type();
        if (!element_type) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected element type in sequence");
            return nullptr;
        }

        if (!expect(TokenType::YRBRACKET, "Expected ']' after sequence type")) {
            return nullptr;
        }

        auto seq_type = make_shared<compiler::types::SingleItemCollectionType>();
        seq_type->kind = compiler::types::SingleItemCollectionType::Seq;
        seq_type->valueType = *element_type;
        return make_unique<compiler::types::Type>(seq_type);
    }

    if (check(TokenType::YLBRACE)) {
        advance();

        // Try refinement type: { var : Type | predicate }
        // Lookahead: IDENT COLON ... PIPE ... RBRACE
        if (check(TokenType::YIDENTIFIER)) {
            size_t saved = current_;
            string var_name(current().lexeme);
            advance();
            if (check(TokenType::YCOLON)) {
                advance(); // consume ':'
                auto base_type = parse_type();
                if (base_type && check(TokenType::YPIPE)) {
                    advance(); // consume '|'
                    auto predicate = parse_refinement_predicate(var_name);
                    if (predicate) {
                        if (!expect(TokenType::YRBRACE, "Expected '}' after refinement type")) return nullptr;
                        auto rt = make_shared<compiler::types::RefinedType>();
                        rt->var_name = var_name;
                        rt->base_type = *base_type;
                        rt->predicate = predicate;
                        return make_unique<compiler::types::Type>(rt);
                    }
                }
            }
            // Not a refinement — restore and fall through
            current_ = saved;
        }

        auto first_type = parse_type();
        if (!first_type) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected type in collection");
            return nullptr;
        }

        if (match(TokenType::YCOLON)) {
            auto value_type = parse_type();
            if (!value_type) {
                error(ParseError::Type::INVALID_SYNTAX, "Expected value type in dict");
                return nullptr;
            }

            if (!expect(TokenType::YRBRACE, "Expected '}' after dict type")) {
                return nullptr;
            }

            auto dict_type = make_shared<compiler::types::DictCollectionType>();
            dict_type->keyType = *first_type;
            dict_type->valueType = *value_type;
            return make_unique<compiler::types::Type>(dict_type);
        } else {
            if (!expect(TokenType::YRBRACE, "Expected '}' after set type")) {
                return nullptr;
            }

            auto set_type = make_shared<compiler::types::SingleItemCollectionType>();
            set_type->kind = compiler::types::SingleItemCollectionType::Set;
            set_type->valueType = *first_type;
            return make_unique<compiler::types::Type>(set_type);
        }
    }

    return parse_primary_type();
}

unique_ptr<compiler::types::Type> ParserImpl::parse_primary_type() {
    if (check(TokenType::YIDENTIFIER)) {
        string type_name(peek().lexeme);
        advance();

        static const unordered_map<string, compiler::types::BuiltinType> builtin_types = {
            {"Bool", compiler::types::Bool},
            {"Byte", compiler::types::Byte},
            {"Int", compiler::types::SignedInt64},
            {"Int16", compiler::types::SignedInt16},
            {"Int32", compiler::types::SignedInt32},
            {"Int64", compiler::types::SignedInt64},
            {"Int128", compiler::types::SignedInt128},
            {"UInt16", compiler::types::UnsignedInt16},
            {"UInt32", compiler::types::UnsignedInt32},
            {"UInt64", compiler::types::UnsignedInt64},
            {"UInt128", compiler::types::UnsignedInt128},
            {"Float", compiler::types::Float64},
            {"Float32", compiler::types::Float32},
            {"Float64", compiler::types::Float64},
            {"Float128", compiler::types::Float128},
            {"Char", compiler::types::Char},
            {"String", compiler::types::String},
            {"Symbol", compiler::types::Symbol},
            {"Unit", compiler::types::Unit}
        };

        // Type application is left-associative: `Dict key value` applies two
        // independent arguments to Dict. Nested applications must be grouped,
        // as in `Seq (Option value)`. Parsing the old form recursively made the
        // first argument consume every following identifier (`Dict (key value)`)
        // and silently erased all but `key` from generated interfaces.
        auto parse_argument_atom = [&]() -> unique_ptr<compiler::types::Type> {
            if (check(TokenType::YLPAREN))
                return parse_product_type();
            if (!check(TokenType::YIDENTIFIER))
                return nullptr;

            string argument_name(current().lexeme);
            advance();
            if (const auto builtin = builtin_types.find(argument_name);
                builtin != builtin_types.end())
                return make_unique<compiler::types::Type>(builtin->second);

            if (argument_name == "Seq" || argument_name == "Set" ||
                argument_name == "Dict") {
                const auto builtin = argument_name == "Seq" ? compiler::types::Seq
                    : argument_name == "Set" ? compiler::types::Set
                    : compiler::types::Dict;
                return make_unique<compiler::types::Type>(builtin);
            }

            auto named = make_shared<compiler::types::NamedType>();
            named->name = argument_name;
            named->type = compiler::types::Type(nullptr);
            return make_unique<compiler::types::Type>(named);
        };

        vector<compiler::types::Type> type_arguments;
        while (check(TokenType::YIDENTIFIER) || check(TokenType::YLPAREN)) {
            auto argument = parse_argument_atom();
            if (!argument) {
                error(ParseError::Type::INVALID_SYNTAX,
                      "Expected type argument after '" + type_name + "'");
                return nullptr;
            }
            type_arguments.push_back(*argument);
        }

        auto it = builtin_types.find(type_name);
        if (it != builtin_types.end()) {
            if (!type_arguments.empty()) {
                error(ParseError::Type::INVALID_SYNTAX,
                      "Built-in type '" + type_name + "' does not accept a type argument");
                return nullptr;
            }
            return make_unique<compiler::types::Type>(it->second);
        }

        if (type_name == "Seq" || type_name == "Set") {
            if (type_arguments.empty()) {
                return make_unique<compiler::types::Type>(
                    type_name == "Seq" ? compiler::types::Seq : compiler::types::Set);
            }
            if (type_arguments.size() != 1) {
                error(ParseError::Type::INVALID_SYNTAX,
                      "Type '" + type_name + "' expects exactly one type argument");
                return nullptr;
            }
            auto collection = make_shared<compiler::types::SingleItemCollectionType>();
            collection->kind = type_name == "Seq"
                ? compiler::types::SingleItemCollectionType::Seq
                : compiler::types::SingleItemCollectionType::Set;
            collection->valueType = type_arguments.front();
            return make_unique<compiler::types::Type>(collection);
        }

        if (type_name == "Dict") {
            if (type_arguments.empty())
                return make_unique<compiler::types::Type>(compiler::types::Dict);
            if (type_arguments.size() == 1) {
                if (const auto* product =
                        std::get_if<shared_ptr<compiler::types::ProductType>>(
                            &type_arguments.front());
                    product && (*product)->types.size() == 2)
                    type_arguments = (*product)->types;
            }
            if (type_arguments.size() != 2) {
                error(ParseError::Type::INVALID_SYNTAX,
                      "Type 'Dict' expects exactly two type arguments");
                return nullptr;
            }
            auto dictionary = make_shared<compiler::types::DictCollectionType>();
            dictionary->keyType = type_arguments[0];
            dictionary->valueType = type_arguments[1];
            return make_unique<compiler::types::Type>(dictionary);
        }

        auto named_type = make_shared<compiler::types::NamedType>();
        named_type->name = type_name;
        if (type_arguments.empty()) {
            named_type->type = compiler::types::Type(nullptr);
        } else if (type_arguments.size() == 1) {
            named_type->type = type_arguments.front();
        } else {
            auto product = make_shared<compiler::types::ProductType>();
            product->types = std::move(type_arguments);
            named_type->type = product;
        }
        return make_unique<compiler::types::Type>(named_type);
    }

    error(ParseError::Type::INVALID_SYNTAX, "Expected type");
    return nullptr;
}

unique_ptr<TypeNameNode> ParserImpl::parse_type_name_node() {
    SourceLocation loc = current_location();

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected type name");
        return nullptr;
    }

    string type_name(peek().lexeme);
    advance();

    static const unordered_map<string, compiler::types::BuiltinType> builtin_types = {
        {"Bool", compiler::types::Bool},
        {"Byte", compiler::types::Byte},
        {"Int", compiler::types::SignedInt64},
        {"Int16", compiler::types::SignedInt16},
        {"Int32", compiler::types::SignedInt32},
        {"Int64", compiler::types::SignedInt64},
        {"Int128", compiler::types::SignedInt128},
        {"UInt16", compiler::types::UnsignedInt16},
        {"UInt32", compiler::types::UnsignedInt32},
        {"UInt64", compiler::types::UnsignedInt64},
        {"UInt128", compiler::types::UnsignedInt128},
        {"Float", compiler::types::Float64},
        {"Float32", compiler::types::Float32},
        {"Float64", compiler::types::Float64},
        {"Float128", compiler::types::Float128},
        {"Char", compiler::types::Char},
        {"String", compiler::types::String},
        {"Symbol", compiler::types::Symbol},
        {"Unit", compiler::types::Unit}
    };

    auto it = builtin_types.find(type_name);
    if (it != builtin_types.end()) {
        return make_unique<BuiltinTypeNode>(loc, it->second);
    }

    auto name_expr = new NameExpr(loc, type_name);
    return make_unique<UserDefinedTypeNode>(loc, name_expr);
}

unique_ptr<TypeDefinition> ParserImpl::parse_type_definition() {
    if (!match(TokenType::YTYPE)) {
        return nullptr;
    }

    SourceLocation loc = previous_location();

    if (!check(TokenType::YIDENTIFIER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected type name after 'type'");
        return nullptr;
    }

    string type_name(current().lexeme);
    advance();

    vector<string> type_params;
    while (check(TokenType::YIDENTIFIER) && !check_ahead(TokenType::YASSIGN)) {
        type_params.push_back(string(current().lexeme));
        advance();
    }

    if (!expect(TokenType::YASSIGN, "Expected '=' in type definition")) {
        return nullptr;
    }

    auto name_node = new UserDefinedTypeNode(loc, new NameExpr(loc, type_name));

    vector<TypeNameNode*> type_names;

    do {
        auto type_expr = parse_type_name();
        if (type_expr) {
            type_names.push_back(type_expr.release());
        }

        if (check(TokenType::YPIPE)) {
            advance();
        } else {
            break;
        }
    } while (true);

    auto type_def = make_unique<TypeDefinition>(loc, name_node, type_names);
    return type_def;
}

unique_ptr<TypeNameNode> ParserImpl::parse_type_name() {
    SourceLocation loc = current_location();

    if (check(TokenType::YIDENTIFIER)) {
        string type_name(advance().lexeme);

        static const unordered_map<string, compiler::types::BuiltinType> builtin_types = {
            {"Bool", compiler::types::Bool},
            {"Byte", compiler::types::Byte},
            {"Int", compiler::types::SignedInt64},
            {"Int16", compiler::types::SignedInt16},
            {"Int32", compiler::types::SignedInt32},
            {"Int64", compiler::types::SignedInt64},
            {"Int128", compiler::types::SignedInt128},
            {"UInt16", compiler::types::UnsignedInt16},
            {"UInt32", compiler::types::UnsignedInt32},
            {"UInt64", compiler::types::UnsignedInt64},
            {"UInt128", compiler::types::UnsignedInt128},
            {"Float", compiler::types::Float64},
            {"Float32", compiler::types::Float32},
            {"Float64", compiler::types::Float64},
            {"Float128", compiler::types::Float128},
            {"Char", compiler::types::Char},
            {"String", compiler::types::String},
            {"Symbol", compiler::types::Symbol},
            {"Unit", compiler::types::Unit}
        };

        auto it = builtin_types.find(type_name);
        if (it != builtin_types.end()) {
            return make_unique<BuiltinTypeNode>(loc, it->second);
        } else {
            return make_unique<UserDefinedTypeNode>(loc, new NameExpr(loc, type_name));
        }
    }

    error(ParseError::Type::INVALID_SYNTAX, "Expected type name");
    return nullptr;
}

// ===== Refinement Predicate Parser =====
// Parses: var > 0, var < N, var != 0, length var > 0, p && q, p || q
// Grammar:
//   predicate   = atom (("&&" | "||") atom)*
//   atom        = "length" IDENT ">" INTEGER
//               | IDENT (">" | "<" | ">=" | "<=" | "==" | "!=") INTEGER

shared_ptr<compiler::types::RefinePredicate> ParserImpl::parse_refinement_predicate(const string& default_var) {
    auto left = parse_refinement_atom(default_var);
    if (!left) return nullptr;

    while (true) {
        if (match(TokenType::YAND)) {
            auto right = parse_refinement_atom(default_var);
            if (!right) return nullptr;
            left = compiler::types::RefinePredicate::make_and(left, right);
        } else if (match(TokenType::YOR)) {
            auto right = parse_refinement_atom(default_var);
            if (!right) return nullptr;
            left = compiler::types::RefinePredicate::make_or(left, right);
        } else {
            break;
        }
    }
    return left;
}

shared_ptr<compiler::types::RefinePredicate> ParserImpl::parse_refinement_atom(const string& default_var) {
    // "length" IDENT ">" INTEGER
    if (check(TokenType::YIDENTIFIER) && string(current().lexeme) == "length") {
        advance(); // consume "length"
        string var = default_var;
        if (check(TokenType::YIDENTIFIER)) {
            var = string(advance().lexeme);
        }
        // Expect > or >=
        compiler::types::RefinePredicate::Op op;
        if (match(TokenType::YGT)) op = compiler::types::RefinePredicate::LengthGt;
        else {
            error(ParseError::Type::INVALID_SYNTAX, "Expected '>' after 'length' in refinement");
            return nullptr;
        }
        if (!check(TokenType::YINTEGER)) {
            error(ParseError::Type::INVALID_SYNTAX, "Expected integer literal in refinement predicate");
            return nullptr;
        }
        int64_t lit = get<int64_t>(advance().value);
        return compiler::types::RefinePredicate::make_length_gt(var, lit);
    }

    // IDENT comparison INTEGER
    string var = default_var;
    if (check(TokenType::YIDENTIFIER)) {
        var = string(advance().lexeme);
    }

    compiler::types::RefinePredicate::Op op;
    if (match(TokenType::YGT))       op = compiler::types::RefinePredicate::Gt;
    else if (match(TokenType::YLT))  op = compiler::types::RefinePredicate::Lt;
    else if (match(TokenType::YGTE)) op = compiler::types::RefinePredicate::Ge;
    else if (match(TokenType::YLTE)) op = compiler::types::RefinePredicate::Le;
    else if (match(TokenType::YEQ))  op = compiler::types::RefinePredicate::Eq;
    else if (match(TokenType::YNEQ)) op = compiler::types::RefinePredicate::Ne;
    else {
        error(ParseError::Type::INVALID_SYNTAX, "Expected comparison operator in refinement predicate");
        return nullptr;
    }

    if (!check(TokenType::YINTEGER)) {
        error(ParseError::Type::INVALID_SYNTAX, "Expected integer literal in refinement predicate");
        return nullptr;
    }
    int64_t lit = get<int64_t>(advance().value);
    return compiler::types::RefinePredicate::make_cmp(op, var, lit);
}

} // namespace yona::parser
