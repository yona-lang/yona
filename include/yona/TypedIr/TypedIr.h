#ifndef YONA_TYPEDIR_TYPEDIR_H
#define YONA_TYPEDIR_TYPEDIR_H

#include "yona/Semantics/SemanticModel.h"
#include "yona/Support/SourceManager.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace yona::ast {
class AstNode;
}

namespace yona::typed_ir {

/// Function-local identity of a typed IR value.
class ValueId final {
public:
  constexpr ValueId() noexcept = default;
  explicit constexpr ValueId(std::uint32_t Value) noexcept : Value(Value) {}

  [[nodiscard]] constexpr bool isValid() const noexcept {
    return Value != InvalidValue;
  }
  [[nodiscard]] constexpr std::uint32_t value() const noexcept { return Value; }

  friend constexpr bool operator==(ValueId, ValueId) noexcept = default;

private:
  static constexpr std::uint32_t InvalidValue =
      (std::numeric_limits<std::uint32_t>::max)();
  std::uint32_t Value = InvalidValue;
};

enum class ValueKind {
  Parameter,
  Constant,
  Instruction,
};

/// Runtime ownership state carried by every typed IR value.
///
/// Trivial values need no retain/release. Borrowed values never own their
/// storage. Owned values must be released or transferred exactly once.
/// Transferred values have moved their ownership to a consumer.
enum class ValueOwnershipKind {
  Trivial,
  Borrowed,
  Owned,
  Transferred,
};

/// Scalar constant payload supported by the canonical bootstrap lowering.
/// std::monostate represents Unit.
using ScalarConstant = std::variant<std::monostate, bool, std::int64_t, double>;

[[nodiscard]] std::string_view
valueOwnershipKindName(ValueOwnershipKind Kind) noexcept;

/// Fully owned typed value record.
///
/// String views and constant() pointers borrow this record and are invalidated
/// by assignment or destruction. Concurrent reads are safe while the record is
/// not assigned or destroyed.
class Value final {
public:
  Value(const Value &) = default;
  Value &operator=(const Value &) = default;
  Value(Value &&) noexcept = default;
  Value &operator=(Value &&) noexcept = default;

  [[nodiscard]] ValueId id() const noexcept;
  [[nodiscard]] ValueKind kind() const noexcept;
  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] std::string_view type() const noexcept;
  [[nodiscard]] std::string_view effects() const noexcept;
  [[nodiscard]] semantics::OwnershipKind typeOwnership() const noexcept;
  [[nodiscard]] ValueOwnershipKind ownership() const noexcept;
  [[nodiscard]] SourceRange range() const noexcept;
  [[nodiscard]] const ScalarConstant *constant() const noexcept;

private:
  friend class Function;

  Value(ValueId Id, ValueKind Kind, std::string Name,
        semantics::NodeSemantics Facts, ValueOwnershipKind Ownership,
        SourceRange Range, std::optional<ScalarConstant> Constant);

  ValueId Id;
  ValueKind Kind;
  std::string Name;
  semantics::NodeSemantics Facts;
  ValueOwnershipKind Ownership;
  SourceRange Range;
  std::optional<ScalarConstant> Constant;
};

/// Ownership-safe typed body for one function.
///
/// Failure:
/// - Mutation methods throw std::invalid_argument for unknown semantic facts,
///   invalid value IDs, illegal ownership transitions, or an invalid result.
/// - Returned value pointers and spans remain valid until the next value
///   insertion. Name/type/effect views remain valid until move or destruction.
///
/// Thread safety:
/// - A Function is not safe for concurrent mutation. Concurrent reads are safe
///   after construction is complete.
class Function final {
public:
  Function(std::string Name, std::string Type, std::string Effects,
           SourceRange Range = SourceRange::unknown());

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] std::string_view type() const noexcept;
  [[nodiscard]] std::string_view effects() const noexcept;
  [[nodiscard]] SourceRange range() const noexcept;
  [[nodiscard]] std::span<const Value> values() const noexcept;
  [[nodiscard]] std::span<const ValueId> parameters() const noexcept;
  [[nodiscard]] std::optional<ValueId> result() const noexcept;

  [[nodiscard]] ValueId appendValue(ValueKind Kind, std::string Name,
                                    semantics::NodeSemantics Facts,
                                    ValueOwnershipKind Ownership,
                                    SourceRange Range);
  [[nodiscard]] ValueId appendConstant(std::string Name,
                                       semantics::NodeSemantics Facts,
                                       ScalarConstant Constant,
                                       SourceRange Range);
  [[nodiscard]] ValueId
  appendSemanticValue(ValueKind Kind, std::string Name,
                      const semantics::SemanticModel &Model,
                      const ast::AstNode *Node, ValueOwnershipKind Ownership,
                      SourceRange Range);

  void transfer(ValueId Id);
  void setResult(ValueId Id);

  [[nodiscard]] const Value *findValue(ValueId Id) const noexcept;

private:
  [[nodiscard]] Value *findMutableValue(ValueId Id) noexcept;

  std::string Name;
  std::string Type;
  std::string Effects;
  SourceRange Range;
  std::vector<Value> Values;
  std::vector<ValueId> Parameters;
  std::optional<ValueId> Result;
};

/// Typed IR container for a single source module.
///
/// Module owns every function/value and only retains the SourceId needed to
/// relate diagnostics and debug locations back to its SemanticModel.
///
/// Failure:
/// - Construction rejects an empty name or invalid source. addFunction rejects
///   duplicate names.
/// - Function references, pointers, and spans are borrowed and may be
///   invalidated by the next successful addFunction call.
///
/// Thread safety:
/// - A Module is not safe for concurrent mutation. Concurrent reads are safe
///   after construction is complete.
class Module final {
public:
  Module(std::string Name, SourceId Source);

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] SourceId source() const noexcept;
  [[nodiscard]] std::span<const Function> functions() const noexcept;

  Function &addFunction(Function FunctionValue);
  [[nodiscard]] const Function *
  findFunction(std::string_view Name) const noexcept;

private:
  std::string Name;
  SourceId Source;
  std::vector<Function> Functions;
};

} // namespace yona::typed_ir

#endif // YONA_TYPEDIR_TYPEDIR_H
