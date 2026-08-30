#include "yona/Lsp/Analysis.h"

#include "yona/Semantics/InterfaceCatalog.h"
#include "yona/Semantics/LinearityChecker.h"
#include "yona/Semantics/RefinementChecker.h"
#include "yona/Semantics/SemanticModel.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/ModuleSource.h"
#include "yona/Syntax/Parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace yona::lsp {
namespace {

bool is_ident_char(unsigned char c) {
  return std::isalnum(c) || c == '_' || c == '\'';
}

int span_size(const Range &r) {
  if (r.end.line == r.start.line)
    return static_cast<int>(r.end.character - r.start.character);
  return 1000 + static_cast<int>(r.end.line - r.start.line);
}

Json range_json(const Range &r) {
  Json start;
  start["line"] = static_cast<int>(r.start.line);
  start["character"] = static_cast<int>(r.start.character);
  Json end;
  end["line"] = static_cast<int>(r.end.line);
  end["character"] = static_cast<int>(r.end.character);
  Json o;
  o["start"] = start;
  o["end"] = end;
  return o;
}

std::optional<std::filesystem::path>
find_module_file(const std::string &fqn,
                 const std::vector<std::string> &roots) {
  if (fqn.empty())
    return std::nullopt;
  std::filesystem::path rel;
  std::string part;
  for (char c : fqn) {
    if (c == '\\' || c == '/') {
      if (part == ".." || part == ".")
        return std::nullopt;
      if (!part.empty()) {
        rel /= part;
        part.clear();
      }
    } else {
      part += c;
    }
  }
  if (part == ".." || part == ".")
    return std::nullopt;
  if (!part.empty())
    rel /= part;
  if (rel.empty() || !rel.is_relative())
    return std::nullopt;
  for (const auto &root : roots) {
    auto base = std::filesystem::path(root) / rel;
    auto yona = base;
    yona += ".yona";
    if (std::filesystem::exists(yona))
      return yona;
    auto yonai = base;
    yonai += ".yonai";
    if (std::filesystem::exists(yonai))
      return yonai;
  }
  return std::nullopt;
}

std::string rtrim_copy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

std::string drop_last_token(std::string s) {
  s = rtrim_copy(std::move(s));
  if (s.empty())
    return s;
  if (is_ident_char(static_cast<unsigned char>(s.back()))) {
    while (!s.empty() && is_ident_char(static_cast<unsigned char>(s.back())))
      s.pop_back();
  } else {
    while (!s.empty() && !std::isspace(static_cast<unsigned char>(s.back())) &&
           !is_ident_char(static_cast<unsigned char>(s.back())))
      s.pop_back();
  }
  return rtrim_copy(std::move(s));
}

std::string drop_last_line(std::string s) {
  s = rtrim_copy(std::move(s));
  auto nl = s.find_last_of('\n');
  if (nl == std::string::npos)
    return {};
  return s.substr(0, nl);
}

constexpr const char *kRecoverSuffixes[] = {
    " 0",    " in 0",          " 0 in 0", " end",           " 0 end",
    "\nend", " then 0 else 0", " else 0", " of _ -> 0 end", " -> 0 end",
    " = 0",
};

Range range_of_yonai_export(std::string_view text, std::string_view name) {
  if (name.empty())
    return Range{};
  std::string needle = "__" + std::string(name) + " ";
  auto p = text.find(needle);
  if (p == std::string_view::npos) {
    needle = "__" + std::string(name) + "\r";
    p = text.find(needle);
  }
  if (p == std::string_view::npos)
    return Range{};
  auto off = p + 2;
  Range r;
  r.start = offset_to_position(text, off);
  r.end = offset_to_position(text, off + name.size());
  return r;
}

} // namespace

struct Occurrence {
  std::string name;
  Range range;
  semantics::BindingId binding;
  bool is_def = false;
  std::string kind;
  std::string type;
  std::string effects;
  semantics::OwnershipKind ownership = semantics::OwnershipKind::Unknown;
  std::string origin_module;
  std::string origin_name;
  std::string detail;
  std::string container;
};

struct Analysis::Impl {
  compiler::DiagnosticEngine diag;
  std::vector<std::string> module_paths;
  std::unique_ptr<parser::Parser> parser;
  std::unique_ptr<semantics::InterfaceCatalog> interface_catalog;
  std::unique_ptr<compiler::typechecker::TypeChecker> checker;
  std::unique_ptr<ast::AstNode> root;
  std::shared_ptr<SourceManager> sources;
  SourceId source;
  std::unique_ptr<semantics::SemanticModel> semantic_model;
  std::vector<Occurrence> occs;
  std::vector<SymbolInfo> symbols;
  bool recovered = false;
  std::vector<compiler::DiagnosticEngine::Record> kept_parse_records;

  void reset() {
    diag = compiler::DiagnosticEngine();
    diag.enable_warning(compiler::WarningFlag::OverlappingPatterns);
    parser = std::make_unique<parser::Parser>();
    interface_catalog =
        std::make_unique<semantics::InterfaceCatalog>(module_paths);
    interface_catalog->appendEnvironmentSearchPaths();
    checker = std::make_unique<compiler::typechecker::TypeChecker>(diag);
    root.reset();
    sources.reset();
    source = SourceId();
    semantic_model.reset();
    occs.clear();
    symbols.clear();
    recovered = false;
    kept_parse_records.clear();
  }

  ast::AstNode *try_recover_ast(std::string_view text, std::string_view uri,
                                bool is_mod);

  void consume_semantic_model(std::string_view text) {
    occs.clear();
    symbols.clear();
    if (!semantic_model)
      return;
    for (const auto &semantic : semantic_model->occurrences()) {
      Occurrence occurrence;
      occurrence.name = semantic.Name;
      occurrence.range = source_to_range(text, semantic.Range);
      occurrence.binding = semantic.Binding;
      occurrence.is_def = semantic.IsDefinition;
      occurrence.kind = std::string(semantics::symbolKindName(semantic.Kind));
      occurrence.type = semantic.Facts.InferredType;
      occurrence.effects = semantic.Facts.Effects;
      occurrence.ownership = semantic.Facts.Ownership;
      occurrence.origin_module = semantic.OriginModule;
      occurrence.origin_name = semantic.OriginName;
      occurrence.detail = semantic.Detail;
      occurrence.container = semantic.Container;
      occs.push_back(std::move(occurrence));
      if (!semantic.IsDefinition)
        continue;
      SymbolInfo symbol;
      symbol.name = semantic.Name;
      symbol.kind = std::string(semantics::symbolKindName(semantic.Kind));
      symbol.range = source_to_range(text, semantic.Range);
      symbol.selection = symbol.range;
      symbol.type = semantic.Facts.InferredType;
      symbol.container = semantic.Container;
      symbol.detail = semantic.Detail;
      symbols.push_back(std::move(symbol));
    }
  }
};

ast::AstNode *Analysis::Impl::try_recover_ast(std::string_view text,
                                              std::string_view uri,
                                              bool is_mod) {
  std::vector<std::string> bases;
  auto add_base = [&](std::string s) {
    if (s.empty())
      return;
    if (std::find(bases.begin(), bases.end(), s) != bases.end())
      return;
    bases.push_back(std::move(s));
  };
  add_base(std::string(text));
  add_base(rtrim_copy(std::string(text)));
  add_base(drop_last_token(std::string(text)));
  add_base(drop_last_line(std::string(text)));

  auto take_module = [&](auto &&Result) -> ast::AstNode * {
    if (!Result.has_value())
      return nullptr;
    auto Parsed = std::move(Result.value());
    sources = std::move(Parsed.Sources);
    source = Parsed.Source;
    root = std::move(Parsed.Module);
    diag.setSources(sources);
    return root.get();
  };
  auto take_expression = [&](auto &&Result) -> ast::AstNode * {
    if (!Result.has_value())
      return nullptr;
    auto Parsed = std::move(Result.value());
    sources = std::move(Parsed.Sources);
    source = Parsed.Source;
    root = std::move(Parsed.Expression);
    diag.setSources(sources);
    return root.get();
  };

  const std::string uri_str(uri);
  for (const auto &base : bases) {
    if (base != text) {
      if (is_mod) {
        if (auto *n = take_module(parser->parseModule(base, uri_str)))
          return n;
      } else if (auto *n =
                     take_expression(parser->parseExpression(base, uri_str))) {
        return n;
      }
    }
    for (auto *suf : kRecoverSuffixes) {
      std::string cand = base;
      cand += suf;
      if (is_mod) {
        if (auto *n = take_module(parser->parseModule(cand, uri_str)))
          return n;
      } else if (auto *n =
                     take_expression(parser->parseExpression(cand, uri_str))) {
        return n;
      }
    }
  }
  return nullptr;
}

Analysis::Analysis() : impl_(std::make_unique<Impl>()) {}
Analysis::~Analysis() = default;
Analysis::Analysis(Analysis &&) noexcept = default;
Analysis &Analysis::operator=(Analysis &&) noexcept = default;

void Analysis::set_module_paths(std::vector<std::string> paths) {
  impl_->module_paths = std::move(paths);
}

void Analysis::analyze(std::string uri, std::string text) {
  uri_ = std::move(uri);
  text_ = std::move(text);
  impl_->reset();
  for (const auto &p : impl_->interface_catalog->searchPaths())
    impl_->checker->add_module_path(p);
  impl_->checker->set_import_type_source(impl_->interface_catalog.get());
  if (auto Prelude = impl_->interface_catalog->installPrelude(*impl_->parser,
                                                              *impl_->checker);
      !Prelude) {
    for (const auto &Error : Prelude.error())
      impl_->diag.error(SourceRange::unknown(), compiler::ErrorCode::E0301,
                        "Prelude interface " + std::to_string(Error.Line) +
                            ":" + std::to_string(Error.Column) + ": " +
                            Error.Message);
  }

  const bool is_mod = yona::is_module_source(text_);
  ast::AstNode *root = nullptr;
  bool parse_ok = false;
  if (is_mod) {
    auto result = impl_->parser->parseModule(text_, uri_);
    if (!result.has_value()) {
      for (auto &e : result.error()) {
        impl_->diag.setSources(e.Sources);
        impl_->diag.error(e.Range, compiler::ErrorCode::E0301, e.Message);
      }
    } else {
      auto parsed = std::move(result.value());
      impl_->sources = std::move(parsed.Sources);
      impl_->source = parsed.Source;
      impl_->root = std::move(parsed.Module);
      impl_->diag.setSources(impl_->sources);
      root = impl_->root.get();
      parse_ok = true;
    }
  } else {
    auto result = impl_->parser->parseExpression(text_, uri_);
    if (!result.has_value()) {
      for (auto &e : result.error()) {
        impl_->diag.setSources(e.Sources);
        impl_->diag.error(e.Range, compiler::ErrorCode::E0301, e.Message);
      }
    } else {
      auto parsed = std::move(result.value());
      impl_->sources = std::move(parsed.Sources);
      impl_->source = parsed.Source;
      impl_->root = std::move(parsed.Expression);
      impl_->diag.setSources(impl_->sources);
      root = impl_->root.get();
      parse_ok = true;
    }
  }
  if (!parse_ok) {
    impl_->kept_parse_records = impl_->diag.records();
    root = impl_->try_recover_ast(text_, uri_, is_mod);
    impl_->recovered = root != nullptr;
  }
  if (root) {
    if (parse_ok) {
      if (is_mod)
        impl_->checker->check_module(static_cast<ast::ModuleDecl *>(root));
      else
        impl_->checker->check(root);
      impl_->checker->solve_constraints();
      compiler::typechecker::RefinementChecker refine(impl_->diag,
                                                      impl_->checker.get());
      refine.check(root);
      compiler::typechecker::LinearityChecker lin(impl_->diag,
                                                  impl_->checker.get());
      lin.check(root);
    }
    impl_->semantic_model = std::make_unique<semantics::SemanticModel>(
        impl_->sources, impl_->source, root, impl_->checker.get(),
        &impl_->diag);
    impl_->consume_semantic_model(text_);
  }
}

std::vector<LspDiagnostic> Analysis::diagnostics() const {
  std::vector<LspDiagnostic> out;
  if (!impl_->recovered && impl_->semantic_model) {
    for (const auto &semantic : impl_->semantic_model->diagnostics()) {
      LspDiagnostic diagnostic;
      diagnostic.range = source_to_range(text_, semantic.Range);
      diagnostic.severity = semantic.Severity;
      diagnostic.code = semantic.Code;
      diagnostic.message = semantic.Message;
      out.push_back(std::move(diagnostic));
    }
    return out;
  }
  const auto &recs =
      impl_->recovered ? impl_->kept_parse_records : impl_->diag.records();
  for (const auto &rec : recs) {
    LspDiagnostic d;
    d.range = source_to_range(text_, rec.Range);
    d.severity = rec.level == compiler::DiagLevel::Error     ? 1
                 : rec.level == compiler::DiagLevel::Warning ? 2
                                                             : 3;
    if (rec.code)
      d.code = compiler::error_code_str(*rec.code);
    d.message = rec.message;
    out.push_back(std::move(d));
  }
  return out;
}

const Occurrence *find_at(const std::vector<Occurrence> &occs, Position pos) {
  const Occurrence *best = nullptr;
  int best_sz = 1 << 30;
  for (const auto &o : occs) {
    if (!o.range.contains(pos))
      continue;
    int sz = span_size(o.range);
    if (sz < best_sz) {
      best_sz = sz;
      best = &o;
    }
  }
  return best;
}

std::optional<HoverInfo> Analysis::hover(Position pos) const {
  auto *o = find_at(impl_->occs, pos);
  if (!o)
    return std::nullopt;
  HoverInfo h;
  h.range = o->range;
  std::ostringstream os;
  os << o->name;
  if (!o->type.empty())
    os << " : " << o->type;
  h.contents = os.str();
  return h;
}

std::vector<Location> Analysis::definition(Position pos) const {
  auto *o = find_at(impl_->occs, pos);
  if (!o)
    return {};
  auto local_defs = [&](const std::string &name, const std::string &origin) {
    std::vector<Location> out;
    for (const auto &c : impl_->occs) {
      const bool same_identity =
          o->origin_module.empty() ? c.binding == o->binding : c.name == name;
      if (c.is_def && same_identity && c.origin_module == origin)
        out.push_back({uri_, c.range});
    }
    return out;
  };

  if (!o->origin_module.empty()) {
    auto file = find_module_file(o->origin_module, impl_->module_paths);
    if (file) {
      auto target_uri = file_uri(file->generic_string());
      if (target_uri == uri_) {
        auto here =
            local_defs(o->origin_name.empty() ? o->name : o->origin_name, "");
        if (!here.empty())
          return here;
      } else {
        std::ifstream in(*file);
        std::string target((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
        if (file->extension() == ".yonai") {
          auto r = range_of_yonai_export(target, o->origin_name);
          return {{target_uri, r}};
        }
        Analysis other;
        other.set_module_paths(impl_->module_paths);
        other.analyze(target_uri, std::move(target));
        std::vector<Location> out;
        if (o->origin_name.empty()) {
          for (const auto &c : other.impl_->occs) {
            if (c.is_def && c.kind == "namespace") {
              out.push_back({target_uri, c.range});
              break;
            }
          }
          if (out.empty())
            out.push_back({target_uri, Range{}});
          return out;
        }
        for (const auto &c : other.impl_->occs) {
          if (c.is_def && c.name == o->origin_name && c.origin_module.empty())
            out.push_back({target_uri, c.range});
        }
        if (!out.empty())
          return out;
      }
    }
  }
  return local_defs(o->name, o->origin_module);
}

std::vector<DocumentHighlight>
Analysis::document_highlight(Position pos) const {
  auto *o = find_at(impl_->occs, pos);
  if (!o)
    return {};
  std::vector<DocumentHighlight> out;
  for (const auto &c : impl_->occs) {
    const bool same_identity = c.binding == o->binding;
    if (!same_identity || c.origin_module != o->origin_module)
      continue;
    DocumentHighlight h;
    h.range = c.range;
    h.kind = c.is_def ? 3 : 2;
    out.push_back(h);
  }
  return out;
}

std::vector<Range> Analysis::references(Position pos, bool include_decl) const {
  auto *o = find_at(impl_->occs, pos);
  if (!o)
    return {};
  std::vector<Range> out;
  for (const auto &c : impl_->occs) {
    const bool same_identity = c.binding == o->binding;
    if (!same_identity || c.origin_module != o->origin_module)
      continue;
    if (!include_decl && c.is_def)
      continue;
    out.push_back(c.range);
  }
  return out;
}

std::vector<SymbolInfo> Analysis::document_symbols() const {
  return impl_->symbols;
}

std::vector<Json> Analysis::completions(Position pos) const {
  (void)pos;
  static const char *keywords[] = {
      "module", "import", "from",   "as",      "export",   "let",
      "in",     "if",     "then",   "else",    "case",     "of",
      "do",     "end",    "try",    "catch",   "raise",    "with",
      "extern", "for",    "effect", "perform", "handle",   "fun",
      "lambda", "record", "type",   "trait",   "instance", "deriving",
      "true",   "false",  "async",  "native",  "io",       "resume"};
  std::unordered_set<std::string> seen;
  std::vector<Json> items;
  auto add = [&](const std::string &name, const std::string &kind,
                 const std::string &detail) {
    if (!seen.insert(name).second)
      return;
    Json it;
    it["label"] = name;
    it["kind"] = kind == "function"    ? 3
                 : kind == "method"    ? 2
                 : kind == "interface" ? 8
                 : kind == "keyword"   ? 14
                                       : 6;
    if (!detail.empty())
      it["detail"] = detail;
    items.push_back(std::move(it));
  };
  for (auto *k : keywords)
    add(k, "keyword", "");
  for (const auto &s : impl_->symbols)
    add(s.name, s.kind, s.type);
  for (const auto &o : impl_->occs)
    add(o.name, o.kind, o.type);
  return items;
}

std::vector<std::uint32_t> Analysis::semantic_tokens() const {
  std::vector<Occurrence> sorted = impl_->occs;
  std::sort(sorted.begin(), sorted.end(),
            [](const Occurrence &a, const Occurrence &b) {
              if (a.range.start.line != b.range.start.line)
                return a.range.start.line < b.range.start.line;
              return a.range.start.character < b.range.start.character;
            });
  std::vector<std::uint32_t> data;
  std::size_t prev_line = 0, prev_col = 0;
  for (const auto &o : sorted) {
    std::uint32_t type = 8; // variable
    if (o.kind == "function")
      type = 0;
    else if (o.kind == "method")
      type = 0;
    else if (o.kind == "type" || o.kind == "class")
      type = 1;
    else if (o.kind == "namespace")
      type = 2;
    else if (o.kind == "interface")
      type = 1;
    auto line = o.range.start.line;
    auto col = o.range.start.character;
    auto delta_line = static_cast<std::uint32_t>(line - prev_line);
    auto delta_col =
        static_cast<std::uint32_t>(line == prev_line ? col - prev_col : col);
    auto len = static_cast<std::uint32_t>(o.range.end.line == o.range.start.line
                                              ? o.range.end.character -
                                                    o.range.start.character
                                              : o.name.size());
    std::uint32_t mods = o.is_def ? 1u : 0u;
    data.push_back(delta_line);
    data.push_back(delta_col);
    data.push_back(len);
    data.push_back(type);
    data.push_back(mods);
    prev_line = line;
    prev_col = col;
  }
  return data;
}

std::optional<std::string>
Analysis::rename(Position pos, std::string_view new_name, Json &edits) const {
  auto *o = find_at(impl_->occs, pos);
  if (!o)
    return std::nullopt;
  Json::Array changes;
  for (const auto &c : impl_->occs) {
    const bool same_identity = c.binding == o->binding;
    if (!same_identity || c.origin_module != o->origin_module)
      continue;
    Json edit;
    edit["range"] = range_json(c.range);
    edit["newText"] = std::string(new_name);
    changes.push_back(std::move(edit));
  }
  Json file;
  file["textDocument"] = Json::Object{{"uri", Json(uri_)}};
  file["edits"] = changes;
  edits = Json::Array{file};
  return o->name;
}

std::optional<Json> Analysis::signature_help(Position pos) const {
  auto make_help = [](const Occurrence &o) -> std::optional<Json> {
    Json sig;
    sig["label"] = o.type.empty() ? o.name : o.name + " : " + o.type;
    Json help;
    help["signatures"] = Json::Array{sig};
    help["activeSignature"] = 0;
    help["activeParameter"] = 0;
    return help;
  };
  if (auto *o = find_at(impl_->occs, pos))
    return make_help(*o);
  // Juxtaposition `f x`: the space trigger leaves the cursor after `f`,
  // so scan left for the applied name.
  auto off = position_to_offset(text_, pos);
  if (off > 0)
    --off;
  while (off > 0 && std::isspace(static_cast<unsigned char>(text_[off])))
    --off;
  while (off > 0 && is_ident_char(static_cast<unsigned char>(text_[off - 1])))
    --off;
  if (off >= text_.size() ||
      !is_ident_char(static_cast<unsigned char>(text_[off])))
    return std::nullopt;
  auto *o = find_at(impl_->occs, offset_to_position(text_, off));
  return o ? make_help(*o) : std::nullopt;
}

std::vector<Json> Analysis::inlay_hints(Range range) const {
  std::vector<Json> out;
  for (const auto &s : impl_->symbols) {
    if (!range.overlaps(s.range))
      continue;
    if (s.type.empty())
      continue;
    Json h;
    h["position"] = Json::Object{
        {"line", Json(static_cast<int>(s.range.end.line))},
        {"character", Json(static_cast<int>(s.range.end.character))},
    };
    h["label"] = " : " + s.type;
    h["kind"] = 1;
    out.push_back(std::move(h));
  }
  return out;
}

std::optional<Json> Analysis::prepare_call_hierarchy(Position pos) const {
  auto *o = find_at(impl_->occs, pos);
  if (!o)
    return std::nullopt;
  Json item;
  item["name"] = o->name;
  item["kind"] = 12;
  item["uri"] = uri_;
  item["range"] = range_json(o->range);
  item["selectionRange"] = range_json(o->range);
  return item;
}

std::vector<Json> Analysis::incoming_calls(std::string_view name) const {
  (void)name;
  return {};
}

std::vector<Json> Analysis::outgoing_calls(std::string_view name) const {
  (void)name;
  return {};
}

std::vector<Json> Analysis::code_actions(Range range) const {
  std::vector<Json> out;
  for (const auto &d : diagnostics()) {
    const bool point_diagnostic = d.range.start == d.range.end;
    if (!d.range.overlaps(range) &&
        !(point_diagnostic && range.contains(d.range.start)))
      continue;
    if (d.code.empty())
      continue;
    Json act;
    const bool trait_instance_diagnostic =
        d.code == "E0105" || d.code == "E0106" ||
        d.message.find("instance") != std::string::npos;
    act["title"] = trait_instance_diagnostic
                       ? "Explain trait instance " + d.code
                       : "Explain " + d.code;
    act["kind"] = "quickfix";
    act["command"] = Json::Object{
        {"title", Json("Explain " + d.code)},
        {"command", Json("yona.explain")},
        {"arguments", Json::Array{Json(d.code)}},
    };
    out.push_back(std::move(act));
  }
  return out;
}

std::vector<SymbolInfo>
Analysis::workspace_symbols(std::string_view query) const {
  std::vector<SymbolInfo> out;
  for (const auto &s : impl_->symbols) {
    if (query.empty() || s.name.find(query) != std::string::npos)
      out.push_back(s);
  }
  return out;
}

std::vector<std::string>
default_module_paths(std::string_view document_path,
                     const std::vector<std::string> &workspace_roots) {
  std::vector<std::string> paths;
  std::unordered_set<std::string> seen;
  auto add = [&](const std::filesystem::path &p) {
    std::error_code ec;
    auto c = std::filesystem::weakly_canonical(p, ec);
    if (ec)
      c = p;
    auto s = c.string();
    if (seen.insert(s).second)
      paths.push_back(s);
  };
#ifdef _WIN32
  const char sep = ';';
#else
  const char sep = ':';
#endif
  if (const char *yp = std::getenv("YONA_PATH"); yp && *yp) {
    std::string cur;
    for (const char *c = yp; *c; ++c) {
      if (*c == sep) {
        if (!cur.empty())
          add(cur);
        cur.clear();
      } else {
        cur.push_back(*c);
      }
    }
    if (!cur.empty())
      add(cur);
  }
  for (const auto &root : workspace_roots) {
    if (!root.empty())
      add(root);
  }
  if (!document_path.empty()) {
    auto parent =
        std::filesystem::path(std::string(document_path)).parent_path();
    if (!parent.empty())
      add(parent);
  }
  add(".");
  if (const char *home = std::getenv("YONA_HOME"); home && *home) {
    add(std::filesystem::path(home) / "lib");
    add(std::filesystem::path(home) / "share" / "yona" / "lib");
  }
  for (auto *cand : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (std::filesystem::exists(std::filesystem::path(cand) /
                                "Prelude.yonai")) {
      add(cand);
      break;
    }
  }
  return paths;
}

} // namespace yona::lsp
