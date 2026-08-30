#ifndef YONA_SEMANTICS_ACCELERATORDIAG_H
#define YONA_SEMANTICS_ACCELERATORDIAG_H

#include <iosfwd>
#include <string_view>

namespace yona::ast {
class AstNode;
class ModuleDecl;
} // namespace yona::ast
namespace yona::compiler::typechecker {
class TypeChecker;
}

namespace yona::compiler {

/// After successful typecheck, print a JSON document with schema
/// `yona.accelerator_diag` listing call sites that match the explicit
/// `Std\Gpu` exports: columnar compute, float async natives, and
/// capability/discovery (`hasGpu`, `available ()`, etc.). Applies use imported
/// names or `Std\Gpu::…` module calls. Each site includes a stable
/// `api_signature` matching `Std\Gpu.yona`.
/// When the checker has a fully-resolved non-trivial inferred type for the
/// apply node, `inferred_type` is included.
/// `input_filename` is emitted as root `file` when non-empty.
/// All inputs are borrowed for the call. Output failure is reflected only in
/// the stream state (or its configured exceptions). Concurrent calls require
/// immutable AST/checker inputs and independently synchronized streams.
void emit_accelerator_diagnostic_report(std::ostream &out, ast::AstNode *root,
                                        const typechecker::TypeChecker *tc,
                                        std::string_view input_filename = {});

/// Same JSON schema as `emit_accelerator_diagnostic_report`, but scans a parsed
/// module AST. When `tc` is null, only syntax surfaces are walked; root has
/// `"report_kind":"module_ast"` and `inferred_type` is never emitted. When `tc`
/// is non-null (after `typecheck_module_for_accelerator_report` succeeds), root
/// has `"report_kind":"module"` and apply sites may include `inferred_type`.
/// The module and optional checker are borrowed for the call; ownership and
/// stream-failure rules match emit_accelerator_diagnostic_report().
void emit_accelerator_diagnostic_report_for_module(
    std::ostream &out, ast::ModuleDecl *mod,
    std::string_view input_filename = {},
    const typechecker::TypeChecker *tc = nullptr);

/// Type-check the same module bodies the accelerator report walks (top-level
/// functions, extern bodies, instance methods, trait default implementations),
/// then solve constraints. Returns false if inference reports errors or
/// constraint solving fails. Both objects are borrowed; the checker and its
/// DiagnosticEngine are mutated, so the call must not overlap other access to
/// either object or mutation/destruction of the module.
bool typecheck_module_for_accelerator_report(ast::ModuleDecl *mod,
                                             typechecker::TypeChecker &tc);

} // namespace yona::compiler

#endif /* YONA_SEMANTICS_ACCELERATORDIAG_H */
