#ifndef YONA_MODEL_EFFECTSOLVER_H
#define YONA_MODEL_EFFECTSOLVER_H
#include <compare>
#include <cstdint>
#include <expected>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yona::compiler::typechecker {

using EffectVarId = std::uint32_t;
using EffectAtomId = std::uint32_t;
using EffectAtomCatalogDomain = std::uint64_t;
inline constexpr EffectAtomCatalogDomain InvalidEffectAtomCatalogDomain = 0;

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

struct EffectRefHasher final {
  [[nodiscard]] std::size_t operator()(EffectRef Effect) const noexcept {
    auto Seed = std::hash<std::uint64_t>{}(Effect.owner);
    Seed ^= std::hash<std::uint32_t>{}(Effect.index) +
            static_cast<std::size_t>(0x9e3779b9u) + (Seed << 6u) + (Seed >> 2u);
    return Seed;
  }
};

/// A still-open effect variable, optionally viewed through a handler mask.
struct EffectProjection {
  EffectVarId variable = 0;
  std::vector<EffectAtomId> excluded_atoms;
  bool opaque = false;

  bool operator==(const EffectProjection &) const = default;
};

/// Deterministic semantic summary consumed by later compiler integration.
struct EffectNormalForm {
  std::vector<EffectAtomId> known_atoms;
  std::vector<EffectProjection> tails;

  [[nodiscard]] bool is_open() const noexcept { return !tails.empty(); }
  [[nodiscard]] bool empty() const noexcept {
    return known_atoms.empty() && tails.empty();
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
  /// Every source graph reference mapped to its fresh destination reference.
  std::unordered_map<EffectRef, EffectRef, EffectRefHasher> references;
};

using EffectReferenceMap =
    std::unordered_map<EffectRef, EffectRef, EffectRefHasher>;
using EffectAtomInstantiator =
    std::function<std::expected<EffectAtomId, std::string>(
        EffectAtomId, const EffectReferenceMap &)>;
using EffectAtomMaskValidator = std::function<bool(EffectAtomId)>;
using EffectMaskMatcher =
    std::function<bool(EffectAtomId Pattern, EffectAtomId Candidate)>;

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
  class InstantiationTransaction;

  explicit EffectSolver(EffectAtomCatalogDomain AtomDomain,
                        EffectAtomMaskValidator MaskValidator = {});
  EffectSolver(const EffectSolver &) = delete;
  EffectSolver &operator=(const EffectSolver &) = delete;
  EffectSolver(EffectSolver &&) noexcept;
  EffectSolver &operator=(EffectSolver &&) noexcept;
  ~EffectSolver();

  [[nodiscard]] EffectRef empty() const noexcept;
  [[nodiscard]] EffectAtomCatalogDomain atomDomain() const noexcept;
  EffectRef atoms(std::vector<EffectAtomId> Atoms);
  EffectRef atoms(std::initializer_list<EffectAtomId> Atoms);
  EffectRef flexible();
  EffectRef derived();
  EffectRef opaque();

  EffectRef join(std::vector<EffectRef> effects);
  EffectRef join(std::initializer_list<EffectRef> effects);
  EffectRef mask(EffectRef source, std::vector<EffectAtomId> ExcludedAtoms);
  EffectRef mask(EffectRef source,
                 std::initializer_list<EffectAtomId> ExcludedAtoms);

  EffectConstraintResult addAtom(EffectRef Derived, EffectAtomId Atom);
  EffectConstraintResult equateAtoms(EffectAtomId Left, EffectAtomId Right);
  EffectConstraintResult materializeMaskMatches(EffectRef Root,
                                                const EffectMaskMatcher &Match);
  [[nodiscard]] EffectAtomId canonicalAtom(EffectAtomId Atom) const;
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
  [[nodiscard]] std::expected<InstantiationTransaction, std::string>
  beginInstantiation(const EffectGraphTemplate &Graph,
                     EffectAtomInstantiator InstantiateAtom);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class EffectSolver::InstantiationTransaction final {
public:
  InstantiationTransaction(InstantiationTransaction &&) noexcept;
  InstantiationTransaction &operator=(InstantiationTransaction &&) noexcept;
  InstantiationTransaction(const InstantiationTransaction &) = delete;
  InstantiationTransaction &
  operator=(const InstantiationTransaction &) = delete;
  ~InstantiationTransaction();

  [[nodiscard]] std::expected<void, std::string>
  prepareCommit(const std::function<bool(EffectAtomId)> &ValidAtom,
                const std::function<bool(EffectAtomId)> &ValidMaskAtom);
  void commitPrepared() noexcept;
  [[nodiscard]] const EffectInstantiation &result() const;

private:
  struct State;
  std::unique_ptr<State> State_;
  explicit InstantiationTransaction(std::unique_ptr<State> State);
  friend class EffectSolver;
};

} // namespace yona::compiler::typechecker

#endif /* YONA_MODEL_EFFECTSOLVER_H */
