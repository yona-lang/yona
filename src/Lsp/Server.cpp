#include "yona/Lsp/Server.h"

#include "yona/Support/Version.h"

#include <iostream>

namespace yona::lsp {
namespace {

Position pos_of(const Json &p) {
  auto pos = p.get("position");
  return Position{static_cast<std::size_t>(pos.get("line").as_int()),
                  static_cast<std::size_t>(pos.get("character").as_int())};
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

Json loc_json(const std::string &uri, const Range &r) {
  Json o;
  o["uri"] = uri;
  o["range"] = range_json(r);
  return o;
}

} // namespace

Server::Server(std::vector<std::string> extra_paths)
    : extra_paths_(std::move(extra_paths)) {}

Analysis &Server::doc(const std::string &uri) {
  auto it = docs_.find(uri);
  if (it == docs_.end())
    it = docs_.emplace(uri, Analysis()).first;
  return it->second;
}

void Server::open_or_change(const Json &params, bool reset) {
  auto docj = params.get("textDocument");
  auto uri = docj.get("uri").as_string();
  std::string text;
  if (params.has("contentChanges")) {
    auto changes = params.get("contentChanges").as_array();
    if (!changes.empty())
      text = changes.back().get("text").as_string();
    else
      text = doc(uri).text();
  } else {
    text = docj.get("text").as_string();
  }
  if (reset || !text.empty() || params.has("contentChanges")) {
    auto &a = doc(uri);
    apply_module_paths(a, uri);
    a.analyze(uri, std::move(text));
  }
}

void Server::apply_module_paths(Analysis &a, const std::string &uri) {
  auto paths = default_module_paths(uri_to_path(uri), workspace_roots_);
  paths.insert(paths.end(), extra_paths_.begin(), extra_paths_.end());
  a.set_module_paths(std::move(paths));
}

Json Server::watched_files(const Json &) {
  for (auto &[uri, a] : docs_) {
    auto text = a.text();
    apply_module_paths(a, uri);
    a.analyze(uri, std::move(text));
  }
  return Json("refreshed");
}

Json Server::diagnostics_notification(const std::string &uri) const {
  auto it = docs_.find(uri);
  Json::Array items;
  if (it != docs_.end()) {
    for (const auto &d : it->second.diagnostics()) {
      Json item;
      item["range"] = range_json(d.range);
      item["severity"] = d.severity;
      if (!d.code.empty())
        item["code"] = d.code;
      item["source"] = "yls";
      item["message"] = d.message;
      items.push_back(std::move(item));
    }
  }
  Json params;
  params["uri"] = uri;
  params["diagnostics"] = items;
  return JsonRpc::notification("textDocument/publishDiagnostics", params);
}

Json Server::initialize(const Json &params) {
  initialized_ = true;
  workspace_roots_.clear();
  auto add_root = [&](const std::string &uri) {
    if (uri.empty())
      return;
    workspace_roots_.push_back(uri_to_path(uri));
  };
  add_root(params.get("rootUri").as_string());
  for (const auto &folder : params.get("workspaceFolders").as_array())
    add_root(folder.get("uri").as_string());
  Json caps;
  caps["textDocumentSync"] = 1;
  caps["hoverProvider"] = true;
  caps["definitionProvider"] = true;
  caps["documentHighlightProvider"] = true;
  caps["referencesProvider"] = true;
  caps["completionProvider"] =
      Json::Object{{"triggerCharacters", Json::Array{Json(".")}}};
  caps["documentSymbolProvider"] = true;
  caps["workspaceSymbolProvider"] = true;
  caps["renameProvider"] = true;
  caps["signatureHelpProvider"] =
      Json::Object{{"triggerCharacters", Json::Array{Json(" ")}}};
  caps["inlayHintProvider"] = true;
  caps["callHierarchyProvider"] = true;
  caps["codeActionProvider"] = true;
  Json legend;
  legend["tokenTypes"] =
      Json::Array{Json("function"),  Json("type"),     Json("namespace"),
                  Json("parameter"), Json("property"), Json("variable"),
                  Json("keyword"),   Json("number"),   Json("variable")};
  legend["tokenModifiers"] = Json::Array{Json("declaration")};
  caps["semanticTokensProvider"] =
      Json::Object{{"legend", legend}, {"full", Json(true)}};
  Json result;
  result["capabilities"] = caps;
  result["serverInfo"] = Json::Object{{"name", Json("yls")},
                                      {"version", Json(YONA_VERSION_STRING)}};
  return result;
}

Json Server::hover(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  auto h = doc(uri).hover(pos_of(params));
  if (!h)
    return nullptr;
  Json result;
  result["contents"] =
      Json::Object{{"kind", Json("markdown")},
                   {"value", Json("```yona\n" + h->contents + "\n```")}};
  result["range"] = range_json(h->range);
  return result;
}

Json Server::definition(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Json::Array locs;
  for (const auto &loc : doc(uri).definition(pos_of(params)))
    locs.push_back(loc_json(loc.uri.empty() ? uri : loc.uri, loc.range));
  return locs;
}

Json Server::document_highlight(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Json::Array items;
  for (const auto &h : doc(uri).document_highlight(pos_of(params))) {
    Json it;
    it["range"] = range_json(h.range);
    it["kind"] = h.kind;
    items.push_back(std::move(it));
  }
  return items;
}

Json Server::references(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  bool decl = params.get("context").get("includeDeclaration").as_bool(true);
  Json::Array locs;
  for (const auto &r : doc(uri).references(pos_of(params), decl))
    locs.push_back(loc_json(uri, r));
  return locs;
}

Json Server::completion(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Json::Array items = doc(uri).completions(pos_of(params));
  return items;
}

Json Server::document_symbol(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Json::Array items;
  for (const auto &s : doc(uri).document_symbols()) {
    Json it;
    it["name"] = s.name;
    it["kind"] = s.kind == "function"    ? 12
                 : s.kind == "type"      ? 5
                 : s.kind == "namespace" ? 3
                                         : 13;
    it["range"] = range_json(s.range);
    it["selectionRange"] = range_json(s.selection);
    if (!s.type.empty())
      it["detail"] = s.type;
    items.push_back(std::move(it));
  }
  return items;
}

Json Server::workspace_symbol(const Json &params) {
  auto q = params.get("query").as_string();
  Json::Array items;
  for (auto &[uri, a] : docs_) {
    for (const auto &s : a.workspace_symbols(q)) {
      Json it;
      it["name"] = s.name;
      it["kind"] = 12;
      it["location"] = loc_json(uri, s.range);
      items.push_back(std::move(it));
    }
  }
  return items;
}

Json Server::semantic_tokens(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Json::Array data;
  for (auto n : doc(uri).semantic_tokens())
    data.push_back(static_cast<int>(n));
  Json result;
  result["data"] = data;
  return result;
}

Json Server::rename(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Json edits;
  auto old =
      doc(uri).rename(pos_of(params), params.get("newName").as_string(), edits);
  if (!old)
    return nullptr;
  Json we;
  we["documentChanges"] = edits;
  return we;
}

Json Server::signature_help(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  auto h = doc(uri).signature_help(pos_of(params));
  return h ? *h : Json(nullptr);
}

Json Server::inlay_hint(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Range r;
  auto rng = params.get("range");
  r.start.line =
      static_cast<std::size_t>(rng.get("start").get("line").as_int());
  r.start.character =
      static_cast<std::size_t>(rng.get("start").get("character").as_int());
  r.end.line = static_cast<std::size_t>(rng.get("end").get("line").as_int());
  r.end.character =
      static_cast<std::size_t>(rng.get("end").get("character").as_int());
  return doc(uri).inlay_hints(r);
}

Json Server::prepare_call_hierarchy(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  auto item = doc(uri).prepare_call_hierarchy(pos_of(params));
  if (!item)
    return Json::Array{};
  return Json::Array{*item};
}

Json Server::code_action(const Json &params) {
  auto uri = params.get("textDocument").get("uri").as_string();
  Range r;
  auto rng = params.get("range");
  r.start.line =
      static_cast<std::size_t>(rng.get("start").get("line").as_int());
  r.start.character =
      static_cast<std::size_t>(rng.get("start").get("character").as_int());
  r.end.line = static_cast<std::size_t>(rng.get("end").get("line").as_int());
  r.end.character =
      static_cast<std::size_t>(rng.get("end").get("character").as_int());
  return doc(uri).code_actions(r);
}

Json Server::handle(const RpcMessage &msg) {
  if (msg.method == "initialize")
    return initialize(msg.params);
  if (msg.method == "initialized")
    return Json("ok");
  if (msg.method == "shutdown") {
    shutdown_ = true;
    return nullptr;
  }
  if (msg.method == "textDocument/didOpen") {
    open_or_change(msg.params, true);
    return Json("opened");
  }
  if (msg.method == "textDocument/didChange") {
    open_or_change(msg.params, false);
    return Json("changed");
  }
  if (msg.method == "textDocument/didClose") {
    docs_.erase(msg.params.get("textDocument").get("uri").as_string());
    return Json("closed");
  }
  if (msg.method == "textDocument/hover")
    return hover(msg.params);
  if (msg.method == "textDocument/definition")
    return definition(msg.params);
  if (msg.method == "textDocument/documentHighlight")
    return document_highlight(msg.params);
  if (msg.method == "textDocument/references")
    return references(msg.params);
  if (msg.method == "textDocument/completion")
    return completion(msg.params);
  if (msg.method == "textDocument/documentSymbol")
    return document_symbol(msg.params);
  if (msg.method == "workspace/symbol")
    return workspace_symbol(msg.params);
  if (msg.method == "textDocument/semanticTokens/full")
    return semantic_tokens(msg.params);
  if (msg.method == "textDocument/rename")
    return rename(msg.params);
  if (msg.method == "textDocument/signatureHelp")
    return signature_help(msg.params);
  if (msg.method == "textDocument/inlayHint")
    return inlay_hint(msg.params);
  if (msg.method == "textDocument/prepareCallHierarchy")
    return prepare_call_hierarchy(msg.params);
  if (msg.method == "callHierarchy/incomingCalls" ||
      msg.method == "callHierarchy/outgoingCalls")
    return Json::Array{};
  if (msg.method == "textDocument/codeAction")
    return code_action(msg.params);
  if (msg.method == "workspace/didChangeWatchedFiles")
    return watched_files(msg.params);
  return nullptr;
}

int Server::run(std::istream &in, std::ostream &out) {
  while (in) {
    auto body = JsonRpc::read_body(in);
    if (!body)
      break;
    auto msg = JsonRpc::parse_message(*body);
    if (!msg)
      continue;
    if (msg->method == "exit")
      return shutdown_ ? 0 : 1;
    auto result = handle(*msg);
    if (msg->method == "textDocument/didOpen" ||
        msg->method == "textDocument/didChange") {
      auto uri = msg->params.get("textDocument").get("uri").as_string();
      JsonRpc::write(out, diagnostics_notification(uri));
      continue;
    }
    if (msg->method == "workspace/didChangeWatchedFiles") {
      for (const auto &[uri, _] : docs_)
        JsonRpc::write(out, diagnostics_notification(uri));
      continue;
    }
    if (msg->method == "initialized" || !msg->has_id)
      continue;
    JsonRpc::write(out, JsonRpc::response(msg->id, result));
  }
  return shutdown_ ? 0 : 1;
}

} // namespace yona::lsp
