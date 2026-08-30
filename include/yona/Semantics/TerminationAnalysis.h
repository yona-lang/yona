#ifndef YONA_SEMANTICS_TERMINATIONANALYSIS_H
#define YONA_SEMANTICS_TERMINATIONANALYSIS_H

#include "yona/Support/SourceManager.h"

#include <string>
#include <vector>

namespace yona::ast {
class AstNode;
}

namespace yona::compiler::termination_analysis {

enum class Relation { Strict, Weak, Unknown };

struct Failure {
  SourceRange call_location;
  std::string caller;
  std::string callee;
  std::string component;
  std::string reason;
};

struct Result {
  std::vector<Failure> failures;
};

/// Analyze a borrowed, immutable AST and return an owned result. SourceRange
/// values still require the parsed source manager for display. The function
/// retains no state and supports concurrent calls on immutable trees.
Result analyze(ast::AstNode &root);

} // namespace yona::compiler::termination_analysis

#endif /* YONA_SEMANTICS_TERMINATIONANALYSIS_H */
