#ifndef YONA_LSP_JSON_H
#define YONA_LSP_JSON_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace yona::lsp {

/// Owning JSON value used by the LSP transport.
///
/// Ownership:
/// - Values recursively own their strings, arrays, objects, and descendants;
///   copies are independent.
/// - Accessors returning references borrow storage from this value when the
///   requested type or key exists. Missing/wrong-type access returns a shared,
///   immutable empty value instead.
/// - Borrowed references must not outlive replacement, movement, or destruction
///   of their owning value.
///
/// Failure:
/// - `parse()` returns a null value for malformed or over-deep input. Because a
///   JSON null is also valid, pass `error` when the distinction matters. The
///   error string is cleared on success and populated on syntax failure.
/// - Conversions return their documented fallback or an empty value rather
///   than throwing for a type mismatch.
///
/// Thread safety:
/// - Independent values, and concurrent const access to one immutable value,
///   are safe. Mutation and access to the same value require synchronization.
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
  Json(const char *v) : data_(std::string(v ? v : "")) {}
  Json(std::string v) : data_(std::move(v)) {}
  Json(Array v) : data_(std::move(v)) {}
  Json(Object v) : data_(std::move(v)) {}

  /// Parse one complete value, allowing only trailing whitespace. Input and
  /// the optional error sink are borrowed for this call.
  static Json parse(std::string_view text, std::string *error = nullptr);

  /// Serialize to an independently owned string.
  std::string dump() const;

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
  bool is_bool() const { return std::holds_alternative<bool>(data_); }
  bool is_number() const {
    return std::holds_alternative<std::int64_t>(data_) ||
           std::holds_alternative<double>(data_);
  }
  bool is_string() const { return std::holds_alternative<std::string>(data_); }
  bool is_array() const { return std::holds_alternative<Array>(data_); }
  bool is_object() const { return std::holds_alternative<Object>(data_); }

  bool as_bool(bool fallback = false) const;
  std::int64_t as_int(std::int64_t fallback = 0) const;
  std::string as_string() const;
  /// Return borrowed storage, or a process-lifetime empty array on mismatch.
  const Array &as_array() const;

  /// Return borrowed storage, or a process-lifetime empty object on mismatch.
  const Object &as_object() const;

  /// Return an owned member copy, or an owned null value when absent.
  Json get(const std::string &key) const;
  bool has(const std::string &key) const;

  /// Convert this value to an object when necessary and return a borrowed
  /// reference to the named member, inserting a null member when absent.
  Json &operator[](const std::string &key);

  /// Return a borrowed member, or a process-lifetime null value when absent.
  const Json &operator[](const std::string &key) const;

private:
  std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array,
               Object>
      data_ = nullptr;
};

} // namespace yona::lsp

#endif /* YONA_LSP_JSON_H */
