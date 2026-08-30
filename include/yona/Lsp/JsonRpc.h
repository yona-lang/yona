#ifndef YONA_LSP_JSONRPC_H
#define YONA_LSP_JSONRPC_H

#include "yona/Lsp/Json.h"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace yona::lsp {

/// Independently owned projection of a JSON-RPC request or notification.
struct RpcMessage {
  Json id;
  bool has_id = false;
  std::string method;
  Json params;
};

/// Stateless JSON-RPC framing and message-construction helpers.
///
/// Ownership:
/// - Inputs are borrowed only for each call; returned strings, messages, and
///   JSON values own their contents.
///
/// Failure:
/// - Transport failures use `false` or `std::nullopt` as detailed below;
///   malformed message JSON also produces `std::nullopt`.
///
/// Thread safety:
/// - Framing and parsing retain no state and may be called concurrently.
/// - Calls sharing an input or output stream must be externally serialized.
class JsonRpc {
public:
  /// Return a complete, independently owned Content-Length frame.
  static std::string encode(const Json &body);

  /// Write and flush one frame. The stream and body are borrowed for the call.
  /// Returns the resulting stream state; a false result may follow a partial
  /// write. Configured iostream exceptions propagate.
  static bool write(std::ostream &out, const Json &body);

  /// Consume one framed body from `in`.
  ///
  /// Returns `std::nullopt` for end-of-stream, a missing length, an oversized
  /// body, a truncated body, or a stream failure. Input already consumed on a
  /// failure is not restored. Configured iostream exceptions propagate.
  static std::optional<std::string>
  read_body(std::istream &in, std::size_t max_bytes = 8 * 1024 * 1024);

  /// Parse an owned message projection. Invalid JSON and non-object values
  /// return `std::nullopt`. Any JSON object is accepted; a missing or
  /// non-string method becomes an empty string and a null ID is absent.
  static std::optional<RpcMessage> parse_message(std::string_view body);

  /// The construction helpers copy their inputs into independently owned JSON.
  static Json response(const Json &id, const Json &result);
  static Json error(const Json &id, int code, std::string_view message);
  static Json notification(std::string_view method, const Json &params);
};

} // namespace yona::lsp

#endif /* YONA_LSP_JSONRPC_H */
