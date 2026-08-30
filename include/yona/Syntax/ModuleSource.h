#ifndef YONA_SYNTAX_MODULESOURCE_H
#define YONA_SYNTAX_MODULESOURCE_H

#include <cctype>
#include <string_view>

namespace yona {

/// True when the first non-comment token is `module`.
///
/// Matches the lexer: `#` starts a line comment through newline (same as
/// `##` docs). Shared by `yonac` and `yls` so stdlib files that open with
/// documentation are compiled and analyzed as modules. The source is borrowed
/// only for the call; the function owns no state, reports no failures, and is
/// safe for concurrent calls.
inline bool is_module_source(std::string_view source) {
  std::size_t i = 0;
  const std::size_t n = source.size();
  while (i < n) {
    const unsigned char c = static_cast<unsigned char>(source[i]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++i;
      continue;
    }
    if (c == '#') {
      while (i < n && source[i] != '\n' && source[i] != '\r')
        ++i;
      continue;
    }
    break;
  }
  if (i + 6 > n)
    return false;
  if (source.substr(i, 6) != "module")
    return false;
  if (i + 6 >= n)
    return true;
  const unsigned char next = static_cast<unsigned char>(source[i + 6]);
  return !std::isalnum(next) && next != '_' && next != '\'';
}

} // namespace yona

#endif /* YONA_SYNTAX_MODULESOURCE_H */
