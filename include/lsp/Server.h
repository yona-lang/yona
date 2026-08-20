#pragma once

#include "lsp/Analysis.h"
#include "lsp/JsonRpc.h"

#include <iosfwd>
#include <string>
#include <unordered_map>

namespace yona::lsp {

class Server {
public:
    explicit Server(std::vector<std::string> extra_paths = {});

    int run(std::istream& in, std::ostream& out);

    Json handle(const RpcMessage& msg);
    Json diagnostics_notification(const std::string& uri) const;

private:
    Analysis& doc(const std::string& uri);
    void open_or_change(const Json& params, bool reset);
    Json initialize(const Json& params);
    Json hover(const Json& params);
    Json definition(const Json& params);
    Json references(const Json& params);
    Json completion(const Json& params);
    Json document_symbol(const Json& params);
    Json workspace_symbol(const Json& params);
    Json semantic_tokens(const Json& params);
    Json rename(const Json& params);
    Json signature_help(const Json& params);
    Json inlay_hint(const Json& params);
    Json prepare_call_hierarchy(const Json& params);
    Json code_action(const Json& params);
    Json watched_files(const Json& params);
    void apply_module_paths(Analysis& a, const std::string& uri);

    std::vector<std::string> extra_paths_;
    std::vector<std::string> workspace_roots_;
    std::unordered_map<std::string, Analysis> docs_;
    bool initialized_ = false;
    bool shutdown_ = false;
};

} // namespace yona::lsp
