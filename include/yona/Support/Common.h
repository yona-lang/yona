//
// Created by akovari on 28.11.24.
//

#ifndef YONA_SUPPORT_COMMON_H
#define YONA_SUPPORT_COMMON_H

#include "yona/Support/SourceManager.h"
#include "yona/Support/Terminal.h"

#include <any>
#include <memory>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace yona {
using std::map;
using std::move;
using std::optional;
using std::ostream;
using std::pair;
using std::shared_ptr;
using std::string;
using std::vector;

/// Process-wide compiler configuration.
///
/// The object owns its strings and paths. References into those containers are
/// invalidated by later configuration changes. Configure it before starting
/// concurrent compilation: reads and writes are not synchronized.
inline struct YonaEnvironment {
  vector<string> search_paths;
  string main_fun_name;
  bool compile_mode = false;
} YONA_ENVIRONMENT;

// Forward declarations for AST nodes
namespace ast {
class AstNode;
}

} // namespace yona

#endif /* YONA_SUPPORT_COMMON_H */
