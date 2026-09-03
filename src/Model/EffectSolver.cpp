#include "yona/Model/EffectSolver.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace yona::compiler::typechecker {
namespace {

enum class EffectNodeKind : std::uint8_t {
  Empty,
  Atoms,
  FlexibleMeta,
  DerivedLeast,
  RigidOpaque,
  Join,
  Mask,
};

std::atomic<std::uint64_t> next_effect_solver_owner{1};

void normalize_atoms(std::vector<EffectAtomId> &Atoms) {
  std::sort(Atoms.begin(), Atoms.end());
  Atoms.erase(std::unique(Atoms.begin(), Atoms.end()), Atoms.end());
}

void merge_atoms(std::vector<EffectAtomId> &Target,
                 const std::vector<EffectAtomId> &Source) {
  Target.insert(Target.end(), Source.begin(), Source.end());
  normalize_atoms(Target);
}

void normalize_summary(EffectNormalForm &summary) {
  normalize_atoms(summary.known_atoms);
  for (auto &tail : summary.tails)
    normalize_atoms(tail.excluded_atoms);
  std::sort(summary.tails.begin(), summary.tails.end(),
            [](const EffectProjection &left, const EffectProjection &right) {
              return std::tie(left.variable, left.opaque, left.excluded_atoms) <
                     std::tie(right.variable, right.opaque,
                              right.excluded_atoms);
            });
  summary.tails.erase(std::unique(summary.tails.begin(), summary.tails.end()),
                      summary.tails.end());
}

void merge_summary(EffectNormalForm &target, const EffectNormalForm &source) {
  target.known_atoms.insert(target.known_atoms.end(),
                            source.known_atoms.begin(),
                            source.known_atoms.end());
  target.tails.insert(target.tails.end(), source.tails.begin(),
                      source.tails.end());
  normalize_summary(target);
}

} // namespace

struct EffectGraphTemplate::Data {
  struct Node {
    EffectNodeKind kind = EffectNodeKind::Empty;
    std::uint32_t original_index = 0;
    EffectVarId original_variable = 0;
    std::vector<EffectAtomId> atoms;
    std::vector<std::uint32_t> inputs;
    std::optional<std::uint32_t> binding;
    std::vector<EffectAtomId> excluded_atoms;
  };

  EffectAtomCatalogDomain atom_domain = InvalidEffectAtomCatalogDomain;
  std::uint64_t source_owner = 0;
  std::vector<Node> nodes;
  std::vector<std::uint32_t> roots;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> equalities;
};

struct EffectSolver::Impl {
  struct Node {
    EffectNodeKind kind = EffectNodeKind::Empty;
    EffectVarId variable = 0;
    std::vector<EffectAtomId> atoms;
    std::vector<EffectRef> inputs;
    std::optional<EffectRef> binding;
    std::vector<EffectAtomId> excluded_atoms;
  };

  std::uint64_t owner =
      next_effect_solver_owner.fetch_add(1, std::memory_order_relaxed);
  std::vector<Node> nodes{{}};
  EffectAtomCatalogDomain atom_domain = InvalidEffectAtomCatalogDomain;
  EffectAtomMaskValidator mask_validator;
  EffectVarId next_variable = 0;
  std::map<std::vector<EffectAtomId>, EffectRef> atom_cache;
  std::map<std::vector<std::uint32_t>, EffectRef> join_cache;
  std::map<std::pair<std::uint32_t, std::vector<EffectAtomId>>, EffectRef>
      mask_cache;
  std::vector<std::pair<EffectRef, EffectRef>> equality_constraints;
  std::vector<EffectAtomId> atom_parents;
  std::uint64_t generation = 0;

  explicit Impl(EffectAtomCatalogDomain AtomDomain,
                EffectAtomMaskValidator MaskValidator)
      : atom_domain(AtomDomain), mask_validator(std::move(MaskValidator)) {
    if (owner == 0)
      throw std::overflow_error("effect solver owner id space exhausted");
    if (atom_domain == InvalidEffectAtomCatalogDomain)
      throw std::invalid_argument(
          "effect solver requires a nonzero atom domain");
  }

  void ensure_atom(EffectAtomId Atom) {
    while (atom_parents.size() <= Atom)
      atom_parents.push_back(static_cast<EffectAtomId>(atom_parents.size()));
  }

  [[nodiscard]] EffectAtomId canonical_atom(EffectAtomId Atom) const {
    if (Atom >= atom_parents.size())
      return Atom;
    while (atom_parents[Atom] != Atom)
      Atom = atom_parents[Atom];
    return Atom;
  }

  void canonicalize_atoms(std::vector<EffectAtomId> &Atoms) const {
    for (auto &Atom : Atoms)
      Atom = canonical_atom(Atom);
    normalize_atoms(Atoms);
  }

  [[nodiscard]] EffectRef empty_ref() const noexcept {
    return EffectRef{owner, 0};
  }

  void require(EffectRef effect) const {
    if (!effect.valid() || effect.owner != owner ||
        effect.index >= nodes.size())
      throw std::invalid_argument(
          "effect reference does not belong to this solver");
  }

  EffectRef append(Node node) {
    const auto index = static_cast<std::uint32_t>(nodes.size());
    nodes.push_back(std::move(node));
    ++generation;
    return EffectRef{owner, index};
  }

  EffectRef resolve_binding(EffectRef effect) const {
    require(effect);
    std::unordered_set<std::uint32_t> seen;
    while (nodes[effect.index].kind == EffectNodeKind::FlexibleMeta &&
           nodes[effect.index].binding) {
      if (!seen.insert(effect.index).second)
        break;
      effect = *nodes[effect.index].binding;
      require(effect);
    }
    return effect;
  }

  bool occurs(EffectVarId variable, EffectRef effect,
              std::unordered_set<std::uint32_t> &seen) const {
    require(effect);
    if (!seen.insert(effect.index).second)
      return false;
    const auto &node = nodes[effect.index];
    if (node.kind == EffectNodeKind::FlexibleMeta) {
      if (node.variable == variable)
        return true;
      return node.binding && occurs(variable, *node.binding, seen);
    }
    for (const auto input : node.inputs)
      if (occurs(variable, input, seen))
        return true;
    return false;
  }

  EffectNormalForm cycle_seed(EffectRef effect) const {
    require(effect);
    const auto &node = nodes[effect.index];
    switch (node.kind) {
    case EffectNodeKind::Atoms: {
      auto Atoms = node.atoms;
      canonicalize_atoms(Atoms);
      return EffectNormalForm{std::move(Atoms), {}};
    }
    case EffectNodeKind::FlexibleMeta:
      return EffectNormalForm{{}, {{node.variable, {}, false}}};
    case EffectNodeKind::RigidOpaque:
      return EffectNormalForm{{}, {{node.variable, {}, true}}};
    default:
      return {};
    }
  }

  bool is_symbolic(EffectRef effect,
                   std::unordered_set<std::uint32_t> &seen) const {
    effect = resolve_binding(effect);
    if (!seen.insert(effect.index).second)
      return false;
    const auto &node = nodes[effect.index];
    switch (node.kind) {
    case EffectNodeKind::FlexibleMeta:
    case EffectNodeKind::DerivedLeast:
    case EffectNodeKind::RigidOpaque:
      return true;
    case EffectNodeKind::Join:
    case EffectNodeKind::Mask:
      return std::any_of(
          node.inputs.begin(), node.inputs.end(),
          [&](EffectRef input) { return is_symbolic(input, seen); });
    case EffectNodeKind::Empty:
    case EffectNodeKind::Atoms:
      return false;
    }
    return false;
  }

  bool is_symbolic(EffectRef effect) const {
    std::unordered_set<std::uint32_t> seen;
    return is_symbolic(effect, seen);
  }

  EffectNormalForm summarize_impl(EffectRef effect,
                                  std::unordered_set<std::uint32_t> &active,
                                  bool follow_equalities) const {
    effect = resolve_binding(effect);
    if (!active.insert(effect.index).second)
      return cycle_seed(effect);

    const auto &node = nodes[effect.index];
    EffectNormalForm result;
    switch (node.kind) {
    case EffectNodeKind::Empty:
      break;
    case EffectNodeKind::Atoms:
      result.known_atoms = node.atoms;
      break;
    case EffectNodeKind::FlexibleMeta:
      result.tails.push_back(EffectProjection{node.variable, {}, false});
      break;
    case EffectNodeKind::RigidOpaque:
      result.tails.push_back(EffectProjection{node.variable, {}, true});
      break;
    case EffectNodeKind::DerivedLeast:
    case EffectNodeKind::Join: {
      result.known_atoms = node.atoms;
      for (const auto input : node.inputs)
        merge_summary(result, summarize_impl(input, active, follow_equalities));
      break;
    }
    case EffectNodeKind::Mask: {
      result = node.inputs.empty() ? EffectNormalForm{}
                                   : summarize_impl(node.inputs.front(), active,
                                                    follow_equalities);
      canonicalize_atoms(result.known_atoms);
      auto Excluded = node.excluded_atoms;
      canonicalize_atoms(Excluded);
      std::erase_if(result.known_atoms, [&](EffectAtomId Atom) {
        return std::binary_search(Excluded.begin(), Excluded.end(), Atom);
      });
      for (auto &tail : result.tails)
        merge_atoms(tail.excluded_atoms, Excluded);
      break;
    }
    }

    if (follow_equalities && is_symbolic(effect)) {
      for (const auto &[constraint_left, constraint_right] :
           equality_constraints) {
        const auto left = resolve_binding(constraint_left);
        const auto right = resolve_binding(constraint_right);
        if (left == effect && right != effect)
          merge_summary(result, summarize_impl(right, active, true));
        else if (right == effect && left != effect)
          merge_summary(result, summarize_impl(left, active, true));
      }
    }

    active.erase(effect.index);
    normalize_summary(result);
    return result;
  }

  EffectNormalForm summarize_raw(EffectRef effect) const {
    std::unordered_set<std::uint32_t> active;
    auto result = summarize_impl(effect, active, false);
    normalize_summary(result);
    return result;
  }

  bool contains_derived(EffectRef effect,
                        std::unordered_set<std::uint32_t> &seen) const {
    require(effect);
    if (!seen.insert(effect.index).second)
      return false;
    const auto &node = nodes[effect.index];
    if (node.kind == EffectNodeKind::DerivedLeast)
      return true;
    if (node.binding && contains_derived(*node.binding, seen))
      return true;
    for (const auto input : node.inputs)
      if (contains_derived(input, seen))
        return true;
    return false;
  }

  bool contains_derived(EffectRef effect) const {
    std::unordered_set<std::uint32_t> seen;
    return contains_derived(effect, seen);
  }

  using BooleanAtom = std::uint64_t;

  struct BooleanExpression {
    bool constant = false;
    std::vector<BooleanAtom> atoms;
  };

  static BooleanAtom effect_variable_atom(EffectVarId variable) {
    return static_cast<BooleanAtom>(variable) << 1;
  }
  static BooleanAtom derived_atom(std::uint32_t index) {
    return (static_cast<BooleanAtom>(index) << 1) | 1;
  }

  static void merge_boolean_expression(BooleanExpression &target,
                                       const BooleanExpression &source) {
    target.constant = target.constant || source.constant;
    target.atoms.insert(target.atoms.end(), source.atoms.begin(),
                        source.atoms.end());
    std::sort(target.atoms.begin(), target.atoms.end());
    target.atoms.erase(std::unique(target.atoms.begin(), target.atoms.end()),
                       target.atoms.end());
  }

  BooleanExpression boolean_expression(EffectRef effect,
                                       EffectAtomId Atom) const {
    Atom = canonical_atom(Atom);
    effect = resolve_binding(effect);
    const auto &node = nodes[effect.index];
    BooleanExpression result;
    switch (node.kind) {
    case EffectNodeKind::Empty:
      break;
    case EffectNodeKind::Atoms: {
      auto Atoms = node.atoms;
      canonicalize_atoms(Atoms);
      result.constant = std::binary_search(Atoms.begin(), Atoms.end(), Atom);
      break;
    }
    case EffectNodeKind::FlexibleMeta:
    case EffectNodeKind::RigidOpaque:
      result.atoms.push_back(effect_variable_atom(node.variable));
      break;
    case EffectNodeKind::DerivedLeast:
      result.atoms.push_back(derived_atom(effect.index));
      break;
    case EffectNodeKind::Join:
      for (const auto input : node.inputs)
        merge_boolean_expression(result, boolean_expression(input, Atom));
      break;
    case EffectNodeKind::Mask:
      if (!node.inputs.empty()) {
        auto Excluded = node.excluded_atoms;
        canonicalize_atoms(Excluded);
        if (!std::binary_search(Excluded.begin(), Excluded.end(), Atom))
          result = boolean_expression(node.inputs.front(), Atom);
      }
      break;
    }
    return result;
  }

  using DerivedFact = std::pair<std::uint32_t, EffectAtomId>;

  bool equality_constraints_satisfiable(
      std::vector<DerivedFact> *forced_facts = nullptr) const {
    if (equality_constraints.empty())
      return true;

    std::vector<EffectAtomId> Atoms;
    for (const auto &node : nodes) {
      Atoms.insert(Atoms.end(), node.atoms.begin(), node.atoms.end());
      Atoms.insert(Atoms.end(), node.excluded_atoms.begin(),
                   node.excluded_atoms.end());
    }
    canonicalize_atoms(Atoms);

    struct HornRule {
      std::vector<BooleanAtom> body;
      std::optional<BooleanAtom> head;
    };

    for (const auto Atom : Atoms) {
      std::vector<HornRule> rules;
      auto add_rule = [&](std::vector<BooleanAtom> body,
                          std::optional<BooleanAtom> head) {
        std::sort(body.begin(), body.end());
        body.erase(std::unique(body.begin(), body.end()), body.end());
        if (head && std::binary_search(body.begin(), body.end(), *head))
          return;
        rules.push_back(HornRule{std::move(body), head});
      };
      auto add_implication = [&](const BooleanExpression &antecedent,
                                 const BooleanExpression &consequent) {
        if (consequent.constant)
          return;
        if (antecedent.constant) {
          add_rule(consequent.atoms, std::nullopt);
          return;
        }
        for (const auto atom : antecedent.atoms)
          add_rule(consequent.atoms, atom);
      };

      for (std::uint32_t index = 0; index < nodes.size(); ++index) {
        const auto &node = nodes[index];
        if (node.kind != EffectNodeKind::DerivedLeast)
          continue;
        BooleanExpression definition;
        definition.constant = [&] {
          auto NodeAtoms = node.atoms;
          canonicalize_atoms(NodeAtoms);
          return std::binary_search(NodeAtoms.begin(), NodeAtoms.end(), Atom);
        }();
        for (const auto input : node.inputs)
          merge_boolean_expression(definition, boolean_expression(input, Atom));
        add_implication(definition,
                        BooleanExpression{false, {derived_atom(index)}});
      }

      for (const auto &[left, right] : equality_constraints) {
        const auto left_expression = boolean_expression(left, Atom);
        const auto right_expression = boolean_expression(right, Atom);
        add_implication(left_expression, right_expression);
        add_implication(right_expression, left_expression);
      }

      auto horn_satisfiable =
          [&](const std::vector<BooleanAtom> &assumed_false) {
            std::set<BooleanAtom> complemented_true(assumed_false.begin(),
                                                    assumed_false.end());
            bool changed = true;
            while (changed) {
              changed = false;
              for (const auto &rule : rules) {
                if (!std::all_of(rule.body.begin(), rule.body.end(),
                                 [&](BooleanAtom atom) {
                                   return complemented_true.contains(atom);
                                 }))
                  continue;
                if (!rule.head)
                  return false;
                changed =
                    complemented_true.insert(*rule.head).second || changed;
              }
            }
            return true;
          };

      if (!horn_satisfiable({}))
        return false;

      std::vector<BooleanAtom> non_forced_derived_atoms;
      for (std::uint32_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].kind != EffectNodeKind::DerivedLeast)
          continue;
        const auto atom = derived_atom(index);
        if (!horn_satisfiable({atom})) {
          if (forced_facts)
            forced_facts->emplace_back(index, Atom);
        } else {
          non_forced_derived_atoms.push_back(atom);
        }
      }

      // A DerivedLeast component is meaningful only when its satisfying
      // assignments have a least element.  If setting every non-entailed
      // derived atom to false is inconsistent, the constraints require an
      // arbitrary choice between incomparable derived cells.  Flexible and
      // opaque atoms remain unconstrained here and can carry a deferred row.
      if (!horn_satisfiable(non_forced_derived_atoms))
        return false;
    }
    return true;
  }

  EffectConstraintResult local_constraint_status(
      const std::pair<EffectRef, EffectRef> &constraint) const {
    const auto left = summarize_raw(constraint.first);
    const auto right = summarize_raw(constraint.second);
    if (left == right && !left.is_open() &&
        !contains_derived(constraint.first) &&
        !contains_derived(constraint.second))
      return EffectConstraintResult::Solved;
    return EffectConstraintResult::Deferred;
  }

  EffectConstraintResult settle_constraints() {
    std::vector<DerivedFact> forced_facts;
    if (!equality_constraints_satisfiable(&forced_facts))
      return EffectConstraintResult::Conflict;
    for (const auto &[index, Atom] : forced_facts) {
      auto &Atoms = nodes[index].atoms;
      Atoms.push_back(canonical_atom(Atom));
      canonicalize_atoms(Atoms);
    }
    for (const auto &constraint : equality_constraints)
      if (local_constraint_status(constraint) ==
          EffectConstraintResult::Deferred)
        return EffectConstraintResult::Deferred;
    return EffectConstraintResult::Solved;
  }

  EffectConstraintResult establish_equality(EffectRef left, EffectRef right) {
    require(left);
    require(right);

    auto ordered_left = left;
    auto ordered_right = right;
    if (ordered_right.index < ordered_left.index)
      std::swap(ordered_left, ordered_right);
    const auto constraint = std::make_pair(ordered_left, ordered_right);
    if (std::find(equality_constraints.begin(), equality_constraints.end(),
                  constraint) != equality_constraints.end()) {
      if (settle_constraints() == EffectConstraintResult::Conflict)
        return EffectConstraintResult::Conflict;
      return local_constraint_status(constraint);
    }

    std::vector<std::pair<EffectRef, EffectRef>> added_inputs;
    auto include_in_derived = [&](EffectRef source, EffectRef sink) {
      auto &node = nodes[sink.index];
      if (node.kind != EffectNodeKind::DerivedLeast)
        return;
      if (std::find(node.inputs.begin(), node.inputs.end(), source) ==
          node.inputs.end()) {
        node.inputs.push_back(source);
        std::sort(node.inputs.begin(), node.inputs.end(),
                  [](EffectRef first, EffectRef second) {
                    return first.index < second.index;
                  });
        added_inputs.emplace_back(source, sink);
      }
    };
    include_in_derived(right, left);
    include_in_derived(left, right);
    equality_constraints.push_back(constraint);

    if (settle_constraints() == EffectConstraintResult::Conflict) {
      equality_constraints.pop_back();
      for (const auto &[source, sink] : added_inputs) {
        auto &inputs = nodes[sink.index].inputs;
        inputs.erase(std::remove(inputs.begin(), inputs.end(), source),
                     inputs.end());
      }
      return EffectConstraintResult::Conflict;
    }
    ++generation;
    return local_constraint_status(constraint);
  }

  enum class BindResult {
    NotApplicable,
    Bound,
    BoundDeferred,
    Occurs,
    Conflict,
  };

  BindResult bind_flexible(EffectRef variable, EffectRef value) {
    auto &node = nodes[variable.index];
    if (node.kind != EffectNodeKind::FlexibleMeta || node.binding)
      return BindResult::NotApplicable;

    std::unordered_set<std::uint32_t> seen;
    if (occurs(node.variable, value, seen))
      return BindResult::Occurs;

    node.binding = value;
    const auto status = settle_constraints();
    if (status == EffectConstraintResult::Conflict) {
      node.binding.reset();
      return BindResult::Conflict;
    }
    ++generation;
    return status == EffectConstraintResult::Deferred
               ? BindResult::BoundDeferred
               : BindResult::Bound;
  }
};

EffectGraphTemplate::EffectGraphTemplate() = default;
EffectGraphTemplate::EffectGraphTemplate(std::shared_ptr<const Data> data)
    : data_(std::move(data)) {}
EffectGraphTemplate::EffectGraphTemplate(const EffectGraphTemplate &) = default;
EffectGraphTemplate::EffectGraphTemplate(EffectGraphTemplate &&) noexcept =
    default;
EffectGraphTemplate &
EffectGraphTemplate::operator=(const EffectGraphTemplate &) = default;
EffectGraphTemplate &
EffectGraphTemplate::operator=(EffectGraphTemplate &&) noexcept = default;
EffectGraphTemplate::~EffectGraphTemplate() = default;

EffectSolver::EffectSolver(EffectAtomCatalogDomain AtomDomain,
                           EffectAtomMaskValidator MaskValidator)
    : impl_(std::make_unique<Impl>(AtomDomain, std::move(MaskValidator))) {}
EffectSolver::EffectSolver(EffectSolver &&) noexcept = default;
EffectSolver &EffectSolver::operator=(EffectSolver &&) noexcept = default;
EffectSolver::~EffectSolver() = default;

EffectRef EffectSolver::empty() const noexcept { return impl_->empty_ref(); }

EffectAtomCatalogDomain EffectSolver::atomDomain() const noexcept {
  return impl_->atom_domain;
}

EffectRef EffectSolver::atoms(std::vector<EffectAtomId> Atoms) {
  for (const auto Atom : Atoms)
    impl_->ensure_atom(Atom);
  impl_->canonicalize_atoms(Atoms);
  if (Atoms.empty())
    return empty();
  if (const auto found = impl_->atom_cache.find(Atoms);
      found != impl_->atom_cache.end())
    return found->second;
  Impl::Node node;
  node.kind = EffectNodeKind::Atoms;
  node.atoms = Atoms;
  const auto result = impl_->append(std::move(node));
  impl_->atom_cache.emplace(std::move(Atoms), result);
  return result;
}

EffectRef EffectSolver::atoms(std::initializer_list<EffectAtomId> Atoms) {
  return this->atoms(std::vector<EffectAtomId>(Atoms));
}

EffectRef EffectSolver::flexible() {
  Impl::Node node;
  node.kind = EffectNodeKind::FlexibleMeta;
  node.variable = impl_->next_variable++;
  return impl_->append(std::move(node));
}

EffectRef EffectSolver::derived() {
  Impl::Node node;
  node.kind = EffectNodeKind::DerivedLeast;
  return impl_->append(std::move(node));
}

EffectRef EffectSolver::opaque() {
  Impl::Node node;
  node.kind = EffectNodeKind::RigidOpaque;
  node.variable = impl_->next_variable++;
  return impl_->append(std::move(node));
}

EffectRef EffectSolver::join(std::vector<EffectRef> effects) {
  std::vector<EffectRef> flattened;
  std::function<void(EffectRef)> append = [&](EffectRef effect) {
    effect = impl_->resolve_binding(effect);
    if (effect == empty())
      return;
    const auto &node = impl_->nodes[effect.index];
    if (node.kind == EffectNodeKind::Join) {
      for (const auto input : node.inputs)
        append(input);
      return;
    }
    flattened.push_back(effect);
  };
  for (const auto effect : effects)
    append(effect);
  std::sort(
      flattened.begin(), flattened.end(),
      [](EffectRef left, EffectRef right) { return left.index < right.index; });
  flattened.erase(std::unique(flattened.begin(), flattened.end()),
                  flattened.end());
  if (flattened.empty())
    return empty();
  if (flattened.size() == 1)
    return flattened.front();

  std::vector<std::uint32_t> key;
  key.reserve(flattened.size());
  for (const auto effect : flattened)
    key.push_back(effect.index);
  if (const auto found = impl_->join_cache.find(key);
      found != impl_->join_cache.end())
    return found->second;

  Impl::Node node;
  node.kind = EffectNodeKind::Join;
  node.inputs = std::move(flattened);
  const auto result = impl_->append(std::move(node));
  impl_->join_cache.emplace(std::move(key), result);
  return result;
}

EffectRef EffectSolver::join(std::initializer_list<EffectRef> effects) {
  return join(std::vector<EffectRef>(effects));
}

EffectRef EffectSolver::mask(EffectRef source,
                             std::vector<EffectAtomId> ExcludedAtoms) {
  source = impl_->resolve_binding(source);
  for (const auto Atom : ExcludedAtoms) {
    if (impl_->mask_validator && !impl_->mask_validator(Atom))
      throw std::invalid_argument(
          "effect masks require published operation atoms");
    impl_->ensure_atom(Atom);
  }
  impl_->canonicalize_atoms(ExcludedAtoms);
  if (source == empty() || ExcludedAtoms.empty())
    return source;

  const auto &source_node = impl_->nodes[source.index];
  if (source_node.kind == EffectNodeKind::Mask && !source_node.inputs.empty()) {
    merge_atoms(ExcludedAtoms, source_node.excluded_atoms);
    impl_->canonicalize_atoms(ExcludedAtoms);
    source = source_node.inputs.front();
  }

  const auto key = std::make_pair(source.index, ExcludedAtoms);
  if (const auto found = impl_->mask_cache.find(key);
      found != impl_->mask_cache.end())
    return found->second;

  Impl::Node node;
  node.kind = EffectNodeKind::Mask;
  node.inputs.push_back(source);
  node.excluded_atoms = ExcludedAtoms;
  const auto result = impl_->append(std::move(node));
  impl_->mask_cache.emplace(key, result);
  return result;
}

EffectRef
EffectSolver::mask(EffectRef source,
                   std::initializer_list<EffectAtomId> ExcludedAtoms) {
  return mask(source, std::vector<EffectAtomId>(ExcludedAtoms));
}

EffectConstraintResult EffectSolver::addAtom(EffectRef Derived,
                                             EffectAtomId Atom) {
  impl_->ensure_atom(Atom);
  Atom = impl_->canonical_atom(Atom);
  Derived = impl_->resolve_binding(Derived);
  if (impl_->nodes[Derived.index].kind != EffectNodeKind::DerivedLeast)
    throw std::invalid_argument(
        "effect atoms can only be added to a derived cell");
  auto &Atoms = impl_->nodes[Derived.index].atoms;
  const auto Previous = Atoms;
  Atoms.push_back(Atom);
  impl_->canonicalize_atoms(Atoms);
  const auto status = impl_->settle_constraints();
  if (status == EffectConstraintResult::Conflict)
    Atoms = Previous;
  else if (Atoms != Previous)
    ++impl_->generation;
  return status;
}

EffectConstraintResult
EffectSolver::materializeMaskMatches(EffectRef Root,
                                     const EffectMaskMatcher &Match) {
  impl_->require(Root);
  if (!Match)
    throw std::invalid_argument("effect mask matcher is empty");

  std::unordered_set<std::uint32_t> Reachable;
  const auto Visit = [&](const auto &Self, EffectRef Current) -> void {
    impl_->require(Current);
    if (!Reachable.insert(Current.index).second)
      return;
    const auto &Node = impl_->nodes[Current.index];
    if (Node.binding)
      Self(Self, *Node.binding);
    for (const auto Input : Node.inputs)
      Self(Self, Input);
  };
  Visit(Visit, Root);
  bool Expanded = true;
  while (Expanded) {
    const auto Before = Reachable.size();
    for (const auto &[Left, Right] : impl_->equality_constraints) {
      if (Reachable.contains(Left.index) || Reachable.contains(Right.index)) {
        Visit(Visit, Left);
        Visit(Visit, Right);
      }
    }
    Expanded = Reachable.size() != Before;
  }

  struct Change {
    std::uint32_t Index;
    std::vector<EffectAtomId> Before;
    std::vector<EffectAtomId> After;
  };
  std::vector<Change> Changes;
  for (const auto Index : Reachable) {
    const auto &Node = impl_->nodes[Index];
    if (Node.kind != EffectNodeKind::Mask || Node.inputs.empty() ||
        Node.excluded_atoms.empty())
      continue;
    auto Exclusions = Node.excluded_atoms;
    const auto Source = summarize(Node.inputs.front());
    for (const auto Pattern : Node.excluded_atoms)
      for (const auto Candidate : Source.known_atoms)
        if (Match(impl_->canonical_atom(Pattern),
                  impl_->canonical_atom(Candidate))) {
          if (impl_->mask_validator &&
              !impl_->mask_validator(impl_->canonical_atom(Candidate)))
            throw std::invalid_argument(
                "effect mask matcher selected a non-operation atom");
          Exclusions.push_back(Candidate);
        }
    impl_->canonicalize_atoms(Exclusions);
    if (Exclusions != Node.excluded_atoms)
      Changes.push_back({Index, Node.excluded_atoms, std::move(Exclusions)});
  }
  if (Changes.empty())
    return EffectConstraintResult::Solved;

  for (const auto &Change : Changes)
    impl_->nodes[Change.Index].excluded_atoms = Change.After;
  impl_->mask_cache.clear();
  const auto Result = impl_->settle_constraints();
  if (Result == EffectConstraintResult::Conflict) {
    for (const auto &Change : Changes)
      impl_->nodes[Change.Index].excluded_atoms = Change.Before;
    impl_->mask_cache.clear();
    return Result;
  }
  ++impl_->generation;
  return Result;
}

EffectConstraintResult EffectSolver::equateAtoms(EffectAtomId Left,
                                                 EffectAtomId Right) {
  impl_->ensure_atom(Left);
  impl_->ensure_atom(Right);
  Left = impl_->canonical_atom(Left);
  Right = impl_->canonical_atom(Right);
  if (Left == Right)
    return EffectConstraintResult::Solved;
  if (Right < Left)
    std::swap(Left, Right);

  const auto PreviousParents = impl_->atom_parents;
  impl_->atom_parents[Right] = Left;
  const auto Status = impl_->settle_constraints();
  if (Status == EffectConstraintResult::Conflict) {
    impl_->atom_parents = PreviousParents;
    return Status;
  }
  impl_->atom_cache.clear();
  impl_->mask_cache.clear();
  ++impl_->generation;
  return Status;
}

EffectAtomId EffectSolver::canonicalAtom(EffectAtomId Atom) const {
  return impl_->canonical_atom(Atom);
}

EffectConstraintResult EffectSolver::include(EffectRef source,
                                             EffectRef derived) {
  impl_->require(source);
  derived = impl_->resolve_binding(derived);
  if (impl_->nodes[derived.index].kind != EffectNodeKind::DerivedLeast)
    throw std::invalid_argument("effect inclusion sink must be a derived cell");
  auto &inputs = impl_->nodes[derived.index].inputs;
  const auto found = std::find(inputs.begin(), inputs.end(), source);
  if (found == inputs.end()) {
    inputs.push_back(source);
    std::sort(inputs.begin(), inputs.end(),
              [](EffectRef left, EffectRef right) {
                return left.index < right.index;
              });
    const auto status = impl_->settle_constraints();
    if (status == EffectConstraintResult::Conflict)
      inputs.erase(std::remove(inputs.begin(), inputs.end(), source),
                   inputs.end());
    else
      ++impl_->generation;
    return status;
  }
  return impl_->settle_constraints();
}

EffectConstraintResult EffectSolver::equate(EffectRef left, EffectRef right) {
  left = impl_->resolve_binding(left);
  right = impl_->resolve_binding(right);
  if (left == right)
    return EffectConstraintResult::Solved;

  const auto bind_left = impl_->bind_flexible(left, right);
  if (bind_left == Impl::BindResult::Bound)
    return EffectConstraintResult::Solved;
  if (bind_left == Impl::BindResult::BoundDeferred)
    return EffectConstraintResult::Deferred;
  if (bind_left == Impl::BindResult::Conflict)
    return EffectConstraintResult::Conflict;
  if (bind_left == Impl::BindResult::NotApplicable) {
    const auto bind_right = impl_->bind_flexible(right, left);
    if (bind_right == Impl::BindResult::Bound)
      return EffectConstraintResult::Solved;
    if (bind_right == Impl::BindResult::BoundDeferred)
      return EffectConstraintResult::Deferred;
    if (bind_right == Impl::BindResult::Conflict)
      return EffectConstraintResult::Conflict;
  }

  return impl_->establish_equality(left, right);
}

EffectNormalForm EffectSolver::summarize(EffectRef effect) const {
  std::unordered_set<std::uint32_t> active;
  auto result = impl_->summarize_impl(effect, active, true);
  impl_->canonicalize_atoms(result.known_atoms);
  for (auto &Tail : result.tails)
    impl_->canonicalize_atoms(Tail.excluded_atoms);
  normalize_summary(result);
  return result;
}

std::optional<EffectVarId> EffectSolver::variable_id(EffectRef effect) const {
  impl_->require(effect);
  const auto &node = impl_->nodes[effect.index];
  if (node.kind == EffectNodeKind::FlexibleMeta ||
      node.kind == EffectNodeKind::RigidOpaque)
    return node.variable;
  return std::nullopt;
}

EffectGraphTemplate EffectSolver::freeze(EffectRef root) const {
  return freeze(std::vector<EffectRef>{root});
}

EffectGraphTemplate EffectSolver::freeze(std::vector<EffectRef> roots) const {
  if (roots.empty())
    throw std::invalid_argument("cannot freeze an empty effect root set");
  for (const auto root : roots)
    impl_->require(root);
  std::set<std::uint32_t> reachable;
  std::function<void(EffectRef)> visit = [&](EffectRef effect) {
    impl_->require(effect);
    if (!reachable.insert(effect.index).second)
      return;
    const auto &node = impl_->nodes[effect.index];
    if (node.binding)
      visit(*node.binding);
    for (const auto input : node.inputs)
      visit(input);
  };
  for (const auto root : roots)
    visit(root);

  bool changed = true;
  while (changed) {
    const auto before = reachable.size();
    for (const auto &[left, right] : impl_->equality_constraints) {
      auto touches_reachable = [&](EffectRef effect) {
        std::unordered_set<std::uint32_t> seen;
        std::function<bool(EffectRef)> touches = [&](EffectRef current) {
          impl_->require(current);
          if (reachable.contains(current.index))
            return true;
          if (!seen.insert(current.index).second)
            return false;
          const auto &node = impl_->nodes[current.index];
          if (node.binding && touches(*node.binding))
            return true;
          return std::any_of(node.inputs.begin(), node.inputs.end(), touches);
        };
        return touches(effect);
      };
      if (touches_reachable(left) || touches_reachable(right)) {
        visit(left);
        visit(right);
      }
    }
    changed = reachable.size() != before;
  }

  auto data = std::make_shared<EffectGraphTemplate::Data>();
  data->atom_domain = impl_->atom_domain;
  data->source_owner = impl_->owner;
  std::unordered_map<std::uint32_t, std::uint32_t> local_index;
  for (const auto index : reachable) {
    local_index[index] = static_cast<std::uint32_t>(data->nodes.size());
    data->nodes.emplace_back();
  }
  for (const auto index : reachable) {
    const auto &source = impl_->nodes[index];
    auto &target = data->nodes[local_index.at(index)];
    target.kind = source.kind;
    target.original_index = index;
    target.original_variable = source.variable;
    target.atoms = source.atoms;
    target.excluded_atoms = source.excluded_atoms;
    impl_->canonicalize_atoms(target.atoms);
    impl_->canonicalize_atoms(target.excluded_atoms);
    for (const auto input : source.inputs)
      target.inputs.push_back(local_index.at(input.index));
    if (source.binding)
      target.binding = local_index.at(source.binding->index);
  }
  data->roots.reserve(roots.size());
  for (const auto root : roots)
    data->roots.push_back(local_index.at(root.index));
  for (const auto &[left, right] : impl_->equality_constraints)
    if (reachable.contains(left.index) && reachable.contains(right.index))
      data->equalities.emplace_back(local_index.at(left.index),
                                    local_index.at(right.index));
  return EffectGraphTemplate(std::move(data));
}

struct EffectSolver::InstantiationTransaction::State {
  EffectSolver *Solver = nullptr;
  std::uint64_t Generation = 0;
  EffectVarId NextVariable = 0;
  std::vector<EffectSolver::Impl::Node> Nodes;
  std::vector<std::pair<EffectRef, EffectRef>> Equalities;
  EffectInstantiation Result;
  std::optional<EffectAtomId> MaxAtom;
  bool Prepared = false;
  bool Committed = false;
};

EffectSolver::InstantiationTransaction::InstantiationTransaction(
    std::unique_ptr<State> State)
    : State_(std::move(State)) {}
EffectSolver::InstantiationTransaction::InstantiationTransaction(
    InstantiationTransaction &&) noexcept = default;
EffectSolver::InstantiationTransaction &
EffectSolver::InstantiationTransaction::operator=(
    InstantiationTransaction &&) noexcept = default;
EffectSolver::InstantiationTransaction::~InstantiationTransaction() = default;

std::expected<void, std::string>
EffectSolver::InstantiationTransaction::prepareCommit(
    const std::function<bool(EffectAtomId)> &ValidAtom,
    const std::function<bool(EffectAtomId)> &ValidMaskAtom) {
  if (!State_ || !State_->Solver)
    return std::unexpected("effect instantiation transaction is empty");
  if (State_->Prepared || State_->Committed)
    return std::unexpected("effect instantiation transaction already prepared");
  auto &Impl = *State_->Solver->impl_;
  if (Impl.generation != State_->Generation)
    return std::unexpected("effect solver changed during instantiation");
  for (const auto &Node : State_->Nodes) {
    for (const auto Atom : Node.atoms) {
      if (!ValidAtom(Atom))
        return std::unexpected("instantiated effect atom is not staged");
      State_->MaxAtom = std::max(State_->MaxAtom.value_or(Atom), Atom);
    }
    for (const auto Atom : Node.excluded_atoms) {
      if (!ValidAtom(Atom))
        return std::unexpected("instantiated mask atom is not staged");
      if (!ValidMaskAtom(Atom))
        return std::unexpected(
            "instantiated mask atom is not an operation atom");
      State_->MaxAtom = std::max(State_->MaxAtom.value_or(Atom), Atom);
    }
  }
  try {
    Impl.nodes.reserve(Impl.nodes.size() + State_->Nodes.size());
    Impl.equality_constraints.reserve(Impl.equality_constraints.size() +
                                      State_->Equalities.size());
    if (State_->MaxAtom)
      Impl.atom_parents.reserve(static_cast<std::size_t>(*State_->MaxAtom) + 1);
  } catch (const std::bad_alloc &) {
    return std::unexpected("unable to reserve effect graph storage");
  }
  State_->Prepared = true;
  return {};
}

void EffectSolver::InstantiationTransaction::commitPrepared() noexcept {
  if (!State_ || !State_->Prepared || State_->Committed)
    std::terminate();
  auto &Impl = *State_->Solver->impl_;
  if (State_->MaxAtom)
    while (Impl.atom_parents.size() <= *State_->MaxAtom)
      Impl.atom_parents.push_back(
          static_cast<EffectAtomId>(Impl.atom_parents.size()));
  for (auto &Node : State_->Nodes)
    Impl.nodes.push_back(std::move(Node));
  for (const auto Equality : State_->Equalities)
    Impl.equality_constraints.push_back(Equality);
  Impl.next_variable = State_->NextVariable;
  ++Impl.generation;
  State_->Committed = true;
}

const EffectInstantiation &
EffectSolver::InstantiationTransaction::result() const {
  if (!State_ || !State_->Committed)
    throw std::logic_error(
        "effect instantiation result is unavailable before commit");
  return State_->Result;
}

std::expected<EffectSolver::InstantiationTransaction, std::string>
EffectSolver::beginInstantiation(const EffectGraphTemplate &Graph,
                                 EffectAtomInstantiator InstantiateAtom) {
  if (!Graph.data_)
    return std::unexpected("cannot instantiate an empty effect template");
  const auto &Data = *Graph.data_;
  if (Data.atom_domain != impl_->atom_domain)
    return std::unexpected("effect template belongs to another atom catalog");

  auto State = std::make_unique<InstantiationTransaction::State>();
  State->Solver = this;
  State->Generation = impl_->generation;
  State->NextVariable = impl_->next_variable;
  std::vector<EffectRef> Mapping(Data.nodes.size());
  State->Nodes.reserve(Data.nodes.size());

  for (std::size_t Index = 0; Index < Data.nodes.size(); ++Index) {
    const auto &Source = Data.nodes[Index];
    if (Source.kind == EffectNodeKind::Empty) {
      Mapping[Index] = empty();
    } else {
      const auto NewIndex =
          static_cast<std::uint32_t>(impl_->nodes.size() + State->Nodes.size());
      Mapping[Index] = EffectRef{impl_->owner, NewIndex};
      Impl::Node Node;
      Node.kind = Source.kind;
      if (Source.kind == EffectNodeKind::FlexibleMeta ||
          Source.kind == EffectNodeKind::RigidOpaque) {
        Node.variable = State->NextVariable++;
        State->Result.variables.emplace(Source.original_variable,
                                        Mapping[Index]);
      }
      State->Nodes.push_back(std::move(Node));
    }
    State->Result.references.emplace(
        EffectRef{Data.source_owner, Source.original_index}, Mapping[Index]);
  }

  std::unordered_map<EffectAtomId, EffectAtomId> AtomMapping;
  const auto InstantiateAtoms = [&](const std::vector<EffectAtomId> &Source,
                                    std::vector<EffectAtomId> &Destination)
      -> std::expected<void, std::string> {
    Destination.reserve(Source.size());
    for (const auto Raw : Source) {
      const auto Atom = canonicalAtom(Raw);
      if (const auto Found = AtomMapping.find(Atom);
          Found != AtomMapping.end()) {
        Destination.push_back(Found->second);
        continue;
      }
      auto Instantiated = InstantiateAtom(Atom, State->Result.references);
      if (!Instantiated)
        return std::unexpected(Instantiated.error());
      const auto Canonical = canonicalAtom(*Instantiated);
      AtomMapping.emplace(Atom, Canonical);
      Destination.push_back(Canonical);
    }
    impl_->canonicalize_atoms(Destination);
    return {};
  };

  for (std::size_t Index = 0; Index < Data.nodes.size(); ++Index) {
    const auto &Source = Data.nodes[Index];
    if (Source.kind == EffectNodeKind::Empty)
      continue;
    auto &Target = State->Nodes[Mapping[Index].index - impl_->nodes.size()];
    if (auto Result = InstantiateAtoms(Source.atoms, Target.atoms); !Result)
      return std::unexpected(Result.error());
    if (auto Result =
            InstantiateAtoms(Source.excluded_atoms, Target.excluded_atoms);
        !Result)
      return std::unexpected(Result.error());
    for (const auto Input : Source.inputs)
      Target.inputs.push_back(Mapping.at(Input));
    if (Source.binding)
      Target.binding = Mapping.at(*Source.binding);
  }
  for (const auto &[Left, Right] : Data.equalities)
    State->Equalities.emplace_back(Mapping.at(Left), Mapping.at(Right));
  for (const auto Root : Data.roots)
    State->Result.roots.push_back(Mapping.at(Root));
  if (State->Result.roots.empty())
    return std::unexpected("effect graph template has no roots");
  State->Result.root = State->Result.roots.front();
  return InstantiationTransaction(std::move(State));
}

EffectInstantiation
EffectSolver::instantiate(const EffectGraphTemplate &Graph) {
  auto Transaction = beginInstantiation(
      Graph,
      [](EffectAtomId Atom, const EffectReferenceMap &)
          -> std::expected<EffectAtomId, std::string> { return Atom; });
  if (!Transaction)
    throw std::invalid_argument(Transaction.error());
  const auto AlwaysValid = [](EffectAtomId) { return true; };
  const auto ValidMaskAtom = [&](EffectAtomId Atom) {
    return !impl_->mask_validator || impl_->mask_validator(Atom);
  };
  if (auto Prepared = Transaction->prepareCommit(AlwaysValid, ValidMaskAtom);
      !Prepared)
    throw std::logic_error(Prepared.error());
  Transaction->commitPrepared();
  return Transaction->result();
}

} // namespace yona::compiler::typechecker
