//
// Codegen — Module system code generation
//
// Interface file I/O, FQN resolution, imports, extern declarations.
//

#include "Codegen.h"
#include "Parser.h"
#include <algorithm>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <map>
#include <optional>
#include <unordered_set>
#include <cctype>

namespace yona::compiler::codegen {
using namespace llvm;
using LType = llvm::Type;

// Forward declarations for type annotation support
static CType yona_type_to_ctype(const types::Type& t);
static std::string yona_type_adt_name(const types::Type& t);
static std::pair<std::vector<CType>, CType> uncurry_type_signature(const types::Type& t);
static std::string ctype_to_string(CType ct);

static std::string encode_contract_atom(std::string value) {
    std::string encoded;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '_' || c == '\\') {
            encoded.push_back(static_cast<char>(c));
        } else {
            static constexpr char hex[] = "0123456789ABCDEF";
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 0x0f]);
        }
    }
    return encoded;
}

static std::string decode_contract_atom(std::string_view value) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string decoded;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex(value[i + 1]);
            const int lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(value[i]);
    }
    return decoded;
}

static std::string encode_field_contract(const ast::FieldType& field) {
    if (field.is_tuple_type) {
        std::string result = "T(";
        for (size_t i = 0; i < field.tuple_types.size(); ++i) {
            if (i) result += ",";
            result += encode_field_contract(field.tuple_types[i]);
        }
        return result + ")";
    }
    if (field.is_function_type) {
        std::string result = "F(";
        for (size_t i = 0; i < field.param_types.size(); ++i) {
            if (i) result += ",";
            result += encode_field_contract(field.param_types[i]);
        }
        result += ";";
        if (!field.return_types.empty())
            result += encode_field_contract(field.return_types.front());
        return result + ")";
    }
    std::string field_name = field.name;
    std::vector<ast::FieldType> arguments = field.type_arguments;
    if (arguments.empty()) {
        std::istringstream words(field_name);
        std::string head;
        words >> head;
        if (!head.empty() && head != field_name) {
            field_name = head;
            std::string argument;
            while (words >> argument)
                arguments.push_back(ast::FieldType::simple(argument));
        }
    }
    std::string result = "N(" + encode_contract_atom(field_name);
    for (const auto& argument : arguments)
        result += "," + encode_field_contract(argument);
    return result + ")";
}

static ast::FieldType decode_field_contract(const std::string& text) {
    std::function<ast::FieldType(std::string_view)> parse;
    auto split_top = [](std::string_view body, char separator) {
        std::vector<std::string_view> parts;
        size_t start = 0;
        int depth = 0;
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '(') ++depth;
            else if (body[i] == ')') --depth;
            else if (body[i] == separator && depth == 0) {
                parts.push_back(body.substr(start, i - start));
                start = i + 1;
            }
        }
        parts.push_back(body.substr(start));
        return parts;
    };
    parse = [&](std::string_view value) -> ast::FieldType {
        if (value.size() < 3 || value[1] != '(' || value.back() != ')')
            return ast::FieldType::simple(std::string(value));
        const char kind = value.front();
        const auto body = value.substr(2, value.size() - 3);
        ast::FieldType result;
        if (kind == 'N') {
            auto parts = split_top(body, ',');
            result.name = parts.empty() ? "" : decode_contract_atom(parts.front());
            for (size_t i = 1; i < parts.size(); ++i)
                result.type_arguments.push_back(parse(parts[i]));
        } else if (kind == 'T') {
            result.is_tuple_type = true;
            if (!body.empty())
                for (auto part : split_top(body, ','))
                    result.tuple_types.push_back(parse(part));
        } else if (kind == 'F') {
            result.is_function_type = true;
            auto halves = split_top(body, ';');
            if (!halves.empty() && !halves[0].empty())
                for (auto part : split_top(halves[0], ','))
                    result.param_types.push_back(parse(part));
            if (halves.size() > 1 && !halves[1].empty())
                result.return_types.push_back(parse(halves[1]));
        }
        return result;
    };
    return parse(text);
}
static CType string_to_ctype(const std::string& s);

/// Compact recursive type grammar used by `.yonai` function signatures.
/// Existing scalar tags stay valid; wrappers use `NAME(payload)`, e.g.
/// `LINEAR(ADT(FileHandle))`. The C ABI still uses CType separately.
static std::string interface_type(CType type, const std::string& adt_name = {}) {
    if (type == CType::ADT && !adt_name.empty()) return "ADT(" + adt_name + ")";
    return ctype_to_string(type);
}

static void parse_interface_type(const std::string& text, CType& type,
                                 std::string& adt_name, bool& linear) {
    linear = false;
    std::string inner = text;
    if (inner.starts_with("LINEAR(") && inner.ends_with(')')) {
        linear = true;
        inner = inner.substr(7, inner.size() - 8);
    } else if (inner == "LINEAR") {
        linear = true; // Legacy marker-only interface.
        type = CType::INT;
        return;
    }
    if (inner.starts_with("ADT(") && inner.ends_with(')')) {
        type = CType::ADT;
        const auto body = inner.substr(4, inner.size() - 5);
        const auto comma = body.find(',');
        adt_name = body.substr(0, comma);
    } else if (inner.starts_with("Seq(") && inner.ends_with(')')) {
        type = CType::SEQ;
    } else if (inner.starts_with("Set(") && inner.ends_with(')')) {
        type = CType::SET;
    } else if (inner.starts_with("Dict(") && inner.ends_with(')')) {
        type = CType::DICT;
    } else if (inner.starts_with("FUNCTION(") && inner.ends_with(')')) {
        type = CType::FUNCTION;
    } else if (inner.starts_with("TUPLE(") && inner.ends_with(')')) {
        type = CType::TUPLE;
    } else if (inner.starts_with("Promise(") && inner.ends_with(')')) {
        type = CType::PROMISE;
    } else {
        type = string_to_ctype(inner);
    }
}

// Field shapes preserve information that CType alone loses, notably tuple
// element types and the return type of a callable ADT field.  The compact
// spelling is deliberately token-safe for `.yonai`: `T(Int,Fn(ADT(Result)))`.
std::string Codegen::encode_field_shape(const AdtInfo::FieldShape& shape) {
    if (!shape.tuple_elements.empty()) {
        std::string encoded = "T(";
        for (size_t i = 0; i < shape.tuple_elements.size(); ++i) {
            if (i != 0) encoded += ',';
            encoded += encode_field_shape(shape.tuple_elements[i]);
        }
        return encoded + ')';
    }
    if (shape.type == CType::FUNCTION)
        return "Fn(" + interface_type(shape.function_return_type,
                                      shape.function_return_adt_name) + ')';
    return interface_type(shape.type);
}

static std::vector<std::string> split_shape_arguments(const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '(') ++depth;
        else if (text[i] == ')') --depth;
        else if (text[i] == ',' && depth == 0) {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(text.substr(start));
    return parts;
}

Codegen::AdtInfo::FieldShape Codegen::decode_field_shape(const std::string& text) {
    AdtInfo::FieldShape shape;
    if (text.starts_with("T(") && text.ends_with(')')) {
        shape.type = CType::TUPLE;
        for (const auto& part : split_shape_arguments(text.substr(2, text.size() - 3)))
            shape.tuple_elements.push_back(decode_field_shape(part));
        return shape;
    }
    if (text.starts_with("Fn(") && text.ends_with(')')) {
        shape.type = CType::FUNCTION;
        bool linear = false;
        parse_interface_type(text.substr(3, text.size() - 4), shape.function_return_type,
                             shape.function_return_adt_name, linear);
        return shape;
    }
    bool linear = false;
    std::string ignored_adt_name;
    parse_interface_type(text, shape.type, ignored_adt_name, linear);
    return shape;
}

static std::string trim_trailing_doc_comments(std::string source) {
    while (!source.empty() && (source.back() == '\n' || source.back() == '\r' ||
                              source.back() == ' ' || source.back() == '\t'))
        source.pop_back();

    while (!source.empty()) {
        size_t line_start = source.find_last_of("\r\n");
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
        size_t first = source.find_first_not_of(" \t", line_start);
        bool is_doc_line = first != std::string::npos &&
                           first + 1 < source.size() &&
                           source[first] == '#' && source[first + 1] == '#';
        if (!is_doc_line) break;

        source.erase(line_start);
        while (!source.empty() && (source.back() == '\n' || source.back() == '\r' ||
                                  source.back() == ' ' || source.back() == '\t'))
            source.pop_back();
    }

    return source;
}

static bool contains_identifier(const std::string& source, const std::string& identifier) {
    size_t pos = source.find(identifier);
    while (pos != std::string::npos) {
        const bool left_boundary = pos == 0 ||
            (!std::isalnum(static_cast<unsigned char>(source[pos - 1])) && source[pos - 1] != '_');
        const size_t end = pos + identifier.size();
        const bool right_boundary = end == source.size() ||
            (!std::isalnum(static_cast<unsigned char>(source[end])) && source[end] != '_');
        if (left_boundary && right_boundary) return true;
        pos = source.find(identifier, pos + 1);
    }
    return false;
}

std::string Codegen::ctype_to_type_name(CType ct) {
    switch (ct) {
        case CType::INT: return "Int";
        case CType::FLOAT: return "Float";
        case CType::BOOL: return "Bool";
        case CType::STRING: return "String";
        case CType::SYMBOL: return "Symbol";
        case CType::SEQ: return "Seq";
        case CType::SET: return "Set";
        case CType::DICT: return "Dict";
        case CType::TUPLE: return "Tuple";
        case CType::UNIT: return "Unit";
        case CType::FUNCTION: return "Function";
        case CType::PROMISE: return "Promise";
        case CType::ADT: return "ADT";
        case CType::BYTE_ARRAY: return "ByteArray";
        case CType::INT_ARRAY: return "IntArray";
        case CType::FLOAT_ARRAY: return "FloatArray";
        case CType::CHANNEL: return "Channel";
        case CType::SUM: return "Sum";
        case CType::RECORD: return "Record";
    }
    return "Int";
}

std::string Codegen::resolve_trait_method(const std::string& method_name, CType arg_type,
                                           const std::string& adt_type_name,
                                           const std::string& requested_trait) {
    std::string type_name = ctype_to_type_name(arg_type);

    // Phase 2: For ADT types, use the specific ADT type name instead of generic "ADT"
    if (arg_type == CType::ADT && !adt_type_name.empty()) {
        type_name = adt_type_name;
    }

    auto lookup = [&](const std::string& trait_name) -> std::string {
        const auto instance = types_.trait_instances.find(
            trait_name + ":" + type_name);
        if (instance != types_.trait_instances.end()) {
            const auto method = instance->second.method_mangled_names.find(method_name);
            if (method != instance->second.method_mangled_names.end())
                return method->second;
        }

        // Multi-parameter and lifted heads are keyed with all declared head
        // arguments (`Foldable:Seq:element`). Receiver-only application can
        // select them when exactly one visible contract has this concrete
        // first head; ambiguous conversions remain unresolved until their
        // target witness supplies the remaining arguments.
        std::vector<std::string> candidates;
        for (const auto& [_, candidate] : types_.trait_instances) {
            if (candidate.trait_name != trait_name ||
                candidate.type_name != type_name)
                continue;
            const auto method = candidate.method_mangled_names.find(method_name);
            if (method != candidate.method_mangled_names.end())
                candidates.push_back(method->second);
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()),
                         candidates.end());
        return candidates.size() == 1 ? candidates.front() : std::string{};
    };

    if (!requested_trait.empty()) return lookup(requested_trait);

    // Unqualified method calls are accepted only when exactly one visible
    // trait both declares the method and has an exact receiver instance.
    // Sort names so selection and ambiguity diagnostics never depend on
    // unordered-map iteration order.
    std::vector<std::string> trait_names;
    for (const auto& [trait_name, trait] : types_.traits)
        if (std::find(trait.method_names.begin(), trait.method_names.end(),
                      method_name) != trait.method_names.end())
            trait_names.push_back(trait_name);
    std::sort(trait_names.begin(), trait_names.end());
    std::string selected;
    for (const auto& trait_name : trait_names) {
        auto candidate = lookup(trait_name);
        if (candidate.empty()) continue;
        if (!selected.empty()) return {}; // ambiguous; type checking diagnoses it
        selected = std::move(candidate);
    }
    return selected;
}

static std::string ctype_to_string(CType ct) {
    switch (ct) {
        case CType::INT: return "INT";
        case CType::FLOAT: return "FLOAT";
        case CType::BOOL: return "BOOL";
        case CType::STRING: return "STRING";
        case CType::SEQ: return "SEQ";
        case CType::TUPLE: return "TUPLE";
        case CType::UNIT: return "UNIT";
        case CType::FUNCTION: return "FUNCTION";
        case CType::SYMBOL: return "SYMBOL";
        case CType::PROMISE: return "PROMISE";
        case CType::SET: return "SET";
        case CType::DICT: return "DICT";
        case CType::ADT: return "ADT";
        case CType::BYTE_ARRAY: return "BYTE_ARRAY";
        case CType::INT_ARRAY: return "INT_ARRAY";
        case CType::FLOAT_ARRAY: return "FLOAT_ARRAY";
        case CType::CHANNEL: return "CHANNEL";
        case CType::SUM: return "SUM";
        case CType::RECORD: return "RECORD";
    }
    return "INT";
}

static CType string_to_ctype(const std::string& s) {
    if (s == "INT") return CType::INT;
    if (s == "FLOAT") return CType::FLOAT;
    if (s == "BOOL") return CType::BOOL;
    if (s == "STRING") return CType::STRING;
    if (s == "SEQ") return CType::SEQ;
    if (s == "TUPLE") return CType::TUPLE;
    if (s == "UNIT") return CType::UNIT;
    if (s == "FUNCTION") return CType::FUNCTION;
    if (s == "SYMBOL") return CType::SYMBOL;
    if (s == "PROMISE") return CType::PROMISE;
    if (s == "SET") return CType::SET;
    if (s == "DICT") return CType::DICT;
    if (s == "ADT") return CType::ADT;
    if (s == "BYTE_ARRAY") return CType::BYTE_ARRAY;
    if (s == "INT_ARRAY") return CType::INT_ARRAY;
    if (s == "FLOAT_ARRAY") return CType::FLOAT_ARRAY;
    if (s == "CHANNEL") return CType::CHANNEL;
    if (s == "SUM") return CType::SUM;
    if (s == "RECORD") return CType::RECORD;
    return CType::INT;
}

Codegen::AdtInfo::FieldShape
Codegen::field_shape_from_field_type(const ast::FieldType& field_type) {
    auto ctype_for_name = [](std::string name) {
        auto space = name.find(' ');
        if (space != std::string::npos) name.resize(space);
        if (name == "Int" || name == "a" || name == "b" || name == "e" || name == "s") return CType::INT;
        if (name == "Float") return CType::FLOAT;
        if (name == "String") return CType::STRING;
        if (name == "Bool") return CType::BOOL;
        if (name == "Symbol") return CType::SYMBOL;
        if (name == "Seq") return CType::SEQ;
        if (name == "Set") return CType::SET;
        if (name == "Dict") return CType::DICT;
        if (name == "Channel") return CType::CHANNEL;
        if (name == "()" || name == "Unit") return CType::UNIT;
        return CType::ADT;
    };

    AdtInfo::FieldShape shape;
    if (field_type.is_tuple_type) {
        shape.type = CType::TUPLE;
        for (const auto& element : field_type.tuple_types)
            shape.tuple_elements.push_back(field_shape_from_field_type(element));
        return shape;
    }
    if (field_type.is_function_type || field_type.name == "Fn" || field_type.name == "fn" ||
        field_type.name == "Function") {
        shape.type = CType::FUNCTION;
        if (!field_type.return_types.empty()) {
            auto result = field_shape_from_field_type(field_type.return_types.front());
            shape.function_return_type = result.type;
            if (result.type == CType::ADT)
                shape.function_return_adt_name = field_type.return_types.front().name;
        }
        return shape;
    }
    shape.type = ctype_for_name(field_type.name);
    return shape;
}

static std::string borrowed_params_to_mask(const std::vector<bool>& borrowed, size_t param_count) {
    std::string mask;
    mask.reserve(param_count);
    for (size_t i = 0; i < param_count; i++)
        mask.push_back((i < borrowed.size() && borrowed[i]) ? '1' : '0');
    return mask;
}

static std::vector<bool> borrowed_mask_to_params(const std::string& mask, size_t param_count) {
    std::vector<bool> borrowed(param_count, false);
    for (size_t i = 0; i < param_count && i < mask.size(); i++)
        borrowed[i] = (mask[i] == '1');
    return borrowed;
}

Codegen::ModuleFunctionMeta Codegen::module_meta_from_compiled(const CompiledFunction& cf) const {
    ModuleFunctionMeta meta;
    meta.param_types = cf.param_types;
    for (size_t i = 0; i < cf.param_types.size(); ++i) {
        const std::string adt_name = i < cf.param_adt_names.size()
            ? cf.param_adt_names[i] : "";
        if (i < cf.param_type_descriptors.size() &&
            !cf.param_type_descriptors[i].empty()) {
            meta.param_type_descriptors.push_back(cf.param_type_descriptors[i]);
        } else {
            meta.param_type_descriptors.push_back(
                interface_type(cf.param_types[i], adt_name));
        }
    }
    meta.return_type = cf.return_type;
    meta.return_type_descriptor = !cf.return_type_descriptor.empty()
        ? cf.return_type_descriptor
        : interface_type(cf.return_type, cf.return_adt_name);
    meta.extern_promise = cf.extern_promise;
    meta.promise_inner_type = cf.promise_inner_type;
    meta.return_adt_name = cf.return_adt_name;
    meta.borrowed_params = cf.borrowed_params;
    meta.return_linear = cf.return_linear || cf.return_adt_name == "Linear";
    meta.tuple_elem_linear = cf.tuple_elem_linear;
    meta.param_linear = cf.param_linear;
    meta.effect_ops = cf.effect_ops;
    meta.effect_row_known = cf.effect_row_known;
    meta.effect_open_rest = cf.effect_open_rest;
    meta.effect_hof = cf.effect_hof;
    meta.effect_scheme = cf.effect_scheme;
    return meta;
}

Codegen::CompiledFunction Codegen::compiled_function_from_meta(llvm::Function* fn,
                                                               const ModuleFunctionMeta& meta,
                                                               CType return_type) const {
    CompiledFunction cf;
    cf.fn = fn;
    cf.return_type = return_type;
    cf.param_types = meta.param_types;
    cf.param_type_descriptors = meta.param_type_descriptors;
    cf.return_type_descriptor = meta.return_type_descriptor;
    cf.borrowed_params = meta.borrowed_params;
    cf.extern_promise = meta.extern_promise;
    cf.promise_inner_type = meta.promise_inner_type;
    cf.return_adt_name = meta.return_adt_name;
    cf.return_linear = meta.return_linear || meta.return_adt_name == "Linear";
    cf.tuple_elem_linear = meta.tuple_elem_linear;
    cf.param_linear = meta.param_linear;
    cf.effect_ops = meta.effect_ops;
    cf.effect_row_known = meta.effect_row_known;
    cf.effect_open_rest = meta.effect_open_rest;
    cf.effect_hof = meta.effect_hof;
    cf.effect_scheme = meta.effect_scheme;
    return cf;
}

bool Codegen::emit_interface_file(const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return false;

    // Write ADT definitions
    // Group constructors by type name
    std::map<std::string, std::vector<std::pair<std::string, const AdtInfo*>>> adts;
    for (auto& [name, info] : types_.adt_constructors)
        adts[info.type_name].push_back({name, &info});

    for (auto& [type_name, ctors] : adts) {
        if (interface_export_filter_active_ &&
            interface_exported_types_.count(type_name) == 0)
            continue;
        const bool opaque = interface_opaque_types_.count(type_name) != 0;
        int max_arity = 0;
        bool is_recursive = false;
        std::sort(ctors.begin(), ctors.end(), [](const auto& left, const auto& right) {
            if (left.second->tag != right.second->tag)
                return left.second->tag < right.second->tag;
            return left.first < right.first;
        });
        for (const auto& [_, c] : ctors) {
            if (c->arity > max_arity) max_arity = c->arity;
            if (c->is_recursive) is_recursive = true;
        }
        out << "ADT " << type_name << " " << ctors.size() << " " << max_arity
            << (is_recursive ? " recursive" : "")
            << (opaque ? " opaque" : "");
        if (const auto params = types_.adt_type_params.find(type_name);
            params != types_.adt_type_params.end() && !params->second.empty()) {
            out << " params";
            for (const auto& param : params->second) out << " " << param;
        }
        out << "\n";
        if (opaque) continue;
        for (const auto& [cname, ctor] : ctors) {
                    out << "CTOR " << cname << " " << ctor->tag << " " << ctor->arity;
                    size_t field_count = std::max(ctor->field_names.size(), ctor->field_types.size());
                    if (field_count > 0) {
                        out << " fields";
                        for (size_t fi = 0; fi < field_count; fi++) {
                            std::string field_name = fi < ctor->field_names.size()
                                ? ctor->field_names[fi]
                                : "_" + std::to_string(fi);
                            CType field_type = fi < ctor->field_types.size()
                                ? ctor->field_types[fi]
                                : CType::INT;
                            out << " " << field_name << ":" << ctype_to_string(field_type);
                            if (fi < ctor->declared_field_types.size())
                                out << "@" << encode_field_contract(
                                    ctor->declared_field_types[fi]);
                            if (field_type == CType::FUNCTION &&
                                fi < ctor->field_fn_return_types.size()) {
                                out << ":" << ctype_to_string(ctor->field_fn_return_types[fi]);
                                if (fi < ctor->field_fn_return_adt_names.size() &&
                                    !ctor->field_fn_return_adt_names[fi].empty())
                                    out << ":" << ctor->field_fn_return_adt_names[fi];
                            }
                        }
                    }
                    out << "\n";
        }
    }

    // Write trait definitions
    std::vector<std::string> trait_keys;
    for (const auto& [name, _] : types_.traits) trait_keys.push_back(name);
    std::sort(trait_keys.begin(), trait_keys.end());
    for (const auto& name : trait_keys) {
        if (interface_export_filter_active_ && !interface_trait_names_.count(name))
            continue;
        const auto& trait = types_.traits.at(name);
        out << "TRAIT " << name;
        for (auto& tp : trait.type_params) out << " " << tp;
        if (trait.type_params.empty()) out << " " << trait.type_param;
        out << " " << trait.method_names.size() << "\n";
        for (const auto& [superclass, parameter] : trait.superclasses)
            out << "  SUPER " << superclass << " " << parameter << "\n";
        for (auto& method : trait.method_names) {
            out << "  METHOD " << method;
            if (const auto descriptor = trait.method_type_descriptors.find(method);
                descriptor != trait.method_type_descriptors.end())
                out << " " << descriptor->second;
            out << "\n";
        }
    }

    // Write trait instances
    std::vector<std::string> instance_keys;
    for (const auto& [key, _] : types_.trait_instances) instance_keys.push_back(key);
    std::sort(instance_keys.begin(), instance_keys.end());
    for (const auto& key : instance_keys) {
        if (interface_export_filter_active_ && !interface_instance_keys_.count(key))
            continue;
        const auto& inst = types_.trait_instances.at(key);
        out << "INSTANCE " << inst.trait_name;
        for (auto& tn : inst.type_names) out << " " << tn;
        if (inst.type_names.empty()) out << " " << inst.type_name;
        out << "\n";
        for (const auto& parameter : inst.type_params)
            out << "  PARAM " << parameter << "\n";
        for (const auto& [trait_name, parameter] : inst.constraints)
            out << "  CONSTRAINT " << trait_name << " " << parameter << "\n";
        std::vector<std::string> methods;
        for (const auto& [method, _] : inst.method_mangled_names)
            methods.push_back(method);
        std::sort(methods.begin(), methods.end());
        for (const auto& method : methods) {
            const auto& mangled_name = inst.method_mangled_names.at(method);
            out << "  IMPL " << method << " " << mangled_name << "\n";
        }
    }

    auto write_function_signature = [&](const std::string& prefix,
                                        const std::string& mangled,
                                        const ModuleFunctionMeta& meta) {
        out << prefix;
        const bool is_promise_row = meta.extern_promise != ast::ExternPromiseKind::Sync;
        switch (meta.extern_promise) {
        case ast::ExternPromiseKind::IoUring: out << "IO "; break;
        case ast::ExternPromiseKind::NativePtr: out << "NAT "; break;
        case ast::ExternPromiseKind::ThreadPool: out << "AFN "; break;
        default: out << "FN "; break;
        }
        out << mangled << " " << meta.param_types.size();
        for (size_t i = 0; i < meta.param_types.size(); i++) {
            if (i < meta.param_linear.size() && meta.param_linear[i])
                out << " LINEAR(" << interface_type(meta.param_types[i]) << ")";
            else
                out << " " << (i < meta.param_type_descriptors.size() &&
                                  !meta.param_type_descriptors[i].empty()
                    ? meta.param_type_descriptors[i] : interface_type(meta.param_types[i]));
        }
        CType printed_ret = is_promise_row ? meta.promise_inner_type : meta.return_type;
        out << " -> ";
        if (!meta.tuple_elem_linear.empty() && printed_ret == CType::TUPLE) {
            out << "TUPLE";
            for (char lin : meta.tuple_elem_linear)
                out << (lin ? " LINEAR" : " INT");
        } else if (meta.return_linear) {
            out << "LINEAR(" << (meta.return_type_descriptor.empty()
                ? interface_type(printed_ret, meta.return_adt_name)
                : meta.return_type_descriptor) << ")";
        } else {
            out << ((is_promise_row || meta.return_type_descriptor.empty())
                ? interface_type(printed_ret, meta.return_adt_name)
                : meta.return_type_descriptor);
            if (!is_promise_row && meta.return_type == CType::ADT &&
                !meta.return_adt_name.empty())
                out << " retadt " << meta.return_adt_name;
        }
        auto borrow_mask = borrowed_params_to_mask(
            meta.borrowed_params, meta.param_types.size());
        if (borrow_mask.find('1') != std::string::npos)
            out << " borrow " << borrow_mask;
        if (meta.effect_row_known) {
            out << " effects ";
            if (meta.effect_ops.empty() && !meta.effect_open_rest) {
                out << "-";
            } else {
                for (size_t i = 0; i < meta.effect_ops.size(); i++) {
                    if (i) out << ",";
                    out << meta.effect_ops[i];
                }
                if (meta.effect_open_rest) out << "|";
            }
            if (meta.effect_hof) out << " hof";
        }
        if (!meta.effect_scheme.empty())
            out << " effectscheme " << meta.effect_scheme;
        out << "\n";
    };

    // Write public function signatures (FN / AFN / IO / NAT).
    std::vector<std::string> meta_keys;
    for (const auto& [mangled, _] : imports_.meta) meta_keys.push_back(mangled);
    std::sort(meta_keys.begin(), meta_keys.end());
    for (const auto& mangled : meta_keys) {
        const auto& meta = imports_.meta.at(mangled);
        if (!imports_.interface_symbols.empty() &&
            imports_.interface_symbols.find(mangled) == imports_.interface_symbols.end())
            continue;
        write_function_signature("", mangled, meta);
    }

    // Write generic function source for cross-module monomorphization
    std::vector<std::string> source_keys;
    for (const auto& [mangled, _] : imports_.function_source)
        source_keys.push_back(mangled);
    std::sort(source_keys.begin(), source_keys.end());
    for (const auto& mangled : source_keys) {
        const auto& source = imports_.function_source.at(mangled);
        bool is_export = imports_.interface_symbols.empty() ||
                         imports_.interface_symbols.count(mangled);
        bool is_private_helper = imports_.private_genfn_symbols.count(mangled);
        if (!is_export && !is_private_helper)
            continue;
        // Extract local name from mangled: yona_Pkg_Mod__funcname -> funcname
        auto pos = mangled.rfind("__");
        std::string local_name = (pos != std::string::npos) ? mangled.substr(pos + 2) : mangled;
        for (const auto& [dependency_name, dependency] : imports_.native_dependencies) {
            if (!contains_identifier(source, dependency_name)) continue;
            write_function_signature(
                "GENFN_DEP " + mangled + " " + dependency_name + " ",
                dependency.c_symbol, dependency.meta);
        }
        // Wrappers around private externs or sibling exports are already
        // represented by their exported FN ABI. Re-emitting their source
        // without those private/local dependencies in scope makes importers
        // fail while reparsing GENFN bodies.
        if (source.find("raw_") != std::string::npos)
            continue;
        // Reparsed generic bodies need every constructor they reference,
        // including private ADTs. Keep this metadata scoped to the GENFN so
        // it enables monomorphization without exporting the ADT itself.
        std::vector<std::string> constructor_keys;
        for (const auto& [ctor_name, _] : types_.adt_constructors)
            constructor_keys.push_back(ctor_name);
        std::sort(constructor_keys.begin(), constructor_keys.end());
        for (const auto& ctor_name : constructor_keys) {
            const auto& ctor = types_.adt_constructors.at(ctor_name);
            if (!contains_identifier(source, ctor_name)) continue;
            out << "GENFN_CTOR " << mangled << " " << ctor_name << " "
                << ctor.type_name << " " << ctor.tag << " " << ctor.arity << " "
                << ctor.total_variants << " " << ctor.max_arity
                << (ctor.is_recursive ? " recursive" : "");
            for (size_t i = 0; i < ctor.field_types.size(); ++i) {
                // Record syntax must survive a GENFN reparse too. Field names
                // are intentionally scoped to this source dependency rather
                // than promoted to a public CTOR interface entry.
                if (i < ctor.field_names.size())
                    out << " fieldname:" << ctor.field_names[i];
                out << " field:" << ctype_to_string(ctor.field_types[i]);
                if (ctor.field_types[i] == CType::FUNCTION &&
                    i < ctor.field_fn_return_types.size()) {
                    out << ":" << ctype_to_string(ctor.field_fn_return_types[i]);
                    if (i < ctor.field_fn_return_adt_names.size() &&
                        !ctor.field_fn_return_adt_names[i].empty())
                        out << ":" << ctor.field_fn_return_adt_names[i];
                }
                if (i < ctor.field_shapes.size())
                    out << " shape:" << encode_field_shape(ctor.field_shapes[i]);
                if (i < ctor.declared_field_types.size())
                    out << " contract:"
                        << encode_field_contract(ctor.declared_field_types[i]);
            }
            out << "\n";
        }
        out << "GENFN_BEGIN " << mangled << " " << local_name << "\n";
        out << trim_trailing_doc_comments(source) << "\n";
        out << "GENFN_END\n";
    }

    return true;
}

bool Codegen::load_interface_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    std::string current_adt;
    int current_total_variants = 0;
    int current_max_arity = 0;
    bool current_is_recursive = false;
    bool current_is_opaque = false;
    std::vector<std::string> current_ctor_names;
    std::string last_instance_key;  // tracks most recently registered INSTANCE
    std::string last_trait_name;    // tracks most recently registered TRAIT

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "ADT") {
            // Finalize previous ADT's total_variants/max_arity
            for (auto& cn : current_ctor_names) {
                types_.adt_constructors[cn].total_variants = current_total_variants;
                types_.adt_constructors[cn].max_arity = current_max_arity;
            }
            current_ctor_names.clear();

            iss >> current_adt >> current_total_variants >> current_max_arity;
            std::string recursive_flag;
            current_is_recursive = false;
            current_is_opaque = false;
            std::vector<std::string> type_params;
            bool reading_params = false;
            while (iss >> recursive_flag) {
                if (recursive_flag == "recursive") current_is_recursive = true;
                if (recursive_flag == "opaque") current_is_opaque = true;
                if (recursive_flag == "params") {
                    reading_params = true;
                    continue;
                }
                if (reading_params) type_params.push_back(recursive_flag);
            }
            auto& registered_params = types_.adt_type_params[current_adt];
            // A module may mention a Prelude ADT in legacy leaked metadata.
            // Never let an incomplete re-export erase the canonical declared
            // arity already loaded from Prelude itself.
            if (!type_params.empty() || registered_params.empty())
                registered_params = std::move(type_params);
        } else if (keyword == "CTOR") {
            if (current_is_opaque) continue;
            std::string name;
            int tag, arity;
            iss >> name >> tag >> arity;
            std::vector<std::string> fnames;
            std::vector<CType> ftypes;
            std::vector<CType> fn_rets;
            std::vector<std::string> fn_ret_adt_names;
            std::vector<ast::FieldType> declared_field_types;
            bool has_field_contracts = false;
            std::string token;
            if (iss >> token && token == "fields") {
                while (iss >> token) {
                    std::vector<std::string> parts;
                    std::stringstream ss(token);
                    std::string part;
                    while (std::getline(ss, part, ':')) parts.push_back(part);
                    if (parts.size() >= 2) {
                        fnames.push_back(parts[0]);
                        std::string abi_type = parts[1];
                        std::string contract;
                        if (const auto at = abi_type.find('@'); at != std::string::npos) {
                            contract = abi_type.substr(at + 1);
                            abi_type.resize(at);
                            has_field_contracts = true;
                        }
                        CType field_type = string_to_ctype(abi_type);
                        ftypes.push_back(field_type);
                        declared_field_types.push_back(contract.empty()
                            ? ast::FieldType::simple(ctype_to_type_name(field_type))
                            : decode_field_contract(contract));
                        if (field_type == CType::FUNCTION && parts.size() >= 3) {
                            fn_rets.push_back(string_to_ctype(parts[2]));
                            fn_ret_adt_names.push_back(parts.size() >= 4 ? parts[3] : "");
                        } else {
                            fn_rets.push_back(CType::INT);
                            fn_ret_adt_names.push_back("");
                        }
                    }
                }
            }
            AdtInfo candidate{current_adt, tag, arity, current_total_variants,
                              current_max_arity, current_is_recursive, fnames, ftypes,
                              fn_rets, fn_ret_adt_names};
            candidate.declared_field_types = std::move(declared_field_types);
            if (const auto existing = types_.adt_constructors.find(name);
                existing != types_.adt_constructors.end() && !has_field_contracts &&
                !existing->second.declared_field_types.empty()) {
                candidate.declared_field_types = existing->second.declared_field_types;
                if (!existing->second.field_shapes.empty())
                    candidate.field_shapes = existing->second.field_shapes;
            }
            types_.adt_constructors[name] = std::move(candidate);
            current_ctor_names.push_back(name);
        } else if (keyword == "TRAIT") {
            // Format: TRAIT name param1 [param2 ...] method_count
            std::string name;
            iss >> name;
            std::vector<std::string> tokens;
            std::string tok;
            while (iss >> tok) tokens.push_back(tok);
            // Last token is method_count (integer), rest are type params
            TraitInfo ti;
            ti.name = name;
            if (!tokens.empty()) {
                for (size_t i = 0; i + 1 < tokens.size(); i++)
                    ti.type_params.push_back(tokens[i]);
                if (!ti.type_params.empty())
                    ti.type_param = ti.type_params[0];
            }
            auto [trait_it, inserted] = types_.traits.try_emplace(name, ti);
            if (!inserted && trait_it->second.type_params.empty() &&
                !ti.type_params.empty()) {
                // Old interfaces leaked every visible Prelude trait.  Such a
                // dependency copy must not erase the canonical contract that
                // was loaded from the owning interface first.
                trait_it->second.type_params = ti.type_params;
                trait_it->second.type_param = ti.type_param;
            }
            last_trait_name = name;
        } else if (keyword == "SUPER") {
            std::string superclass, parameter;
            iss >> superclass >> parameter;
            if (auto it = types_.traits.find(last_trait_name);
                it != types_.traits.end() && !superclass.empty())
                if (std::find(it->second.superclasses.begin(),
                              it->second.superclasses.end(),
                              std::pair{superclass, parameter}) ==
                    it->second.superclasses.end())
                    it->second.superclasses.emplace_back(
                        std::move(superclass), std::move(parameter));
        } else if (keyword == "METHOD") {
            std::string method_name;
            iss >> method_name;
            // Add to the most recently registered TRAIT
            if (!last_trait_name.empty()) {
                auto it = types_.traits.find(last_trait_name);
                if (it != types_.traits.end()) {
                    if (std::find(it->second.method_names.begin(),
                                  it->second.method_names.end(), method_name) ==
                        it->second.method_names.end())
                        it->second.method_names.push_back(method_name);
                    std::string descriptor;
                    iss >> descriptor;
                    if (!descriptor.empty() &&
                        !it->second.method_type_descriptors.count(method_name))
                        it->second.method_type_descriptors.emplace(
                            method_name, std::move(descriptor));
                }
            }
        } else if (keyword == "INSTANCE") {
            // Format: INSTANCE trait_name type1 [type2 ...]
            std::string trait_name;
            iss >> trait_name;
            std::vector<std::string> type_names;
            std::string tn;
            while (iss >> tn) type_names.push_back(tn);
            std::string type_name = type_names.empty() ? "" : type_names[0];
            std::string key = trait_name;
            for (auto& t : type_names) key += ":" + t;
            const size_t declared_head_arity = [&] {
                auto trait = types_.traits.find(trait_name);
                return trait == types_.traits.end() ? size_t{0}
                                                    : trait->second.type_params.size();
            }();
            if (declared_head_arity > 0 && type_names.size() < declared_head_arity) {
                bool complete_instance_exists = false;
                for (const auto& [_, existing] : types_.trait_instances) {
                    const auto& heads = existing.type_names;
                    if (existing.trait_name == trait_name && !heads.empty() &&
                        !type_names.empty() && heads.front() == type_names.front() &&
                        heads.size() >= declared_head_arity) {
                        complete_instance_exists = true;
                        break;
                    }
                }
                if (complete_instance_exists) {
                    last_instance_key.clear();
                    continue;
                }
            }
            if (declared_head_arity > 0 && type_names.size() >= declared_head_arity &&
                !type_names.empty()) {
                for (auto existing = types_.trait_instances.begin();
                     existing != types_.trait_instances.end();) {
                    const auto& heads = existing->second.type_names;
                    if (existing->second.trait_name == trait_name && !heads.empty() &&
                        heads.front() == type_names.front() &&
                        heads.size() < declared_head_arity)
                        existing = types_.trait_instances.erase(existing);
                    else
                        ++existing;
                }
            }
            TraitInstanceInfo tii;
            tii.trait_name = trait_name;
            tii.type_name = type_name;
            tii.type_names = type_names;
            auto [instance_it, inserted] =
                types_.trait_instances.try_emplace(key, tii);
            if (!inserted) {
                if (instance_it->second.type_names.empty())
                    instance_it->second.type_names = type_names;
                if (instance_it->second.type_name.empty())
                    instance_it->second.type_name = type_name;
                if (instance_it->second.trait_name.empty())
                    instance_it->second.trait_name = trait_name;
            }
            last_instance_key = key;
        } else if (keyword == "PARAM") {
            std::string parameter;
            iss >> parameter;
            if (auto it = types_.trait_instances.find(last_instance_key);
                it != types_.trait_instances.end() && !parameter.empty())
                if (std::find(it->second.type_params.begin(),
                              it->second.type_params.end(), parameter) ==
                    it->second.type_params.end())
                    it->second.type_params.push_back(std::move(parameter));
        } else if (keyword == "CONSTRAINT") {
            std::string trait_name, parameter;
            iss >> trait_name >> parameter;
            if (auto it = types_.trait_instances.find(last_instance_key);
                it != types_.trait_instances.end() && !trait_name.empty())
                if (std::find(it->second.constraints.begin(),
                              it->second.constraints.end(),
                              std::pair{trait_name, parameter}) ==
                    it->second.constraints.end())
                    it->second.constraints.emplace_back(
                        std::move(trait_name), std::move(parameter));
        } else if (keyword == "IMPL") {
            std::string method_name, mangled_name;
            iss >> method_name >> mangled_name;
            // Add to the most recently registered INSTANCE
            if (!last_instance_key.empty()) {
                auto it = types_.trait_instances.find(last_instance_key);
                if (it != types_.trait_instances.end() &&
                    !it->second.method_mangled_names.count(method_name))
                    it->second.method_mangled_names.emplace(
                        std::move(method_name), std::move(mangled_name));
            }
        } else if (keyword == "FN" || keyword == "AFN" || keyword == "IO" ||
                   keyword == "NAT" || keyword == "GENFN_DEP") {
            const bool is_genfn_dependency = keyword == "GENFN_DEP";
            std::string dependency_owner, dependency_local;
            if (is_genfn_dependency) {
                iss >> dependency_owner >> dependency_local >> keyword;
            }
            ast::ExternPromiseKind ext_kind = ast::ExternPromiseKind::Sync;
            if (keyword == "AFN") ext_kind = ast::ExternPromiseKind::ThreadPool;
            else if (keyword == "IO") ext_kind = ast::ExternPromiseKind::IoUring;
            else if (keyword == "NAT") ext_kind = ast::ExternPromiseKind::NativePtr;
            std::string mangled;
            int param_count;
            iss >> mangled >> param_count;

            ModuleFunctionMeta meta;
            meta.param_linear.assign((size_t)param_count, 0);
            for (int i = 0; i < param_count; i++) {
                std::string type_str;
                iss >> type_str;
                CType parsed_type = CType::INT;
                std::string parsed_adt;
                bool parsed_linear = false;
                parse_interface_type(type_str, parsed_type, parsed_adt, parsed_linear);
                meta.param_types.push_back(parsed_type);
                meta.param_type_descriptors.push_back(type_str);
                meta.param_linear[(size_t)i] = parsed_linear;
            }
            std::string arrow;
            iss >> arrow; // "->"
            std::string ret_str;
            iss >> ret_str;
            CType parsed_ret = CType::INT;
            std::string parsed_ret_adt;
            bool parsed_return_linear = false;
            parse_interface_type(ret_str, parsed_ret, parsed_ret_adt, parsed_return_linear);
            meta.return_linear = parsed_return_linear;
            if (!parsed_ret_adt.empty()) meta.return_adt_name = parsed_ret_adt;
            const bool is_promise_row = ext_kind != ast::ExternPromiseKind::Sync;
            meta.return_type = is_promise_row ? CType::PROMISE : parsed_ret;
            meta.return_type_descriptor = ret_str;
            meta.extern_promise = ext_kind;
            if (is_promise_row) meta.promise_inner_type = parsed_ret;
            meta.borrowed_params.assign((size_t)param_count, false);
            std::string trailing;
            while (iss >> trailing) {
                if (trailing == "borrow") {
                    std::string mask;
                    if (iss >> mask)
                        meta.borrowed_params = borrowed_mask_to_params(mask, (size_t)param_count);
                } else if (trailing == "retadt") {
                    iss >> meta.return_adt_name;
                    if (meta.return_adt_name == "Linear")
                        meta.return_linear = true;
                } else if (trailing == "LINEAR") {
                    meta.tuple_elem_linear.push_back(1);
                } else if (trailing == "effects") {
                    meta.effect_row_known = true;
                    std::string ops;
                    if (iss >> ops) {
                        if (ops == "|") {
                            meta.effect_open_rest = true;
                        } else {
                            if (!ops.empty() && ops.back() == '|') {
                                meta.effect_open_rest = true;
                                ops.pop_back();
                            }
                            std::string cur;
                            for (char c : ops) {
                                if (c == ',') {
                                    if (!cur.empty()) meta.effect_ops.push_back(cur);
                                    cur.clear();
                                } else {
                                    cur += c;
                                }
                            }
                            if (!cur.empty()) meta.effect_ops.push_back(cur);
                        }
                    }
                } else if (trailing == "hof") {
                    meta.effect_hof = true;
                } else if (trailing == "effectscheme") {
                    iss >> meta.effect_scheme;
                }
            }

            // Function metadata is immutable once loaded.  Legacy interfaces
            // sometimes repeated dependency rows with an older, less precise
            // ABI (for example `Ordering` as `INT`).  Preserve the owning
            // interface's earlier canonical contract.
            if (is_genfn_dependency) {
                imports_.private_genfn_dependencies[dependency_owner].push_back(
                    {dependency_local, NativeDependency{mangled, meta}});
            }
            imports_.meta.try_emplace(mangled, std::move(meta));
        } else if (keyword == "GENFN_BEGIN") {
            // Parse: GENFN_BEGIN mangled_name local_name
            std::string mangled, local_name;
            iss >> mangled >> local_name;
            std::string source;
            while (std::getline(in, line)) {
                if (line == "GENFN_END") break;
                if (!source.empty()) source += "\n";
                source += line;
            }
            // GENFN is authoritative source regardless of whether its body
            // calls a mangled extern. Pure Yona wrappers such as IO.isatty
            // intentionally delegate to one; dropping them here turns the
            // wrapper itself into a nonexistent external symbol.
            imports_.imported_sources[mangled] = {source, local_name};
        } else if (keyword == "GENFN_CTOR") {
            std::string mangled, ctor_name, type_name;
            int tag = 0, arity = 0, total_variants = 0, max_arity = 0;
            iss >> mangled >> ctor_name >> type_name >> tag >> arity >> total_variants >> max_arity;
            std::string flag;
            bool is_recursive = false;
            std::vector<CType> field_types, fn_return_types;
            std::vector<std::string> field_names;
            std::vector<std::string> fn_return_adt_names;
            std::vector<AdtInfo::FieldShape> field_shapes;
            std::vector<ast::FieldType> declared_field_types;
            while (iss >> flag) {
                if (flag == "recursive") is_recursive = true;
                if (flag.rfind("fieldname:", 0) == 0) {
                    field_names.push_back(flag.substr(10));
                } else if (flag.rfind("field:", 0) == 0) {
                    std::vector<std::string> parts;
                    std::stringstream fields(flag.substr(6));
                    std::string part;
                    while (std::getline(fields, part, ':')) parts.push_back(part);
                    field_types.push_back(parts.empty() ? CType::INT : string_to_ctype(parts[0]));
                    fn_return_types.push_back(parts.size() > 1 ? string_to_ctype(parts[1]) : CType::INT);
                    fn_return_adt_names.push_back(parts.size() > 2 ? parts[2] : "");
                } else if (flag.rfind("shape:", 0) == 0) {
                    field_shapes.push_back(decode_field_shape(flag.substr(6)));
                } else if (flag.rfind("contract:", 0) == 0) {
                    declared_field_types.push_back(
                        decode_field_contract(flag.substr(9)));
                }
            }
            while (field_shapes.size() < field_types.size()) {
                size_t i = field_shapes.size();
                AdtInfo::FieldShape fallback;
                fallback.type = field_types[i];
                if (fallback.type == CType::FUNCTION && i < fn_return_types.size()) {
                    fallback.function_return_type = fn_return_types[i];
                    fallback.function_return_adt_name = i < fn_return_adt_names.size()
                        ? fn_return_adt_names[i] : "";
                }
                field_shapes.push_back(std::move(fallback));
            }
            if (!mangled.empty() && !ctor_name.empty()) {
                AdtInfo info{type_name, tag, arity, total_variants, max_arity,
                             is_recursive, field_names, field_types, fn_return_types,
                             fn_return_adt_names, field_shapes};
                info.declared_field_types = std::move(declared_field_types);
                imports_.private_genfn_ctors[mangled].push_back(
                    {ctor_name, std::move(info)});
            }
        }
    }
    return true;
}

// Build FQN string and filesystem path from an FqnExpr
std::pair<std::string, std::filesystem::path> Codegen::build_fqn_path(FqnExpr* fqn) {
    std::string mod_fqn;
    std::filesystem::path mod_path;
    if (fqn->packageName.has_value()) {
        auto* pkg = fqn->packageName.value();
        for (size_t i = 0; i < pkg->parts.size(); i++) {
            if (i > 0) mod_fqn += "\\";
            mod_fqn += pkg->parts[i]->value;
            mod_path /= pkg->parts[i]->value;
        }
        mod_fqn += "\\";
    }
    mod_fqn += fqn->moduleName->value;
    mod_path /= fqn->moduleName->value;
    return {mod_fqn, mod_path};
}

// Load .yonai interface file for a module, falling back to .yona source.
void Codegen::load_module_interface(const std::filesystem::path& mod_path) {
    auto yonai_name = mod_path;
    yonai_name.replace_extension(".yonai");
    for (auto& search_path : module_paths_) {
        auto candidate = std::filesystem::path(search_path) / yonai_name;
        if (std::filesystem::exists(candidate)) {
            load_interface_file(candidate.string());
            return;
        }
    }
    // Fallback: pure-Yona stdlib source. Parse and register declarations.
    auto yona_name = mod_path;
    yona_name.replace_extension(".yona");
    for (auto& search_path : module_paths_) {
        auto candidate = std::filesystem::path(search_path) / yona_name;
        if (std::filesystem::exists(candidate)) {
            load_yona_module(candidate);
            return;
        }
    }
}

void Codegen::load_module_by_fqn(const std::string& mod_fqn) {
    std::filesystem::path p;
    std::string rest = mod_fqn;
    while (!rest.empty()) {
        auto pos = rest.find('\\');
        if (pos == std::string::npos) {
            p /= rest;
            break;
        }
        p /= rest.substr(0, pos);
        rest = rest.substr(pos + 1);
    }
    load_module_interface(p);
}

typechecker::ImportedFnSig Codegen::sig_from_meta(const ModuleFunctionMeta& meta) {
    typechecker::ImportedFnSig sig;
    sig.arity = (int)meta.param_types.size();
    sig.return_linear = meta.return_linear || meta.return_adt_name == "Linear";
    sig.tuple_elem_linear = meta.tuple_elem_linear;
    sig.param_linear = meta.param_linear;
    sig.param_tags.reserve(meta.param_types.size());
    for (auto ct : meta.param_types)
        sig.param_tags.push_back(ctype_to_string(ct));
    sig.return_tag = ctype_to_string(meta.return_type);
    sig.param_descriptors = meta.param_type_descriptors;
    sig.return_descriptor = meta.return_type_descriptor;
    if (meta.return_linear && !sig.return_descriptor.empty() &&
        !sig.return_descriptor.starts_with("LINEAR("))
        sig.return_descriptor = "LINEAR(" + sig.return_descriptor + ")";
    sig.return_linear_adt_name = meta.return_linear ? meta.return_adt_name : "";
    sig.effect_scheme = meta.effect_scheme;
    if (!sig.tuple_elem_linear.empty())
        sig.return_linear = false;
    return sig;
}

static std::vector<std::string> interface_constructor_names(
        const Codegen& codegen, const std::string& module_fqn) {
    std::filesystem::path relative;
    std::string remaining = module_fqn;
    while (!remaining.empty()) {
        const auto separator = remaining.find('\\');
        relative /= separator == std::string::npos
            ? remaining : remaining.substr(0, separator);
        if (separator == std::string::npos) break;
        remaining.erase(0, separator + 1);
    }
    relative.replace_extension(".yonai");
    for (const auto& root : codegen.module_paths_) {
        std::ifstream input(std::filesystem::path(root) / relative);
        if (!input) continue;
        std::vector<std::string> names;
        std::string line;
        bool opaque = false;
        while (std::getline(input, line)) {
            std::istringstream tokens(line);
            std::string keyword;
            tokens >> keyword;
            if (keyword == "ADT") {
                opaque = line.find(" opaque") != std::string::npos;
            } else if (keyword == "CTOR" && !opaque) {
                std::string name;
                tokens >> name;
                if (!name.empty()) names.push_back(std::move(name));
            }
        }
        return names;
    }
    return {};
}

static std::vector<typechecker::ImportedInstanceSig> interface_instance_heads(
        const Codegen& codegen, const std::string& module_fqn) {
    std::filesystem::path relative;
    std::string remaining = module_fqn;
    while (!remaining.empty()) {
        const auto separator = remaining.find('\\');
        relative /= separator == std::string::npos
            ? remaining : remaining.substr(0, separator);
        if (separator == std::string::npos) break;
        remaining.erase(0, separator + 1);
    }
    relative.replace_extension(".yonai");
    for (const auto& root : codegen.module_paths_) {
        std::ifstream input(std::filesystem::path(root) / relative);
        if (!input) continue;
        std::vector<typechecker::ImportedInstanceSig> instances;
        typechecker::ImportedInstanceSig* current = nullptr;
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream tokens(line);
            std::string keyword;
            if (!(tokens >> keyword)) continue;
            if (keyword == "INSTANCE") {
                typechecker::ImportedInstanceSig instance;
                tokens >> instance.trait_name;
                std::string type_name;
                while (tokens >> type_name) instance.type_names.push_back(type_name);
                if (!instance.trait_name.empty() && !instance.type_names.empty()) {
                    instances.push_back(std::move(instance));
                    current = &instances.back();
                }
            } else if (keyword == "PARAM" && current) {
                std::string parameter;
                if (tokens >> parameter) current->type_params.push_back(std::move(parameter));
            } else if (keyword == "CONSTRAINT" && current) {
                std::string trait_name, parameter;
                if (tokens >> trait_name >> parameter)
                    current->constraints.emplace_back(
                        std::move(trait_name), std::move(parameter));
            }
        }
        return instances;
    }
    return {};
}

static std::string constructor_field_descriptor(const ast::FieldType& field) {
    if (field.is_tuple_type) {
        std::string result = "TUPLE(";
        for (size_t i = 0; i < field.tuple_types.size(); ++i) {
            if (i) result += ",";
            result += constructor_field_descriptor(field.tuple_types[i]);
        }
        return result + ")";
    }
    if (field.is_function_type) {
        std::string result = field.return_types.empty()
            ? "UNIT" : constructor_field_descriptor(field.return_types.front());
        for (auto it = field.param_types.rbegin(); it != field.param_types.rend(); ++it)
            result = "FUNCTION(" + constructor_field_descriptor(*it) + "," + result + ")";
        return result;
    }
    if (!field.name.empty() &&
        std::islower(static_cast<unsigned char>(field.name.front())))
        return "VAR(" + field.name + ")";
    auto builtin = [](const std::string& name) -> std::string {
        if (name == "Int" || name == "Byte" || name == "Char") return "INT";
        if (name == "Float") return "FLOAT";
        if (name == "Bool") return "BOOL";
        if (name == "String") return "STRING";
        if (name == "Symbol") return "SYMBOL";
        if (name == "Unit" || name == "()") return "UNIT";
        return {};
    };
    if (const auto scalar = builtin(field.name); !scalar.empty()) return scalar;
    if (field.name == "Seq" || field.name == "Set") {
        if (field.type_arguments.empty())
            return field.name == "Seq" ? "SEQ" : "SET";
        return field.name + "(" +
            constructor_field_descriptor(field.type_arguments.front()) + ")";
    }
    if (field.name == "Dict") {
        if (field.type_arguments.size() < 2) return "DICT";
        return "Dict(" + constructor_field_descriptor(field.type_arguments[0]) +
            "," + constructor_field_descriptor(field.type_arguments[1]) + ")";
    }
    std::string result = "ADT(" + field.name;
    for (const auto& argument : field.type_arguments)
        result += "," + constructor_field_descriptor(argument);
    return result + ")";
}

std::optional<typechecker::ImportedFnSig>
Codegen::ImportTypes::imported_function_sig(const std::string& module_fqn,
                                            const std::string& name) {
    cg_->load_module_by_fqn(module_fqn);
    auto it = cg_->imports_.meta.find(mangle_name(module_fqn, name));
    if (it != cg_->imports_.meta.end()) {
        auto signature = sig_from_meta(it->second);
        // Legacy C-backed interfaces carried the nominal return separately as
        // `-> ADT retadt Iterator`. Reconstruct the structural descriptor so
        // inference does not erase Iterator to anonymous `ADT a`.
        if (signature.return_descriptor == "ADT" &&
            !it->second.return_adt_name.empty()) {
            signature.return_descriptor =
                "ADT(" + it->second.return_adt_name;
            if (const auto params =
                    cg_->types_.adt_type_params.find(it->second.return_adt_name);
                params != cg_->types_.adt_type_params.end()) {
                for (const auto& parameter : params->second)
                    signature.return_descriptor += ",VAR(" + parameter + ")";
            }
            signature.return_descriptor += ")";
        }
        return signature;
    }

    const auto constructors = interface_constructor_names(*cg_, module_fqn);
    if (std::find(constructors.begin(), constructors.end(), name) == constructors.end())
        return std::nullopt;
    const auto constructor = cg_->types_.adt_constructors.find(name);
    if (constructor == cg_->types_.adt_constructors.end()) return std::nullopt;

    typechecker::ImportedFnSig signature;
    signature.arity = constructor->second.arity;
    for (size_t i = 0; i < static_cast<size_t>(constructor->second.arity); ++i) {
        const CType type = i < constructor->second.field_types.size()
            ? constructor->second.field_types[i] : CType::INT;
        signature.param_tags.push_back(ctype_to_string(type));
        signature.param_descriptors.push_back(
            i < constructor->second.declared_field_types.size()
                ? constructor_field_descriptor(constructor->second.declared_field_types[i])
                : interface_type(type));
    }
    signature.return_tag = "ADT";
    signature.return_descriptor = "ADT(" + constructor->second.type_name;
    if (const auto params = cg_->types_.adt_type_params.find(constructor->second.type_name);
        params != cg_->types_.adt_type_params.end()) {
        for (const auto& parameter : params->second)
            signature.return_descriptor += ",VAR(" + parameter + ")";
    }
    signature.return_descriptor += ")";
    return signature;
}

std::vector<std::string>
Codegen::ImportTypes::imported_module_exports(const std::string& module_fqn) {
    cg_->load_module_by_fqn(module_fqn);
    std::string expected_prefix = "yona_";
    for (char c : module_fqn)
        expected_prefix += (c == '\\') ? '_' : c;
    expected_prefix += "__";
    std::vector<std::string> names;
    for (auto& [mangled, _] : cg_->imports_.meta) {
        if (mangled.find(expected_prefix) != 0)
            continue;
        std::string tail = mangled.substr(expected_prefix.size());
        if (tail.find("__") != std::string::npos)
            continue;
        names.push_back(std::move(tail));
    }
    for (auto& constructor : interface_constructor_names(*cg_, module_fqn))
        if (std::find(names.begin(), names.end(), constructor) == names.end())
            names.push_back(std::move(constructor));
    return names;
}

std::vector<typechecker::ImportedInstanceSig>
Codegen::ImportTypes::imported_instances(const std::string& module_fqn) {
    cg_->load_module_by_fqn(module_fqn);
    return interface_instance_heads(*cg_, module_fqn);
}

bool Codegen::load_yona_module(const std::filesystem::path& yona_path) {
    auto canonical = std::filesystem::canonical(yona_path).string();
    if (loaded_yona_paths_.count(canonical)) return true; // already loaded
    loaded_yona_paths_.insert(canonical);

    std::ifstream in(yona_path);
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    parser::Parser parser;
    // Pre-register known constructors so pattern matching parses correctly.
    for (auto& [name, info] : types_.adt_constructors)
        parser.register_constructor(name, info.type_name, info.tag, info.arity, info.field_names);

    auto result = parser.parse_module(source, yona_path.string());
    if (!result.has_value()) {
        if (diag_) {
            for (auto& e : result.error())
                diag_->error(e.location, compiler::ErrorCode::E0301,
                             "in stdlib module " + yona_path.filename().string() + ": " + e.message);
        }
        return false;
    }

    auto* mod_ptr = result.value().get();
    // Keep AST alive — deferred functions hold pointers into it.
    loaded_yona_modules_.push_back(std::move(result.value()));
    register_yona_module_decls(mod_ptr);
    return true;
}

void Codegen::register_yona_module_decls(ast::ModuleDecl* mod) {
    // First pass: collect names of ADTs in this module that contain a
    // function-typed field. Any ADT that references one of these names in
    // a field must also be heap-allocated, because the closure ABI for
    // function fields returns a pointer (i64), and a non-recursive flat
    // struct ADT containing such a function field would not survive a
    // closure-returning-it round trip.
    std::unordered_set<std::string> heap_adts;
    for (auto* adt : mod->adt_declarations) {
        types_.adt_type_params[adt->name] = adt->type_params;
        for (auto* ctor : adt->variants) {
            for (auto& ft : ctor->field_type_names) {
                if (ft.is_function_type) { heap_adts.insert(adt->name); break; }
            }
            if (heap_adts.count(adt->name)) break;
        }
    }
    // Fixpoint: any ADT that references an already-heap ADT in a field is
    // also heap. Two passes are enough for the typical
    // `Stream a = Stream (() -> Step a)` / `Step a = Yield a (Stream a) | Done`
    // mutual reference; for deeper chains we just iterate.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* adt : mod->adt_declarations) {
            if (heap_adts.count(adt->name)) continue;
            for (auto* ctor : adt->variants) {
                for (auto& ft : ctor->field_type_names) {
                    // The parser stores `Stream a` as a single string with
                    // the head followed by space-separated type variables.
                    std::string head = ft.name;
                    auto sp = head.find(' ');
                    if (sp != std::string::npos) head = head.substr(0, sp);
                    if (heap_adts.count(head)) {
                        heap_adts.insert(adt->name);
                        changed = true;
                        break;
                    }
                }
                if (heap_adts.count(adt->name)) break;
            }
        }
    }

    // ===== ADTs =====
    for (auto* adt : mod->adt_declarations) {
        int max_arity = 0;
        bool is_recursive = heap_adts.count(adt->name) > 0;
        for (auto* ctor : adt->variants) {
            int a = static_cast<int>(ctor->field_type_names.size());
            if (a > max_arity) max_arity = a;
            for (auto& ft : ctor->field_type_names) {
                if (ft.is_function_type || ft.name == adt->name ||
                    ft.name == "Fn" || ft.name == "fn" || ft.name == "Function") {
                    is_recursive = true; break;
                }
            }
        }
        // Helper: name a head identifier → CType (with the same rules as
        // the field-type loop below). Used for the return type of a
        // function-typed field, e.g. `(() -> Stream a)` returns Stream.
        auto name_to_ctype = [&](const std::string& name) -> CType {
            if (name == "Int") return CType::INT;
            if (name == "Float") return CType::FLOAT;
            if (name == "String") return CType::STRING;
            if (name == "Bool") return CType::BOOL;
            if (name == "Symbol") return CType::SYMBOL;
            if (name == "Channel") return CType::CHANNEL;
            if (name == "()" || name == "Unit") return CType::UNIT;
            if (name == "Seq") return CType::SEQ;
            if (name == "Set") return CType::SET;
            if (name == "Dict") return CType::DICT;
            // Anything else (Stream, Option, Result, ...) is treated as ADT.
            return CType::ADT;
        };

        for (size_t ci = 0; ci < adt->variants.size(); ci++) {
            auto* ctor = adt->variants[ci];
            int arity = static_cast<int>(ctor->field_type_names.size());
            std::vector<CType> ftypes;
            std::vector<CType> fn_rets;
            std::vector<std::string> fn_ret_adt_names;
            std::vector<AdtInfo::FieldShape> field_shapes;
            for (auto& ft : ctor->field_type_names) {
                auto shape = field_shape_from_field_type(ft);
                ftypes.push_back(shape.type);
                fn_rets.push_back(shape.function_return_type);
                fn_ret_adt_names.push_back(shape.function_return_adt_name);
                field_shapes.push_back(std::move(shape));
            }
            AdtInfo info;
            info.type_name = adt->name;
            info.tag = static_cast<int>(ci);
            info.arity = arity;
            info.total_variants = static_cast<int>(adt->variants.size());
            info.max_arity = max_arity;
            info.is_recursive = is_recursive;
            info.field_names = ctor->field_names;
            info.field_types = ftypes;
            info.field_fn_return_types = fn_rets;
            info.field_fn_return_adt_names = fn_ret_adt_names;
            info.field_shapes = std::move(field_shapes);
            info.declared_field_types = ctor->field_type_names;
            types_.adt_constructors[ctor->name] = info;
        }
    }

    // ===== Trait declarations =====
    for (auto* trait : mod->trait_declarations) {
        if (types_.traits.count(trait->name)) continue;
        TraitInfo ti;
        ti.name = trait->name;
        ti.type_param = trait->type_param;
        ti.type_params = trait->type_params;
        ti.superclasses.assign(trait->superclasses.begin(), trait->superclasses.end());
        for (auto& m : trait->methods) {
            ti.method_names.push_back(m.name);
            ti.method_type_descriptors[m.name] = source_type_descriptor(m.type_signature);
            if (m.default_impl) ti.default_impls[m.name] = m.default_impl;
        }
        types_.traits[trait->name] = ti;
    }

    // ===== Trait instances =====
    for (auto* inst : mod->instance_declarations) {
        std::string key = inst->trait_name;
        for (auto& tn : inst->type_names) key += ":" + tn;
        if (inst->type_names.empty()) key += ":" + inst->type_name;
        if (types_.trait_instances.count(key)) continue;
        TraitInstanceInfo tii;
        tii.trait_name = inst->trait_name;
        tii.type_name = inst->type_name;
        tii.type_names = inst->type_names;
        tii.type_params = inst->type_params;
        tii.constraints.assign(inst->constraints.begin(), inst->constraints.end());
        for (auto* method : inst->methods) {
            std::string mangled = mangle_trait_instance_method(inst, method->name);
            tii.method_mangled_names[method->name] = mangled;
            codegen_function_def(method, mangled);
            // An instance implementation is part of the public trait
            // dictionary even when its method name is not a normal module
            // export. Preserve its Yona source so importers can compile the
            // selected concrete method without requiring an adjacent .o file.
            if (!method->source_text.empty())
                imports_.private_genfn_symbols.insert(mangled);
        }
        types_.trait_instances[key] = tii;
    }

    // ===== Extern declarations =====
    for (auto* ext : mod->extern_declarations) {
        codegen_extern_decl(ext);
    }

    // ===== Functions (all deferred — compile on demand at call sites) =====
    for (auto* func : mod->functions) {
        codegen_function_def(func, func->name);
    }
}

Codegen::GenfnNameIsolation::GenfnNameIsolation(Codegen& cg, std::string mangled)
    : cg(cg) {
    ++cg.genfn_isolation_depth_;
    saved_externs = cg.imports_.extern_functions;
    saved_compiled_functions = cg.compiled_functions_;
    saved_deferred_functions = cg.deferred_functions_;
    saved_named_values = cg.named_values_;
    saved_adt_constructors = cg.types_.adt_constructors;
    cg.active_genfn_isolations_.push_back(this);
    cg.imports_.extern_functions.clear();
    auto sep = mangled.rfind("__");
    std::string module_prefix = (sep == std::string::npos) ? "" : mangled.substr(0, sep + 2);
    if (!module_prefix.empty()) {
        for (const auto& [dep_mangled, dep_meta] : cg.imports_.meta) {
            if (dep_mangled.rfind(module_prefix, 0) != 0) continue;
            // The root GENFN is compiled under its local source name. Adding
            // its exported symbol as an external dependency makes an
            // unqualified trait call in the body (for example `hash head`
            // inside Hash (Seq a)) recursively call the sequence instance
            // instead of dispatching on the element type.
            if (dep_mangled == mangled && mangled.rfind("yona_", 0) != 0)
                continue;
            auto dep_sep = dep_mangled.rfind("__");
            if (dep_sep == std::string::npos) continue;
            std::string dep_name = dep_mangled.substr(dep_sep + 2);
            cg.imports_.extern_functions[dep_name] = dep_mangled;
            if (dep_meta.param_types.empty() &&
                cg.compiled_functions_.find(dep_name) == cg.compiled_functions_.end()) {
                auto* ret_ty = cg.llvm_type(dep_meta.return_type);
                auto* fn_type = llvm::FunctionType::get(ret_ty, {}, false);
                auto* fn = cg.module_->getFunction(dep_mangled);
                if (!fn) fn = Function::Create(fn_type, Function::ExternalLinkage,
                                               dep_mangled, cg.module_.get());
                cg.compiled_functions_[dep_name] =
                    cg.compiled_function_from_meta(fn, dep_meta, dep_meta.return_type);
                scoped_cafs.push_back(dep_name);
            }
        }
    }
    if (const auto dependencies = cg.imports_.private_genfn_dependencies.find(mangled);
        dependencies != cg.imports_.private_genfn_dependencies.end()) {
        for (const auto& [local_name, dependency] : dependencies->second) {
            cg.compiled_functions_.erase(local_name);
            cg.deferred_functions_.erase(local_name);
            cg.named_values_.erase(local_name);
            cg.imports_.extern_functions[local_name] = dependency.c_symbol;
            scoped_dependency_names.push_back(local_name);
        }
    }
}

void Codegen::GenfnNameIsolation::restore() {
    if (restored) return;
    restored = true;
    const auto active = std::find(cg.active_genfn_isolations_.begin(),
                                  cg.active_genfn_isolations_.end(), this);
    if (active != cg.active_genfn_isolations_.end())
        cg.active_genfn_isolations_.erase(active);
    --cg.genfn_isolation_depth_;
    cg.compiled_functions_ = std::move(saved_compiled_functions);
    for (auto it = cg.deferred_functions_.begin();
         it != cg.deferred_functions_.end();) {
        if (!saved_deferred_functions.count(it->first))
            it = cg.deferred_functions_.erase(it);
        else
            ++it;
    }
    for (const auto& [name, deferred] : saved_deferred_functions) {
        auto current = cg.deferred_functions_.find(name);
        if (current == cg.deferred_functions_.end())
            cg.deferred_functions_.emplace(name, deferred);
        else if (current->second.ast != deferred.ast)
            current->second = deferred;
    }
    cg.named_values_ = std::move(saved_named_values);
    cg.imports_.extern_functions = std::move(saved_externs);
    cg.types_.adt_constructors = std::move(saved_adt_constructors);
}

Codegen::ActiveNamedValueSnapshot::ActiveNamedValueSnapshot(
    Codegen& cg, NamedValueBindings& bindings)
    : cg_(cg), bindings_(bindings) {
    cg_.active_named_value_snapshots_.push_back(&bindings_);
}

Codegen::ActiveNamedValueSnapshot::~ActiveNamedValueSnapshot() {
    const auto active = std::find(cg_.active_named_value_snapshots_.begin(),
                                  cg_.active_named_value_snapshots_.end(),
                                  &bindings_);
    if (active != cg_.active_named_value_snapshots_.end())
        cg_.active_named_value_snapshots_.erase(active);
}

Codegen::ActiveTypedValueSnapshot::ActiveTypedValueSnapshot(
    Codegen& cg, TypedValue& value)
    : cg_(cg), value_(value) {
    cg_.active_typed_value_snapshots_.push_back(&value_);
}

Codegen::ActiveTypedValueSnapshot::~ActiveTypedValueSnapshot() {
    const auto active = std::find(cg_.active_typed_value_snapshots_.begin(),
                                  cg_.active_typed_value_snapshots_.end(),
                                  &value_);
    if (active != cg_.active_typed_value_snapshots_.end())
        cg_.active_typed_value_snapshots_.erase(active);
}

void Codegen::migrate_function_references(Function* obsolete,
                                          Function* replacement) {
    if (!obsolete || !replacement || obsolete == replacement) return;

    auto migrate_bindings = [&](auto& bindings) {
        for (auto& [_, value] : bindings)
            if (value.val == obsolete)
                value.val = replacement;
    };
    auto migrate_functions = [&](auto& functions) {
        for (auto& [_, compiled] : functions)
            if (compiled.fn == obsolete)
                compiled.fn = replacement;
    };

    migrate_bindings(named_values_);
    migrate_functions(compiled_functions_);
    for (auto* bindings : active_named_value_snapshots_) {
        if (bindings)
            migrate_bindings(*bindings);
    }
    for (auto* value : active_typed_value_snapshots_) {
        if (value && value->val == obsolete)
            value->val = replacement;
    }
    for (auto* isolation : active_genfn_isolations_) {
        if (!isolation) continue;
        migrate_bindings(isolation->saved_named_values);
        migrate_functions(isolation->saved_compiled_functions);
    }
}

void Codegen::install_private_genfn_ctors(const std::string& mangled) {
    auto it = imports_.private_genfn_ctors.find(mangled);
    if (it == imports_.private_genfn_ctors.end()) return;
    for (const auto& [name, source_info] : it->second) {
        auto info = source_info;
        if (const auto existing = types_.adt_constructors.find(name);
            existing != types_.adt_constructors.end()) {
            if (info.declared_field_types.empty())
                info.declared_field_types = existing->second.declared_field_types;
            if (info.field_shapes.empty())
                info.field_shapes = existing->second.field_shapes;
        }
        types_.adt_constructors[name] = std::move(info);
    }
}

void Codegen::register_sibling_genfns(const std::string& mangled) {
    auto sep = mangled.rfind("__");
    if (sep == std::string::npos) return;
    std::string module_prefix = mangled.substr(0, sep + 2);
    // Imported GENFNs are module-level. Analyze them without the caller's
    // locals — otherwise a parameter named `rest` looks like a free var of
    // `sortBy`'s `[pivot|rest]` clause and the sibling is compiled as a
    // dummy-INT closure (`undefined function 'cmp'`).
    auto saved_nv = named_values_;
    ActiveNamedValueSnapshot saved_nv_snapshot(*this, saved_nv);
    named_values_.clear();
    const auto root_source_it = imports_.imported_sources.find(mangled);
    std::vector<std::string> reachable_sources;
    if (root_source_it != imports_.imported_sources.end())
        reachable_sources.push_back(root_source_it->second.source_text);
    const std::string root_local_name = root_source_it == imports_.imported_sources.end()
        ? std::string{} : root_source_it->second.local_name;
    std::unordered_set<std::string> registered_dependencies;
    bool discovered_dependency = true;
    while (discovered_dependency) {
        discovered_dependency = false;
    for (const auto& [dep_mangled, ifs] : imports_.imported_sources) {
        if (dep_mangled == mangled) continue;
        const bool same_module_prefix = dep_mangled.rfind(module_prefix, 0) == 0;
        // Prelude trait instances retain their historical unqualified ABI
        // names (`Eq_Seq__eq`), while private Prelude helpers use the normal
        // `yona_Prelude__` module prefix. Treat both spellings as one defining
        // module when materializing an instance GENFN dependency closure.
        const bool prelude_instance_dependency =
            mangled.rfind("yona_", 0) != 0 &&
            dep_mangled.rfind("yona_Prelude__", 0) == 0;
        if (!same_module_prefix && !prelude_instance_dependency) continue;
        if (ifs.local_name == root_local_name || registered_dependencies.count(dep_mangled))
            continue;
        bool referenced = false;
        for (const auto& source : reachable_sources) {
            if (contains_identifier(source, ifs.local_name)) {
                referenced = true;
                break;
            }
        }
        if (!referenced) continue;
        install_private_genfn_ctors(dep_mangled);
        auto reparsed = reparse_genfn(ifs.local_name, ifs.source_text);
        if (!reparsed || reparsed->functions.empty()) continue;
        auto* func_ast = reparsed->functions[0];
        reparsed->functions.clear();
        imports_.imported_ast_nodes.push_back(std::unique_ptr<FunctionExpr>(func_ast));
        compiled_functions_.erase(ifs.local_name);
        deferred_functions_.erase(ifs.local_name);
        named_values_.erase(ifs.local_name);
        codegen_function_def(func_ast, ifs.local_name);
        registered_dependencies.insert(dep_mangled);
        reachable_sources.push_back(ifs.source_text);
        discovered_dependency = true;
    }
    }
    named_values_ = std::move(saved_nv);
}

TypedValue Codegen::dummy_typed_value(CType ct) {
    auto* i64_ty = LType::getInt64Ty(*context_);
    auto* ptr_ty = PointerType::get(*context_, 0);
    switch (ct) {
    case CType::FLOAT:
        return {ConstantFP::get(LType::getDoubleTy(*context_), 0.0), ct};
    case CType::BOOL:
        return {ConstantInt::get(LType::getInt1Ty(*context_), 0), ct};
    case CType::STRING:
    case CType::SEQ:
    case CType::FUNCTION:
    case CType::SET:
    case CType::DICT:
    case CType::BYTE_ARRAY:
    case CType::INT_ARRAY:
    case CType::FLOAT_ARRAY:
    case CType::PROMISE:
    case CType::CHANNEL:
        return {ConstantPointerNull::get(ptr_ty), ct};
    default:
        return {ConstantInt::get(i64_ty, 0), ct};
    }
}

TypedValue Codegen::materialize_imported_function_value(const std::string& name) {
    auto ext_it = imports_.extern_functions.find(name);
    if (ext_it == imports_.extern_functions.end())
        return {};
    const std::string mangled = ext_it->second;

    auto wrap_existing = [&](Function* fn, CType ret) -> TypedValue {
        if (!fn || !builder_ || !builder_->GetInsertBlock())
            return {};
        Value* clo = wrap_in_closure(fn, ret);
        TypedValue tv{clo, CType::FUNCTION, {ret}};
        named_values_[name] = tv;
        return tv;
    };

    auto cf_it = compiled_functions_.find(name);
    if (cf_it != compiled_functions_.end() && cf_it->second.fn) {
        size_t user_arity = cf_it->second.param_types.size() - cf_it->second.capture_names.size();
        if (user_arity > 0)
            return wrap_existing(cf_it->second.fn, cf_it->second.return_type);
    }

    auto genfn_it = imports_.imported_sources.find(mangled);
    auto meta_it = imports_.meta.find(mangled);
    if (genfn_it != imports_.imported_sources.end() && meta_it != imports_.meta.end() &&
        !meta_it->second.param_types.empty()) {
        std::vector<TypedValue> dummy_args;
        for (auto ct : meta_it->second.param_types)
            dummy_args.push_back(dummy_typed_value(ct));
        int errors_before = error_count_;
        // The parser needs private constructor metadata before it sees the
        // exported source. Keep it inside the same isolation scope as the
        // later compilation so it cannot leak into the importing module.
        GenfnNameIsolation iso(*this, mangled);
        install_private_genfn_ctors(mangled);
        auto reparsed = reparse_genfn(genfn_it->second.local_name, genfn_it->second.source_text);
        if (reparsed && !reparsed->functions.empty()) {
            auto* func_ast = reparsed->functions[0];
            reparsed->functions.clear();
            imports_.imported_ast_nodes.push_back(std::unique_ptr<FunctionExpr>(func_ast));
            register_sibling_genfns(mangled);
            codegen_function_def(func_ast, name);
            auto def_it = deferred_functions_.find(name);
            if (def_it != deferred_functions_.end()) {
                compile_function(name, def_it->second, dummy_args);
                auto cf2 = compiled_functions_.find(name);
                std::optional<CompiledFunction> materialized;
                if (cf2 != compiled_functions_.end() && cf2->second.fn &&
                    error_count_ == errors_before)
                    materialized = cf2->second;
                iso.restore();
                if (materialized) {
                    compiled_functions_[name] = std::move(*materialized);
                    imports_.extern_functions.erase(name);
                    auto& compiled = compiled_functions_.at(name);
                    return wrap_existing(compiled.fn, compiled.return_type);
                }
            } else {
                iso.restore();
            }
        }
    }

    if (meta_it == imports_.meta.end())
        return {};
    auto& meta = meta_it->second;
    std::vector<LType*> arg_types;
    for (auto ct : meta.param_types)
        arg_types.push_back(llvm_type(ct));
    auto* fn_type = llvm::FunctionType::get(llvm_type(meta.return_type), arg_types, false);
    auto* ext_fn = module_->getFunction(mangled);
    if (!ext_fn)
        ext_fn = Function::Create(fn_type, Function::ExternalLinkage, mangled, module_.get());
    compiled_functions_[name] = compiled_function_from_meta(ext_fn, meta, meta.return_type);
    return wrap_existing(ext_fn, meta.return_type);
}

std::unique_ptr<ast::ModuleDecl> Codegen::reparse_genfn(
    const std::string& local_name, const std::string& source_text) {
    parser::Parser parser;
    // Register known constructors so pattern matching parses correctly
    for (auto& [name, info] : types_.adt_constructors) {
        parser.register_constructor(name, info.type_name, info.tag, info.arity, info.field_names);
    }
    std::string mod_source = "module __Import\nexport " + local_name + "\n" + source_text + "\n";
    auto result = parser.parse_module(mod_source, "<imported>");
    if (!result.has_value())
        return nullptr;
    auto mod = std::move(result.value());
    // Callers steal FunctionExpr* and destroy this wrapper module. The
    // function's parent still pointed at ModuleDecl, so later parent
    // walks (accelerator import resolution on `Yield` / other applies)
    // followed a dangling pointer into freed memory — SIGSEGV on
    // Stream.map's lazy ADT path after HOF closure materialization.
    for (auto* fn : mod->functions) {
        if (fn)
            fn->parent = nullptr;
    }
    return mod;
}

// Every ADT has one stable opaque-pointer ABI. Constructor shape remains
// compile-time metadata used for field extraction and ownership masks.
LType* Codegen::adt_llvm_type(const std::string& type_name) {
    (void)type_name;
    return PointerType::get(*context_, 0);
}

// Register trait instance methods as extern function declarations
// so that re-parsed GENFN bodies can call them via trait dispatch.
void Codegen::register_trait_externs() {
    for (auto& [key, inst] : types_.trait_instances) {
        for (auto& [method_name, mangled] : inst.method_mangled_names) {
            if (compiled_functions_.count(mangled) > 0) continue;
            auto meta_it = imports_.meta.find(mangled);
            if (meta_it != imports_.meta.end()) {
                auto& meta = meta_it->second;
                std::vector<LType*> param_types;
                for (size_t i = 0; i < meta.param_types.size(); i++) {
                    if (meta.param_types[i] == CType::ADT) {
                        param_types.push_back(adt_llvm_type(inst.type_name));
                    } else {
                        param_types.push_back(llvm_type(meta.param_types[i]));
                    }
                }
                auto* ret_llvm = (meta.return_type == CType::ADT)
                    ? adt_llvm_type(meta.return_adt_name.empty()
                        ? inst.type_name : meta.return_adt_name)
                    : llvm_type(meta.return_type);
                auto* fn_type = llvm::FunctionType::get(ret_llvm, param_types, false);
                auto* fn = module_->getFunction(mangled);
                if (!fn)
                    fn = Function::Create(fn_type, Function::ExternalLinkage, mangled, module_.get());
                auto compiled = compiled_function_from_meta(fn, meta, meta.return_type);
                compiled_functions_[mangled] = std::move(compiled);
            }
        }
    }
}

llvm::Function* Codegen::declare_import_extern_fn(const std::string& mangled,
                                                   const ModuleFunctionMeta& meta) {
    auto i64_ty = LType::getInt64Ty(*context_);
    auto ptr_ty = PointerType::get(*context_, 0);
    std::vector<LType*> param_types;
    for (auto ct : meta.param_types) param_types.push_back(llvm_type(ct));

    llvm::Type* ret_ty = nullptr;
    switch (meta.extern_promise) {
    case ast::ExternPromiseKind::Sync:
        return nullptr;
    case ast::ExternPromiseKind::IoUring:
        ret_ty = i64_ty;
        break;
    case ast::ExternPromiseKind::NativePtr:
        ret_ty = ptr_ty;
        break;
    case ast::ExternPromiseKind::ThreadPool:
        ret_ty = llvm_type(meta.promise_inner_type);
        break;
    }
    auto* fn_type = llvm::FunctionType::get(ret_ty, param_types, false);
    llvm::Function* fn = module_->getFunction(mangled);
    if (!fn) fn = Function::Create(fn_type, Function::ExternalLinkage, mangled, module_.get());
    return fn;
}

void Codegen::bind_imported_promise_cf(const std::string& logical_name, llvm::Function* fn,
                                      const ModuleFunctionMeta& meta) {
    CompiledFunction cf;
    cf.fn = fn;
    cf.return_type = CType::PROMISE;
    cf.param_types = meta.param_types;
    cf.borrowed_params = meta.borrowed_params;
    cf.extern_promise = meta.extern_promise;
    cf.promise_inner_type = meta.promise_inner_type;
    compiled_functions_[logical_name] = cf;
    named_values_[logical_name] = {fn, CType::FUNCTION, {meta.promise_inner_type}};
}

// Register a single imported function/constructor by name
void Codegen::register_import(const std::string& mod_fqn,
                               const std::string& func_name,
                               const std::string& import_name) {
    // Check if it's an ADT constructor
    auto ctor_it = types_.adt_constructors.find(func_name);
    if (ctor_it != types_.adt_constructors.end()) {
        if (ctor_it->second.arity > 0)
            named_values_[import_name] = {nullptr, CType::FUNCTION};
        if (import_name != func_name)
            types_.adt_constructors[import_name] = ctor_it->second;
        return;
    }

    // .yona stdlib fallback: function already registered as deferred
    // (e.g., from a Std/X.yona file loaded by load_module_interface).
    // Just alias the name if needed; resolution finds it via deferred_functions_.
    auto def_it = deferred_functions_.find(func_name);
    if (def_it != deferred_functions_.end()) {
        if (import_name != func_name)
            deferred_functions_[import_name] = def_it->second;
        return;
    }

    std::string mangled = mangle_name(mod_fqn, func_name);

    // Register as extern — the pre-compiled version from the module is the default.
    // GENFN source (if available) is stored in imports_.imported_sources for
    // on-demand monomorphization when call-site types differ from the module's.
    auto meta_it = imports_.meta.find(mangled);
    if (meta_it != imports_.meta.end() && meta_it->second.extern_promise != ast::ExternPromiseKind::Sync) {
        llvm::Function* fn = declare_import_extern_fn(mangled, meta_it->second);
        bind_imported_promise_cf(import_name, fn, meta_it->second);
    } else if (meta_it != imports_.meta.end() && meta_it->second.param_types.empty()) {
        // Zero-arity function: create extern declaration so it can be called.
        // Don't set named_values_ — let codegen_identifier find it in compiled_functions_
        // and return it as a callable function reference.
        auto& meta = meta_it->second;
        auto* ret_ty = llvm_type(meta.return_type);
        auto* fn_type = llvm::FunctionType::get(ret_ty, {}, false);
        auto* fn = module_->getFunction(mangled);
        if (!fn) fn = Function::Create(fn_type, Function::ExternalLinkage, mangled, module_.get());
        compiled_functions_[import_name] = compiled_function_from_meta(fn, meta, meta.return_type);
        imports_.extern_functions[import_name] = mangled;
    } else {
        named_values_[import_name] = {nullptr, CType::FUNCTION};
        imports_.extern_functions[import_name] = mangled;
    }
}

// Register ALL exports from a loaded .yonai (wildcard import)
void Codegen::register_all_imports(const std::string& mod_fqn) {
    // For .yona fallback modules, deferred_functions_ already holds them by
    // local name. No additional setup needed — call sites resolve directly.

    // Register all functions from imports_.meta
    for (auto& [mangled, meta] : imports_.meta) {
        // Extract function name from mangled: yona_Pkg_Mod__func -> func
        auto pos = mangled.rfind("__");
        if (pos != std::string::npos) {
            std::string func_name = mangled.substr(pos + 2);
            // Only register if this function belongs to this module
            std::string expected_prefix = "yona_";
            for (char c : mod_fqn) expected_prefix += (c == '\\') ? '_' : c;
            expected_prefix += "__";
            if (mangled.find(expected_prefix) == 0) {
                std::string exported_tail = mangled.substr(expected_prefix.size());
                if (exported_tail.find("__") != std::string::npos)
                    continue;
                if (meta.extern_promise != ast::ExternPromiseKind::Sync) {
                    llvm::Function* fn = declare_import_extern_fn(mangled, meta);
                    bind_imported_promise_cf(func_name, fn, meta);
                } else {
                    named_values_[func_name] = {nullptr, CType::FUNCTION};
                    imports_.extern_functions[func_name] = mangled;
                }
            }
        }
    }
    // Register all constructors
    for (auto& [name, info] : types_.adt_constructors) {
        if (info.type_name.find(mod_fqn) != std::string::npos ||
            imports_.meta.count(mangle_name(mod_fqn, name)) > 0) {
            // Already registered by load_interface_file
        }
    }
}

TypedValue Codegen::codegen_import(ImportExpr* node) {
    for (auto* clause : node->clauses) {
        if (clause->get_type() == AST_FUNCTIONS_IMPORT) {
            auto* fi = static_cast<FunctionsImport*>(clause);
            auto [mod_fqn, mod_path] = build_fqn_path(fi->fromFqn);
            load_module_interface(mod_path);

            for (auto* alias : fi->aliases) {
                std::string func_name = alias->name->value;
                std::string import_name = alias->alias ? alias->alias->value : func_name;
                register_import(mod_fqn, func_name, import_name);
            }
            register_trait_externs();
        } else if (clause->get_type() == AST_MODULE_IMPORT) {
            // Wildcard import: import Std\List in expr
            auto* mi = static_cast<ModuleImport*>(clause);
            auto [mod_fqn, mod_path] = build_fqn_path(mi->fqn);
            load_module_interface(mod_path);
            register_all_imports(mod_fqn);
            register_trait_externs();
        }
    }

    return codegen(node->expr);
}

TypedValue Codegen::codegen_extern_decl(ExternDeclExpr* node) {
    // Extract parameter types and return type from the declared type
    std::vector<LType*> param_types;
    std::vector<CType> param_ctypes;
    std::vector<std::string> param_descriptors;
    CType ret_ctype = CType::INT;

    auto current_type = node->declared_type;

    // Uncurry: A -> B -> C becomes params=[A, B], ret=C
    while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(current_type)) {
        auto ft = std::get<std::shared_ptr<types::FunctionType>>(current_type);
        const auto* named_param = std::get_if<std::shared_ptr<types::NamedType>>(
            &ft->argumentType);
        const bool param_is_variable = named_param && *named_param &&
            !(*named_param)->name.empty() &&
            std::islower(static_cast<unsigned char>((*named_param)->name.front()));
        CType param_ct = param_is_variable ? CType::INT
                                           : yona_type_to_ctype(ft->argumentType);
        param_ctypes.push_back(param_ct);
        param_descriptors.push_back(source_type_descriptor(ft->argumentType));
        param_types.push_back(llvm_type(param_ct));
        current_type = ft->returnType;
    }
    const auto* named_return = std::get_if<std::shared_ptr<types::NamedType>>(
        &current_type);
    const bool return_is_variable = named_return && *named_return &&
        !(*named_return)->name.empty() &&
        std::islower(static_cast<unsigned char>((*named_return)->name.front()));
    ret_ctype = return_is_variable ? CType::INT : yona_type_to_ctype(current_type);
    std::string ret_adt_name = (ret_ctype == CType::ADT)
        ? yona_type_adt_name(current_type) : "";
    // For ADT returns, externs always come from the C runtime returning a
    // heap-allocated ADT pointer (i64 cast). Use ptr in the function signature.
    // For `extern io` (io_uring submit-and-return), the C function returns
    // an i64 uring user_data ID; the codegen sees a Promise and auto-awaits.
    // For `extern native`, C returns opaque yona_promise_t* (LLVM i8* / ptr).
    auto i64_ty = LType::getInt64Ty(*context_);
    auto ptr_ty = PointerType::get(*context_, 0);
    auto ret_llvm = node->extern_promise == ast::ExternPromiseKind::IoUring
        ? static_cast<LType*>(i64_ty)
        : (node->extern_promise == ast::ExternPromiseKind::NativePtr
                ? static_cast<LType*>(ptr_ty)
                : ((ret_ctype == CType::ADT) ? static_cast<LType*>(ptr_ty) : llvm_type(ret_ctype)));

    // The C ABI symbol may differ from the local Yona name (e.g.
    // `extern channel_new : Int -> Channel = "yona_Std_Channel__channel"`).
    // The LLVM function takes the ABI symbol; we register it locally under
    // the Yona name so call sites resolve naturally.
    const std::string& c_sym = node->c_symbol.empty() ? node->name : node->c_symbol;

    auto fn_type = llvm::FunctionType::get(ret_llvm, param_types, false);
    auto* fn = module_->getFunction(c_sym);
    // Source declarations are authoritative. A previously loaded interface
    // can contain an older or less precise ABI (notably generic collection
    // descriptors); retaining that declaration makes LLVM silently bitcast
    // pointer arguments to integers and corrupts the runtime heap.
    if (fn && fn->isDeclaration() && fn->getFunctionType() != fn_type) {
        compiled_functions_.erase(node->name);
        if (!fn->use_empty()) {
            report_error(node->source_context,
                "extern ABI mismatch for '" + c_sym +
                "': an earlier declaration already has live callers; "
                "make the source and interface signatures agree");
            return {};
        }
        fn->eraseFromParent();
        fn = nullptr;
    }
    if (!fn) {
        fn = Function::Create(fn_type, Function::ExternalLinkage,
                               c_sym, module_.get());
    }

    // Register as a compiled function. `extern_promise` selects call/await lowering.
    CompiledFunction cf;
    cf.fn = fn;
    const bool is_promise_extern = node->extern_promise != ast::ExternPromiseKind::Sync;
    cf.return_type = is_promise_extern ? CType::PROMISE : ret_ctype;
    cf.param_types = param_ctypes;
    cf.param_type_descriptors = std::move(param_descriptors);
    cf.return_type_descriptor = source_type_descriptor(current_type);
    cf.return_adt_name = ret_adt_name;
    cf.extern_promise = node->extern_promise;
    // Prelude's Array primitives are observational runtime intrinsics. They
    // never consume the collection/string/array they inspect; recording that
    // contract here lets wrapper methods infer and export the same borrow
    // mask. Mutable/consuming runtime APIs remain on the normal callee-owns
    // path.
    cf.borrowed_params.assign(param_ctypes.size(), false);
    if (c_sym.starts_with("yona_Prelude__Array_") && !cf.borrowed_params.empty())
        cf.borrowed_params[0] = true;
    if (is_promise_extern) cf.promise_inner_type = ret_ctype;
    compiled_functions_[node->name] = cf;
    imports_.native_dependencies[node->name] = {
        c_sym, module_meta_from_compiled(cf)};
    named_values_[node->name] = {
        fn, CType::FUNCTION,
        is_promise_extern ? std::vector<CType>{cf.promise_inner_type} : std::vector<CType>{}};

    // Compile the body (nullptr for module-level externs)
    if (node->body) return codegen(node->body);
    return {};
}

// ===== Local static helpers for type annotations =====

static CType yona_type_to_ctype(const types::Type& t) {
    if (std::holds_alternative<types::BuiltinType>(t)) {
        switch (std::get<types::BuiltinType>(t)) {
            case types::SignedInt64: case types::SignedInt32:
            case types::SignedInt16: case types::SignedInt128:
            case types::UnsignedInt64: case types::UnsignedInt32:
            case types::UnsignedInt16: case types::UnsignedInt128:
                return CType::INT;
            case types::Float64: case types::Float32: case types::Float128:
                return CType::FLOAT;
            case types::Bool: return CType::BOOL;
            case types::String: return CType::STRING;
            case types::Symbol: return CType::SYMBOL;
            case types::Unit: return CType::UNIT;
            case types::Seq: return CType::SEQ;
            case types::Set: return CType::SET;
            case types::Dict: return CType::DICT;
            default: return CType::INT;
        }
    }
    if (std::holds_alternative<std::shared_ptr<types::FunctionType>>(t))
        return CType::FUNCTION;
    if (std::holds_alternative<std::shared_ptr<types::SingleItemCollectionType>>(t)) {
        auto& col = std::get<std::shared_ptr<types::SingleItemCollectionType>>(t);
        return (col->kind == types::SingleItemCollectionType::Seq) ? CType::SEQ : CType::SET;
    }
    if (std::holds_alternative<std::shared_ptr<types::DictCollectionType>>(t))
        return CType::DICT;
    if (std::holds_alternative<std::shared_ptr<types::ProductType>>(t))
        return CType::TUPLE;
    if (std::holds_alternative<std::shared_ptr<types::NamedType>>(t)) {
        auto& nt = std::get<std::shared_ptr<types::NamedType>>(t);
        // Bare collection annotations (`Seq`, `Set`, `Dict`) parse as named
        // types, while bracketed collection annotations use collection nodes.
        // Both spellings must retain their collection C ABI in `.yonai`.
        if (nt->name == "Seq") return CType::SEQ;
        if (nt->name == "Set") return CType::SET;
        if (nt->name == "Dict") return CType::DICT;
        if (nt->name == "Channel") return CType::CHANNEL;
        if (nt->name == "FloatArray") return CType::FLOAT_ARRAY;
        if (nt->name == "IntArray") return CType::INT_ARRAY;
        if (nt->name == "ByteArray") return CType::BYTE_ARRAY;
        return CType::ADT;
    }
    if (std::holds_alternative<std::shared_ptr<types::PromiseType>>(t))
        return CType::PROMISE;
    if (std::holds_alternative<std::shared_ptr<types::SumType>>(t))
        return CType::SUM;
    if (std::holds_alternative<std::shared_ptr<types::RefinedType>>(t))
        return yona_type_to_ctype(std::get<std::shared_ptr<types::RefinedType>>(t)->base_type);
    return CType::INT;
}

// Extract the ADT type name from a Yona type, if present.
// Returns the name (e.g., "Option", "Result") for NamedType, empty otherwise.
// Channel maps to CType::CHANNEL — not an ADT — so we exclude it.
static std::string yona_type_adt_name(const types::Type& t) {
    if (std::holds_alternative<std::shared_ptr<types::NamedType>>(t)) {
        auto& nt = std::get<std::shared_ptr<types::NamedType>>(t);
        if (nt->name == "Channel") return "";
        return nt->name;
    }
    if (std::holds_alternative<std::shared_ptr<types::RefinedType>>(t))
        return yona_type_adt_name(std::get<std::shared_ptr<types::RefinedType>>(t)->base_type);
    return "";
}

static std::pair<std::vector<CType>, CType> uncurry_type_signature(const types::Type& t) {
    std::vector<CType> params;
    const types::Type* current = &t;
    while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(*current)) {
        auto& ft = std::get<std::shared_ptr<types::FunctionType>>(*current);
        params.push_back(yona_type_to_ctype(ft->argumentType));
        current = &ft->returnType;
    }
    return {params, yona_type_to_ctype(*current)};
}

void Codegen::populate_interface_effect_rows(ast::ModuleDecl* mod,
                                             typechecker::TypeChecker& tc) {
    if (!mod || !mod->fqn) return;
    std::string fqn;
    if (mod->fqn->packageName.has_value()) {
        auto* pkg = mod->fqn->packageName.value();
        for (size_t i = 0; i < pkg->parts.size(); i++) {
            if (i > 0) fqn += "\\";
            fqn += pkg->parts[i]->value;
        }
        fqn += "\\";
    }
    fqn += mod->fqn->moduleName->value;

    // Sibling-aware: private helpers must be in scope while inferring exports
    // (same path compile_module uses for .yonai FN rows). Per-function check()
    // cannot see unexported names and reports a spurious E0104.
    tc.check_module(mod);

    std::unordered_set<std::string> export_set(mod->exports.begin(), mod->exports.end());
    for (auto* func : mod->functions) {
        if (!func || export_set.count(func->name) == 0) continue;
        auto* ty = tc.type_of(func);
        if (!ty) continue;
        auto row = tc.effect_row_info(tc.zonk(ty));
        std::string mangled = mangle_name(fqn, func->name);
        auto it = imports_.meta.find(mangled);
        if (it != imports_.meta.end()) {
            it->second.effect_ops = row.ops;
            it->second.effect_row_known = true;
            it->second.effect_open_rest = row.open_rest;
            it->second.effect_hof = row.hof;
            it->second.effect_scheme = tc.serialize_effect_scheme(tc.zonk(ty));
        }
        auto cf_it = compiled_functions_.find(func->name);
        if (cf_it != compiled_functions_.end()) {
            cf_it->second.effect_ops = row.ops;
            cf_it->second.effect_row_known = true;
            cf_it->second.effect_open_rest = row.open_rest;
            cf_it->second.effect_hof = row.hof;
            cf_it->second.effect_scheme = tc.serialize_effect_scheme(tc.zonk(ty));
        }
    }
}

} // namespace yona::compiler::codegen
