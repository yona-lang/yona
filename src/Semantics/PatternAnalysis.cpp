#include "yona/Semantics/PatternAnalysis.h"

#include <algorithm>
#include <unordered_set>

namespace yona::compiler::pattern_analysis {
using ast::AstNode;
using ast::CaseClause;
using ast::CaseExpr;
using ast::LiteralExpr;
using ast::PatternNode;
using ast::AsDataStructurePattern;
using ast::AST_AS_DATA_STRUCTURE_PATTERN;
using ast::AST_BYTE_EXPR;
using ast::AST_CONSTRUCTOR_PATTERN;
using ast::AST_FALSE_LITERAL_EXPR;
using ast::AST_FLOAT_EXPR;
using ast::AST_HEAD_TAILS_PATTERN;
using ast::AST_CHARACTER_EXPR;
using ast::AST_INTEGER_EXPR;
using ast::AST_OR_PATTERN;
using ast::AST_PATTERN_VALUE;
using ast::AST_RECORD_PATTERN;
using ast::AST_SEQ_PATTERN;
using ast::AST_STRING_EXPR;
using ast::AST_TRUE_LITERAL_EXPR;
using ast::AST_TUPLE_PATTERN;
using ast::AST_UNDERSCORE_NODE;
using ast::AST_UNDERSCORE_PATTERN;
using ast::AST_UNIT_EXPR;
using ast::ByteExpr;
using ast::ConstructorPattern;
using ast::FloatExpr;
using ast::HeadTailsPattern;
using ast::CharacterExpr;
using ast::IdentifierExpr;
using ast::IntegerExpr;
using ast::OrPattern;
using ast::PatternValue;
using ast::RecordPattern;
using ast::SeqPattern;
using ast::StringExpr;
using ast::SymbolExpr;
using ast::TuplePattern;

namespace {
PatternNode *strip_alias(PatternNode *pattern) {
  while (pattern && pattern->get_type() == AST_AS_DATA_STRUCTURE_PATTERN)
    pattern = static_cast<AsDataStructurePattern *>(pattern)->pattern;
  return pattern;
}
bool is_catch_all(PatternNode *pattern) {
  pattern = strip_alias(pattern);
  return pattern && (pattern->get_type() == AST_UNDERSCORE_PATTERN ||
                     pattern->get_type() == AST_UNDERSCORE_NODE ||
                     (pattern->get_type() == AST_PATTERN_VALUE &&
                      std::get_if<IdentifierExpr *>(
                          &static_cast<PatternValue *>(pattern)->expr)));
}
std::optional<std::string_view> constructor_name(PatternNode *pattern) {
  pattern = strip_alias(pattern);
  if (!pattern)
    return std::nullopt;
  if (pattern->get_type() == AST_CONSTRUCTOR_PATTERN)
    return static_cast<ConstructorPattern *>(pattern)->constructor_name;
  if (pattern->get_type() == AST_RECORD_PATTERN)
    return static_cast<RecordPattern *>(pattern)->recordType;
  return std::nullopt;
}
bool equal_literal(const AstNode *left, const AstNode *right) {
  if (!left || !right || left->get_type() != right->get_type())
    return false;
  switch (left->get_type()) {
  case AST_INTEGER_EXPR:
    return static_cast<const IntegerExpr *>(left)->value ==
           static_cast<const IntegerExpr *>(right)->value;
  case AST_BYTE_EXPR:
    return static_cast<const ByteExpr *>(left)->value ==
           static_cast<const ByteExpr *>(right)->value;
  case AST_FLOAT_EXPR:
    return static_cast<const FloatExpr *>(left)->value ==
           static_cast<const FloatExpr *>(right)->value;
  case AST_STRING_EXPR:
    return static_cast<const StringExpr *>(left)->value ==
           static_cast<const StringExpr *>(right)->value;
  case AST_CHARACTER_EXPR:
    return static_cast<const CharacterExpr *>(left)->value ==
           static_cast<const CharacterExpr *>(right)->value;
  case AST_TRUE_LITERAL_EXPR:
  case AST_FALSE_LITERAL_EXPR:
  case AST_UNIT_EXPR:
    return true;
  default:
    return false;
  }
}
} // namespace

bool covers(PatternNode *cover, PatternNode *candidate) {
  cover = strip_alias(cover);
  candidate = strip_alias(candidate);
  if (!cover || !candidate)
    return false;
  if (is_catch_all(cover))
    return true;
  if (cover->get_type() == AST_PATTERN_VALUE) {
    auto *value = static_cast<PatternValue *>(cover);
    if (std::get_if<IdentifierExpr *>(&value->expr))
      return true;
    if (candidate->get_type() != AST_PATTERN_VALUE)
      return false;
    auto *other = static_cast<PatternValue *>(candidate);
    if (auto *left =
            std::get_if<LiteralExpr<std::nullptr_t> *>(&value->expr)) {
      auto *right =
          std::get_if<LiteralExpr<std::nullptr_t> *>(&other->expr);
      return right && static_cast<AstNode *>(*left)->get_type() ==
                          static_cast<AstNode *>(*right)->get_type();
    }
    if (auto *left = std::get_if<LiteralExpr<void *> *>(&value->expr)) {
      auto *right = std::get_if<LiteralExpr<void *> *>(&other->expr);
      if (!right)
        return false;
      auto *lhs = static_cast<AstNode *>(*left);
      auto *rhs = static_cast<AstNode *>(*right);
      return equal_literal(lhs, rhs);
    }
    if (auto *left = std::get_if<SymbolExpr *>(&value->expr)) {
      auto *right = std::get_if<SymbolExpr *>(&other->expr);
      return right && (*left)->value == (*right)->value;
    }
    return false;
  }
  if (cover->get_type() == AST_OR_PATTERN) {
    for (const auto &alternative : static_cast<OrPattern *>(cover)->patterns)
      if (covers(alternative.get(), candidate))
        return true;
    return false;
  }
  if (cover->get_type() == AST_CONSTRUCTOR_PATTERN &&
      candidate->get_type() == AST_CONSTRUCTOR_PATTERN) {
    auto *left = static_cast<ConstructorPattern *>(cover);
    auto *right = static_cast<ConstructorPattern *>(candidate);
    if (left->constructor_name != right->constructor_name ||
        left->sub_patterns.size() != right->sub_patterns.size())
      return false;
    for (size_t i = 0; i < left->sub_patterns.size(); ++i)
      if (!covers(left->sub_patterns[i], right->sub_patterns[i]))
        return false;
    return true;
  }
  auto covers_product = [&](const auto &left, const auto &right) {
    if (left->patterns.size() != right->patterns.size())
      return false;
    for (size_t i = 0; i < left->patterns.size(); ++i)
      if (!covers(left->patterns[i], right->patterns[i]))
        return false;
    return true;
  };
  if (cover->get_type() == AST_TUPLE_PATTERN &&
      candidate->get_type() == AST_TUPLE_PATTERN)
    return covers_product(static_cast<TuplePattern *>(cover),
                          static_cast<TuplePattern *>(candidate));
  if (cover->get_type() == AST_SEQ_PATTERN &&
      candidate->get_type() == AST_SEQ_PATTERN)
    return covers_product(static_cast<SeqPattern *>(cover),
                          static_cast<SeqPattern *>(candidate));
  if (cover->get_type() == AST_HEAD_TAILS_PATTERN) {
    auto *left = static_cast<HeadTailsPattern *>(cover);
    if (candidate->get_type() == AST_SEQ_PATTERN) {
      auto *right = static_cast<SeqPattern *>(candidate);
      if (right->patterns.size() < left->heads.size() ||
          !is_catch_all(left->tail))
        return false;
      for (size_t i = 0; i < left->heads.size(); ++i)
        if (!covers(left->heads[i], right->patterns[i]))
          return false;
      return true;
    }
    if (candidate->get_type() == AST_HEAD_TAILS_PATTERN) {
      auto *right = static_cast<HeadTailsPattern *>(candidate);
      if (right->heads.size() < left->heads.size() || !is_catch_all(left->tail))
        return false;
      for (size_t i = 0; i < left->heads.size(); ++i)
        if (!covers(left->heads[i], right->heads[i]))
          return false;
      return true;
    }
  }
  return false;
}

Result analyze_case(const CaseExpr &node,
                    const ConstructorCatalog &constructors) {
  Result result;
  std::string family;
  bool has_wildcard = false, saw_bool = false, saw_unit = false;
  std::unordered_set<std::string> covered_constructors, covered_atoms;
  std::vector<PatternNode *> prior_unguarded;
  const auto identify_family = [&](const auto &self,
                                   PatternNode *pattern) -> void {
    pattern = strip_alias(pattern);
    if (!pattern || !family.empty())
      return;
    if (pattern->get_type() == AST_OR_PATTERN) {
      for (const auto &alt : static_cast<OrPattern *>(pattern)->patterns)
        self(self, alt.get());
      return;
    }
    if (const auto name = constructor_name(pattern))
      if (const auto info = constructors.lookup(*name))
        family = info->family;
  };
  const auto collect = [&](const auto &self, PatternNode *pattern) -> void {
    pattern = strip_alias(pattern);
    if (!pattern || has_wildcard)
      return;
    if (pattern->get_type() == AST_OR_PATTERN) {
      for (const auto &alt : static_cast<OrPattern *>(pattern)->patterns)
        self(self, alt.get());
      return;
    }
    if (is_catch_all(pattern)) {
      has_wildcard = true;
      return;
    }
    if (const auto name = constructor_name(pattern)) {
      covered_constructors.emplace(*name);
      if (const auto info = constructors.lookup(*name); info && family.empty())
        family = info->family;
      return;
    }
    if (pattern->get_type() == AST_PATTERN_VALUE)
      if (auto *literal = std::get_if<LiteralExpr<void *> *>(
              &static_cast<PatternValue *>(pattern)->expr)) {
        if ((*literal)->get_type() == AST_TRUE_LITERAL_EXPR) {
          saw_bool = true;
          covered_atoms.emplace("True");
        }
        if ((*literal)->get_type() == AST_FALSE_LITERAL_EXPR) {
          saw_bool = true;
          covered_atoms.emplace("False");
        }
        if ((*literal)->get_type() == AST_UNIT_EXPR) {
          saw_unit = true;
          covered_atoms.emplace("()");
        }
      }
  };
  for (size_t index = 0; index < node.clauses.size(); ++index) {
    auto *clause = node.clauses[index];
    if (!clause)
      continue;
    identify_family(identify_family, clause->pattern);
    if (clause->guard)
      continue;
    auto *candidate = strip_alias(clause->pattern);
    bool covered = false;
    if (candidate && candidate->get_type() == AST_OR_PATTERN) {
      covered = true;
      for (const auto &alt : static_cast<OrPattern *>(candidate)->patterns) {
        bool alt_covered = false;
        for (auto *prior : prior_unguarded)
          alt_covered = alt_covered || covers(prior, alt.get());
        covered = covered && alt_covered;
      }
    } else
      for (auto *prior : prior_unguarded)
        covered = covered || covers(prior, candidate);
    if (is_catch_all(candidate) && saw_bool && covered_atoms.count("True") &&
        covered_atoms.count("False"))
      covered = true;
    if (is_catch_all(candidate) && !family.empty()) {
      const auto members = constructors.members(family);
      bool complete = !members.empty();
      for (const auto &name : members)
        complete = complete && covered_constructors.count(name);
      covered = covered || complete;
    }
    const bool before_wildcard = has_wildcard;
    collect(collect, candidate);
    if (covered || before_wildcard)
      result.unreachable_clauses.push_back(index);
    prior_unguarded.push_back(candidate);
  }
  if (has_wildcard)
    return result;
  if (!family.empty()) {
    std::vector<std::string> missing;
    for (const auto &name : constructors.members(family))
      if (!covered_constructors.count(name))
        missing.push_back(name);
    std::sort(missing.begin(), missing.end());
    if (!missing.empty())
      result.incomplete = FiniteCoverage{family, std::move(missing)};
  } else if (saw_bool) {
    std::vector<std::string> missing;
    for (const auto value : {"False", "True"})
      if (!covered_atoms.count(value))
        missing.emplace_back(value);
    if (!missing.empty())
      result.incomplete = FiniteCoverage{"Bool", std::move(missing)};
  } else if (saw_unit && !covered_atoms.count("()"))
    result.incomplete = FiniteCoverage{"Unit", {"()"}};
  return result;
}
} // namespace yona::compiler::pattern_analysis
