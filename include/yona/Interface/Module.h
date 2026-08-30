#ifndef YONA_INTERFACE_MODULE_H
#define YONA_INTERFACE_MODULE_H

#include "yona/Model/ModuleIdentity.h"

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace yona::interface {

/// Execution convention recorded for an exported function.
enum class FunctionKind {
  Synchronous,
  ThreadPool,
  Io,
  Native,
};

/// Closed or open effect row attached to a function contract.
struct EffectRow {
  std::vector<std::string> Operations;
  std::string Scheme;
  bool IsKnown = false;
  bool IsOpen = false;
  bool IsHigherOrder = false;
};

/// Canonical function contract keyed by its local Yona symbol.
///
/// The contract owns all strings and containers. It does not store a generated
/// C export spelling: callers derive that spelling from the owning module.
struct Function {
  FunctionKind Kind = FunctionKind::Synchronous;
  std::string Name;
  std::vector<std::string> ParameterTypes;
  std::string ReturnType;
  std::vector<bool> BorrowedParameters;
  std::vector<bool> TupleElementLinear;
  EffectRow Effects;

  /// Derive the canonical public C export. Invalid local names throw
  /// `std::invalid_argument`. This operation has no shared mutable state.
  [[nodiscard]] std::string
  exportName(const model::ModuleIdentity &Module) const;
};

struct AdtField {
  std::string Name;
  std::string Type;
};

struct Constructor {
  std::string Name;
  std::size_t Tag = 0;
  std::size_t Arity = 0;
  std::vector<AdtField> Fields;
};

struct Adt {
  std::string Name;
  std::size_t VariantCount = 0;
  std::size_t MaxArity = 0;
  bool IsRecursive = false;
  bool IsOpaque = false;
  std::vector<std::string> TypeParameters;
  std::vector<Constructor> Constructors;
};

struct TraitSupertype {
  std::string Name;
  std::string Parameter;
};

struct TraitMethod {
  std::string Name;
  std::string Type;
};

struct Trait {
  std::string Name;
  std::size_t MethodCount = 0;
  std::vector<std::string> TypeParameters;
  std::vector<TraitSupertype> Supertypes;
  std::vector<TraitMethod> Methods;
};

/// A structural reference to a Yona export.
///
/// Both the module identity and local key are owned. Generated C spellings are
/// deliberately absent and are derived only when crossing the C boundary.
struct ExportReference {
  ExportReference(model::ModuleIdentity Module, std::string LocalName);

  model::ModuleIdentity Module;
  std::string LocalName;

  /// Derive the canonical public C export. An invalid local name throws
  /// std::invalid_argument; no state is retained.
  [[nodiscard]] std::string exportName() const;
};

struct TraitConstraint {
  std::string TraitName;
  std::string Parameter;
};

struct TraitImplementation {
  TraitImplementation(std::string MethodName, ExportReference Target);

  std::string MethodName;
  ExportReference Target;
};

struct TraitInstance {
  std::string TraitName;
  std::vector<std::string> TypeHeads;
  std::vector<std::string> TypeParameters;
  std::vector<TraitConstraint> Constraints;
  std::vector<TraitImplementation> Implementations;
};

struct NativeReference {
  std::string Symbol;
};

using DependencyTarget = std::variant<NativeReference, ExportReference>;

/// A private binding needed when reparsing a generic function body.
struct GenericDependency {
  GenericDependency(Function Contract, DependencyTarget Target);

  Function Contract;
  DependencyTarget Target;
};

struct GenericConstructor {
  std::string Name;
  std::string TypeName;
  std::size_t Tag = 0;
  std::size_t Arity = 0;
  std::size_t VariantCount = 0;
  std::size_t MaxArity = 0;
  bool IsRecursive = false;
  std::vector<AdtField> Fields;
};

struct GenericFunction {
  std::string Name;
  std::string SourceName;
  std::vector<GenericDependency> Dependencies;
  std::vector<GenericConstructor> Constructors;
  std::string Source;
};

/// Fully owned, immutable-by-convention representation of one `.yonai` file.
///
/// The type contains no global state and is safe to read concurrently after it
/// has been published. Mutating one instance concurrently requires external
/// synchronization.
struct InterfaceModule {
  explicit InterfaceModule(model::ModuleIdentity Identity);

  model::ModuleIdentity Identity;
  std::vector<Adt> Adts;
  std::vector<Trait> Traits;
  std::vector<TraitInstance> Instances;
  std::vector<Function> Functions;
  std::vector<GenericFunction> GenericFunctions;
};

} // namespace yona::interface

#endif /* YONA_INTERFACE_MODULE_H */
