#include "yona/Lsp/Server.h"

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
  std::vector<std::string> extra;
  bool stdio = true;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--stdio")
      stdio = true;
    else if (a == "-I" && i + 1 < argc)
      extra.emplace_back(argv[++i]);
    else if (a == "--help" || a == "-h") {
      std::cerr << "yls — Yona language server\n"
                << "Usage: yls [--stdio] [-I path]\n";
      return 0;
    }
  }
  (void)stdio;
#ifdef _WIN32
  // Content-Length framing is byte-exact; text-mode CRLF translation desyncs
  // the body length on Windows. ios::binary is an openmode (file streams);
  // stdio cin/cout need _setmode.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  yona::lsp::Server server(std::move(extra));
  return server.run(std::cin, std::cout);
}
