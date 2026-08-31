/// TypeChecker â€” core HM inference for Yona.
///
/// Walks the AST and infers types for every node. Supports:
/// - Literals (Int, Float, String, Bool, Symbol, Unit)
/// - Identifiers (env lookup with instantiation)
/// - Let bindings (with let-polymorphism / generalization)
/// - Functions (parameter inference from usage)
/// - Application (unify callee with Arrow(arg, result))
/// - If expressions (condition must be Bool, branches must unify)
/// - Binary operators (dispatched via env lookup)
/// - Tuples, sequences, do-blocks

#include "yona/Semantics/TypeChecker.h"

#include "yona/Interface/Reader.h"
#include "yona/Model/ModuleIdentity.h"
#include "yona/Semantics/BorrowEscapeAnalysis.h"
#include "yona/Semantics/ModuleFunctionDependencies.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace yona::compiler::typechecker {
using ast::ApplyExpr;
using ast::AstNode;
using ast::CaseExpr;
using ast::ExprCall;
using ast::ExprNode;
using ast::FunctionExpr;
using ast::IdentifierExpr;
using ast::LetExpr;
using ast::ModuleDecl;
using ast::NameCall;
using ast::PatternNode;
using ast::TupleExpr;
using ast::AsDataStructurePattern;
using ast::AST_ADD_EXPR;
using ast::AST_APPLY_EXPR;
using ast::AST_AS_DATA_STRUCTURE_PATTERN;
using ast::AST_BYTE_EXPR;
using ast::AST_CASE_EXPR;
using ast::AST_CONS_LEFT_EXPR;
using ast::AST_CONS_RIGHT_EXPR;
using ast::AST_CONSTRUCTOR_PATTERN;
using ast::AST_DICT_EXPR;
using ast::AST_DICT_GENERATOR_EXPR;
using ast::AST_DICT_PATTERN;
using ast::AST_DIVIDE_EXPR;
using ast::AST_DO_EXPR;
using ast::AST_EQ_EXPR;
using ast::AST_EXTERN_DECL;
using ast::AST_FALSE_LITERAL_EXPR;
using ast::AST_FIELD_ACCESS_EXPR;
using ast::AST_FIELD_UPDATE_EXPR;
using ast::AST_FLOAT_EXPR;
using ast::AST_FUNCTION_EXPR;
using ast::AST_GT_EXPR;
using ast::AST_GTE_EXPR;
using ast::AST_HANDLE_EXPR;
using ast::AST_HEAD_TAILS_PATTERN;
using ast::AST_CHARACTER_EXPR;
using ast::AST_IDENTIFIER_EXPR;
using ast::AST_IF_EXPR;
using ast::AST_IMPORT_EXPR;
using ast::AST_IN_EXPR;
using ast::AST_INTEGER_EXPR;
using ast::AST_JOIN_EXPR;
using ast::AST_LET_EXPR;
using ast::AST_LITERAL_EXPR;
using ast::AST_LOGICAL_AND_EXPR;
using ast::AST_LOGICAL_NOT_OP_EXPR;
using ast::AST_LOGICAL_OR_EXPR;
using ast::AST_LT_EXPR;
using ast::AST_LTE_EXPR;
using ast::AST_MAIN;
using ast::AST_MODULO_EXPR;
using ast::AST_MULTIPLY_EXPR;
using ast::AST_NEQ_EXPR;
using ast::AST_OR_PATTERN;
using ast::AST_PATTERN_VALUE;
using ast::AST_PERFORM_EXPR;
using ast::AST_PIPE_LEFT_EXPR;
using ast::AST_PIPE_RIGHT_EXPR;
using ast::AST_POWER_EXPR;
using ast::AST_RAISE_EXPR;
using ast::AST_RANGE_SEQUENCE_EXPR;
using ast::AST_RECORD_PATTERN;
using ast::AST_REMOVE_EXPR;
using ast::AST_SEQ_GENERATOR_EXPR;
using ast::AST_SEQ_PATTERN;
using ast::AST_SET_EXPR;
using ast::AST_SET_GENERATOR_EXPR;
using ast::AST_STRING_EXPR;
using ast::AST_SUBTRACT_EXPR;
using ast::AST_SYMBOL_EXPR;
using ast::AST_TRUE_LITERAL_EXPR;
using ast::AST_TRY_CATCH_EXPR;
using ast::AST_TUPLE_EXPR;
using ast::AST_TUPLE_PATTERN;
using ast::AST_TYPED_PATTERN;
using ast::AST_UNDERSCORE_PATTERN;
using ast::AST_UNIT_EXPR;
using ast::AST_VALUES_SEQUENCE_EXPR;
using ast::AST_WITH_EXPR;
using ast::AST_RECORD_LITERAL_EXPR;
using ast::AstNodeType;
using ast::LiteralExpr;
using ast::BinaryOpExpr;
using ast::BodyWithGuards;
using ast::BodyWithoutGuards;
using ast::CollectionExtractorExpr;
using ast::ConsLeftExpr;
using ast::ConsRightExpr;
using ast::ConstructorPattern;
using ast::DictExpr;
using ast::DictGeneratorExpr;
using ast::DictPattern;
using ast::DoExpr;
using ast::ExternDeclExpr;
using ast::FieldAccessExpr;
using ast::FieldType;
using ast::FieldUpdateExpr;
using ast::FqnExpr;
using ast::FunctionsImport;
using ast::HandleExpr;
using ast::HeadTailsPattern;
using ast::IfExpr;
using ast::ImportExpr;
using ast::InExpr;
using ast::JoinExpr;
using ast::KeyValueCollectionExtractorExpr;
using ast::LambdaAlias;
using ast::LogicalNotOpExpr;
using ast::MainNode;
using ast::ModuleImport;
using ast::OrPattern;
using ast::PatternAlias;
using ast::PatternValue;
using ast::PatternWithoutGuards;
using ast::PerformExpr;
using ast::RaiseExpr;
using ast::RecordLiteralExpr;
using ast::RecordPattern;
using ast::RemoveExpr;
using ast::SeqGeneratorExpr;
using ast::SeqPattern;
using ast::SetExpr;
using ast::SetGeneratorExpr;
using ast::SymbolExpr;
using ast::TryCatchExpr;
using ast::TuplePattern;
using ast::TypedPattern;
using ast::ValueAlias;
using ast::ValueCollectionExtractorExpr;
using ast::ValueExpr;
using ast::ValuesSequenceExpr;
using ast::WithExpr;

/// Simple edit distance for "did you mean?" suggestions.
static size_t edit_distance(const std::string &a, const std::string &b) {
  if (a.empty())
    return b.size();
  if (b.empty())
    return a.size();
  std::vector<std::vector<size_t>> dp(a.size() + 1,
                                      std::vector<size_t>(b.size() + 1));
  for (size_t i = 0; i <= a.size(); i++)
    dp[i][0] = i;
  for (size_t j = 0; j <= b.size(); j++)
    dp[0][j] = j;
  for (size_t i = 1; i <= a.size(); i++)
    for (size_t j = 1; j <= b.size(); j++)
      dp[i][j] =
          std::min({dp[i - 1][j] + 1, dp[i][j - 1] + 1,
                    dp[i - 1][j - 1] + (a[i - 1] != b[j - 1] ? 1u : 0u)});
  return dp[a.size()][b.size()];
}

static std::string fqn_to_string(FqnExpr *fqn) {
  if (!fqn || !fqn->moduleName)
    return {};
  std::string s;
  if (fqn->packageName.has_value()) {
    auto *pkg = fqn->packageName.value();
    for (size_t i = 0; i < pkg->parts.size(); i++) {
      if (i)
        s += "\\";
      s += pkg->parts[i]->value;
    }
    s += "\\";
  }
  s += fqn->moduleName->value;
  return s;
}

/// A parameterless module binding serializes as an implementation-only root
/// arrow at `$`; a source-visible thunk starts at `$/r`.  Rebase that visible
/// subtree without decoding and re-encoding individual effect entries: labels,
/// tail ids, and masks are part of the interface contract.
static std::optional<std::string>
normalize_zero_arity_thunk_scheme(std::string_view encoded) {
  if (encoded.empty())
    return std::nullopt;

  bool found_hidden_root = false;
  bool found_visible_root = false;
  std::string normalized;
  size_t start = 0;
  while (start <= encoded.size()) {
    const size_t end = encoded.find(';', start);
    const std::string_view field = encoded.substr(
        start,
        end == std::string_view::npos ? std::string_view::npos : end - start);
    if (field.empty())
      return std::nullopt;
    const size_t delimiter = field.find('#');
    if (delimiter == std::string_view::npos)
      return std::nullopt;
    const std::string_view path = field.substr(0, delimiter);
    if (path == "$") {
      if (found_hidden_root)
        return std::nullopt;
      found_hidden_root = true;
    } else if (path == "$/r" || path.starts_with("$/r/")) {
      found_visible_root = found_visible_root || path == "$/r";
      if (!normalized.empty())
        normalized += ';';
      normalized += '$';
      normalized.append(field.substr(3));
    } else {
      // A valid hidden module-binding graph contains only its root and the
      // source-visible return subtree. Preserve unknown graphs conservatively.
      return std::nullopt;
    }
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  if (!found_hidden_root || !found_visible_root)
    return std::nullopt;
  return normalized;
}

TypeChecker::TypeChecker(DiagnosticEngine &diag)
    : unifier_(arena_, uf_, diag), diag_(diag) {
  root_env_ = std::make_shared<TypeEnv>();
  register_builtins(*root_env_, arena_);
}

void TypeChecker::add_module_path(std::string path) {
  ModulePaths.push_back(std::move(path));
}

std::vector<std::string> TypeChecker::closed_effect_ops(MonoTypePtr type) {
  return effect_row_info(type).ops;
}

TypeChecker::EffectRowInfo TypeChecker::effect_row_info(MonoTypePtr type) {
  EffectRowInfo info;
  auto *z = unifier_.resolve(type);
  if (!z)
    return info;
  std::vector<EffectRef> stages;
  for (auto *stage = z; stage && stage->tag == MonoType::Arrow;
       stage = unifier_.resolve(stage->return_type)) {
    if (const auto effect = callee_effect(stage))
      stages.push_back(*effect);
  }
  if (!stages.empty()) {
    const auto summary = arena_.effect_solver().summarize(
        arena_.effect_solver().join(std::move(stages)));
    info.ops = summary.known_labels;
    info.open_rest = summary.is_open();
  } else if (z->tag == MonoType::Var) {
    // An unresolved function type has no proof of effect-freedom.
    info.open_rest = true;
  }
  if (z && z->tag == MonoType::Arrow) {
    auto *p = unifier_.resolve(z->param_type);
    info.hof = p && p->tag == MonoType::Arrow;
  }
  return info;
}

TypeChecker::InterfaceSignature TypeChecker::serialize_interface_signature(
    MonoTypePtr type, std::size_t visible_parameter_count) {
  auto *visible_type = zonk(type);
  if (!visible_type)
    throw std::invalid_argument(
        "cannot serialize a null inferred interface type");

  // A module definition with no source patterns is inferred as a Unit thunk
  // so its body can carry effects. That arrow is an implementation detail of
  // the checker, not a parameter in the exported interface contract.
  if (visible_parameter_count == 0) {
    auto *hidden = unifier_.resolve(visible_type);
    if (!hidden || hidden->tag != MonoType::Arrow) {
      throw std::invalid_argument(
          "parameterless module binding is missing its hidden Unit arrow");
    }
    auto *parameter = unifier_.resolve(hidden->param_type);
    if (!parameter || parameter->tag != MonoType::Con ||
        parameter->con != TyCon::Unit) {
      throw std::invalid_argument(
          "parameterless module binding has a non-Unit hidden parameter");
    }
    visible_type = hidden->return_type;
  }

  std::vector<MonoTypePtr> parameter_types;
  parameter_types.reserve(visible_parameter_count);
  auto *return_type = visible_type;
  for (std::size_t index = 0; index < visible_parameter_count; ++index) {
    auto *arrow = unifier_.resolve(return_type);
    if (!arrow || arrow->tag != MonoType::Arrow) {
      throw std::invalid_argument(
          "inferred interface type has fewer arrows than source parameters");
    }
    parameter_types.push_back(arrow->param_type);
    return_type = arrow->return_type;
  }

  std::unordered_map<TypeId, std::string> variable_names;
  auto variable_name = [&](TypeId id) -> const std::string & {
    if (const auto found = variable_names.find(id);
        found != variable_names.end())
      return found->second;
    const std::size_t ordinal = variable_names.size();
    std::string name;
    if (ordinal < 26)
      name.assign(1, static_cast<char>('a' + ordinal));
    else
      name = "t" + std::to_string(ordinal);
    return variable_names.emplace(id, std::move(name)).first->second;
  };

  std::function<std::string(MonoTypePtr)> descriptor;
  descriptor = [&](MonoTypePtr current) -> std::string {
    current = unifier_.resolve(current);
    if (!current)
      throw std::invalid_argument(
          "cannot serialize a null nested interface type");
    switch (current->tag) {
    case MonoType::Var:
      return "VAR(" + variable_name(current->var_id) + ")";
    case MonoType::Con:
      switch (current->con) {
      case TyCon::Int:
        return "INT";
      case TyCon::Float:
        return "FLOAT";
      case TyCon::Bool:
        return "BOOL";
      case TyCon::String:
        return "STRING";
      case TyCon::Char:
        return "CHAR";
      case TyCon::Byte:
        return "BYTE";
      case TyCon::Symbol:
        return "SYMBOL";
      case TyCon::Unit:
        return "UNIT";
      case TyCon::ByteArray:
        return "BYTE_ARRAY";
      case TyCon::Seq:
        return "SEQ";
      case TyCon::Set:
        return "SET";
      case TyCon::Dict:
        return "DICT";
      case TyCon::Tuple:
        return "TUPLE";
      case TyCon::Function:
        return "FUNCTION";
      case TyCon::Promise:
        return "PROMISE";
      }
      break;
    case MonoType::Arrow:
      return "FUNCTION(" + descriptor(current->param_type) + "," +
             descriptor(current->return_type) + ")";
    case MonoType::MTuple: {
      std::string result = "TUPLE(";
      for (std::size_t index = 0; index < current->elements.size(); ++index) {
        if (index)
          result += ',';
        result += descriptor(current->elements[index]);
      }
      return result + ")";
    }
    case MonoType::MRecord:
      // The current interface grammar has only the ABI-level RECORD token and
      // cannot preserve structural fields or a row rest. Do not publish a
      // descriptor that would silently weaken the inferred contract.
      throw std::invalid_argument(
          "structural record types are not representable in interfaces");
    case MonoType::App: {
      const auto application = [&](std::string_view name,
                                   std::size_t arity) -> std::string {
        if (current->args.size() != arity)
          throw std::invalid_argument("interface type '" + current->type_name +
                                      "' has an unsupported arity");
        std::string result(name);
        result += '(';
        for (std::size_t index = 0; index < current->args.size(); ++index) {
          if (index)
            result += ',';
          result += descriptor(current->args[index]);
        }
        return result + ')';
      };
      if (current->type_name == "Linear")
        return application("LINEAR", 1);
      if (current->type_name == "Seq")
        return application("Seq", 1);
      if (current->type_name == "Set")
        return application("Set", 1);
      if (current->type_name == "Dict")
        return application("Dict", 2);
      if (current->type_name == "Promise")
        return application("Promise", 1);
      // A legacy bare ADT descriptor is imported as this anonymous wildcard
      // application. It has no nominal name that can be made more precise by
      // a forwarding source binding, so preserve the wildcard spelling.
      if (current->type_name == "ADT" && current->args.size() == 1)
        return "ADT";
      if (current->type_name == "ByteArray" && current->args.empty())
        return "BYTE_ARRAY";
      if (current->type_name == "IntArray" && current->args.empty())
        return "INT_ARRAY";
      if (current->type_name == "FloatArray" && current->args.empty())
        return "FLOAT_ARRAY";
      if (current->type_name.empty())
        throw std::invalid_argument(
            "cannot serialize an unnamed interface type application");
      std::string result = "ADT(" + current->type_name;
      for (auto *argument : current->args)
        result += ',' + descriptor(argument);
      return result + ')';
    }
    }
    throw std::invalid_argument("unsupported inferred interface type");
  };

  InterfaceSignature signature;
  signature.parameter_descriptors.reserve(parameter_types.size());
  for (auto *parameter : parameter_types)
    signature.parameter_descriptors.push_back(descriptor(parameter));
  signature.return_descriptor = descriptor(return_type);
  signature.effect_scheme = serialize_effect_scheme(visible_type);
  return signature;
}

bool TypeChecker::is_effect_free(MonoTypePtr type) {
  if (!type)
    return false;
  auto row = effect_row_info(zonk(type));
  return row.ops.empty() && !row.open_rest;
}

std::string TypeChecker::serialize_effect_scheme(MonoTypePtr type) {
  std::vector<std::pair<std::string, EffectRef>> arrows;
  std::function<void(MonoTypePtr, const std::string &)> collect =
      [&](MonoTypePtr current, const std::string &path) {
        current = unifier_.resolve(current);
        if (!current)
          return;
        if (current->tag == MonoType::Arrow) {
          if (const auto effect = callee_effect(current))
            arrows.emplace_back(path, *effect);
          collect(current->param_type, path + "/p");
          collect(current->return_type, path + "/r");
        } else if (current->tag == MonoType::App) {
          for (size_t index = 0; index < current->args.size(); ++index)
            collect(current->args[index], path + "/a" + std::to_string(index));
        } else if (current->tag == MonoType::MTuple) {
          for (size_t index = 0; index < current->elements.size(); ++index)
            collect(current->elements[index],
                    path + "/t" + std::to_string(index));
        } else if (current->tag == MonoType::MRecord) {
          for (const auto &[name, field] : current->record_fields)
            collect(field, path + "/f" + name);
        }
      };
  collect(type, "$");
  std::sort(arrows.begin(), arrows.end(),
            [](const auto &left, const auto &right) {
              return left.first < right.first;
            });

  std::map<std::pair<bool, EffectVarId>, unsigned> external_variables;
  unsigned next_variable = 0;
  const auto encode_labels = [](const std::vector<std::string> &labels) {
    std::string encoded;
    for (size_t index = 0; index < labels.size(); ++index) {
      if (index)
        encoded += ',';
      encoded += labels[index];
    }
    return encoded;
  };

  std::string encoded;
  for (const auto &[path, effect] : arrows) {
    const auto summary = arena_.effect_solver().summarize(effect);
    if (!encoded.empty())
      encoded += ';';
    encoded += path + "#" + encode_labels(summary.known_labels) + "#";
    for (size_t index = 0; index < summary.tails.size(); ++index) {
      if (index)
        encoded += '|';
      const auto &tail = summary.tails[index];
      const auto key = std::make_pair(tail.opaque, tail.variable);
      const auto [it, inserted] =
          external_variables.emplace(key, next_variable);
      if (inserted)
        ++next_variable;
      encoded += tail.opaque ? "o" : "f";
      encoded += std::to_string(it->second);
      if (!tail.excluded_labels.empty())
        encoded += "~" + encode_labels(tail.excluded_labels);
    }
  }
  return encoded;
}

MonoTypePtr TypeChecker::apply_effect_scheme(MonoTypePtr type,
                                             std::string_view encoded) {
  struct Tail {
    EffectRef source;
    std::vector<std::string> excluded_labels;
  };
  struct Entry {
    std::vector<std::string> labels;
    std::vector<Tail> tails;
  };
  std::map<std::string, Entry> entries;
  std::map<std::string, EffectRef> variables;
  bool valid = true;

  const auto split = [](std::string_view text, char delimiter) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= text.size()) {
      const size_t end = text.find(delimiter, start);
      parts.push_back(text.substr(start, end == std::string_view::npos
                                             ? std::string_view::npos
                                             : end - start));
      if (end == std::string_view::npos)
        break;
      start = end + 1;
    }
    return parts;
  };
  const auto labels_from = [&](std::string_view text) {
    std::vector<std::string> labels;
    if (text.empty())
      return labels;
    for (const auto label : split(text, ',')) {
      if (label.empty()) {
        valid = false;
        continue;
      }
      labels.emplace_back(label);
    }
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    return labels;
  };

  if (valid) {
    for (const auto field : split(encoded, ';')) {
      if (field.empty()) {
        if (!encoded.empty())
          valid = false;
        continue;
      }
      const auto first = field.find('#');
      const auto second = first == std::string_view::npos
                              ? std::string_view::npos
                              : field.find('#', first + 1);
      if (first == std::string_view::npos || second == std::string_view::npos ||
          field.find('#', second + 1) != std::string_view::npos) {
        valid = false;
        break;
      }
      const std::string path(field.substr(0, first));
      if (path.empty() || entries.contains(path)) {
        valid = false;
        break;
      }
      Entry entry;
      entry.labels = labels_from(field.substr(first + 1, second - first - 1));
      const auto tails = field.substr(second + 1);
      if (!tails.empty()) {
        for (const auto tail : split(tails, '|')) {
          if (tail.size() < 2 || (tail.front() != 'f' && tail.front() != 'o')) {
            valid = false;
            break;
          }
          const auto mask = tail.find('~');
          const auto id_text = tail.substr(1, mask == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : mask - 1);
          if (id_text.empty() ||
              !std::all_of(id_text.begin(), id_text.end(),
                           [](char c) { return c >= '0' && c <= '9'; })) {
            valid = false;
            break;
          }
          const std::string key(tail.substr(0, mask));
          auto variable = variables.find(key);
          if (variable == variables.end()) {
            const bool opaque = tail.front() == 'o';
            variable =
                variables
                    .emplace(key, opaque ? arena_.effect_solver().opaque()
                                         : arena_.effect_solver().flexible())
                    .first;
          }
          entry.tails.push_back({
              variable->second,
              mask == std::string_view::npos
                  ? std::vector<std::string>{}
                  : labels_from(tail.substr(mask + 1)),
          });
        }
      }
      if (!valid)
        break;
      entries.emplace(path, std::move(entry));
    }
  }

  const auto opaque_all = [&](const auto &self,
                              MonoTypePtr current) -> MonoTypePtr {
    current = unifier_.resolve(current);
    if (!current)
      return current;
    if (current->tag == MonoType::Arrow)
      return arena_.make_arrow(self(self, current->param_type),
                               self(self, current->return_type),
                               arena_.effect_solver().opaque());
    if (current->tag == MonoType::App) {
      std::vector<MonoTypePtr> args;
      for (const auto input : current->args)
        args.push_back(self(self, input));
      return arena_.make_app(current->type_name, std::move(args));
    }
    if (current->tag == MonoType::MTuple) {
      std::vector<MonoTypePtr> elements;
      for (const auto input : current->elements)
        elements.push_back(self(self, input));
      return arena_.make_tuple(std::move(elements));
    }
    if (current->tag == MonoType::MRecord) {
      std::vector<std::pair<std::string, MonoTypePtr>> fields;
      fields.reserve(current->record_fields.size());
      for (const auto &[name, field] : current->record_fields)
        fields.emplace_back(name, self(self, field));
      return arena_.make_record(
          std::move(fields),
          current->row_rest ? self(self, current->row_rest) : nullptr);
    }
    return current;
  };
  if (!valid)
    return opaque_all(opaque_all, type);

  const auto rebuild = [&](const auto &self, MonoTypePtr current,
                           const std::string &path) -> MonoTypePtr {
    current = unifier_.resolve(current);
    if (!current)
      return current;
    if (current->tag == MonoType::Arrow) {
      const auto found = entries.find(path);
      if (found == entries.end())
        return opaque_all(opaque_all, current);
      std::vector<EffectRef> sources;
      if (!found->second.labels.empty())
        sources.push_back(arena_.effect_solver().labels(found->second.labels));
      for (const auto &tail : found->second.tails) {
        if (!tail.source.valid())
          return opaque_all(opaque_all, current);
        sources.push_back(
            arena_.effect_solver().mask(tail.source, tail.excluded_labels));
      }
      return arena_.make_arrow(self(self, current->param_type, path + "/p"),
                               self(self, current->return_type, path + "/r"),
                               arena_.effect_solver().join(std::move(sources)));
    }
    if (current->tag == MonoType::App) {
      std::vector<MonoTypePtr> args;
      for (size_t index = 0; index < current->args.size(); ++index)
        args.push_back(self(self, current->args[index],
                            path + "/a" + std::to_string(index)));
      return arena_.make_app(current->type_name, std::move(args));
    }
    if (current->tag == MonoType::MTuple) {
      std::vector<MonoTypePtr> elements;
      for (size_t index = 0; index < current->elements.size(); ++index)
        elements.push_back(self(self, current->elements[index],
                                path + "/t" + std::to_string(index)));
      return arena_.make_tuple(std::move(elements));
    }
    if (current->tag == MonoType::MRecord) {
      std::vector<std::pair<std::string, MonoTypePtr>> fields;
      fields.reserve(current->record_fields.size());
      for (const auto &[name, field] : current->record_fields)
        fields.emplace_back(name, self(self, field, path + "/f" + name));
      return arena_.make_record(
          std::move(fields), current->row_rest
                                 ? self(self, current->row_rest, path + "/row")
                                 : nullptr);
    }
    return current;
  };
  return rebuild(rebuild, type, "$");
}

void TypeChecker::check_module(ast::ModuleDecl *mod) {
  if (!mod)
    return;

  // Constructors must be visible while checking the module that declares
  // them: smart constructors and destructor-style functions commonly use
  // them in their own definitions.  Imported ADTs are registered by the
  // caller, but a module's declarations have not crossed that boundary yet.
  for (auto *adt : mod->adt_declarations) {
    if (!adt)
      continue;
    std::vector<std::pair<std::string, int>> constructors;
    std::vector<std::vector<ast::FieldType>> field_types;
    std::vector<std::vector<std::string>> field_names;
    constructors.reserve(adt->variants.size());
    for (auto *ctor : adt->variants) {
      if (!ctor)
        continue;
      constructors.emplace_back(
          ctor->name, static_cast<int>(ctor->field_type_names.size()));
      field_types.push_back(ctor->field_type_names);
      field_names.push_back(ctor->field_names);
    }
    register_adt(adt->name, adt->type_params, constructors, field_types,
                 field_names);
    for (const auto &trait_name : adt->derive_traits) {
      bool lawful = true;
      if (trait_name == "Eq" || trait_name == "Ord" || trait_name == "Hash" ||
          trait_name == "Show" || trait_name == "Send" ||
          trait_name == "Shareable") {
        for (auto *ctor : adt->variants) {
          for (size_t field_index = 0;
               field_index < ctor->field_type_names.size(); ++field_index) {
            const auto &field = ctor->field_type_names[field_index];
            const bool unsupported =
                field.is_function_type || field.name == "Function" ||
                field.name == "Promise" || field.name == "Channel" ||
                field.name == "Linear" || field.name == "ByteArray" ||
                field.name == "IntArray" || field.name == "FloatArray";
            if (!unsupported)
              continue;
            diag_.error(
                adt->Range, ErrorCode::E0400,
                "cannot derive " + trait_name + " for '" + adt->name +
                    "': field " + ctor->name + "." +
                    std::to_string(field_index + 1) + " has non-lawful type '" +
                    (field.is_function_type ? "Function" : field.name) + "'");
            ++error_count_;
            lawful = false;
            break;
          }
          if (!lawful)
            break;
        }
      }
      if ((trait_name == "Ord" || trait_name == "Hash") &&
          std::find(adt->derive_traits.begin(), adt->derive_traits.end(),
                    "Eq") == adt->derive_traits.end()) {
        bool explicit_eq = false;
        for (auto *instance : mod->instance_declarations)
          if (instance && instance->trait_name == "Eq" &&
              !instance->type_names.empty() &&
              instance->type_names.front() == adt->name) {
            explicit_eq = true;
            break;
          }
        if (!explicit_eq) {
          diag_.error(adt->Range, ErrorCode::E0400,
                      "deriving " + trait_name + " for '" + adt->name +
                          "' requires Eq; add `deriving Eq, " + trait_name +
                          "`");
          ++error_count_;
          lawful = false;
        }
      }
      if (lawful) {
        if (trait_name == "Send" || trait_name == "Shareable") {
          std::vector<std::pair<std::string, std::string>> constraints;
          for (const auto &parameter : adt->type_params)
            constraints.emplace_back(trait_name, parameter);
          register_instance(trait_name, {adt->name}, adt->type_params,
                            std::move(constraints));
        } else {
          register_instance(trait_name, {adt->name});
        }
      }
    }
  }

  for (auto *trait : mod->trait_declarations) {
    if (!trait)
      continue;
    register_trait(trait->name, trait->type_params);
    for (const auto &[superclass, _] : trait->superclasses)
      register_trait_superclass(trait->name, superclass);
    for (const auto &method : trait->methods) {
      std::unordered_map<std::string, MonoTypePtr> variables;
      auto *method_type =
          from_ast_type_impl(method.type_signature, 0, variables);
      std::vector<MonoTypePtr> trait_arguments;
      for (const auto &parameter : trait->type_params) {
        auto found = variables.find(parameter);
        if (found == variables.end()) {
          auto *variable = arena_.fresh_var(0);
          uf_.add_var(variable->var_id, 0);
          found = variables.emplace(parameter, variable).first;
        }
        trait_arguments.push_back(found->second);
      }
      register_trait_method(trait->name, method.name, method_type);
      if (auto scheme = root_env_->lookup(method.name);
          scheme && !trait_arguments.empty()) {
        auto quantified = scheme->quantified_vars;
        for (auto *argument : trait_arguments) {
          auto *resolved = unifier_.resolve(argument);
          if (resolved && resolved->tag == MonoType::Var &&
              std::find(quantified.begin(), quantified.end(),
                        resolved->var_id) == quantified.end())
            quantified.push_back(resolved->var_id);
        }
        root_env_->bind_scheme(
            method.name,
            TypeScheme(std::move(quantified),
                       {Constraint(trait->name, std::move(trait_arguments))},
                       scheme->body));
      }
    }
  }

  std::unordered_set<std::string> local_instance_keys;
  for (auto *instance : mod->instance_declarations) {
    if (!instance)
      continue;
    std::string key = instance->trait_name;
    for (const auto &type_name : instance->type_names)
      key += ":" + type_name;
    if (!local_instance_keys.insert(key).second) {
      diag_.error(instance->Range, ErrorCode::E0400,
                  "duplicate visible trait instance '" + key +
                      "'; instance selection must be coherent");
      ++error_count_;
      continue;
    }
    register_instance(
        instance->trait_name, instance->type_names, instance->type_params,
        {instance->constraints.begin(), instance->constraints.end()});
  }

  auto env = root_env_->child();
  // Module-level externs are declarations in the same lexical scope as the
  // module's Yona definitions.  The expression form handles its own nested
  // body, but module declarations have no body and therefore must be bound
  // explicitly before any function is inferred.
  for (auto *ext : mod->extern_declarations) {
    if (!ext)
      continue;
    auto *declared = from_ast_type(ext->declared_type, 0);
    env->bind_scheme(ext->name, generalize(declared, -1));
  }
  // Infer dependency SCCs in callee-first order. Only members of the current
  // recursive component are preliminary and monomorphic; completed sibling
  // components are generalized before any dependent is checked.
  ModuleExportResolver resolve_exports;
  if (import_src_)
    resolve_exports = [this](const std::string &module_fqn) {
      return import_src_->imported_module_exports(module_fqn);
    };
  for (const auto &component :
       module_function_components(mod, resolve_exports)) {
    std::unordered_map<std::string, MonoTypePtr> prelim;
    if (component.recursive)
      for (auto *func : component.functions) {
        if (!func || func->name.empty())
          continue;
        const auto effect = arena_.effect_solver().derived();
        predeclared_function_body_effects_[func] = effect;
        MonoTypePtr declaration = nullptr;
        if (func->patterns.empty()) {
          declaration = arena_.fresh_var(0);
          uf_.add_var(declaration->var_id, 0);
        } else {
          auto *result = arena_.fresh_var(0);
          uf_.add_var(result->var_id, 0);
          declaration = result;
          for (int index = static_cast<int>(func->patterns.size()) - 1;
               index >= 0; --index) {
            auto *parameter = arena_.fresh_var(0);
            uf_.add_var(parameter->var_id, 0);
            declaration = arena_.make_arrow(
                parameter, declaration,
                index == static_cast<int>(func->patterns.size()) - 1
                    ? effect
                    : arena_.effect_solver().empty());
          }
        }
        env->bind(func->name, declaration);
        prelim[func->name] = declaration;
      }

    std::vector<std::pair<ast::FunctionExpr *, MonoTypePtr>> inferred_functions;
    inferred_functions.reserve(component.functions.size());
    for (auto *func : component.functions) {
      if (!func)
        continue;
      const auto pit = prelim.find(func->name);
      if (pit != prelim.end())
        recursive_self_contexts_.push_back({pit->second});
      auto *inferred = infer(func, env, 0);
      if (pit != prelim.end())
        recursive_self_contexts_.pop_back();
      if (!inferred)
        continue;
      // A parameterless module definition is a value (a CAF), whereas an
      // expression lambda with no written parameters is a Unit thunk.
      auto *type = unifier_.resolve(inferred);
      if (func->patterns.empty() && type && type->tag == MonoType::Arrow &&
          unifier_.resolve(type->param_type)->tag == MonoType::Con &&
          unifier_.resolve(type->param_type)->con == TyCon::Unit)
        type = type->return_type;
      if (func->type_signature.has_value()) {
        auto *declared = from_ast_type(*func->type_signature, 0);
        unifier_.unify(type, declared, func->Range,
                       "against the declared type of '" + func->name + "'");
      }
      if (pit != prelim.end())
        unifier_.unify(pit->second, type, func->Range,
                       "in module function '" + func->name + "'");
      inferred_functions.emplace_back(func, type);
    }

    for (auto &[func, type] : inferred_functions)
      env->bind_scheme(func->name, generalize(unifier_.resolve(type), -1));
    for (auto *func : component.functions)
      predeclared_function_body_effects_.erase(func);
  }
}

MonoTypePtr TypeChecker::check(AstNode *node) {
  return infer(node, root_env_, 0);
}

MonoTypePtr TypeChecker::type_of(AstNode *node) const {
  auto it = type_map_.find(node);
  return (it != type_map_.end()) ? it->second : nullptr;
}

std::optional<TypeChecker::SelectedTraitInstance>
TypeChecker::selected_trait_instance(const ApplyExpr *application) const {
  const auto found = selected_trait_instances_.find(application);
  if (found == selected_trait_instances_.end())
    return std::nullopt;
  return found->second;
}

void TypeChecker::record(AstNode *node, MonoTypePtr type) {
  type_map_[node] = type;
}

MonoTypePtr TypeChecker::zonk(MonoTypePtr type) {
  type = unifier_.resolve(type);
  if (!type)
    return nullptr;
  switch (type->tag) {
  case MonoType::Var:
    return type; // unresolved var
  case MonoType::Con:
    return type;
  case MonoType::Arrow:
    return arena_.make_arrow(zonk(type->param_type), zonk(type->return_type),
                             type->arrow_effect);
  case MonoType::App: {
    std::vector<MonoTypePtr> args;
    for (auto *a : type->args)
      args.push_back(zonk(a));
    return arena_.make_app(type->type_name, args);
  }
  case MonoType::MTuple: {
    std::vector<MonoTypePtr> elems;
    for (auto *e : type->elements)
      elems.push_back(zonk(e));
    return arena_.make_tuple(elems);
  }
  case MonoType::MRecord: {
    std::vector<std::pair<std::string, MonoTypePtr>> fields;
    for (auto &[name, ft] : type->record_fields)
      fields.push_back({name, zonk(ft)});
    MonoTypePtr rest = type->row_rest ? zonk(type->row_rest) : nullptr;
    return arena_.make_record(fields, rest);
  }
  default:
    return type;
  }
}

// ===== Main Dispatch =====

MonoTypePtr TypeChecker::infer(AstNode *node, std::shared_ptr<TypeEnv> env,
                               int level) {
  if (!node)
    return arena_.make_con(TyCon::Unit);

  MonoTypePtr result = nullptr;
  auto ty = node->get_type();

  switch (ty) {
  case AST_INTEGER_EXPR:
    result = infer_integer(node);
    break;
  case AST_FLOAT_EXPR:
    result = infer_float(node);
    break;
  case AST_STRING_EXPR:
    result = infer_string(node);
    break;
  case AST_TRUE_LITERAL_EXPR:
  case AST_FALSE_LITERAL_EXPR:
    result = infer_bool(node);
    break;
  case AST_SYMBOL_EXPR:
    result = infer_symbol(node);
    break;
  case AST_LITERAL_EXPR: {
    // LiteralExpr<T> â€” determine type from the actual literal value type
    if (dynamic_cast<LiteralExpr<float> *>(node))
      result = infer_float(node);
    else if (dynamic_cast<LiteralExpr<bool> *>(node))
      result = infer_bool(node);
    else if (dynamic_cast<LiteralExpr<std::string> *>(node))
      result = infer_string(node);
    else if (dynamic_cast<LiteralExpr<int> *>(node))
      result = infer_integer(node);
    else
      result = arena_.make_con(TyCon::Unit);
    break;
  }
  case AST_UNIT_EXPR:
    result = arena_.make_con(TyCon::Unit);
    break;
  case AST_MAIN:
    result = infer(static_cast<MainNode *>(node)->node, env, level);
    break;
  case AST_IDENTIFIER_EXPR:
    result = infer_identifier(static_cast<IdentifierExpr *>(node), env, level);
    break;
  case AST_LET_EXPR:
    result = infer_let(static_cast<LetExpr *>(node), env, level);
    break;
  case AST_FUNCTION_EXPR:
    result = infer_function(static_cast<FunctionExpr *>(node), env, level);
    break;
  case AST_APPLY_EXPR:
    result = infer_apply(static_cast<ApplyExpr *>(node), env, level);
    break;
  case AST_IF_EXPR:
    result = infer_if(static_cast<IfExpr *>(node), env, level);
    break;
  case AST_TUPLE_EXPR:
    result = infer_tuple(static_cast<TupleExpr *>(node), env, level);
    break;
  case AST_VALUES_SEQUENCE_EXPR:
    result = infer_seq(static_cast<ValuesSequenceExpr *>(node), env, level);
    break;
  case AST_DO_EXPR:
    result = infer_do(static_cast<DoExpr *>(node), env, level);
    break;
  case AST_CASE_EXPR:
    result = infer_case(static_cast<CaseExpr *>(node), env, level);
    break;
  case AST_CONS_LEFT_EXPR:
    result = infer_cons(static_cast<ConsLeftExpr *>(node), env, level);
    break;
  case AST_RECORD_LITERAL_EXPR: {
    auto *rec = static_cast<RecordLiteralExpr *>(node);
    std::vector<std::pair<std::string, MonoTypePtr>> fields;
    for (auto &[name, expr] : rec->fields) {
      auto *field_type = infer(expr, env, level);
      fields.push_back({name, field_type});
    }
    result = arena_.make_record(fields);
    break;
  }
  case AST_FIELD_ACCESS_EXPR: {
    auto *fa = static_cast<FieldAccessExpr *>(node);
    auto *obj_type = infer(fa->identifier, env, level);
    auto *resolved_object = unifier_.resolve(obj_type);

    // Named-record ADTs retain their nominal identity. Resolve their
    // declared field directly instead of forcing the nominal App type
    // to unify with an unrelated structural open row.
    if (resolved_object && resolved_object->tag == MonoType::App) {
      for (const auto &[_, constructor] : constructor_registry_) {
        if (constructor.adt_name != resolved_object->type_name)
          continue;
        const auto field =
            std::find(constructor.field_names.begin(),
                      constructor.field_names.end(), fa->name->value);
        if (field == constructor.field_names.end())
          continue;
        const size_t field_index = static_cast<size_t>(
            std::distance(constructor.field_names.begin(), field));
        if (field_index >= constructor.field_types.size())
          break;

        std::function<MonoTypePtr(const ast::FieldType &)> declared_type;
        declared_type = [&](const ast::FieldType &value) -> MonoTypePtr {
          if (value.is_tuple_type) {
            std::vector<MonoTypePtr> elements;
            for (const auto &element : value.tuple_types)
              elements.push_back(declared_type(element));
            return arena_.make_tuple(elements);
          }
          if (value.is_function_type) {
            MonoTypePtr function_result =
                value.return_types.empty()
                    ? arena_.make_con(TyCon::Unit)
                    : declared_type(value.return_types.front());
            for (auto it = value.param_types.rbegin();
                 it != value.param_types.rend(); ++it)
              function_result =
                  arena_.make_arrow(declared_type(*it), function_result);
            return function_result;
          }
          for (size_t i = 0; i < constructor.type_params.size(); ++i)
            if (value.name == constructor.type_params[i] &&
                i < resolved_object->args.size())
              return resolved_object->args[i];
          if (value.name == "Int")
            return arena_.make_con(TyCon::Int);
          if (value.name == "Float")
            return arena_.make_con(TyCon::Float);
          if (value.name == "Bool")
            return arena_.make_con(TyCon::Bool);
          if (value.name == "String")
            return arena_.make_con(TyCon::String);
          if (value.name == "Symbol")
            return arena_.make_con(TyCon::Symbol);
          if (value.name == "()" || value.name == "Unit")
            return arena_.make_con(TyCon::Unit);
          std::vector<MonoTypePtr> arguments;
          for (const auto &argument : value.type_arguments)
            arguments.push_back(declared_type(argument));
          return arena_.make_app(value.name, arguments);
        };
        result = declared_type(constructor.field_types[field_index]);
        break;
      }
      if (result)
        break;
    }
    // Constrain obj to be a record with this field
    auto *field_var = arena_.fresh_var(level);
    uf_.add_var(field_var->var_id, level);
    auto *row_var = arena_.fresh_var(level);
    uf_.add_var(row_var->var_id, level);
    auto *expected_record =
        arena_.make_record({{fa->name->value, field_var}}, row_var);
    unifier_.unify(obj_type, expected_record, node->Range,
                   "in field access '." + fa->name->value + "'");
    result = unifier_.resolve(field_var);
    break;
  }
  case AST_PERFORM_EXPR:
    result = infer_perform(static_cast<PerformExpr *>(node), env, level);
    break;
  case AST_HANDLE_EXPR:
    result = infer_handle(static_cast<HandleExpr *>(node), env, level);
    break;

    // === Phase 1: Quick wins ===

  case AST_BYTE_EXPR:
    result = arena_.make_con(TyCon::Int);
    break; // byte as Int
  case AST_CHARACTER_EXPR:
    result = arena_.make_con(TyCon::Int);
    break; // char code as Int

  case AST_RANGE_SEQUENCE_EXPR:
    // [start..end] or [start..end..step] â€” all Int, returns Seq Int
    result = arena_.make_app("Seq", {arena_.make_con(TyCon::Int)});
    break;

  case AST_LOGICAL_NOT_OP_EXPR: {
    auto *e = static_cast<LogicalNotOpExpr *>(node);
    auto *t = infer(e->expr, env, level);
    unifier_.unify(t, arena_.make_con(TyCon::Bool), node->Range,
                   "in logical not");
    result = arena_.make_con(TyCon::Bool);
    break;
  }

  case AST_RAISE_EXPR: {
    // raise expr â€” type of the raise is a fresh var (bottom/never returns)
    auto *re = static_cast<RaiseExpr *>(node);
    infer(re->value, env, level);
    result = arena_.fresh_var(level);
    uf_.add_var(result->var_id, level);
    break;
  }

  case AST_IMPORT_EXPR: {
    auto *imp = static_cast<ImportExpr *>(node);
    auto import_env = env->child();
    auto saved_boundaries = concurrency_boundaries_;
    for (auto *clause : imp->clauses) {
      if (auto *fi = dynamic_cast<FunctionsImport *>(clause)) {
        std::string mod_fqn = fqn_to_string(fi->fromFqn);
        if (import_src_)
          for (const auto &instance : import_src_->imported_instances(mod_fqn))
            register_instance(instance.trait_name, instance.type_names,
                              instance.type_params, instance.constraints);
        for (auto *fa : fi->aliases) {
          std::string src_name = fa->name->value;
          std::string bind_name = (fa->alias && !fa->alias->value.empty())
                                      ? fa->alias->value
                                      : src_name;
          bind_import_name(import_env, mod_fqn, src_name, bind_name, level);
          if (mod_fqn == "Std\\Task" && src_name == "spawn")
            concurrency_boundaries_[bind_name] = ConcurrencyBoundary::TaskSpawn;
          else if (mod_fqn == "Std\\Channel" && src_name == "send")
            concurrency_boundaries_[bind_name] =
                ConcurrencyBoundary::ChannelSend;
        }
      } else if (auto *mi = dynamic_cast<ModuleImport *>(clause)) {
        if (import_src_ && mi->fqn) {
          std::string mod = mi->fqn->to_string();
          for (const auto &instance : import_src_->imported_instances(mod))
            register_instance(instance.trait_name, instance.type_names,
                              instance.type_params, instance.constraints);
          for (auto &name : import_src_->imported_module_exports(mod))
            bind_import_name(import_env, mod, name, name, level);
          if (mod == "Std\\Task")
            concurrency_boundaries_["spawn"] = ConcurrencyBoundary::TaskSpawn;
          else if (mod == "Std\\Channel")
            concurrency_boundaries_["send"] = ConcurrencyBoundary::ChannelSend;
        }
      }
    }
    result = infer(imp->expr, import_env, level);
    concurrency_boundaries_ = std::move(saved_boundaries);
    break;
  }

  case AST_EXTERN_DECL: {
    // extern name : Type in body â€” bind the declared type (so Linear
    // returns are visible to LinearityChecker) and infer the body.
    auto *ext = static_cast<ExternDeclExpr *>(node);
    auto child_env = env->child();
    auto *declared = from_ast_type(ext->declared_type, level);
    child_env->bind_scheme(ext->name, generalize(declared, -1));
    result = infer(ext->body, child_env, level);
    break;
  }

    // === Phase 1: Generators ===

  case AST_SEQ_GENERATOR_EXPR: {
    auto *gen = static_cast<SeqGeneratorExpr *>(node);
    auto gen_env = env->child();
    if (gen->is_parallel)
      capture_frames_.push_back({gen_env.get(), {}, {}});
    bind_collection_extractor(gen->collectionExtractor, gen_env, level);
    auto *body_type = infer(gen->reducerExpr, gen_env, level);
    if (gen->is_parallel) {
      auto captures = std::move(capture_frames_.back().types);
      capture_frames_.pop_back();
      require_trait("Send", body_type, gen->Range,
                    "result of a parallel comprehension iteration");
      require_captures_shareable(
          captures, gen->Range,
          "value shared with a parallel comprehension iteration");
      if (auto *extractor = dynamic_cast<ValueCollectionExtractorExpr *>(
              gen->collectionExtractor)) {
        auto *source = extractor->collection
                           ? zonk(type_of(extractor->collection))
                           : nullptr;
        if (source && source->tag == MonoType::App && !source->args.empty())
          require_trait("Shareable", source->args.front(), gen->Range,
                        "element of a parallel comprehension source");
      }
    }
    result = arena_.make_app("Seq", {body_type});
    break;
  }

  case AST_SET_GENERATOR_EXPR: {
    auto *gen = static_cast<SetGeneratorExpr *>(node);
    auto gen_env = env->child();
    bind_collection_extractor(gen->collectionExtractor, gen_env, level);
    auto *body_type = infer(gen->reducerExpr, gen_env, level);
    result = arena_.make_app("Set", {body_type});
    break;
  }

  case AST_DICT_GENERATOR_EXPR: {
    auto *gen = static_cast<DictGeneratorExpr *>(node);
    auto gen_env = env->child();
    bind_collection_extractor(gen->collectionExtractor, gen_env, level);
    auto *key_type = infer(gen->reducerExpr->key, gen_env, level);
    auto *val_type = infer(gen->reducerExpr->value, gen_env, level);
    result = arena_.make_app("Dict", {key_type, val_type});
    break;
  }

  case AST_SET_EXPR: {
    auto *se = static_cast<SetExpr *>(node);
    if (se->values.empty()) {
      // `{}` is the shared empty literal for persistent sets and
      // dictionaries. Keep its kind context-polymorphic; the first
      // consuming operation (`put`, union, etc.) selects the
      // concrete collection type. Codegen uses their common HAMT
      // empty representation.
      result = arena_.fresh_var(level);
      uf_.add_var(result->var_id, level);
    } else {
      auto *elem_type = infer(se->values[0], env, level);
      for (size_t i = 1; i < se->values.size(); i++) {
        auto *t = infer(se->values[i], env, level);
        unifier_.unify(elem_type, t, node->Range,
                       "in set literal (all elements must have same type)");
      }
      result = arena_.make_app("Set", {unifier_.resolve(elem_type)});
    }
    break;
  }

  case AST_DICT_EXPR: {
    auto *de = static_cast<DictExpr *>(node);
    auto *key_var = arena_.fresh_var(level);
    uf_.add_var(key_var->var_id, level);
    auto *val_var = arena_.fresh_var(level);
    uf_.add_var(val_var->var_id, level);
    for (auto &[k, v] : de->values) {
      auto *kt = infer(k, env, level);
      auto *vt = infer(v, env, level);
      unifier_.unify(key_var, kt, node->Range, "in dict literal key");
      unifier_.unify(val_var, vt, node->Range, "in dict literal value");
    }
    result = arena_.make_app(
        "Dict", {unifier_.resolve(key_var), unifier_.resolve(val_var)});
    break;
  }

    // === Phase 2: Control flow completions ===

  case AST_WITH_EXPR: {
    auto *we = static_cast<WithExpr *>(node);
    infer(we->contextExpr, env, level);
    auto child_env = env->child();
    auto *ctx_var = arena_.fresh_var(level);
    uf_.add_var(ctx_var->var_id, level);
    child_env->bind(we->name->value, ctx_var);
    result = infer(we->bodyExpr, child_env, level);
    break;
  }

  case AST_TRY_CATCH_EXPR: {
    auto *tc = static_cast<TryCatchExpr *>(node);
    auto *try_type = infer(tc->tryExpr, env, level);
    if (tc->catchExpr) {
      for (auto *cp : tc->catchExpr->patterns) {
        // Extract body from the catch pattern's variant
        if (auto *pwog = std::get_if<PatternWithoutGuards *>(&cp->pattern)) {
          if (*pwog && (*pwog)->expr) {
            // A catch pattern introduces the same lexical bindings as a case
            // pattern. Keep each clause isolated so its bindings neither leak
            // into sibling clauses nor overwrite an enclosing name.
            auto catch_env = env->child();
            infer_pattern(cp->matchPattern, catch_env, level);
            auto *catch_type = infer((*pwog)->expr, catch_env, level);
            unifier_.unify(try_type, catch_type, node->Range,
                           "in try/catch (all branches must have same type)");
          }
        }
      }
    }
    result = unifier_.resolve(try_type);
    break;
  }

  case AST_CONS_RIGHT_EXPR: {
    // Same as cons left but reversed: seq :> elem
    auto *cr = static_cast<ConsRightExpr *>(node);
    auto *seq_type = infer(cr->left, env, level);
    auto *elem_type = infer(cr->right, env, level);
    auto *expected_seq = arena_.make_app("Seq", {elem_type});
    unifier_.unify(seq_type, expected_seq, node->Range, "in cons right (:>)");
    result = unifier_.resolve(expected_seq);
    break;
  }

  case AST_IN_EXPR: {
    // x in coll â€” Seq a / Set a (element) or Dict k v (key) â†’ Bool
    auto *ie = static_cast<InExpr *>(node);
    auto *elem_type = infer(ie->left, env, level);
    auto *coll_type = infer(ie->right, env, level);
    auto *resolved = unifier_.resolve(coll_type);
    if (resolved && resolved->tag == MonoType::App &&
        resolved->type_name == "Set" && !resolved->args.empty()) {
      unifier_.unify(elem_type, resolved->args[0], node->Range,
                     "in membership (in) set element");
    } else if (resolved && resolved->tag == MonoType::App &&
               resolved->type_name == "Dict" && !resolved->args.empty()) {
      unifier_.unify(elem_type, resolved->args[0], node->Range,
                     "in membership (in) dict key");
    } else {
      auto *expected_seq = arena_.make_app("Seq", {elem_type});
      unifier_.unify(coll_type, expected_seq, node->Range,
                     "in membership (in) sequence");
    }
    result = arena_.make_con(TyCon::Bool);
    break;
  }

  case AST_REMOVE_EXPR: {
    // a -- b â€” remove elements of b from a (same collection type)
    auto *re = static_cast<RemoveExpr *>(node);
    auto *left_type = infer(re->left, env, level);
    auto *right_type = infer(re->right, env, level);
    unifier_.unify(
        left_type, right_type, node->Range,
        "in remove (--) (both sides must have the same collection type)");
    result = unifier_.resolve(left_type);
    break;
  }

  case AST_FIELD_UPDATE_EXPR: {
    auto *fu = static_cast<FieldUpdateExpr *>(node);
    auto *obj_type = infer(fu->identifier, env, level);
    for (auto &[name, expr] : fu->updates)
      infer(expr, env, level);
    result = obj_type; // update returns same type as original
    break;
  }

  default:
    // Binary operators
    if (auto *binop = dynamic_cast<BinaryOpExpr *>(node)) {
      result = infer_binary(binop, env, level);
      break;
    }
    // Fallback: return a fresh variable
    result = arena_.fresh_var(level);
    uf_.add_var(result->var_id, level);
    break;
  }

  if (result)
    record(node, result);
  return result;
}

// ===== Literals =====

MonoTypePtr TypeChecker::infer_integer(AstNode *) {
  return arena_.make_con(TyCon::Int);
}
MonoTypePtr TypeChecker::infer_float(AstNode *) {
  return arena_.make_con(TyCon::Float);
}
MonoTypePtr TypeChecker::infer_string(AstNode *) {
  return arena_.make_con(TyCon::String);
}
MonoTypePtr TypeChecker::infer_bool(AstNode *) {
  return arena_.make_con(TyCon::Bool);
}
MonoTypePtr TypeChecker::infer_symbol(AstNode *) {
  return arena_.make_con(TyCon::Symbol);
}

// ===== Identifier =====

MonoTypePtr TypeChecker::infer_identifier(IdentifierExpr *node,
                                          std::shared_ptr<TypeEnv> env,
                                          int level) {
  auto scheme = env->lookup(node->name->value);
  if (!scheme) {
    std::string msg = "undefined variable '" + node->name->value + "'";
    // Suggest closest match
    auto names = env->all_names();
    std::string best;
    size_t best_dist = 4; // max distance to suggest
    for (auto &n : names) {
      auto d = edit_distance(node->name->value, n);
      if (d < best_dist) {
        best_dist = d;
        best = n;
      }
    }
    if (!best.empty())
      msg += "; did you mean '" + best + "'?";
    diag_.error(node->Range, ErrorCode::E0103, msg);
    error_count_++;
    return arena_.fresh_var(level);
  }
  auto *inferred = instantiate(*scheme, level);
  for (auto &frame : capture_frames_) {
    if (env->bound_through(node->name->value, frame.local_root))
      continue;
    if (frame.names.insert(node->name->value).second)
      frame.types.push_back(inferred);
  }
  return inferred;
}

// ===== Let Binding =====

/// Pre-scan nested let expressions and bind all lambda alias names with fresh
/// type vars. This enables mutual recursion across nested let blocks â€”
/// `let f = ... g ... in let g = ... f ... in expr` â€” matching the codegen's
/// deferred compilation behavior.
static void prescan_let_lambdas(AstNode *node, std::shared_ptr<TypeEnv> env,
                                TypeArena &arena, UnionFind &uf, int level) {
  auto *let_node = dynamic_cast<LetExpr *>(node);
  if (!let_node)
    return;

  for (auto *alias : let_node->aliases) {
    if (auto *la = dynamic_cast<LambdaAlias *>(alias)) {
      // Only pre-bind if not already bound (avoids overwriting)
      if (!env->lookup(la->name->value)) {
        auto *v = arena.fresh_var(level + 1);
        uf.add_var(v->var_id, level + 1);
        env->bind(la->name->value, v);
      }
    }
  }
  // Recurse into the body to find nested lets
  prescan_let_lambdas(let_node->expr, env, arena, uf, level);
}

MonoTypePtr TypeChecker::infer_let(LetExpr *node, std::shared_ptr<TypeEnv> env,
                                   int level) {
  auto child_env = env->child();

  // Pass 0: Pre-scan this and nested let blocks for all lambda names.
  // Enables mutual recursion across nested lets.
  prescan_let_lambdas(node, child_env, arena_, uf_, level);

  // Pass 1: Pre-bind every local lambda with the same complete curried
  // skeleton used for a recursive module component.  A bare value variable
  // loses the fact that a sibling call reaches a derived body cell, leaving
  // a pure local cycle spuriously open.  Only the final source application
  // runs the body; earlier curried stages are pure closures.
  struct LambdaPrelim {
    LambdaAlias *la;
    MonoTypePtr declaration;
    MonoTypePtr inferred = nullptr;
  };
  std::vector<LambdaPrelim> lambda_prelims;
  for (auto *alias : node->aliases) {
    if (auto *la = dynamic_cast<LambdaAlias *>(alias)) {
      const auto body_effect = arena_.effect_solver().derived();
      predeclared_function_body_effects_[la->lambda] = body_effect;
      auto *result = arena_.fresh_var(level + 1);
      uf_.add_var(result->var_id, level + 1);
      MonoTypePtr declaration = result;
      if (la->lambda->patterns.empty()) {
        declaration = arena_.make_arrow(arena_.make_con(TyCon::Unit), result,
                                        body_effect);
      } else {
        for (int index = static_cast<int>(la->lambda->patterns.size()) - 1;
             index >= 0; --index) {
          auto *parameter = arena_.fresh_var(level + 1);
          uf_.add_var(parameter->var_id, level + 1);
          declaration = arena_.make_arrow(
              parameter, declaration,
              index == static_cast<int>(la->lambda->patterns.size()) - 1
                  ? body_effect
                  : arena_.effect_solver().empty());
        }
      }
      child_env->bind(la->name->value, declaration);
      lambda_prelims.push_back({la, declaration});
    }
  }

  // Pass 2: Infer all alias types.
  size_t lambda_idx = 0;
  for (auto *alias : node->aliases) {
    if (auto *va = dynamic_cast<ValueAlias *>(alias)) {
      auto *rhs_type = infer(va->expr, child_env, level + 1);
      auto scheme = generalize(rhs_type, level);
      child_env->bind_scheme(va->identifier->name->value, scheme);
    } else if (auto *pa = dynamic_cast<PatternAlias *>(alias)) {
      auto *rhs_type = infer(pa->expr, child_env, level + 1);
      auto *pat_type = infer_pattern(pa->pattern, child_env, level + 1);
      unifier_.unify(rhs_type, pat_type, pa->Range, "in pattern destructuring");
    } else if (dynamic_cast<LambdaAlias *>(alias)) {
      auto &prelim = lambda_prelims[lambda_idx++];
      recursive_self_contexts_.push_back({prelim.declaration});
      auto *fn_type = infer(prelim.la->lambda, child_env, level + 1);
      recursive_self_contexts_.pop_back();
      unifier_.unify(prelim.declaration, fn_type, prelim.la->lambda->Range,
                     "in recursive function '" + prelim.la->name->value + "'");
      // A recursive let group has one evolving effect graph.  Freezing
      // a scheme here would snapshot a sibling-derived cell before the
      // sibling has added its body/callback edges.  Generalize the
      // complete group below after every alias has been inferred.
      prelim.inferred = fn_type;
    }
  }

  for (const auto &prelim : lambda_prelims) {
    if (!prelim.inferred)
      continue;
    auto scheme = generalize(prelim.inferred, level);
    child_env->bind_scheme(prelim.la->name->value, scheme);
    if (const auto captures = function_capture_types_.find(prelim.la->lambda);
        captures != function_capture_types_.end())
      named_function_capture_types_[prelim.la->name->value] = captures->second;
  }

  for (const auto &prelim : lambda_prelims)
    predeclared_function_body_effects_.erase(prelim.la->lambda);

  return infer(node->expr, child_env, level);
}

// ===== Function =====

MonoTypePtr TypeChecker::infer_function(FunctionExpr *node,
                                        std::shared_ptr<TypeEnv> env,
                                        int level) {
  auto fn_env = env->child();

  MonoTypePtr declared_cursor = nullptr;
  if (node->type_signature.has_value())
    declared_cursor = from_ast_type(*node->type_signature, level);

  // Seed annotated parameters before checking the body. This is essential
  // for nominal record field access: `f : Request -> String; f r = r.path`
  // must bind `r` as Request while `.path` is inferred, not only unify the
  // completed function with its signature afterward.
  std::vector<MonoTypePtr> param_types;
  for (auto *pat : node->patterns) {
    MonoTypePtr param_var = nullptr;
    auto *resolved_declared = unifier_.resolve(declared_cursor);
    if (resolved_declared && resolved_declared->tag == MonoType::Arrow) {
      param_var = resolved_declared->param_type;
      declared_cursor = resolved_declared->return_type;
    } else {
      param_var = arena_.fresh_var(level);
      uf_.add_var(param_var->var_id, level);
    }
    param_types.push_back(param_var);

    // A function parameter is a pattern, not just an identifier.  Infer it
    // in the function scope so nested bindings (for example `Some x` or
    // `Pair left right`) are visible to the body, then constrain it to the
    // parameter's fresh type variable.
    auto *pattern_type = infer_pattern(pat, fn_env, level);
    unifier_.unify(param_var, pattern_type, pat->Range,
                   "in function parameter pattern");
  }

  // Capture types are retained as compile-time metadata for concurrency
  // boundaries. Sequential closures remain unrestricted; spawn and parallel
  // constructs later require each captured value to be Shareable.
  capture_frames_.push_back({fn_env.get(), {}, {}});

  // A function body is a least-derived effect cell.  Calls include their
  // callee expression into this cell; independent callbacks therefore form
  // an ACI join rather than competing for a single row tail.
  const auto predeclared = predeclared_function_body_effects_.find(node);
  const auto body_effect =
      predeclared == predeclared_function_body_effects_.end()
          ? arena_.effect_solver().derived()
          : predeclared->second;
  latent_effect_stack_.push_back({body_effect});
  MonoTypePtr body_type = nullptr;
  for (auto *body : node->bodies) {
    if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body)) {
      auto *bt = infer(bwg->expr, fn_env, level);
      if (!body_type)
        body_type = bt;
      else
        unifier_.unify(body_type, bt, node->Range, "in function body");
    }
  }
  if (!body_type)
    body_type = arena_.make_con(TyCon::Unit);
  if (declared_cursor)
    unifier_.unify(body_type, declared_cursor, node->Range,
                   "against the declared function result type");

  function_capture_types_[node] = capture_frames_.back().types;
  capture_frames_.pop_back();

  const CollectedRow collected = latent_effect_stack_.back();
  latent_effect_stack_.pop_back();

  // Build curried arrow type: a -> b -> c -> ret. Evaluation begins only
  // once the final source parameter is supplied; earlier partial
  // applications merely allocate a closure and therefore have an empty row.
  // Module summaries intentionally aggregate all stages in effect_row_info.
  // `\() -> body` parses with zero patterns; it is still a thunk (Unit -> ret).
  MonoTypePtr fn_type = body_type;
  if (param_types.empty())
    fn_type = arena_.make_arrow(arena_.make_con(TyCon::Unit), body_type,
                                collected.effect);
  else {
    for (int i = (int)param_types.size() - 1; i >= 0; i--)
      fn_type = arena_.make_arrow(param_types[i], fn_type,
                                  i == (int)param_types.size() - 1
                                      ? collected.effect
                                      : arena_.effect_solver().empty());
  }

  check_param_borrow_annotations(node);
  return fn_type;
}

void TypeChecker::check_param_borrow_annotations(FunctionExpr *node) {
  if (node->param_borrow.empty())
    return;

  for (size_t i = 0; i < node->patterns.size(); ++i) {
    bool want = i < node->param_borrow.size() && node->param_borrow[i];
    if (!want)
      continue;

    auto *pat = node->patterns[i];
    if (pat->get_type() != AST_PATTERN_VALUE) {
      diag_.error(pat->Range, ErrorCode::E0603,
                  "`@borrow` is only allowed on simple identifier parameters");
      error_count_++;
      continue;
    }
    auto *pv = static_cast<PatternValue *>(pat);
    if (!std::holds_alternative<IdentifierExpr *>(pv->expr)) {
      diag_.error(pat->Range, ErrorCode::E0603,
                  "`@borrow` is only allowed on simple identifier parameters");
      error_count_++;
      continue;
    }
    const std::string &pname =
        std::get<IdentifierExpr *>(pv->expr)->name->value;

    bool escapes = false;
    for (auto *body : node->bodies) {
      if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body)) {
        if (compiler::analysis::heap_param_may_escape(bwg->expr, pname, true))
          escapes = true;
      } else if (auto *g = dynamic_cast<BodyWithGuards *>(body)) {
        if (compiler::analysis::heap_param_may_escape(g->guard, pname, false) ||
            compiler::analysis::heap_param_may_escape(g->expr, pname, true))
          escapes = true;
      }
    }
    if (escapes) {
      diag_.error(pat->Range, ErrorCode::E0603,
                  "borrowed parameter '" + pname +
                      "' must not escape "
                      "(return, store in literal, capture in closure, or be a "
                      "case scrutinee)");
      error_count_++;
    }
  }
}

// ===== Application =====

MonoTypePtr TypeChecker::infer_apply(ApplyExpr *node,
                                     std::shared_ptr<TypeEnv> env, int level) {
  // Infer callee
  MonoTypePtr callee_type = nullptr;
  size_t constraint_begin = deferred_constraints_.size();
  size_t constraint_end = constraint_begin;
  if (auto *nc = dynamic_cast<NameCall *>(node->call)) {
    auto scheme = env->lookup(nc->name->value);
    if (scheme) {
      callee_type = instantiate(*scheme, level);
      constraint_end = deferred_constraints_.size();
      for (size_t i = constraint_begin; i < constraint_end; ++i)
        deferred_constraints_[i].origin = node;
    } else {
      std::string msg = "undefined function '" + nc->name->value + "'";
      auto names = env->all_names();
      std::string best;
      size_t best_dist = 4;
      for (auto &n : names) {
        auto d = edit_distance(nc->name->value, n);
        if (d < best_dist) {
          best_dist = d;
          best = n;
        }
      }
      if (!best.empty())
        msg += "; did you mean '" + best + "'?";
      diag_.error(node->Range, ErrorCode::E0104, msg);
      error_count_++;
      return arena_.fresh_var(level);
    }
  } else if (auto *ec = dynamic_cast<ExprCall *>(node->call)) {
    callee_type = infer(ec->expr, env, level);
  } else {
    callee_type = arena_.fresh_var(level);
    uf_.add_var(callee_type->var_id, level);
  }

  bool direct_recursive_call = false;
  if (auto *initial = unifier_.resolve(callee_type)) {
    for (const auto &self : recursive_self_contexts_) {
      if (self.preliminary && unifier_.resolve(self.preliminary) == initial) {
        direct_recursive_call = true;
        break;
      }
      for (const auto continuation : self.continuations) {
        if (continuation == callee_type ||
            (continuation && unifier_.resolve(continuation) == initial)) {
          direct_recursive_call = true;
          break;
        }
      }
      if (direct_recursive_call)
        break;
    }
  }

  // Apply each argument
  MonoTypePtr result_type = callee_type;
  for (auto &arg_variant : node->args) {
    AstNode *arg_node =
        std::holds_alternative<ExprNode *>(arg_variant)
            ? static_cast<AstNode *>(std::get<ExprNode *>(arg_variant))
            : static_cast<AstNode *>(std::get<ValueExpr *>(arg_variant));

    auto *arg_type = infer(arg_node, env, level);

    auto *resolved = unifier_.resolve(result_type);

    auto *result_var = arena_.fresh_var(level);
    uf_.add_var(result_var->var_id, level);
    auto *expected_fn = arena_.make_arrow(arg_type, result_var,
                                          arena_.effect_solver().flexible());

    if (!unifier_.unify(result_type, expected_fn, node->Range,
                        "in function application")) {
      // Imported `.yonai` tags distinguish SEQ from ADT (Stream). A
      // failed apply here is a hard mismatch, not partial inference â€”
      // increment so CLI/`has_direct_errors()` reject the program.
      auto is_app = [](MonoTypePtr t, const char *name) {
        return t && t->tag == MonoType::App && t->type_name == name;
      };
      auto is_collection = [&](MonoTypePtr t) {
        return is_app(t, "Seq") || is_app(t, "Set") || is_app(t, "Dict");
      };
      auto *expected_param = unifier_.resolve(result_type);
      if (expected_param && expected_param->tag == MonoType::Arrow)
        expected_param = unifier_.resolve(expected_param->param_type);
      auto *actual = unifier_.resolve(arg_type);
      if ((is_app(expected_param, "ADT") && is_collection(actual)) ||
          (is_collection(expected_param) && is_app(actual, "ADT"))) {
        if (is_app(actual, "Seq") || is_app(expected_param, "Seq"))
          diag_.note(node->Range,
                     "a Seq is not a Stream; wrap a sequence with fromSeq");
        error_count_++;
      }
      return result_var;
    }

    // After unification a formerly-unknown callee resolves to the
    // expected arrow.  Its solver effect is now equal to the declared or
    // inferred arrow effect, so inclusion records the exact call edge.
    auto *after = unifier_.resolve(resolved);
    if (!direct_recursive_call)
      apply_callee_effects(after, node->Range);

    if (direct_recursive_call && after && after->tag == MonoType::Arrow) {
      const auto continuation = after->return_type;
      for (auto &self : recursive_self_contexts_)
        if (std::find(self.continuations.begin(), self.continuations.end(),
                      continuation) == self.continuations.end())
          self.continuations.push_back(continuation);
    }

    // Keep the original return type so inner-arrow effects survive multi-arg
    // apply
    if (after && after->tag == MonoType::Arrow && after->return_type)
      result_type = after->return_type;
    else
      result_type = result_var;
  }

  std::string concurrency_callee;
  std::optional<ConcurrencyBoundary> concurrency_boundary;
  for (ApplyExpr *current = node; current;) {
    if (auto *named = dynamic_cast<NameCall *>(current->call)) {
      concurrency_callee = named->name->value;
      if (const auto found = concurrency_boundaries_.find(concurrency_callee);
          found != concurrency_boundaries_.end())
        concurrency_boundary = found->second;
      break;
    }
    if (auto *expression = dynamic_cast<ExprCall *>(current->call)) {
      current = dynamic_cast<ApplyExpr *>(expression->expr);
      continue;
    }
    break;
  }
  if (concurrency_boundary)
    enforce_concurrency_boundary(node, concurrency_callee,
                                 concurrency_boundary);

  return unifier_.resolve(result_type);
}

void TypeChecker::require_trait(const std::string &trait_name, MonoTypePtr type,
                                const SourceRange &loc, std::string context) {
  if (!type)
    return;
  deferred_constraints_.emplace_back(trait_name, unifier_.resolve(type), loc,
                                     std::move(context));
}

void TypeChecker::require_captures_shareable(
    const std::vector<MonoTypePtr> &captures, const SourceRange &loc,
    const std::string &context) {
  for (auto *capture : captures)
    if (auto *resolved = unifier_.resolve(capture);
        resolved && resolved->tag != MonoType::Arrow)
      require_trait("Shareable", resolved, loc, context);
}

void TypeChecker::enforce_concurrency_boundary(
    ApplyExpr *node, const std::string &callee_name,
    std::optional<ConcurrencyBoundary> boundary) {
  if (!boundary)
    return;
  std::vector<AstNode *> arguments;
  std::vector<ApplyExpr *> chain;
  for (ApplyExpr *current = node; current;) {
    chain.push_back(current);
    if (auto *expression = dynamic_cast<ExprCall *>(current->call))
      current = dynamic_cast<ApplyExpr *>(expression->expr);
    else
      break;
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (auto &argument : (*it)->args)
      arguments.push_back(
          std::holds_alternative<ExprNode *>(argument)
              ? static_cast<AstNode *>(std::get<ExprNode *>(argument))
              : static_cast<AstNode *>(std::get<ValueExpr *>(argument)));
  }

  if (*boundary == ConcurrencyBoundary::ChannelSend && arguments.size() == 2) {
    require_trait("Send", type_of(arguments[1]), node->Range,
                  "value sent through Std\\Channel.send");
    return;
  }
  if (*boundary != ConcurrencyBoundary::TaskSpawn || arguments.size() != 1)
    return;

  auto *thunk_type = zonk(type_of(arguments.front()));
  if (thunk_type && thunk_type->tag == MonoType::Arrow)
    require_trait("Send", thunk_type->return_type, node->Range,
                  "result returned from Std\\Task.spawn");

  std::vector<MonoTypePtr> captures;
  if (auto *function = dynamic_cast<FunctionExpr *>(arguments.front())) {
    if (const auto found = function_capture_types_.find(function);
        found != function_capture_types_.end())
      captures = found->second;
  } else if (auto *identifier =
                 dynamic_cast<IdentifierExpr *>(arguments.front())) {
    if (const auto found =
            named_function_capture_types_.find(identifier->name->value);
        found != named_function_capture_types_.end())
      captures = found->second;
  }
  require_captures_shareable(
      captures, node->Range,
      "value captured by the closure passed to Std\\Task.spawn");
}

std::optional<EffectRef> TypeChecker::callee_effect(MonoTypePtr callee) {
  callee = unifier_.resolve(callee);
  if (!callee)
    return std::nullopt;
  if (callee->tag == MonoType::Arrow &&
      callee->effect_solver == &arena_.effect_solver() &&
      callee->arrow_effect.valid())
    return callee->arrow_effect;
  return std::nullopt;
}

void TypeChecker::include_ambient_effect(EffectRef effect,
                                         const SourceRange &loc,
                                         const std::string &context) {
  if (latent_effect_stack_.empty())
    return;
  const auto result = arena_.effect_solver().include(
      effect, latent_effect_stack_.back().effect);
  if (result == EffectConstraintResult::Conflict) {
    diag_.error(loc, ErrorCode::E0100,
                "incompatible effect constraints " + context);
    ++error_count_;
  }
}

void TypeChecker::apply_callee_effects(MonoTypePtr callee,
                                       const SourceRange &apply_loc) {
  const auto effect = callee_effect(callee);
  if (!effect)
    return;
  if (!latent_effect_stack_.empty()) {
    include_ambient_effect(*effect, apply_loc, "in function application");
    return;
  }

  const auto summary = arena_.effect_solver().summarize(*effect);
  for (const auto &op : summary.known_labels) {
    const auto origin = effect_origins_.find(op);
    const auto &loc =
        origin != effect_origins_.end() && origin->second.isValid()
            ? origin->second
            : apply_loc;
    diag_.error(loc, ErrorCode::E0202,
                "unhandled effect operation '" + op + "'");
    diag_.note(apply_loc, "applied here with no covering handler for " + op);
    ++error_count_;
  }
}

void TypeChecker::collect_effect_roots(MonoTypePtr type,
                                       std::vector<EffectRef> &roots) {
  type = unifier_.resolve(type);
  if (!type)
    return;
  if (const auto effect = callee_effect(type)) {
    if (std::find(roots.begin(), roots.end(), *effect) == roots.end())
      roots.push_back(*effect);
  }
  switch (type->tag) {
  case MonoType::Arrow:
    collect_effect_roots(type->param_type, roots);
    collect_effect_roots(type->return_type, roots);
    break;
  case MonoType::App:
    for (const auto input : type->args)
      collect_effect_roots(input, roots);
    break;
  case MonoType::MTuple:
    for (const auto input : type->elements)
      collect_effect_roots(input, roots);
    break;
  case MonoType::MRecord:
    for (const auto &[_, input] : type->record_fields)
      collect_effect_roots(input, roots);
    break;
  case MonoType::Var:
  case MonoType::Con:
    break;
  }
}

// ===== If =====

MonoTypePtr TypeChecker::infer_if(IfExpr *node, std::shared_ptr<TypeEnv> env,
                                  int level) {
  auto *cond_type = infer(node->condition, env, level);
  unifier_.unify(cond_type, arena_.make_con(TyCon::Bool), node->Range,
                 "in if condition (expected Bool)");

  auto *then_type = infer(node->thenExpr, env, level);
  auto *else_type = infer(node->elseExpr, env, level);
  unifier_.unify(then_type, else_type, node->Range,
                 "in if branches (then and else must have same type)");

  return unifier_.resolve(then_type);
}

// ===== Binary Operators =====

std::string TypeChecker::op_name(AstNodeType type) {
  switch (type) {
  case AST_ADD_EXPR:
    return "+";
  case AST_SUBTRACT_EXPR:
    return "-";
  case AST_MULTIPLY_EXPR:
    return "*";
  case AST_DIVIDE_EXPR:
    return "/";
  case AST_MODULO_EXPR:
    return "%";
  case AST_POWER_EXPR:
    return "**";
  case AST_EQ_EXPR:
    return "==";
  case AST_NEQ_EXPR:
    return "!=";
  case AST_LT_EXPR:
    return "<";
  case AST_GT_EXPR:
    return ">";
  case AST_LTE_EXPR:
    return "<=";
  case AST_GTE_EXPR:
    return ">=";
  case AST_LOGICAL_AND_EXPR:
    return "&&";
  case AST_LOGICAL_OR_EXPR:
    return "||";
  case AST_JOIN_EXPR:
    return "++";
  case AST_REMOVE_EXPR:
    return "--";
  case AST_IN_EXPR:
    return "in";
  case AST_CONS_LEFT_EXPR:
    return "::";
  case AST_PIPE_LEFT_EXPR:
    return "<|";
  case AST_PIPE_RIGHT_EXPR:
    return "|>";
  default:
    return "?";
  }
}

MonoTypePtr TypeChecker::infer_binary(BinaryOpExpr *node,
                                      std::shared_ptr<TypeEnv> env, int level) {
  // The parser makes ++ chains left-associative. Infer the left spine
  // iteratively so long normal concatenations do not consume one native
  // stack frame per operand in Windows Debug builds.
  if (node->get_type() == AST_JOIN_EXPR &&
      dynamic_cast<JoinExpr *>(node->left)) {
    std::vector<ExprNode *> operands;
    ExprNode *current = node;
    while (auto *join = dynamic_cast<JoinExpr *>(current)) {
      operands.push_back(join->right);
      current = join->left;
    }
    operands.push_back(current);
    std::reverse(operands.begin(), operands.end());

    auto *result = infer(operands.front(), env, level);
    const auto is_string = [](MonoTypePtr type) {
      return type && type->tag == MonoType::Con && type->con == TyCon::String;
    };
    for (size_t i = 1; i < operands.size(); ++i) {
      auto *right_type = infer(operands[i], env, level);
      if (is_string(unifier_.resolve(result)) ||
          is_string(unifier_.resolve(right_type))) {
        auto *string = arena_.make_con(TyCon::String);
        unifier_.unify(
            result, string, node->Range,
            "in operator ++ (both operands must be String or both Seq)");
        unifier_.unify(
            right_type, string, node->Range,
            "in operator ++ (both operands must be String or both Seq)");
        result = string;
      } else {
        auto *element = arena_.fresh_var(level);
        uf_.add_var(element->var_id, level);
        auto *sequence = arena_.make_app("Seq", {element});
        unifier_.unify(result, sequence, node->Range,
                       "in operator ++ (left sequence)");
        unifier_.unify(right_type, sequence, node->Range,
                       "in operator ++ (right sequence)");
        result = unifier_.resolve(sequence);
      }
    }
    return result;
  }

  auto *left_type = infer(node->left, env, level);
  auto *right_type = infer(node->right, env, level);

  std::string name = op_name(node->get_type());

  // Equality and ordering are trait-governed source operations. Keep the
  // ordinary operator scheme for operand/result unification, but record the
  // semantic obligation here so every frontend mode validates it before
  // LLVM lowering. `!=` shares Eq.eq and the relational operators share
  // Ord.compare; there is deliberately no second operator-specific contract.
  if (name == "==" || name == "!=") {
    deferred_constraints_.push_back(
        {"Eq", left_type, node->Range, "required by operator '" + name + "'"});
  } else if (name == "<" || name == ">" || name == "<=" || name == ">=") {
    deferred_constraints_.push_back(
        {"Ord", left_type, node->Range, "required by operator '" + name + "'"});
  }

  if (name == "++") {
    auto *left = unifier_.resolve(left_type);
    auto *right = unifier_.resolve(right_type);
    const auto is_string = [](MonoTypePtr type) {
      return type && type->tag == MonoType::Con && type->con == TyCon::String;
    };
    if (is_string(left) || is_string(right)) {
      auto *string = arena_.make_con(TyCon::String);
      unifier_.unify(
          left_type, string, node->Range,
          "in operator ++ (both operands must be String or both Seq)");
      unifier_.unify(
          right_type, string, node->Range,
          "in operator ++ (both operands must be String or both Seq)");
      return string;
    }
    auto *element = arena_.fresh_var(level);
    uf_.add_var(element->var_id, level);
    auto *sequence = arena_.make_app("Seq", {element});
    unifier_.unify(left_type, sequence, node->Range,
                   "in operator ++ (left sequence)");
    unifier_.unify(right_type, sequence, node->Range,
                   "in operator ++ (right sequence)");
    return unifier_.resolve(sequence);
  }

  auto scheme = env->lookup(name);
  if (!scheme) {
    // Fallback: assume left_type -> left_type -> left_type
    unifier_.unify(left_type, right_type, node->Range, "in operator " + name);
    return unifier_.resolve(left_type);
  }

  // Instantiate operator type and unify with arguments
  auto *op_type = instantiate(*scheme, level);
  auto *result_var = arena_.fresh_var(level);
  uf_.add_var(result_var->var_id, level);

  auto *expected =
      arena_.make_arrow(left_type, arena_.make_arrow(right_type, result_var));
  unifier_.unify(op_type, expected, node->Range, "in operator " + name);

  return unifier_.resolve(result_var);
}

// ===== Tuple =====

MonoTypePtr TypeChecker::infer_tuple(TupleExpr *node,
                                     std::shared_ptr<TypeEnv> env, int level) {
  std::vector<MonoTypePtr> elem_types;
  for (auto *v : node->values)
    elem_types.push_back(infer(v, env, level));
  return arena_.make_tuple(elem_types);
}

// ===== Sequence =====

MonoTypePtr TypeChecker::infer_seq(ValuesSequenceExpr *node,
                                   std::shared_ptr<TypeEnv> env, int level) {
  if (node->values.empty())
    return arena_.make_app("Seq", {arena_.fresh_var(level)});

  auto *elem_type = infer(node->values[0], env, level);
  for (size_t i = 1; i < node->values.size(); i++) {
    auto *t = infer(node->values[i], env, level);
    unifier_.unify(elem_type, t, node->Range,
                   "in sequence literal (all elements must have same type)");
  }
  return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
}

// ===== Do Block =====

MonoTypePtr TypeChecker::infer_do(DoExpr *node, std::shared_ptr<TypeEnv> env,
                                  int level) {
  MonoTypePtr last_type = arena_.make_con(TyCon::Unit);
  for (auto *step : node->steps)
    last_type = infer(step, env, level);
  return last_type;
}

// ===== Generalization =====

void TypeChecker::collect_free_vars(MonoTypePtr type, int level,
                                    std::vector<TypeId> &vars) {
  type = unifier_.resolve(type);
  if (!type)
    return;
  if (type->tag == MonoType::Var) {
    if (uf_.level(type->var_id) > level) {
      // Check not already collected
      for (auto id : vars)
        if (id == type->var_id)
          return;
      vars.push_back(type->var_id);
    }
    return;
  }
  if (type->tag == MonoType::Arrow) {
    collect_free_vars(type->param_type, level, vars);
    collect_free_vars(type->return_type, level, vars);
  }
  if (type->tag == MonoType::App)
    for (auto *a : type->args)
      collect_free_vars(a, level, vars);
  if (type->tag == MonoType::MTuple)
    for (auto *e : type->elements)
      collect_free_vars(e, level, vars);
  if (type->tag == MonoType::MRecord) {
    for (auto &[_, ft] : type->record_fields)
      collect_free_vars(ft, level, vars);
    if (type->row_rest)
      collect_free_vars(type->row_rest, level, vars);
  }
}

TypeScheme TypeChecker::generalize(MonoTypePtr type, int level) {
  type = zonk(type);
  std::vector<TypeId> free_vars;
  collect_free_vars(type, level, free_vars);
  TypeScheme scheme(free_vars, type);
  collect_effect_roots(type, scheme.effect_roots);
  if (!scheme.effect_roots.empty())
    scheme.effect_graph = arena_.effect_solver().freeze(scheme.effect_roots);
  return scheme;
}

MonoTypePtr TypeChecker::substitute(
    MonoTypePtr type, const std::unordered_map<TypeId, MonoTypePtr> &subst,
    const std::vector<std::pair<EffectRef, EffectRef>> &effect_subst) {
  type = unifier_.resolve(type);
  if (!type)
    return nullptr;
  if (type->tag == MonoType::Var) {
    auto it = subst.find(type->var_id);
    if (it != subst.end())
      return it->second;
    return type;
  }
  if (type->tag == MonoType::Con)
    return type;
  if (type->tag == MonoType::Arrow) {
    auto effect = type->arrow_effect;
    for (const auto &[source, replacement] : effect_subst)
      if (effect == source) {
        effect = replacement;
        break;
      }
    return arena_.make_arrow(substitute(type->param_type, subst, effect_subst),
                             substitute(type->return_type, subst, effect_subst),
                             effect);
  }
  if (type->tag == MonoType::App) {
    std::vector<MonoTypePtr> new_args;
    for (auto *a : type->args)
      new_args.push_back(substitute(a, subst, effect_subst));
    return arena_.make_app(type->type_name, new_args);
  }
  if (type->tag == MonoType::MTuple) {
    std::vector<MonoTypePtr> new_elems;
    for (auto *e : type->elements)
      new_elems.push_back(substitute(e, subst, effect_subst));
    return arena_.make_tuple(new_elems);
  }
  if (type->tag == MonoType::MRecord) {
    std::vector<std::pair<std::string, MonoTypePtr>> new_fields;
    for (auto &[name, ft] : type->record_fields)
      new_fields.push_back({name, substitute(ft, subst, effect_subst)});
    MonoTypePtr new_rest = type->row_rest
                               ? substitute(type->row_rest, subst, effect_subst)
                               : nullptr;
    return arena_.make_record(new_fields, new_rest);
  }
  return type;
}

MonoTypePtr TypeChecker::instantiate(const TypeScheme &scheme, int level) {
  if (scheme.quantified_vars.empty() && scheme.constraints.empty() &&
      !scheme.effect_graph)
    return scheme.body;

  std::unordered_map<TypeId, MonoTypePtr> subst;
  for (auto id : scheme.quantified_vars) {
    auto *fresh = arena_.fresh_var(level);
    uf_.add_var(fresh->var_id, level);
    subst[id] = fresh;
  }

  std::vector<std::pair<EffectRef, EffectRef>> effect_subst;
  if (scheme.effect_graph) {
    const auto graph = arena_.effect_solver().instantiate(*scheme.effect_graph);
    if (graph.roots.size() != scheme.effect_roots.size())
      throw std::logic_error(
          "effect graph root count changed during instantiation");
    effect_subst.reserve(graph.roots.size());
    for (size_t index = 0; index < graph.roots.size(); ++index)
      effect_subst.emplace_back(scheme.effect_roots[index], graph.roots[index]);
  }

  // Record deferred constraints (substituted)
  for (auto &c : scheme.constraints) {
    std::vector<MonoTypePtr> arguments;
    for (auto *type : c.types)
      arguments.push_back(substitute(type, subst, effect_subst));
    deferred_constraints_.emplace_back(c.trait_name, std::move(arguments),
                                       SourceRange{}, "");
  }

  return substitute(scheme.body, subst, effect_subst);
}

// ===== Case Expression =====

MonoTypePtr TypeChecker::infer_case(CaseExpr *node,
                                    std::shared_ptr<TypeEnv> env, int level) {
  auto *scrut_type = infer(node->expr, env, level);

  MonoTypePtr result_type = nullptr;

  for (auto *clause : node->clauses) {
    auto clause_env = env->child();

    // Infer pattern and bind variables
    auto *pat_type = infer_pattern(clause->pattern, clause_env, level);
    unifier_.unify(scrut_type, pat_type, clause->Range, "in case pattern");

    // Infer body
    auto *body_type = infer(clause->body, clause_env, level);

    if (!result_type)
      result_type = body_type;
    else
      unifier_.unify(result_type, body_type, clause->Range,
                     "in case branches (all must have same type)");
  }

  return result_type ? unifier_.resolve(result_type)
                     : arena_.make_con(TyCon::Unit);
}

// ===== Pattern Inference =====

MonoTypePtr TypeChecker::infer_pattern(PatternNode *pat,
                                       std::shared_ptr<TypeEnv> env,
                                       int level) {
  if (!pat)
    return arena_.fresh_var(level);

  switch (pat->get_type()) {
  case AST_UNDERSCORE_PATTERN: {
    // Wildcard: matches anything
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_PATTERN_VALUE: {
    auto *pv = static_cast<PatternValue *>(pat);
    // Identifier binding: fresh var, bind in env
    if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr)) {
      auto *v = arena_.fresh_var(level);
      uf_.add_var(v->var_id, level);
      env->bind((*id)->name->value, v);
      return v;
    }
    // Symbol literal
    if (std::get_if<SymbolExpr *>(&pv->expr))
      return arena_.make_con(TyCon::Symbol);
    // Integer literal
    if (auto *lit = std::get_if<LiteralExpr<void *> *>(&pv->expr)) {
      auto *an = static_cast<AstNode *>(*lit);
      if (an->get_type() == AST_INTEGER_EXPR)
        return arena_.make_con(TyCon::Int);
      if (an->get_type() == AST_FLOAT_EXPR)
        return arena_.make_con(TyCon::Float);
      if (an->get_type() == AST_STRING_EXPR)
        return arena_.make_con(TyCon::String);
    }
    // Fallback
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_HEAD_TAILS_PATTERN: {
    auto *htp = static_cast<HeadTailsPattern *>(pat);
    auto *elem_type = arena_.fresh_var(level);
    uf_.add_var(elem_type->var_id, level);

    // Head patterns: each must match elem_type
    for (auto *head_pat : htp->heads) {
      auto *head_type = infer_pattern(head_pat, env, level);
      unifier_.unify(elem_type, head_type, pat->Range, "in head-tail pattern");
    }

    // Tail: must be Seq(elem_type)
    if (htp->tail) {
      auto *tail_type = infer_pattern(htp->tail, env, level);
      auto *seq_type = arena_.make_app("Seq", {elem_type});
      unifier_.unify(tail_type, seq_type, pat->Range, "in tail pattern");
    }

    return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
  }

  case AST_SEQ_PATTERN: {
    auto *sp = static_cast<SeqPattern *>(pat);
    auto *elem_type = arena_.fresh_var(level);
    uf_.add_var(elem_type->var_id, level);
    for (auto *sub : sp->patterns) {
      auto *sub_type = infer_pattern(sub, env, level);
      unifier_.unify(elem_type, sub_type, pat->Range, "in sequence pattern");
    }
    return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
  }

  case AST_TUPLE_PATTERN: {
    auto *tp = static_cast<TuplePattern *>(pat);
    if (tp->patterns.empty())
      return arena_.make_con(TyCon::Unit);
    std::vector<MonoTypePtr> elem_types;
    for (auto *sub : tp->patterns)
      elem_types.push_back(infer_pattern(sub, env, level));
    return arena_.make_tuple(elem_types);
  }

  case AST_CONSTRUCTOR_PATTERN: {
    auto *cp = static_cast<ConstructorPattern *>(pat);
    auto ctor_it = constructor_registry_.find(cp->constructor_name);
    if (ctor_it != constructor_registry_.end()) {
      auto &info = ctor_it->second;
      // Instantiate ADT type params with fresh vars
      std::vector<MonoTypePtr> type_arg_vars;
      for (size_t i = 0; i < info.type_params.size(); i++) {
        auto *v = arena_.fresh_var(level);
        uf_.add_var(v->var_id, level);
        type_arg_vars.push_back(v);
      }
      auto field_type =
          [&](const ast::FieldType &field,
              const std::unordered_map<std::string, MonoTypePtr> &params,
              const auto &self) -> MonoTypePtr {
        if (field.is_tuple_type) {
          std::vector<MonoTypePtr> elements;
          for (const auto &element : field.tuple_types)
            elements.push_back(self(element, params, self));
          return arena_.make_tuple(elements);
        }
        if (field.is_function_type) {
          MonoTypePtr result =
              field.return_types.empty()
                  ? arena_.make_con(TyCon::Unit)
                  : self(field.return_types.front(), params, self);
          for (auto it = field.param_types.rbegin();
               it != field.param_types.rend(); ++it)
            result = arena_.make_arrow(self(*it, params, self), result);
          return result;
        }
        if (auto it = params.find(field.name); it != params.end())
          return it->second;
        if (field.name == "Int")
          return arena_.make_con(TyCon::Int);
        if (field.name == "Float")
          return arena_.make_con(TyCon::Float);
        if (field.name == "Bool")
          return arena_.make_con(TyCon::Bool);
        if (field.name == "String")
          return arena_.make_con(TyCon::String);
        if (field.name == "Symbol")
          return arena_.make_con(TyCon::Symbol);
        if (field.name == "()" || field.name == "Unit")
          return arena_.make_con(TyCon::Unit);
        std::vector<MonoTypePtr> arguments;
        for (const auto &argument : field.type_arguments)
          arguments.push_back(self(argument, params, self));
        return arena_.make_app(field.name, arguments);
      };
      std::unordered_map<std::string, MonoTypePtr> params;
      for (size_t i = 0;
           i < info.type_params.size() && i < type_arg_vars.size(); ++i)
        params[info.type_params[i]] = type_arg_vars[i];

      // Constructor fields and tuple elements are distinct levels
      // of structure. `Box (Int, Int)` declares one tuple field,
      // so its pattern is `Box ((a, b))`, not `Box (a, b)`.
      // Infer malformed sub-patterns to keep their bindings usable
      // while reporting the structural error only once.
      if (cp->sub_patterns.size() != info.field_types.size()) {
        for (auto *sub_pattern : cp->sub_patterns)
          infer_pattern(sub_pattern, env, level);

        const auto actual_count = cp->sub_patterns.size();
        const auto declared_count = info.field_types.size();
        std::string message = "constructor pattern '" + cp->constructor_name +
                              "' has " + std::to_string(actual_count) +
                              (actual_count == 1 ? " field" : " fields") +
                              ", but constructor declares " +
                              std::to_string(declared_count) +
                              (declared_count == 1 ? " field" : " fields");
        if (declared_count == 1)
          message += " of type " + info.field_types[0].to_string();
        diag_.error(pat->Range, ErrorCode::E0100, message);

        if (declared_count == 1 && info.field_types[0].is_tuple_type) {
          diag_.note(pat->Range,
                     "match the tuple field with " + cp->constructor_name +
                         " ((first, second)); " + cp->constructor_name +
                         " (first, second) supplies two constructor fields");
        }
        return arena_.make_app(info.adt_name, type_arg_vars);
      }

      // Bind every sub-pattern against its canonical declared field type.
      for (size_t i = 0; i < cp->sub_patterns.size(); i++) {
        auto *sub_type = infer_pattern(cp->sub_patterns[i], env, level);
        auto *expected = field_type(info.field_types[i], params, field_type);
        if (!unifier_.unify(sub_type, expected, pat->Range,
                            "in field " + std::to_string(i + 1) +
                                " of constructor pattern '" +
                                cp->constructor_name + "'")) {
          const auto &declared = info.field_types[i];
          diag_.note(pat->Range,
                     "'" + cp->constructor_name + "' declares field " +
                         std::to_string(i + 1) + " as " + declared.to_string());
        }
      }
      return arena_.make_app(info.adt_name, type_arg_vars);
    }
    // Unknown constructor â€” fresh var
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    for (auto *sub : cp->sub_patterns)
      infer_pattern(sub, env, level);
    return v;
  }

  case AST_TYPED_PATTERN: {
    auto *tp = static_cast<TypedPattern *>(pat);
    // Map type name to MonoType
    MonoTypePtr bound_type;
    if (tp->type_name == "Int")
      bound_type = arena_.make_con(TyCon::Int);
    else if (tp->type_name == "Float")
      bound_type = arena_.make_con(TyCon::Float);
    else if (tp->type_name == "Bool")
      bound_type = arena_.make_con(TyCon::Bool);
    else if (tp->type_name == "String")
      bound_type = arena_.make_con(TyCon::String);
    else if (tp->type_name == "Symbol")
      bound_type = arena_.make_con(TyCon::Symbol);
    else if (tp->type_name == "ByteArray")
      bound_type = arena_.make_con(TyCon::ByteArray);
    else {
      // Unknown or ADT type â€” use a named App type
      bound_type = arena_.make_app(tp->type_name, {});
    }
    env->bind(tp->binding_name, bound_type);
    // The pattern matches a sum type containing this alternative
    // Return the scrutinee type (sum) rather than the inner type
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_OR_PATTERN: {
    auto *op = static_cast<OrPattern *>(pat);
    MonoTypePtr or_type = nullptr;
    for (auto &alt : op->patterns) {
      auto *alt_type = infer_pattern(alt.get(), env, level);
      if (!or_type)
        or_type = alt_type;
      else
        unifier_.unify(or_type, alt_type, pat->Range, "in or-pattern");
    }
    return or_type ? or_type : arena_.fresh_var(level);
  }

  case AST_DICT_PATTERN: {
    auto *dp = static_cast<DictPattern *>(pat);
    auto *key_var = arena_.fresh_var(level);
    uf_.add_var(key_var->var_id, level);
    auto *val_var = arena_.fresh_var(level);
    uf_.add_var(val_var->var_id, level);
    for (auto &[key_pat, val_pat] : dp->keyValuePairs) {
      auto *kt = infer_pattern(key_pat, env, level);
      auto *vt = infer_pattern(val_pat, env, level);
      unifier_.unify(key_var, kt, pat->Range, "in dict pattern key");
      unifier_.unify(val_var, vt, pat->Range, "in dict pattern value");
    }
    return arena_.make_app(
        "Dict", {unifier_.resolve(key_var), unifier_.resolve(val_var)});
  }

  case AST_RECORD_PATTERN: {
    auto *rp = static_cast<RecordPattern *>(pat);
    for (auto &[name_expr, sub_pat] : rp->items) {
      auto *sub_type = infer_pattern(sub_pat, env, level);
      if (name_expr && sub_pat->get_type() == AST_PATTERN_VALUE) {
        auto *pv = static_cast<PatternValue *>(sub_pat);
        if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr))
          env->bind((*id)->name->value, sub_type);
      }
    }
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_AS_DATA_STRUCTURE_PATTERN: {
    auto *asp = static_cast<AsDataStructurePattern *>(pat);
    auto *inner_type =
        infer_pattern(static_cast<PatternNode *>(asp->pattern), env, level);
    if (asp->identifier)
      env->bind(asp->identifier->name->value, inner_type);
    return inner_type;
  }

  default: {
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }
  }
}

// ===== Collection Extractor Binding =====

void TypeChecker::bind_collection_extractor(CollectionExtractorExpr *ce,
                                            std::shared_ptr<TypeEnv> env,
                                            int level) {
  if (!ce)
    return;

  if (auto *vce = dynamic_cast<ValueCollectionExtractorExpr *>(ce)) {
    // Infer collection type to get element type
    auto *elem_type = arena_.fresh_var(level);
    uf_.add_var(elem_type->var_id, level);
    if (vce->collection) {
      auto *col_type = infer(vce->collection, env, level);
      auto *resolved = unifier_.resolve(col_type);
      if (resolved && resolved->tag == MonoType::App &&
          resolved->type_name == "Iterator" && !resolved->args.empty()) {
        unifier_.unify(resolved->args.front(), elem_type, ce->Range,
                       "in iterator generator");
      } else {
        auto *expected = arena_.make_app("Seq", {elem_type});
        unifier_.unify(col_type, expected, ce->Range,
                       "in generator collection");
      }
    }
    // Bind the iteration variable
    if (auto *id = std::get_if<IdentifierExpr *>(&vce->expr))
      env->bind((*id)->name->value, elem_type);
    // Infer guard condition if present
    if (vce->condition)
      infer(vce->condition, env, level);
  } else if (auto *kvce = dynamic_cast<KeyValueCollectionExtractorExpr *>(ce)) {
    auto *key_type = arena_.fresh_var(level);
    uf_.add_var(key_type->var_id, level);
    auto *val_type = arena_.fresh_var(level);
    uf_.add_var(val_type->var_id, level);
    if (kvce->collection) {
      auto *col_type = infer(kvce->collection, env, level);
      auto *expected = arena_.make_app("Dict", {key_type, val_type});
      unifier_.unify(col_type, expected, ce->Range,
                     "in dict generator collection");
    }
    if (auto *id = std::get_if<IdentifierExpr *>(&kvce->keyExpr))
      env->bind((*id)->name->value, key_type);
    if (auto *id = std::get_if<IdentifierExpr *>(&kvce->valueExpr))
      env->bind((*id)->name->value, val_type);
    if (kvce->condition)
      infer(kvce->condition, env, level);
  }
}

// ===== Cons =====

MonoTypePtr TypeChecker::infer_cons(ConsLeftExpr *node,
                                    std::shared_ptr<TypeEnv> env, int level) {
  auto *elem_type = infer(node->left, env, level);
  auto *seq_type = infer(node->right, env, level);
  auto *expected_seq = arena_.make_app("Seq", {elem_type});
  unifier_.unify(seq_type, expected_seq, node->Range, "in cons (::) operator");
  return unifier_.resolve(expected_seq);
}

// ===== ADT Registration =====

void TypeChecker::register_adt(
    const std::string &type_name, const std::vector<std::string> &type_params,
    const std::vector<std::pair<std::string, int>> &constructors,
    const std::vector<std::vector<ast::FieldType>> &field_types,
    const std::vector<std::vector<std::string>> &field_names) {
  if (field_types.size() != constructors.size())
    throw std::invalid_argument(
        "ADT registration requires field types for every constructor");
  if (!field_names.empty() && field_names.size() != constructors.size())
    throw std::invalid_argument(
        "ADT registration field-name groups must match constructors");

  adt_type_params_[type_name] = type_params;
  for (size_t constructor_index = 0; constructor_index < constructors.size();
       ++constructor_index) {
    const auto &[ctor_name, arity] = constructors[constructor_index];
    const auto &fields = field_types[constructor_index];
    if (arity < 0 || fields.size() != static_cast<size_t>(arity))
      throw std::invalid_argument(
          "ADT constructor arity must match its canonical field types");
    static const std::vector<std::string> EmptyNames;
    const auto &names =
        field_names.empty() ? EmptyNames : field_names[constructor_index];
    if (!names.empty() && names.size() != fields.size())
      throw std::invalid_argument(
          "named ADT constructor fields must match constructor arity");
    constructor_registry_[ctor_name] = {type_name, arity, type_params, fields,
                                        names};

    // Register constructor as a function in root env:
    // For arity 0: constructor is a value of type ADT
    // For arity N: constructor is a function a1 -> ... -> aN -> ADT(params...)
    std::vector<TypeId> quant_vars;
    std::vector<MonoTypePtr> param_vars;
    for (auto &tp : type_params) {
      auto *v = arena_.fresh_var(0);
      uf_.add_var(v->var_id, 0);
      quant_vars.push_back(v->var_id);
      param_vars.push_back(v);
    }

    MonoTypePtr result_type = param_vars.empty()
                                  ? arena_.make_app(type_name, {})
                                  : arena_.make_app(type_name, param_vars);

    auto field_type = [&](const ast::FieldType &field,
                          const auto &self) -> MonoTypePtr {
      if (field.is_tuple_type) {
        std::vector<MonoTypePtr> elements;
        for (const auto &element : field.tuple_types)
          elements.push_back(self(element, self));
        return arena_.make_tuple(elements);
      }
      if (field.is_function_type) {
        MonoTypePtr result = field.return_types.empty()
                                 ? arena_.make_con(TyCon::Unit)
                                 : self(field.return_types.front(), self);
        for (auto it = field.param_types.rbegin();
             it != field.param_types.rend(); ++it)
          result = arena_.make_arrow(self(*it, self), result);
        return result;
      }
      for (size_t i = 0; i < type_params.size(); ++i)
        if (field.name == type_params[i])
          return param_vars[i];
      if (field.name == "Int")
        return arena_.make_con(TyCon::Int);
      if (field.name == "Float")
        return arena_.make_con(TyCon::Float);
      if (field.name == "Bool")
        return arena_.make_con(TyCon::Bool);
      if (field.name == "String")
        return arena_.make_con(TyCon::String);
      if (field.name == "Symbol")
        return arena_.make_con(TyCon::Symbol);
      if (field.name == "()" || field.name == "Unit")
        return arena_.make_con(TyCon::Unit);
      std::vector<MonoTypePtr> arguments;
      for (const auto &argument : field.type_arguments)
        arguments.push_back(self(argument, self));
      return arena_.make_app(field.name, arguments);
    };

    if (arity == 0) {
      root_env_->bind_scheme(ctor_name, TypeScheme(quant_vars, result_type));
    } else {
      // Build curried function: a -> b -> ... -> ADT(params)
      // Each constructor arg gets a fresh var (polymorphic)
      MonoTypePtr fn_type = result_type;
      for (int i = arity - 1; i >= 0; i--) {
        MonoTypePtr arg_type;
        if (i < static_cast<int>(fields.size()))
          arg_type = field_type(fields[i], field_type);
        else if (i < (int)param_vars.size())
          arg_type = param_vars[i];
        else {
          arg_type = arena_.fresh_var(0);
          uf_.add_var(arg_type->var_id, 0);
          quant_vars.push_back(arg_type->var_id);
        }
        fn_type = arena_.make_arrow(arg_type, fn_type);
      }
      root_env_->bind_scheme(ctor_name, TypeScheme(quant_vars, fn_type));
    }
  }
}

// ===== Trait Registration =====

void TypeChecker::register_trait(const std::string &trait_name,
                                 std::vector<std::string> type_params) {
  trait_type_params_[trait_name] = std::move(type_params);
}

void TypeChecker::register_trait_method(const std::string &trait_name,
                                        const std::string &method_name,
                                        MonoTypePtr method_type) {
  std::vector<TypeId> qvars;
  collect_free_vars(method_type, -1, qvars);

  std::vector<Constraint> constraints;
  // Add trait constraint on the first free var (the type param)
  if (!qvars.empty()) {
    // Find the first var that appears as a param type
    auto *resolved = unifier_.resolve(method_type);
    MonoTypePtr constrained = nullptr;
    if (resolved && resolved->tag == MonoType::Arrow)
      constrained = unifier_.resolve(resolved->param_type);
    if (!constrained) {
      auto *v = arena_.fresh_var(0);
      uf_.add_var(v->var_id, 0);
      constrained = v;
      qvars.push_back(v->var_id);
    }
    constraints.push_back({trait_name, constrained});
  }

  root_env_->bind_scheme(method_name,
                         TypeScheme(qvars, constraints, method_type));
}

void TypeChecker::register_trait_method_descriptor(
    const std::string &trait_name, const std::string &method_name,
    const std::string &descriptor) {
  ImportedFnSig signature;
  signature.return_descriptor = descriptor;
  std::unordered_map<std::string, MonoTypePtr> variables;
  auto *method_type = mono_from_import_sig(signature, 0, &variables);
  register_trait_method(trait_name, method_name, method_type);
  if (const auto parameters = trait_type_params_.find(trait_name);
      parameters != trait_type_params_.end()) {
    std::vector<MonoTypePtr> arguments;
    for (const auto &parameter : parameters->second) {
      auto found = variables.find(parameter);
      if (found == variables.end()) {
        auto *variable = arena_.fresh_var(0);
        uf_.add_var(variable->var_id, 0);
        found = variables.emplace(parameter, variable).first;
      }
      arguments.push_back(found->second);
    }
    if (auto scheme = root_env_->lookup(method_name);
        scheme && !arguments.empty()) {
      auto quantified = scheme->quantified_vars;
      for (auto *argument : arguments) {
        auto *resolved = unifier_.resolve(argument);
        if (resolved && resolved->tag == MonoType::Var &&
            std::find(quantified.begin(), quantified.end(), resolved->var_id) ==
                quantified.end())
          quantified.push_back(resolved->var_id);
      }
      root_env_->bind_scheme(
          method_name,
          TypeScheme(std::move(quantified),
                     {Constraint(trait_name, std::move(arguments))},
                     scheme->body));
    }
  }
}

void TypeChecker::register_builtin_function(const std::string &function_name,
                                            MonoTypePtr function_type) {
  std::vector<TypeId> quantified;
  collect_free_vars(function_type, -1, quantified);
  root_env_->bind_scheme(function_name, TypeScheme(quantified, function_type));
}

void TypeChecker::register_interface_function(
    const interface::Function &Function) {
  register_builtin_function(Function.Name,
                            mono_from_interface_function(Function, 0));
}

void TypeChecker::register_trait_superclass(
    const std::string &trait_name, const std::string &superclass_name) {
  auto &superclasses = trait_superclasses_[trait_name];
  if (std::find(superclasses.begin(), superclasses.end(), superclass_name) ==
      superclasses.end()) {
    superclasses.push_back(superclass_name);
    std::sort(superclasses.begin(), superclasses.end());
  }
}

void TypeChecker::register_instance(
    const std::string &trait_name, std::vector<std::string> type_names,
    std::vector<std::string> type_params,
    std::vector<std::pair<std::string, std::string>> constraints) {
  auto &instances = trait_instances_[trait_name];
  const auto duplicate =
      std::find_if(instances.begin(), instances.end(),
                   [&](const InstanceContract &instance) {
                     return instance.type_names == type_names &&
                            instance.type_params == type_params &&
                            instance.constraints == constraints;
                   });
  if (duplicate != instances.end())
    return;
  instances.push_back(
      {std::move(type_names), std::move(type_params), std::move(constraints)});
  std::sort(instances.begin(), instances.end(),
            [](const InstanceContract &left, const InstanceContract &right) {
              return left.type_names < right.type_names;
            });
}

bool TypeChecker::solve_constraints() {
  bool all_ok = true;
  std::unordered_set<std::string> reported;
  std::vector<DeferredConstraint> worklist = deferred_constraints_;
  for (size_t work_index = 0; work_index < worklist.size(); ++work_index) {
    const auto &dc = worklist[work_index];
    std::vector<MonoTypePtr> resolved;
    std::vector<std::string> type_names;
    std::vector<std::string> type_heads;
    bool unresolved = false;
    for (auto *argument : dc.types) {
      auto *concrete = zonk(argument);
      if (concrete->tag == MonoType::Var)
        unresolved = true;
      resolved.push_back(concrete);
      type_names.push_back(pretty_print(concrete));
      type_heads.push_back(
          concrete->tag == MonoType::App ? concrete->type_name
          : concrete->tag == MonoType::MTuple && concrete->elements.empty()
              ? "Unit"
          : concrete->tag == MonoType::MTuple ? "Tuple"
                                              : pretty_print(concrete));
      if (type_names.back() == "()")
        type_heads.back() = "Unit";
    }
    if (unresolved || resolved.empty())
      continue;

    std::string application = dc.trait_name;
    for (const auto &name : type_names)
      application += " " + name;
    const std::string obligation_key = application + ":" + dc.context;
    const bool first_report = reported.insert(obligation_key).second;

    auto it = trait_instances_.find(dc.trait_name);
    if (it == trait_instances_.end()) {
      std::string message = "no instances for trait '" + dc.trait_name + "'";
      if (!dc.context.empty())
        message += " (" + dc.context + ")";
      if (first_report)
        diag_.error(dc.loc, ErrorCode::E0106, message);
      all_ok = false;
      continue;
    }
    struct Match {
      const InstanceContract *instance;
      std::unordered_map<std::string, MonoTypePtr> parameters;
    };
    std::vector<Match> matches;
    for (const auto &instance : it->second) {
      const auto &heads = instance.type_names;
      if (heads.size() != resolved.size())
        continue;
      std::unordered_map<std::string, MonoTypePtr> parameters;
      if (!resolved.empty() && resolved.front()->tag == MonoType::App) {
        for (size_t i = 0; i < instance.type_params.size() &&
                           i < resolved.front()->args.size();
             ++i)
          parameters[instance.type_params[i]] = zonk(resolved.front()->args[i]);
      } else if (!resolved.empty() &&
                 resolved.front()->tag == MonoType::MTuple) {
        for (size_t i = 0; i < instance.type_params.size() &&
                           i < resolved.front()->elements.size();
             ++i)
          parameters[instance.type_params[i]] =
              zonk(resolved.front()->elements[i]);
      }
      bool compatible = true;
      for (size_t i = 0; i < heads.size(); ++i) {
        const bool parameter =
            std::find(instance.type_params.begin(), instance.type_params.end(),
                      heads[i]) != instance.type_params.end();
        if (parameter) {
          if (const auto found = parameters.find(heads[i]);
              found != parameters.end()) {
            auto *bound = zonk(found->second);
            if (bound->tag == MonoType::Var) {
              parameters[heads[i]] = resolved[i];
            } else if (resolved[i]->tag != MonoType::Var &&
                       pretty_print(bound) != type_names[i]) {
              compatible = false;
              break;
            }
          }
          parameters[heads[i]] = resolved[i];
        } else if (heads[i] != type_names[i] && heads[i] != type_heads[i]) {
          compatible = false;
          break;
        }
      }
      if (compatible)
        matches.push_back({&instance, std::move(parameters)});
    }
    if (matches.empty()) {
      std::string message = "no instance for '" + application + "'";
      if (!dc.context.empty())
        message += " (" + dc.context + ")";
      if (first_report)
        diag_.error(dc.loc, ErrorCode::E0105, message);
      if (first_report && dc.trait_name == "Eq") {
        diag_.note(
            dc.loc,
            "add `deriving Eq` to a value ADT, define an `instance Eq " +
                type_heads.front() +
                "`, or pass an explicit comparator such as Std\\Test.equalBy");
      } else if (first_report && dc.trait_name == "Ord") {
        diag_.note(dc.loc,
                   "add `deriving Ord` (and Eq), define an `instance Ord " +
                       type_heads.front() +
                       "`, or call an explicit comparison function");
      } else if (first_report && dc.trait_name == "Send") {
        diag_.note(
            dc.loc,
            "move only values with a `Send` instance across this boundary; "
            "immutable ADTs may add `deriving Send`, native arrays may move "
            "but not "
            "be shared, and Linear resources or promises must remain on their "
            "owning task");
      } else if (first_report && dc.trait_name == "Shareable") {
        diag_.note(dc.loc,
                   "capture only immutable values with a `Shareable` instance; "
                   "immutable ADTs may add `deriving Send, Shareable`, "
                   "synchronized channel "
                   "endpoints are shareable, and mutable native arrays require "
                   "a snapshot");
      }
      all_ok = false;
      continue;
    }
    if (matches.size() > 1) {
      if (first_report)
        diag_.error(dc.loc, ErrorCode::E0400,
                    "ambiguous visible instances for '" + application +
                        "'; remove the duplicate import or instance");
      all_ok = false;
      continue;
    }

    const auto &match = matches.front();
    const auto &instance = *match.instance;
    if (dc.origin)
      selected_trait_instances_[dc.origin] = {dc.trait_name,
                                              instance.type_names};
    if (const auto superclasses = trait_superclasses_.find(dc.trait_name);
        superclasses != trait_superclasses_.end()) {
      for (const auto &superclass : superclasses->second)
        worklist.emplace_back(superclass, resolved.front(), dc.loc,
                              "superclass of " + application);
    }
    for (const auto &[required_trait, parameter] : instance.constraints) {
      const auto bound = match.parameters.find(parameter);
      if (bound == match.parameters.end()) {
        diag_.error(dc.loc, ErrorCode::E0400,
                    "malformed instance contract for '" + application +
                        "': constraint parameter '" + parameter +
                        "' is not declared");
        all_ok = false;
        continue;
      }
      worklist.emplace_back(required_trait, bound->second, dc.loc,
                            "required by " + application);
    }
  }
  return all_ok;
}

// ===== Effect Registration =====

void TypeChecker::register_effect(
    const std::string &effect_name, const std::string &type_param,
    const std::vector<std::tuple<std::string, std::vector<MonoTypePtr>,
                                 MonoTypePtr>> &operations) {
  (void)type_param; // type param is used for documentation; operations already
                    // carry concrete types
  for (auto &[op_name, param_types, return_type] : operations) {
    std::string key = effect_name + "." + op_name;
    effect_ops_[key] = {effect_name, param_types, return_type};
  }
}

// ===== Perform =====

MonoTypePtr TypeChecker::infer_perform(PerformExpr *node,
                                       std::shared_ptr<TypeEnv> env,
                                       int level) {
  std::string op_key = node->effect_name + "." + node->operation_name;

  if (!latent_effect_stack_.empty()) {
    effect_origins_.try_emplace(op_key, node->Range);
    const auto result = arena_.effect_solver().add_label(
        latent_effect_stack_.back().effect, op_key);
    if (result == EffectConstraintResult::Conflict) {
      diag_.error(node->Range, ErrorCode::E0100,
                  "incompatible effect constraints for '" + op_key + "'");
      ++error_count_;
    }
  } else {
    unhandled_effect_locations_.push_back(node->Range);
    diag_.warning(node->Range, ErrorCode::E0200,
                  "effect operation '" + op_key +
                      "' may not be handled; "
                      "ensure a 'handle...with' block provides a handler for " +
                      node->effect_name,
                  WarningFlag::UnhandledEffect);
  }

  // Look up the operation's type signature
  auto it = effect_ops_.find(op_key);
  if (it != effect_ops_.end()) {
    auto &info = it->second;
    // `perform State.get ()` is the 0-arg surface (Unit is not a payload).
    // Ops that actually take Unit (`Gpu.oom`) keep the argument.
    size_t expected = info.param_types.size();
    std::vector<ExprNode *> payload;
    payload.reserve(node->args.size());
    for (auto *arg : node->args) {
      if (expected == 0 && arg && arg->get_type() == AST_UNIT_EXPR) {
        infer(arg, env, level);
        continue;
      }
      payload.push_back(arg);
    }
    size_t actual = payload.size();
    if (actual != expected) {
      diag_.error(node->Range, ErrorCode::E0201,
                  "effect operation '" + op_key + "' expects " +
                      std::to_string(expected) + " argument(s), got " +
                      std::to_string(actual));
      error_count_++;
    }
    for (size_t i = 0; i < payload.size() && i < info.param_types.size(); i++) {
      auto *arg_type = infer(payload[i], env, level);
      unifier_.unify(arg_type, info.param_types[i], node->Range,
                     "in argument " + std::to_string(i + 1) + " of perform " +
                         op_key);
    }
    return info.return_type;
  }

  // Unknown effect â€” infer args, return fresh var
  for (auto *arg : node->args)
    infer(arg, env, level);

  auto *v = arena_.fresh_var(level);
  uf_.add_var(v->var_id, level);
  return v;
}

// ===== Handle =====

MonoTypePtr TypeChecker::infer_handle(HandleExpr *node,
                                      std::shared_ptr<TypeEnv> env, int level) {
  // Collect which operations this handle block covers
  std::vector<std::string> handled_ops;
  for (auto *clause : node->clauses) {
    if (!clause->is_return_clause) {
      handled_ops.push_back(clause->effect_name + "." + clause->operation_name);
    }
  }

  // Infer the protected body in an isolated derived cell, then project it
  // through a symbolic mask into the surrounding function.  The mask stays
  // symbolic so effects contributed by an unknown callback after helper
  // instantiation are still removed by this handler.
  const auto body_effect = arena_.effect_solver().derived();
  latent_effect_stack_.push_back({body_effect});
  auto *body_type = infer(node->body, env, level);
  latent_effect_stack_.pop_back();
  include_ambient_effect(arena_.effect_solver().mask(body_effect, handled_ops),
                         node->Range, "in handled expression");

  // The result type of the whole handle expression.
  // If there's a return clause, it transforms the body result â€” so result_type
  // may differ from body_type. Start with a fresh var.
  auto *result_type = arena_.fresh_var(level);
  uf_.add_var(result_type->var_id, level);

  // Check for a return clause first to establish the result type
  bool has_return = false;
  for (auto *clause : node->clauses) {
    if (clause->is_return_clause) {
      has_return = true;
      auto clause_env = env->child();
      clause_env->bind(clause->return_binding, body_type);
      auto *clause_type = infer(clause->body, clause_env, level);
      unifier_.unify(result_type, clause_type, clause->Range,
                     "in return handler clause");
      break;
    }
  }
  // No return clause â†’ result is body type
  if (!has_return)
    unifier_.unify(result_type, body_type, node->body->Range, "in handle body");

  result_type = unifier_.resolve(result_type);

  // Infer operation handler clauses
  for (auto *clause : node->clauses) {
    if (clause->is_return_clause)
      continue;

    auto clause_env = env->child();
    std::string op_key = clause->effect_name + "." + clause->operation_name;

    // Bind operation argument names
    auto op_it = effect_ops_.find(op_key);
    for (size_t i = 0; i < clause->arg_names.size(); i++) {
      MonoTypePtr arg_type;
      if (op_it != effect_ops_.end() && i < op_it->second.param_types.size())
        arg_type = op_it->second.param_types[i];
      else {
        arg_type = arena_.fresh_var(level);
        uf_.add_var(arg_type->var_id, level);
      }
      clause_env->bind(clause->arg_names[i], arg_type);
    }

    // Bind resume: function from op's return type to result type
    if (!clause->resume_name.empty()) {
      MonoTypePtr resume_param;
      if (op_it != effect_ops_.end())
        resume_param = op_it->second.return_type;
      else {
        resume_param = arena_.fresh_var(level);
        uf_.add_var(resume_param->var_id, level);
      }
      auto *resume_type = arena_.make_arrow(resume_param, result_type);
      clause_env->bind(clause->resume_name, resume_type);
    }

    auto *clause_type = infer(clause->body, clause_env, level);
    unifier_.unify(result_type, clause_type, clause->Range,
                   "in handler clause for " + op_key);
    result_type = unifier_.resolve(result_type);
  }

  return result_type;
}

void TypeChecker::bind_import_name(std::shared_ptr<TypeEnv> env,
                                   const std::string &module_fqn,
                                   const std::string &func_name,
                                   const std::string &bind_name, int level) {
  std::optional<yona::interface::InterfaceModule> interface_module;
  if (!module_fqn.empty() && !func_name.empty()) {
    try {
      const yona::model::ModuleIdentity identity(module_fqn);
      auto loaded =
          yona::interface::readModuleFromSearchPaths(ModulePaths, identity);
      if (loaded && loaded->has_value())
        interface_module.emplace(std::move(**loaded));
    } catch (const std::invalid_argument &) {
      // The normal unknown-import diagnostic remains authoritative for an
      // invalid source-level module name.
    }
  }
  const yona::interface::Function *row =
      interface_module
          ? yona::interface::findFunction(*interface_module, func_name)
          : nullptr;
  std::optional<ImportedFnSig> lin;
  if (row) {
    ImportedFnSig Signature;
    Signature.param_descriptors = row->ParameterTypes;
    Signature.return_descriptor = row->ReturnType;
    Signature.effect_scheme = row->Effects.Scheme;
    lin = std::move(Signature);
  } else if (import_src_) {
    lin = import_src_->imported_function_sig(module_fqn, func_name);
  }

  if (row) {
    if (!row->Effects.IsKnown && require_effect_free_)
      has_unknown_effect_rows_ = true;
    auto *Exact = mono_from_interface_function(*row, level);
    env->bind_scheme(bind_name, generalize(Exact, level - 1));
    return;
  }
  if (lin) {
    if (require_effect_free_)
      has_unknown_effect_rows_ = true;
    env->bind_scheme(bind_name,
                     generalize(mono_from_import_sig(*lin, level), -1));
    return;
  }
  if (require_effect_free_)
    has_unknown_effect_rows_ = true;
  auto *v = arena_.fresh_var(level);
  uf_.add_var(v->var_id, level);
  env->bind(bind_name, v);
}

MonoTypePtr TypeChecker::mono_from_import_sig(
    const ImportedFnSig &sig, int level,
    std::unordered_map<std::string, MonoTypePtr> *output_descriptor_variables) {
  auto fresh = [this, level]() {
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  };
  // Bare collection/function/ADT descriptors are canonical structural
  // wildcards. Scalar descriptors, including INT, are always exact; generic
  // variables must use VAR(name).
  auto from_atom = [&](const std::string &tag) -> MonoTypePtr {
    if (tag == "INT")
      return arena_.make_con(TyCon::Int);
    if (tag == "STRING")
      return arena_.make_con(TyCon::String);
    if (tag == "BOOL")
      return arena_.make_con(TyCon::Bool);
    if (tag == "FLOAT")
      return arena_.make_con(TyCon::Float);
    if (tag == "CHAR")
      return arena_.make_con(TyCon::Char);
    if (tag == "BYTE")
      return arena_.make_con(TyCon::Byte);
    if (tag == "SYMBOL")
      return arena_.make_con(TyCon::Symbol);
    if (tag == "UNIT")
      return arena_.make_con(TyCon::Unit);
    if (tag == "SEQ")
      return arena_.make_app("Seq", {fresh()});
    if (tag == "SET")
      return arena_.make_app("Set", {fresh()});
    if (tag == "DICT")
      return arena_.make_app("Dict", {fresh(), fresh()});
    if (tag == "BYTE_ARRAY")
      return arena_.make_con(TyCon::ByteArray);
    if (tag == "INT_ARRAY")
      return arena_.make_app("IntArray", {});
    if (tag == "FLOAT_ARRAY")
      return arena_.make_app("FloatArray", {});
    if (tag == "CHANNEL")
      return arena_.make_app("Channel", {fresh()});
    if (tag == "PROMISE")
      return arena_.make_app("Promise", {fresh()});
    if (tag == "FUNCTION")
      return arena_.make_arrow(fresh(), fresh());
    if (tag == "ADT")
      return arena_.make_app("ADT", {fresh()});
    if (tag == "LINEAR" || tag == "TUPLE")
      return fresh();
    throw std::invalid_argument("unknown canonical interface type: " + tag);
  };
  std::unordered_map<std::string, MonoTypePtr> descriptor_variables;
  auto from_descriptor = [&](const std::string &text) -> MonoTypePtr {
    std::function<MonoTypePtr(const std::string &)> parse;
    parse = [&](const std::string &value) -> MonoTypePtr {
      auto open = value.find('(');
      if (open == std::string::npos)
        return from_atom(value);
      if (!value.ends_with(')'))
        throw std::invalid_argument("malformed canonical interface type: " +
                                    value);
      std::string name = value.substr(0, open);
      std::vector<std::string> parts;
      std::string part;
      int depth = 0;
      for (size_t i = open + 1; i + 1 < value.size(); ++i) {
        char c = value[i];
        if (c == '(')
          ++depth;
        else if (c == ')')
          --depth;
        if (c == ',' && depth == 0) {
          parts.push_back(part);
          part.clear();
        } else
          part += c;
      }
      if (!part.empty())
        parts.push_back(part);
      if (name == "VAR" && parts.size() == 1) {
        auto found = descriptor_variables.find(parts[0]);
        if (found != descriptor_variables.end())
          return found->second;
        auto *variable = fresh();
        descriptor_variables[parts[0]] = variable;
        return variable;
      }
      if (name == "ADT" && !parts.empty()) {
        std::vector<MonoTypePtr> type_arguments;
        for (size_t i = 1; i < parts.size(); ++i)
          type_arguments.push_back(parse(parts[i]));
        if (type_arguments.empty()) {
          if (const auto Declared = adt_type_params_.find(parts[0]);
              Declared != adt_type_params_.end()) {
            for (size_t i = 0; i < Declared->second.size(); ++i)
              type_arguments.push_back(fresh());
          }
        }
        return arena_.make_app(parts[0], type_arguments);
      }
      std::vector<MonoTypePtr> args;
      for (const auto &nested : parts)
        args.push_back(parse(nested));
      if (name == "TUPLE")
        return arena_.make_tuple(args);
      if (name == "FUNCTION" && args.size() == 2)
        return arena_.make_arrow(args[0], args[1]);
      if (name == "LINEAR" && args.size() == 1)
        return arena_.make_app("Linear", args);
      if (name == "Seq" || name == "SET" || name == "Set" || name == "DICT" ||
          name == "Dict" || name == "Promise")
        return arena_.make_app(name == "SET"    ? "Set"
                               : name == "DICT" ? "Dict"
                                                : name,
                               args);
      throw std::invalid_argument("unknown canonical interface type: " + value);
    };
    return parse(text);
  };
  if (sig.return_descriptor.empty())
    throw std::invalid_argument(
        "imported function requires a canonical return descriptor");
  MonoTypePtr ret = from_descriptor(sig.return_descriptor);
  MonoTypePtr fn = ret;
  for (auto Descriptor = sig.param_descriptors.rbegin();
       Descriptor != sig.param_descriptors.rend(); ++Descriptor) {
    if (Descriptor->empty())
      throw std::invalid_argument(
          "imported function requires canonical parameter descriptors");
    fn = arena_.make_arrow(from_descriptor(*Descriptor), fn);
  }
  if (output_descriptor_variables)
    *output_descriptor_variables = descriptor_variables;
  return fn;
}

MonoTypePtr
TypeChecker::mono_from_interface_function(const interface::Function &Function,
                                          int Level) {
  ImportedFnSig Signature;
  Signature.param_descriptors = Function.ParameterTypes;
  Signature.return_descriptor = Function.ReturnType;
  Signature.effect_scheme = Function.Effects.Scheme;
  auto *Type = mono_from_import_sig(Signature, Level);
  const auto ShiftedThunkScheme =
      Function.ParameterTypes.empty()
          ? normalize_zero_arity_thunk_scheme(Function.Effects.Scheme)
          : std::nullopt;
  const bool ReturnDescriptorIsArrow =
      unifier_.resolve(Type) && unifier_.resolve(Type)->tag == MonoType::Arrow;
  if (ShiftedThunkScheme && !ReturnDescriptorIsArrow)
    Type = arena_.make_arrow(arena_.make_con(TyCon::Unit), Type);
  // Legacy interfaces predate effect schemes. A non-empty/open known effect
  // row cannot describe a CAF value because effects attach to calls, so it is
  // sufficient evidence for the same visible Unit thunk. Unknown and
  // explicitly pure zero-arity native/CAF rows deliberately remain values.
  const bool LegacyKnownUnitThunk =
      Function.ParameterTypes.empty() && Function.Effects.IsKnown &&
      Function.Effects.Scheme.empty() &&
      (!Function.Effects.Operations.empty() || Function.Effects.IsOpen) &&
      !ReturnDescriptorIsArrow;
  if (LegacyKnownUnitThunk)
    Type = arena_.make_arrow(arena_.make_con(TyCon::Unit), Type);
  if (!Function.Effects.IsKnown)
    return Type;
  if (ShiftedThunkScheme)
    return apply_effect_scheme(Type, *ShiftedThunkScheme);
  if (!Function.Effects.Scheme.empty())
    return apply_effect_scheme(Type, Function.Effects.Scheme);

  std::vector<EffectRef> Sources;
  if (!Function.Effects.Operations.empty())
    Sources.push_back(
        arena_.effect_solver().labels(Function.Effects.Operations));
  if (Function.Effects.IsOpen)
    Sources.push_back(arena_.effect_solver().opaque());
  const auto ImportedEffect = arena_.effect_solver().join(std::move(Sources));
  std::function<MonoTypePtr(MonoTypePtr, bool)> AttachEffects;
  AttachEffects = [&](MonoTypePtr Current, bool FirstParameter) -> MonoTypePtr {
    Current = unifier_.resolve(Current);
    if (!Current || Current->tag != MonoType::Arrow)
      return Current;
    MonoTypePtr Parameter = Current->param_type;
    if (Function.Effects.IsHigherOrder && FirstParameter) {
      auto *ResolvedParameter = unifier_.resolve(Parameter);
      if (ResolvedParameter && ResolvedParameter->tag == MonoType::Arrow)
        Parameter = AttachEffects(ResolvedParameter, false);
    }
    return arena_.make_arrow(
        Parameter, AttachEffects(Current->return_type, false), ImportedEffect);
  };
  return AttachEffects(Type, true);
}

MonoTypePtr TypeChecker::from_ast_type(const yona::compiler::types::Type &t,
                                       int level) {
  std::unordered_map<std::string, MonoTypePtr> variables;
  return from_ast_type_impl(t, level, variables);
}

MonoTypePtr TypeChecker::from_ast_type_impl(
    const yona::compiler::types::Type &t, int level,
    std::unordered_map<std::string, MonoTypePtr> &variables) {
  using yona::compiler::types::BuiltinType;
  using yona::compiler::types::FunctionType;
  using yona::compiler::types::NamedType;
  using yona::compiler::types::ProductType;
  using yona::compiler::types::PromiseType;
  using yona::compiler::types::RefinedType;
  using yona::compiler::types::SingleItemCollectionType;

  if (std::holds_alternative<std::nullptr_t>(t)) {
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }
  if (std::holds_alternative<BuiltinType>(t)) {
    switch (std::get<BuiltinType>(t)) {
    case BuiltinType::Bool:
      return arena_.make_con(TyCon::Bool);
    case BuiltinType::String:
      return arena_.make_con(TyCon::String);
    case BuiltinType::Symbol:
      return arena_.make_con(TyCon::Symbol);
    case BuiltinType::Unit:
      return arena_.make_con(TyCon::Unit);
    case BuiltinType::Float32:
    case BuiltinType::Float64:
    case BuiltinType::Float128:
      return arena_.make_con(TyCon::Float);
    case BuiltinType::Seq:
      return arena_.make_app("Seq", {[&]() {
                               auto *v = arena_.fresh_var(level);
                               uf_.add_var(v->var_id, level);
                               return v;
                             }()});
    case BuiltinType::Set:
      return arena_.make_app("Set", {[&]() {
                               auto *v = arena_.fresh_var(level);
                               uf_.add_var(v->var_id, level);
                               return v;
                             }()});
    default:
      return arena_.make_con(TyCon::Int);
    }
  }
  if (std::holds_alternative<std::shared_ptr<FunctionType>>(t)) {
    auto &ft = std::get<std::shared_ptr<FunctionType>>(t);
    return arena_.make_arrow(
        from_ast_type_impl(ft->argumentType, level, variables),
        from_ast_type_impl(ft->returnType, level, variables));
  }
  if (std::holds_alternative<std::shared_ptr<ProductType>>(t)) {
    auto &pt = std::get<std::shared_ptr<ProductType>>(t);
    std::vector<MonoTypePtr> elems;
    elems.reserve(pt->types.size());
    for (auto &e : pt->types)
      elems.push_back(from_ast_type_impl(e, level, variables));
    return arena_.make_tuple(elems);
  }
  if (std::holds_alternative<std::shared_ptr<NamedType>>(t)) {
    auto &nt = std::get<std::shared_ptr<NamedType>>(t);
    if (!nt->name.empty() &&
        std::islower(static_cast<unsigned char>(nt->name.front())) &&
        std::holds_alternative<std::nullptr_t>(nt->type)) {
      if (const auto found = variables.find(nt->name); found != variables.end())
        return found->second;
      auto *variable = arena_.fresh_var(level);
      uf_.add_var(variable->var_id, level);
      variables[nt->name] = variable;
      return variable;
    }
    if (!std::holds_alternative<std::nullptr_t>(nt->type)) {
      // Surface syntax writes multi-parameter applications as
      // `Result (a, e)`.  The product here is argument punctuation,
      // not a single tuple argument: normalize it to the same n-ary
      // App representation used by constructor schemes and `.yonai`.
      const auto params = adt_type_params_.find(nt->name);
      if (const auto *product =
              std::get_if<std::shared_ptr<ProductType>>(&nt->type);
          product && params != adt_type_params_.end() &&
          params->second.size() > 1 &&
          (*product)->types.size() == params->second.size()) {
        std::vector<MonoTypePtr> arguments;
        arguments.reserve((*product)->types.size());
        for (const auto &argument : (*product)->types)
          arguments.push_back(from_ast_type_impl(argument, level, variables));
        return arena_.make_app(nt->name, arguments);
      }
      return arena_.make_app(nt->name,
                             {from_ast_type_impl(nt->type, level, variables)});
    }
    return arena_.make_app(nt->name, {});
  }
  if (std::holds_alternative<std::shared_ptr<PromiseType>>(t)) {
    auto &pr = std::get<std::shared_ptr<PromiseType>>(t);
    return from_ast_type_impl(pr->valueType, level,
                              variables); // auto-await: Promise T ~ T
  }
  if (std::holds_alternative<std::shared_ptr<RefinedType>>(t)) {
    auto &rt = std::get<std::shared_ptr<RefinedType>>(t);
    return from_ast_type_impl(rt->base_type, level, variables);
  }
  if (std::holds_alternative<std::shared_ptr<SingleItemCollectionType>>(t)) {
    auto &col = std::get<std::shared_ptr<SingleItemCollectionType>>(t);
    auto *elem = from_ast_type_impl(col->valueType, level, variables);
    const char *name =
        (col->kind == SingleItemCollectionType::Seq) ? "Seq" : "Set";
    return arena_.make_app(name, {elem});
  }
  auto *v = arena_.fresh_var(level);
  uf_.add_var(v->var_id, level);
  return v;
}

} // namespace yona::compiler::typechecker
