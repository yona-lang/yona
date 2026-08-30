#ifndef YONA_LSP_SERVER_H
#define YONA_LSP_SERVER_H

#include "yona/Lsp/Analysis.h"
#include "yona/Lsp/JsonRpc.h"

#include <iosfwd>
#include <string>
#include <unordered_map>

namespace yona::lsp {

/// Single-threaded JSON-RPC/LSP server state machine.
///
/// Ownership:
/// - The server owns its search paths, workspace roots, and analyzed documents.
/// - `run()` borrows both streams for the complete blocking event loop.
/// - Public handler results are independently owned JSON values.
///
/// Failure:
/// - `run()` returns zero if its loop terminates after shutdown was requested,
///   and one if it terminates before shutdown.
/// - Malformed JSON messages are skipped. End-of-stream and framing errors end
///   the loop; output failures are reflected in the stream but not returned
///   separately. Configured iostream and unexpected filesystem exceptions
///   propagate.
/// - `handle()` returns JSON null for unsupported methods.
///
/// Thread safety:
/// - The server has no internal synchronization. `run()`, `handle()`, and
///   `diagnostics_notification()` must not overlap on the same instance.
class Server {
public:
  explicit Server(std::vector<std::string> extra_paths = {});

  int run(std::istream &in, std::ostream &out);

  Json handle(const RpcMessage &msg);
  Json diagnostics_notification(const std::string &uri) const;

private:
  Analysis &doc(const std::string &uri);
  void open_or_change(const Json &params, bool reset);
  Json initialize(const Json &params);
  Json hover(const Json &params);
  Json definition(const Json &params);
  Json document_highlight(const Json &params);
  Json references(const Json &params);
  Json completion(const Json &params);
  Json document_symbol(const Json &params);
  Json workspace_symbol(const Json &params);
  Json semantic_tokens(const Json &params);
  Json rename(const Json &params);
  Json signature_help(const Json &params);
  Json inlay_hint(const Json &params);
  Json prepare_call_hierarchy(const Json &params);
  Json code_action(const Json &params);
  Json watched_files(const Json &params);
  void apply_module_paths(Analysis &a, const std::string &uri);

  std::vector<std::string> extra_paths_;
  std::vector<std::string> workspace_roots_;
  std::unordered_map<std::string, Analysis> docs_;
  bool initialized_ = false;
  bool shutdown_ = false;
};

} // namespace yona::lsp

#endif /* YONA_LSP_SERVER_H */
