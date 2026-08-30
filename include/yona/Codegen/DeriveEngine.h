#ifndef YONA_CODEGEN_DERIVEENGINE_H
#define YONA_CODEGEN_DERIVEENGINE_H
/// DeriveEngine — auto-derive trait instances for ADTs.
///
/// Each CodegenSession owns an independent strategy registry. Built-in
/// strategies are installed by DeriveEngine's constructor; callers may add
/// session-local strategies explicitly without mutating process-global state.

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yona::compiler::codegen {

/// Metadata for a single ADT constructor.
struct DeriveCtorInfo {
  std::string name;
  int tag;
  int arity;
  std::vector<std::string> field_names;
  /// Per-field: type param name ("a", "b") if polymorphic, empty if concrete
  std::vector<std::string> field_type_refs;
  /// Per-field: concrete type name ("Int", "String", etc.) from FieldType
  std::vector<std::string> field_type_names;
};

/// Metadata for an ADT, collected for derive expansion.
struct DeriveAdtInfo {
  std::string type_name;
  std::vector<std::string> type_params;
  std::vector<DeriveCtorInfo> constructors;
  bool is_recursive = false;
};

/// Generator function: takes ADT metadata, returns method source text.
using DeriveGeneratorFn = std::function<std::string(const DeriveAdtInfo &)>;

/// A registered derivable trait strategy.
struct DeriveStrategyInfo {
  std::string trait_name;
  std::vector<std::string> method_names;
  DeriveGeneratorFn generator;
};

/// Registry and dispatcher for auto-derive strategies.
class DeriveEngine {
public:
  DeriveEngine();

  /// Register or replace a strategy in this session only.
  void registerStrategy(std::string TraitName,
                        std::vector<std::string> MethodNames,
                        DeriveGeneratorFn Generator);

  /// Check if a trait is derivable.
  [[nodiscard]] bool isDerivable(const std::string &TraitName) const;

  /// Get the strategy for a trait (nullptr if not derivable).
  [[nodiscard]] const DeriveStrategyInfo *
  getStrategy(const std::string &TraitName) const;

  /// Get all registered strategy names (for error messages).
  [[nodiscard]] std::vector<std::string> allDerivableTraits() const;

private:
  std::unordered_map<std::string, DeriveStrategyInfo> Strategies;
};

} // namespace yona::compiler::codegen

#endif /* YONA_CODEGEN_DERIVEENGINE_H */
