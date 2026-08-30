#ifndef YONA_LSP_ANALYSIS_H
#define YONA_LSP_ANALYSIS_H

#include "yona/Lsp/Json.h"
#include "yona/Lsp/Protocol.h"
#include "yona/Lsp/Utf16.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yona::lsp {

/// Stateful frontend analysis facade used by yls.
///
/// Ownership:
/// - The facade owns the current URI, source text, compiler state, and semantic
///   query data. Module-search paths are copied into the facade.
/// - Query results are independent values owned by the caller.
/// - `uri()` and `text()` return references valid until facade destruction.
///   `analyze()` and move assignment replace the values observed through those
///   references and may invalidate character pointers and iterators.
///
/// Failure:
/// - `analyze()` replaces the previous document state even when parsing or
///   semantic analysis fails. Expected source failures are exposed through
///   `diagnostics()`; query misses produce empty vectors or `std::nullopt`.
/// - `rename()` updates its output parameter only when it returns a name.
/// - Allocation and filesystem exceptions are not translated to diagnostics.
/// - A moved-from instance may only be destroyed or move-assigned.
///
/// Thread safety:
/// - The class has no internal synchronization. Serialize all operations on an
///   instance, including const queries while `analyze()` is running.
/// - Distinct instances may be used concurrently while their shared process
///   environment and searched files remain unchanged.
class Analysis {
public:
  Analysis();
  ~Analysis();

  Analysis(const Analysis &) = delete;
  Analysis &operator=(const Analysis &) = delete;
  Analysis(Analysis &&) noexcept;
  Analysis &operator=(Analysis &&) noexcept;

  /// Replace the paths used by subsequent analyses. Existing query data is not
  /// recomputed until `analyze()` is called again.
  void set_module_paths(std::vector<std::string> paths);

  /// Replace the current document and all analysis results.
  void analyze(std::string uri, std::string text);

  const std::string &uri() const { return uri_; }
  const std::string &text() const { return text_; }

  std::vector<LspDiagnostic> diagnostics() const;
  std::optional<HoverInfo> hover(Position pos) const;
  std::vector<Location> definition(Position pos) const;
  std::vector<Range> references(Position pos, bool include_decl) const;
  std::vector<DocumentHighlight> document_highlight(Position pos) const;
  std::vector<SymbolInfo> document_symbols() const;
  std::vector<Json> completions(Position pos) const;
  std::vector<std::uint32_t> semantic_tokens() const;
  std::optional<std::string> rename(Position pos, std::string_view new_name,
                                    Json &edits) const;
  std::optional<Json> signature_help(Position pos) const;
  std::vector<Json> inlay_hints(Range range) const;
  std::optional<Json> prepare_call_hierarchy(Position pos) const;
  std::vector<Json> incoming_calls(std::string_view name) const;
  std::vector<Json> outgoing_calls(std::string_view name) const;
  std::vector<Json> code_actions(Range range) const;
  std::vector<SymbolInfo> workspace_symbols(std::string_view query) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string uri_;
  std::string text_;
};

/// Discover module-search paths from the process environment, workspace roots,
/// document parent, current directory, and installed-layout candidates.
///
/// Inputs are borrowed for the call and the returned strings are owned.
/// Filesystem canonicalization failures retain the uncanonicalized candidate;
/// filesystem exceptions from other probes may propagate. The process
/// environment and working directory must not be mutated concurrently.
std::vector<std::string>
default_module_paths(std::string_view document_path,
                     const std::vector<std::string> &workspace_roots = {});

} // namespace yona::lsp

#endif /* YONA_LSP_ANALYSIS_H */
