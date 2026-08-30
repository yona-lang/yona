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
  Labels,
  FlexibleMeta,
  DerivedLeast,
  RigidOpaque,
  Join,
  Mask,
};

std::atomic<std::uint64_t> next_effect_solver_owner{1};

void normalize_labels(std::vector<std::string> &labels) {
  std::sort(labels.begin(), labels.end());
  labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
}

void merge_labels(std::vector<std::string> &target,
                  const std::vector<std::string> &source) {
  target.insert(target.end(), source.begin(), source.end());
  normalize_labels(target);
}

void normalize_summary(EffectNormalForm &summary) {
  normalize_labels(summary.known_labels);
  for (auto &tail : summary.tails)
    normalize_labels(tail.excluded_labels);
  std::sort(
      summary.tails.begin(), summary.tails.end(),
      [](const EffectProjection &left, const EffectProjection &right) {
        return std::tie(left.variable, left.opaque, left.excluded_labels) <
               std::tie(right.variable, right.opaque, right.excluded_labels);
      });
  summary.tails.erase(std::unique(summary.tails.begin(), summary.tails.end()),
                      summary.tails.end());
}

void merge_summary(EffectNormalForm &target, const EffectNormalForm &source) {
  target.known_labels.insert(target.known_labels.end(),
                             source.known_labels.begin(),
                             source.known_labels.end());
  target.tails.insert(target.tails.end(), source.tails.begin(),
                      source.tails.end());
  normalize_summary(target);
}

} // namespace

struct EffectGraphTemplate::Data {
  struct Node {
    EffectNodeKind kind = EffectNodeKind::Empty;
    EffectVarId original_variable = 0;
    std::vector<std::string> labels;
    std::vector<std::uint32_t> inputs;
    std::optional<std::uint32_t> binding;
    std::vector<std::string> excluded_labels;
  };

  std::vector<Node> nodes;
  std::vector<std::uint32_t> roots;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> equalities;
};

struct EffectSolver::Impl {
  struct Node {
    EffectNodeKind kind = EffectNodeKind::Empty;
    EffectVarId variable = 0;
    std::vector<std::string> labels;
    std::vector<EffectRef> inputs;
    std::optional<EffectRef> binding;
    std::vector<std::string> excluded_labels;
  };

  std::uint64_t owner =
      next_effect_solver_owner.fetch_add(1, std::memory_order_relaxed);
  std::vector<Node> nodes{{}};
  EffectVarId next_variable = 0;
  std::map<std::vector<std::string>, EffectRef> label_cache;
  std::map<std::vector<std::uint32_t>, EffectRef> join_cache;
  std::map<std::pair<std::uint32_t, std::vector<std::string>>, EffectRef>
      mask_cache;
  std::vector<std::pair<EffectRef, EffectRef>> equality_constraints;

  Impl() {
    if (owner == 0)
      throw std::overflow_error("effect solver owner id space exhausted");
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
    case EffectNodeKind::Labels:
      return EffectNormalForm{node.labels, {}};
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
    case EffectNodeKind::Labels:
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
    case EffectNodeKind::Labels:
      result.known_labels = node.labels;
      break;
    case EffectNodeKind::FlexibleMeta:
      result.tails.push_back(EffectProjection{node.variable, {}, false});
      break;
    case EffectNodeKind::RigidOpaque:
      result.tails.push_back(EffectProjection{node.variable, {}, true});
      break;
    case EffectNodeKind::DerivedLeast:
    case EffectNodeKind::Join: {
      result.known_labels = node.labels;
      for (const auto input : node.inputs)
        merge_summary(result, summarize_impl(input, active, follow_equalities));
      break;
    }
    case EffectNodeKind::Mask: {
      result = node.inputs.empty() ? EffectNormalForm{}
                                   : summarize_impl(node.inputs.front(), active,
                                                    follow_equalities);
      std::erase_if(result.known_labels, [&](const std::string &label) {
        return std::binary_search(node.excluded_labels.begin(),
                                  node.excluded_labels.end(), label);
      });
      for (auto &tail : result.tails)
        merge_labels(tail.excluded_labels, node.excluded_labels);
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
                                       const std::string &label) const {
    effect = resolve_binding(effect);
    const auto &node = nodes[effect.index];
    BooleanExpression result;
    switch (node.kind) {
    case EffectNodeKind::Empty:
      break;
    case EffectNodeKind::Labels:
      result.constant =
          std::binary_search(node.labels.begin(), node.labels.end(), label);
      break;
    case EffectNodeKind::FlexibleMeta:
    case EffectNodeKind::RigidOpaque:
      result.atoms.push_back(effect_variable_atom(node.variable));
      break;
    case EffectNodeKind::DerivedLeast:
      result.atoms.push_back(derived_atom(effect.index));
      break;
    case EffectNodeKind::Join:
      for (const auto input : node.inputs)
        merge_boolean_expression(result, boolean_expression(input, label));
      break;
    case EffectNodeKind::Mask:
      if (!std::binary_search(node.excluded_labels.begin(),
                              node.excluded_labels.end(), label) &&
          !node.inputs.empty())
        result = boolean_expression(node.inputs.front(), label);
      break;
    }
    return result;
  }

  using DerivedFact = std::pair<std::uint32_t, std::string>;

  bool equality_constraints_satisfiable(
      std::vector<DerivedFact> *forced_facts = nullptr) const {
    if (equality_constraints.empty())
      return true;

    std::vector<std::string> labels;
    for (const auto &node : nodes) {
      labels.insert(labels.end(), node.labels.begin(), node.labels.end());
      labels.insert(labels.end(), node.excluded_labels.begin(),
                    node.excluded_labels.end());
    }
    normalize_labels(labels);

    struct HornRule {
      std::vector<BooleanAtom> body;
      std::optional<BooleanAtom> head;
    };

    for (const auto &label : labels) {
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
        definition.constant =
            std::binary_search(node.labels.begin(), node.labels.end(), label);
        for (const auto input : node.inputs)
          merge_boolean_expression(definition,
                                   boolean_expression(input, label));
        add_implication(definition,
                        BooleanExpression{false, {derived_atom(index)}});
      }

      for (const auto &[left, right] : equality_constraints) {
        const auto left_expression = boolean_expression(left, label);
        const auto right_expression = boolean_expression(right, label);
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
            forced_facts->emplace_back(index, label);
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
    for (const auto &[index, label] : forced_facts) {
      auto &labels = nodes[index].labels;
      labels.push_back(label);
      normalize_labels(labels);
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

EffectSolver::EffectSolver() : impl_(std::make_unique<Impl>()) {}
EffectSolver::EffectSolver(EffectSolver &&) noexcept = default;
EffectSolver &EffectSolver::operator=(EffectSolver &&) noexcept = default;
EffectSolver::~EffectSolver() = default;

EffectRef EffectSolver::empty() const noexcept { return impl_->empty_ref(); }

EffectRef EffectSolver::labels(std::vector<std::string> labels) {
  normalize_labels(labels);
  if (labels.empty())
    return empty();
  if (const auto found = impl_->label_cache.find(labels);
      found != impl_->label_cache.end())
    return found->second;
  Impl::Node node;
  node.kind = EffectNodeKind::Labels;
  node.labels = labels;
  const auto result = impl_->append(std::move(node));
  impl_->label_cache.emplace(std::move(labels), result);
  return result;
}

EffectRef EffectSolver::labels(std::initializer_list<std::string> labels) {
  return this->labels(std::vector<std::string>(labels));
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
                             std::vector<std::string> excluded_labels) {
  source = impl_->resolve_binding(source);
  normalize_labels(excluded_labels);
  if (source == empty() || excluded_labels.empty())
    return source;

  const auto &source_node = impl_->nodes[source.index];
  if (source_node.kind == EffectNodeKind::Mask && !source_node.inputs.empty()) {
    merge_labels(excluded_labels, source_node.excluded_labels);
    source = source_node.inputs.front();
  }

  const auto key = std::make_pair(source.index, excluded_labels);
  if (const auto found = impl_->mask_cache.find(key);
      found != impl_->mask_cache.end())
    return found->second;

  Impl::Node node;
  node.kind = EffectNodeKind::Mask;
  node.inputs.push_back(source);
  node.excluded_labels = excluded_labels;
  const auto result = impl_->append(std::move(node));
  impl_->mask_cache.emplace(key, result);
  return result;
}

EffectRef
EffectSolver::mask(EffectRef source,
                   std::initializer_list<std::string> excluded_labels) {
  return mask(source, std::vector<std::string>(excluded_labels));
}

EffectConstraintResult EffectSolver::add_label(EffectRef derived,
                                               std::string label) {
  derived = impl_->resolve_binding(derived);
  if (impl_->nodes[derived.index].kind != EffectNodeKind::DerivedLeast)
    throw std::invalid_argument(
        "effect labels can only be added to a derived cell");
  auto &labels = impl_->nodes[derived.index].labels;
  const auto previous = labels;
  labels.push_back(std::move(label));
  normalize_labels(labels);
  const auto status = impl_->settle_constraints();
  if (status == EffectConstraintResult::Conflict)
    labels = previous;
  return status;
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
  std::unordered_map<std::uint32_t, std::uint32_t> local_index;
  for (const auto index : reachable) {
    local_index[index] = static_cast<std::uint32_t>(data->nodes.size());
    data->nodes.emplace_back();
  }
  for (const auto index : reachable) {
    const auto &source = impl_->nodes[index];
    auto &target = data->nodes[local_index.at(index)];
    target.kind = source.kind;
    target.original_variable = source.variable;
    target.labels = source.labels;
    target.excluded_labels = source.excluded_labels;
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

EffectInstantiation
EffectSolver::instantiate(const EffectGraphTemplate &graph) {
  if (!graph.data_)
    throw std::invalid_argument("cannot instantiate an empty effect template");
  const auto &data = *graph.data_;
  std::vector<EffectRef> mapping(data.nodes.size());
  EffectInstantiation result;

  for (std::size_t index = 0; index < data.nodes.size(); ++index) {
    const auto &source = data.nodes[index];
    switch (source.kind) {
    case EffectNodeKind::Empty:
      mapping[index] = empty();
      break;
    case EffectNodeKind::FlexibleMeta:
      mapping[index] = flexible();
      result.variables[source.original_variable] = mapping[index];
      break;
    case EffectNodeKind::RigidOpaque:
      mapping[index] = opaque();
      result.variables[source.original_variable] = mapping[index];
      break;
    default: {
      Impl::Node node;
      node.kind = source.kind;
      node.labels = source.labels;
      node.excluded_labels = source.excluded_labels;
      mapping[index] = impl_->append(std::move(node));
      break;
    }
    }
  }

  for (std::size_t index = 0; index < data.nodes.size(); ++index) {
    if (data.nodes[index].kind == EffectNodeKind::Empty)
      continue;
    auto &target = impl_->nodes[mapping[index].index];
    target.inputs.clear();
    for (const auto input : data.nodes[index].inputs)
      target.inputs.push_back(mapping.at(input));
    if (data.nodes[index].binding)
      target.binding = mapping.at(*data.nodes[index].binding);
  }
  for (const auto &[left, right] : data.equalities)
    impl_->equality_constraints.emplace_back(mapping.at(left),
                                             mapping.at(right));
  result.roots.reserve(data.roots.size());
  for (const auto root : data.roots)
    result.roots.push_back(mapping.at(root));
  if (result.roots.empty())
    throw std::logic_error("effect graph template has no roots");
  result.root = result.roots.front();
  return result;
}

} // namespace yona::compiler::typechecker
