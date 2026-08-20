#pragma once

#include "lsp/Json.h"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace yona::lsp {

struct RpcMessage {
    Json id;
    bool has_id = false;
    std::string method;
    Json params;
};

class JsonRpc {
public:
    static std::string encode(const Json& body);
    static bool write(std::ostream& out, const Json& body);
    static std::optional<std::string> read_body(std::istream& in, std::size_t max_bytes = 8 * 1024 * 1024);
    static std::optional<RpcMessage> parse_message(std::string_view body);

    static Json response(const Json& id, const Json& result);
    static Json error(const Json& id, int code, std::string_view message);
    static Json notification(std::string_view method, const Json& params);
};

} // namespace yona::lsp
