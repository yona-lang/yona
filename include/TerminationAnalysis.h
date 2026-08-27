#pragma once

#include "SourceLocation.h"

#include <string>
#include <vector>

namespace yona::ast {
class AstNode;
}

namespace yona::compiler::termination_analysis {

enum class Relation { Strict, Weak, Unknown };

struct Failure {
    SourceLocation call_location;
    std::string caller;
    std::string callee;
    std::string component;
    std::string reason;
};

struct Result {
    std::vector<Failure> failures;
};

Result analyze(ast::AstNode& root);

} // namespace yona::compiler::termination_analysis
