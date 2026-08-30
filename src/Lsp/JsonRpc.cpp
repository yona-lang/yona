#include "yona/Lsp/JsonRpc.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace yona::lsp {

std::string JsonRpc::encode(const Json &body) {
  auto dumped = body.dump();
  std::ostringstream os;
  os << "Content-Length: " << dumped.size() << "\r\n\r\n" << dumped;
  return os.str();
}

bool JsonRpc::write(std::ostream &out, const Json &body) {
  out << encode(body) << std::flush;
  return static_cast<bool>(out);
}

std::optional<std::string> JsonRpc::read_body(std::istream &in,
                                              std::size_t max_bytes) {
  std::size_t length = 0;
  bool saw_length = false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      break;
    auto colon = line.find(':');
    if (colon == std::string::npos)
      continue;
    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())))
      value.erase(value.begin());
    for (char &c : name)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name == "content-length") {
      length =
          static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      saw_length = true;
    }
  }
  if (!in || !saw_length || length > max_bytes)
    return std::nullopt;
  std::string body(length, '\0');
  in.read(body.data(), static_cast<std::streamsize>(length));
  if (in.gcount() != static_cast<std::streamsize>(length))
    return std::nullopt;
  return body;
}

std::optional<RpcMessage> JsonRpc::parse_message(std::string_view body) {
  std::string err;
  Json j = Json::parse(body, &err);
  if (!j.is_object())
    return std::nullopt;
  RpcMessage m;
  if (j.has("id") && !j.get("id").is_null()) {
    m.has_id = true;
    m.id = j.get("id");
  }
  m.method = j.get("method").as_string();
  m.params = j.get("params");
  return m;
}

Json JsonRpc::response(const Json &id, const Json &result) {
  Json o;
  o["jsonrpc"] = "2.0";
  o["id"] = id;
  o["result"] = result;
  return o;
}

Json JsonRpc::error(const Json &id, int code, std::string_view message) {
  Json err;
  err["code"] = code;
  err["message"] = std::string(message);
  Json o;
  o["jsonrpc"] = "2.0";
  o["id"] = id;
  o["error"] = err;
  return o;
}

Json JsonRpc::notification(std::string_view method, const Json &params) {
  Json o;
  o["jsonrpc"] = "2.0";
  o["method"] = std::string(method);
  o["params"] = params;
  return o;
}

} // namespace yona::lsp
