#pragma once

#include "lsp/Json.h"
#include "lsp/Utf16.h"
#include "typed_core/Query.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yona::lsp {

using LspDiagnostic = typed_core::Diagnostic;
using SymbolInfo = typed_core::Symbol;
using HoverInfo = typed_core::Hover;

/// Frontend analysis facade used by yls. No LLVM types in this header.
class Analysis {
public:
    Analysis();
    ~Analysis();

    Analysis(const Analysis&) = delete;
    Analysis& operator=(const Analysis&) = delete;
    Analysis(Analysis&&) noexcept;
    Analysis& operator=(Analysis&&) noexcept;

    void set_module_paths(std::vector<std::string> paths);

    void analyze(std::string uri, std::string text);

    const std::string& uri() const { return uri_; }
    const std::string& text() const { return text_; }

    std::vector<LspDiagnostic> diagnostics() const;
    std::optional<HoverInfo> hover(Position pos) const;
    std::vector<Range> definition(Position pos) const;
    std::vector<Range> references(Position pos, bool include_decl) const;
    std::vector<SymbolInfo> document_symbols() const;
    std::vector<Json> completions(Position pos) const;
    std::vector<std::uint32_t> semantic_tokens() const;
    std::optional<std::string> rename(Position pos, std::string_view new_name, Json& edits) const;
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

std::vector<std::string> default_module_paths(
    std::string_view document_path, const std::vector<std::string>& workspace_roots = {});

} // namespace yona::lsp
