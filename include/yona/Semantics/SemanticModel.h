#ifndef YONA_SEMANTICS_SEMANTICMODEL_H
#define YONA_SEMANTICS_SEMANTICMODEL_H

#include "yona/Support/SourceManager.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace yona::ast {
class AstNode;
}

namespace yona::compiler {
class DiagnosticEngine;
namespace typechecker {
class TypeChecker;
}
} // namespace yona::compiler

namespace yona::semantics {

/// Model-local identity of a lexical or module-level binding.
class BindingId final {
public:
  constexpr BindingId() noexcept = default;
  explicit constexpr BindingId(std::uint64_t Value) noexcept : Value(Value) {}

  [[nodiscard]] constexpr bool isValid() const noexcept { return Value != 0; }
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return Value; }

  friend constexpr bool operator==(BindingId, BindingId) noexcept = default;

private:
  std::uint64_t Value = 0;
};

enum class SymbolKind {
  Variable,
  Function,
  Namespace,
  Type,
  Interface,
  Method,
  Instance,
};

[[nodiscard]] std::string_view symbolKindName(SymbolKind Kind) noexcept;

enum class OwnershipKind {
  Unknown,
  Unrestricted,
  Linear,
};

[[nodiscard]] std::string_view ownershipKindName(OwnershipKind Kind) noexcept;

/// Type, effect, and ownership facts copied from semantic analysis.
///
/// Strings are owned by the model and do not retain TypeChecker internals.
struct NodeSemantics {
  std::string InferredType;
  std::string Effects;
  OwnershipKind Ownership = OwnershipKind::Unknown;
};

/// One definition or reference in the source program.
struct SemanticOccurrence {
  BindingId Binding;
  SourceRange Range;
  std::string Name;
  SymbolKind Kind = SymbolKind::Variable;
  bool IsDefinition = false;
  NodeSemantics Facts;
  std::string OriginModule;
  std::string OriginName;
  std::string Detail;
  std::string Container;
};

struct SemanticDiagnostic {
  SourceRange Range;
  int Severity = 1;
  std::string Code;
  std::string Message;
};

/// Immutable source-backed semantic index shared by tooling consumers.
///
/// Ownership:
/// - The model shares ownership of Sources and copies every type/effect string.
/// - Root, TypeChecker, and Diagnostics are only borrowed during construction.
/// - Returned pointers and spans remain valid until model destruction.
///
/// Failure:
/// - Construction throws std::invalid_argument for a null manager and
///   std::out_of_range for a source ID not owned by Sources. Analysis
///   diagnostics are data, not construction failures.
///
/// Thread safety:
/// - A completed model is immutable and supports concurrent reads.
/// - Construction must not overlap mutation of Root or TypeChecker.
class SemanticModel final {
public:
  SemanticModel(std::shared_ptr<SourceManager> Sources, SourceId Source,
                ast::AstNode *Root,
                compiler::typechecker::TypeChecker *TypeChecker,
                compiler::DiagnosticEngine *Diagnostics);
  ~SemanticModel();

  SemanticModel(const SemanticModel &) = delete;
  SemanticModel &operator=(const SemanticModel &) = delete;
  SemanticModel(SemanticModel &&) noexcept;
  SemanticModel &operator=(SemanticModel &&) noexcept;

  [[nodiscard]] const SourceManager &sourceManager() const noexcept;
  [[nodiscard]] SourceId sourceId() const noexcept;
  [[nodiscard]] std::string_view sourceText() const noexcept;
  [[nodiscard]] SourceRange rootRange() const noexcept;
  [[nodiscard]] const NodeSemantics *rootFacts() const noexcept;

  [[nodiscard]] std::span<const SemanticOccurrence>
  occurrences() const noexcept;
  [[nodiscard]] std::span<const SemanticDiagnostic>
  diagnostics() const noexcept;

  [[nodiscard]] const SemanticOccurrence *
  occurrenceAt(std::size_t ByteOffset) const noexcept;
  [[nodiscard]] const SemanticOccurrence *
  definition(BindingId Binding) const noexcept;
  [[nodiscard]] std::vector<const SemanticOccurrence *>
  references(BindingId Binding, bool IncludeDefinition) const;
  [[nodiscard]] const NodeSemantics *
  factsFor(const ast::AstNode *Node) const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> Implementation;
};

} // namespace yona::semantics

#endif /* YONA_SEMANTICS_SEMANTICMODEL_H */
