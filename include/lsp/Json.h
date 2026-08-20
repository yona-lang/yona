#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace yona::lsp {

class Json {
public:
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;

    Json() = default;
    Json(std::nullptr_t) : data_(nullptr) {}
    Json(bool v) : data_(v) {}
    Json(int v) : data_(static_cast<std::int64_t>(v)) {}
    Json(std::int64_t v) : data_(v) {}
    Json(double v) : data_(v) {}
    Json(const char* v) : data_(std::string(v ? v : "")) {}
    Json(std::string v) : data_(std::move(v)) {}
    Json(Array v) : data_(std::move(v)) {}
    Json(Object v) : data_(std::move(v)) {}

    static Json parse(std::string_view text, std::string* error = nullptr);
    std::string dump() const;

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_bool() const { return std::holds_alternative<bool>(data_); }
    bool is_number() const { return std::holds_alternative<std::int64_t>(data_) || std::holds_alternative<double>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_array() const { return std::holds_alternative<Array>(data_); }
    bool is_object() const { return std::holds_alternative<Object>(data_); }

    bool as_bool(bool fallback = false) const;
    std::int64_t as_int(std::int64_t fallback = 0) const;
    std::string as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    Json get(const std::string& key) const;
    bool has(const std::string& key) const;

    Json& operator[](const std::string& key);
    const Json& operator[](const std::string& key) const;

private:
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> data_ = nullptr;
};

} // namespace yona::lsp
