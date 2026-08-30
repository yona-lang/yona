#ifndef YONA_MODEL_EFFECTSOLVER_H
#define YONA_MODEL_EFFECTSOLVER_H
#include <compare>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yona::compiler::typechecker {

using EffectVarId = std::uint32_t;

/// Stable solver-local handle to an effect expression.
///
/// The handle owns nothing and is meaningful only with the EffectSolver that
/// created it. Moving the solver transfers that association to the destination;
/// destroying it invalidates every handle.
struct EffectRef {
  std::uint64_t owner = 0;
  std::uint32_t index = UINT32_MAX;

  [[nodiscard]] bool valid() const noexcept {
    return owner != 0 && index != UINT32_MAX;
  }
  auto operator<=>(const EffectRef &) const = default;
};

/// A still-open effect variable, optionally viewed through a handler mask.
struct EffectProjection {
  EffectVarId variable = 0;
  std::vector<std::string> excluded_labels;
  bool opaque = false;

  bool operator==(const EffectProjection &) const = default;
};

/// Deterministic semantic summary consumed by later compiler integration.
struct EffectNormalForm {
  std::vector<std::string> known_labels;
  std::vector<EffectProjection> tails;

  [[nodiscard]] bool is_open() const noexcept { return !tails.empty(); }
  [[nodiscard]] bool empty() const noexcept {
    return known_labels.empty() && tails.empty();
  }
  bool operator==(const EffectNormalForm &) const = default;
};

enum class EffectConstraintResult {
  Solved,
  Deferred,
  Conflict,
};

/// Immutable solver-independent snapshot used by type schemes.
///
/// Copies share ownership of immutable graph data and support concurrent
/// reads. A default-constructed instance is empty and cannot be instantiated.
class EffectGraphTemplate {
public:
  EffectGraphTemplate();
  EffectGraphTemplate(const EffectGraphTemplate &);
  EffectGraphTemplate(EffectGraphTemplate &&) noexcept;
  EffectGraphTemplate &operator=(const EffectGraphTemplate &);
  EffectGraphTemplate &operator=(EffectGraphTemplate &&) noexcept;
  ~EffectGraphTemplate();

private:
  struct Data;
  std::shared_ptr<const Data> data_;

  explicit EffectGraphTemplate(std::shared_ptr<const Data> data);
  friend class EffectSolver;
};

struct EffectInstantiation {
  EffectRef root;
  /// Fresh roots in the same order passed to the matching `freeze` overload.
  /// A type scheme commonly contains several arrows whose effects share
  /// variables; cloning them as one graph preserves that correlation.
  std::vector<EffectRef> roots;
  /// Original template variable id -> fresh solver-local variable handle.
  std::unordered_map<EffectVarId, EffectRef> variables;
};

/// Positive effect-constraint graph. Aggregation is ACI join/inclusion;
/// equality is an explicit, separate operation.
///
/// Every returned EffectRef is borrowed from this solver. Passing an invalid
/// or foreign handle throws std::invalid_argument; freezing no roots and
/// instantiating an empty template do likewise. Constraint conflicts are
/// reported with EffectConstraintResult and roll back the rejected update.
/// The graph is mutable and unsynchronized: serialize all operations that can
/// overlap mutation. Concurrent const queries are valid only while no thread
/// mutates or destroys the solver.
class EffectSolver {
public:
  EffectSolver();
  EffectSolver(const EffectSolver &) = delete;
  EffectSolver &operator=(const EffectSolver &) = delete;
  EffectSolver(EffectSolver &&) noexcept;
  EffectSolver &operator=(EffectSolver &&) noexcept;
  ~EffectSolver();

  [[nodiscard]] EffectRef empty() const noexcept;
  EffectRef labels(std::vector<std::string> labels);
  EffectRef labels(std::initializer_list<std::string> labels);
  EffectRef flexible();
  EffectRef derived();
  EffectRef opaque();

  EffectRef join(std::vector<EffectRef> effects);
  EffectRef join(std::initializer_list<EffectRef> effects);
  EffectRef mask(EffectRef source, std::vector<std::string> excluded_labels);
  EffectRef mask(EffectRef source,
                 std::initializer_list<std::string> excluded_labels);

  EffectConstraintResult add_label(EffectRef derived, std::string label);
  EffectConstraintResult include(EffectRef source, EffectRef derived);
  EffectConstraintResult equate(EffectRef left, EffectRef right);

  [[nodiscard]] EffectNormalForm summarize(EffectRef effect) const;
  [[nodiscard]] std::optional<EffectVarId> variable_id(EffectRef effect) const;

  [[nodiscard]] EffectGraphTemplate freeze(EffectRef root) const;
  /// Freeze a connected-or-disconnected set of effect roots as one graph.
  /// This is required for polymorphic function types: parameter and result
  /// arrows may share effect variables even when neither root reaches the
  /// other structurally.
  [[nodiscard]] EffectGraphTemplate freeze(std::vector<EffectRef> roots) const;
  /// Clone graph into this solver and return solver-local handles. The template
  /// remains independently owned. Throws std::invalid_argument when graph is
  /// empty; malformed internal graph data raises std::logic_error.
  EffectInstantiation instantiate(const EffectGraphTemplate &graph);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace yona::compiler::typechecker

#endif /* YONA_MODEL_EFFECTSOLVER_H */
