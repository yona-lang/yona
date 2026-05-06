// yonac — Yona compiler
//
// Compiles Yona source code to native executables or object files via LLVM.
//
// Usage:
//   yonac input.yona                  # compile expression to executable
//   yonac input.yona -o output        # compile to output
//   yonac -e "1 + 2"                  # compile expression
//   yonac --emit-ir -e "1 + 2"        # print LLVM IR
//   yonac --emit-obj -e "1 + 2"       # emit object file
//   yonac module.yona                 # compile module to .o + .yonai
//   yonac -I lib main.yona            # compile with module search path
//   yonac -Wall -Werror main.yona     # enable warnings, treat as errors
//   yonac --explain E0100             # explain error code E0100
//   yonac --emit-accelerator-report f.yona -I lib  # JSON: Std\GPU sites
//   yonac --emit-accelerator-report --emit-accelerator-report-with-types mod.yona -I lib  # module + types

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <unordered_set>

#include <CLI/CLI.hpp>
#include "Parser.h"
#include "Codegen.h"
#include "Diagnostic.h"
#include "InProcessLld.h"
#include "LinkerPlan.h"
#include "typechecker/TypeChecker.h"
#include "typechecker/RefinementChecker.h"
#include "typechecker/LinearityChecker.h"
#include "AcceleratorDiag.h"
#include "yona_vulkan_link_cfg.h"

using namespace std;
using namespace yona;
using namespace yona::compiler;
using namespace yona::compiler::codegen;

static const char* yonac_cc_exe() {
    const char* e = getenv("YONAC_CC");
    if (e && *e) return e;
#ifdef _WIN32
    return "clang";
#else
    return "cc";
#endif
}

static string shell_stderr_null() {
#ifdef _WIN32
    return " 2>nul";
#else
    return " 2>/dev/null";
#endif
}

static string q_cmd_path(const filesystem::path& p) {
    return "\"" + p.string() + "\"";
}

#ifdef _WIN32
/** Full path to vulkan-1.lib for packaged yona_runtime (gpu_stub vk* imports).
 *  Prefer CMake-configured path (same as Vulkan::Vulkan at configure time), else VULKAN_SDK. */
static string yona_windows_vulkan_import_lib_path() {
#if YONA_HAVE_CONFIGURED_VULKAN_IMPORT_LIB
    {
        filesystem::path p(YONA_CONFIGURED_VULKAN_IMPORT_LIB_PATH);
        if (filesystem::exists(p))
            return p.string();
    }
#endif
    const char* sdk = getenv("VULKAN_SDK");
    if (!sdk || !sdk[0])
        return {};
    using std::filesystem::path;
    const char* cands[] = {"Lib/vulkan-1.lib", "Lib32/vulkan-1.lib", "lib/vulkan-1.lib"};
    for (const char* rel : cands) {
        path cand = path(sdk) / rel;
        if (exists(cand))
            return cand.string();
    }
    return {};
}
#endif

/** Optional GPU Vulkan compile flags for scratch compiled_runtime.c (env
 *  YONA_COMPILE_GPU_VULKAN=1 + VULKAN_SDK — matches test/yona_link_util).
 *  Packaged sysroot objects are built by CMake with -DYONA_HAS_VULKAN when
 *  find_package(Vulkan) succeeds; yonac links user exes with -lvulkan (Unix) or
 *  CMake-resolved / VULKAN_SDK vulkan-1.lib (Windows — see yona_vulkan_link_cfg.h). */
static string yona_runtime_vulkan_cflags() {
    const char* on = getenv("YONA_COMPILE_GPU_VULKAN");
    if (!on || string(on) == "0")
        return "";
    const char* sdk = getenv("VULKAN_SDK");
    if (!sdk || !*sdk)
        return "";
    std::filesystem::path inc = std::filesystem::path(sdk) / "Include";
    if (!std::filesystem::exists(inc / "vulkan" / "vulkan.h"))
        inc = std::filesystem::path(sdk) / "include";
    if (!std::filesystem::exists(inc / "vulkan" / "vulkan.h"))
        return "";
    return string(" -DYONA_COMPILE_GPU_VULKAN=1 -I") + q_cmd_path(inc);
}

static string llvm_link_executable() {
#ifdef _WIN32
    const char* tool = "llvm-link.exe";
#else
    const char* tool = "llvm-link";
#endif
    const char* cc = getenv("YONAC_CC");
    if (!cc || !*cc) return tool;
    filesystem::path cc_path(cc);
    if (!cc_path.has_parent_path()) return tool;
    return (cc_path.parent_path() / tool).string();
}

static const char* const platform_runtime_sources[] = {
#ifdef _WIN32
    "file_windows.c", "net_windows.c", "os_windows.c",
#else
    "file_linux.c", "net_linux.c", "os_linux.c",
#endif
};

static vector<filesystem::path> embedded_runtime_sources(const filesystem::path& root) {
    vector<filesystem::path> sources = {
        root / "src" / "compiled_runtime.c",
        root / "src" / "runtime" / "seq.c",
        root / "src" / "runtime" / "hamt.c",
        root / "src" / "runtime" / "exceptions.c",
        root / "src" / "runtime" / "closures.c",
        root / "src" / "runtime" / "gpu_vulkan.c",
        root / "src" / "runtime" / "gpu_vulkan_device.c",
        root / "src" / "runtime" / "gpu_vulkan_compute.c",
        root / "src" / "runtime" / "gpu_vulkan_ops.c",
        root / "src" / "runtime" / "gpu_cpu.c",
#ifdef _WIN32
        root / "src" / "runtime" / "platform" / "async_win32.c",
        root / "src" / "runtime" / "platform" / "channel_win32.c",
#else
        root / "src" / "runtime" / "platform" / "async_posix.c",
        root / "src" / "runtime" / "platform" / "channel_posix.c",
#endif
    };
    for (const char* pf : platform_runtime_sources)
        sources.push_back(root / "src" / "runtime" / "platform" / pf);
    return sources;
}

static bool artifact_stale_against_sources(const filesystem::path& artifact,
                                           const vector<filesystem::path>& sources) {
    if (!filesystem::exists(artifact)) return true;
    auto artifact_time = filesystem::last_write_time(artifact);
    for (const auto& source : sources) {
        if (filesystem::exists(source) && filesystem::last_write_time(source) > artifact_time)
            return true;
    }
    return false;
}

static filesystem::path canonical_if_exists(const filesystem::path& p) {
    std::error_code ec;
    if (!filesystem::exists(p, ec)) return {};
    auto c = filesystem::weakly_canonical(p, ec);
    return ec ? p : c;
}

static void push_unique_root(vector<filesystem::path>& roots,
                             unordered_set<string>& seen,
                             const filesystem::path& p) {
    auto c = canonical_if_exists(p);
    if (c.empty()) return;
    string k = c.string();
    if (seen.insert(k).second) roots.push_back(c);
}

static vector<filesystem::path> discover_sysroots(const char* argv0, const string& sysroot_opt) {
    vector<filesystem::path> roots;
    unordered_set<string> seen;

    if (!sysroot_opt.empty()) push_unique_root(roots, seen, filesystem::path(sysroot_opt));
    if (const char* h = getenv("YONA_HOME")) {
        if (*h) push_unique_root(roots, seen, filesystem::path(h));
    }
    if (argv0 && *argv0) {
        auto exe = canonical_if_exists(filesystem::path(argv0).parent_path());
        if (!exe.empty()) {
            push_unique_root(roots, seen, exe);
            push_unique_root(roots, seen, exe.parent_path());
        }
    }
    push_unique_root(roots, seen, filesystem::current_path());
    push_unique_root(roots, seen, filesystem::current_path().parent_path());
    return roots;
}

static bool is_module_source(const string& source) {
    // Match lexer: `#` starts a line comment through newline (same as `##` docs).
    // Without this, stdlib modules whose first token is `module` only after
    // doc lines (e.g. lib/Std/Http.yona) were parsed as expressions and failed.
    size_t i = 0;
    const size_t n = source.size();
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
    if (i + 6 > n) return false;
    return source.compare(i, 6, "module") == 0;
}

int main(int argc, char* argv[]) {
    CLI::App app{"yonac — Yona compiler"};

    string input_file;
    string expression;
    string output_file;
    bool emit_ir = false;
    bool emit_obj = false;
    bool emit_accelerator_report = false;
    bool emit_accelerator_report_with_types = false;
    bool flag_wall = false;
    bool flag_wextra = false;
    bool flag_werror = false;
    bool flag_w = false;
    bool flag_debug = false;
    int opt_level = 2;
    vector<string> include_paths;
    string sysroot_path;
    string explain_code;
    string linker_mode_opt;

    app.add_option("input", input_file, "Input .yona file");
    app.add_option("-e,--expression", expression, "Compile expression");
    app.add_option("-o,--output", output_file, "Output file");
    app.add_option("-I,--include", include_paths, "Module search paths (for .yonai files)");
    app.add_option("--sysroot", sysroot_path,
                   "Yona distribution root (used to find lib/ and runtime objects)");
    app.add_option("--linker-mode", linker_mode_opt,
                   "Linker mode: auto|bundled|system|inprocess (also via YONAC_LINKER_MODE)");
    app.add_option("-O", opt_level, "Optimization level (0-3, default 2)")
       ->check(CLI::Range(0, 3));
    app.add_flag("--emit-ir", emit_ir, "Print LLVM IR instead of compiling");
    app.add_flag("--emit-obj", emit_obj, "Emit object file only (don't link)");
    app.add_flag("--emit-accelerator-report", emit_accelerator_report,
                 "Print JSON of Std\\GPU-shaped call sites and exit (no codegen): "
                 "expression programs after typecheck; modules from AST scan by default");
    app.add_flag("--emit-accelerator-report-with-types", emit_accelerator_report_with_types,
                 "With --emit-accelerator-report on a module, run the typechecker first "
                 "(JSON report_kind \"module\", optional inferred_type per site)");
    app.add_flag("--Wall", flag_wall, "Enable common warnings");
    app.add_flag("--Wextra", flag_wextra, "Enable all warnings");
    app.add_flag("--Werror", flag_werror, "Treat warnings as errors");
    app.add_flag("-w", flag_w, "Suppress all warnings");
    app.add_flag("-g,--debug", flag_debug, "Emit DWARF debug information");
    app.add_option("--explain", explain_code, "Show detailed explanation for an error code (e.g., E0100)");

    CLI11_PARSE(app, argc, argv);

    if (emit_accelerator_report && emit_ir) {
        cerr << "Error: --emit-accelerator-report cannot be combined with --emit-ir" << endl;
        return 1;
    }
    if (emit_accelerator_report && emit_obj) {
        cerr << "Error: --emit-accelerator-report cannot be combined with --emit-obj" << endl;
        return 1;
    }
    if (emit_accelerator_report_with_types && !emit_accelerator_report) {
        cerr << "Error: --emit-accelerator-report-with-types requires --emit-accelerator-report" << endl;
        return 1;
    }

    // --explain: print explanation and exit
    if (!explain_code.empty()) {
        auto code = compiler::parse_error_code(explain_code);
        if (code) {
            string explanation = compiler::error_explanation(*code);
            if (!explanation.empty()) {
                cout << explanation << endl;
                return 0;
            }
        }
        cerr << "Unknown error code: " << explain_code << endl;
        return 1;
    }

    // Get source code
    string source;
    string filename;
    if (!expression.empty()) {
        source = expression;
        filename = "<expression>";
    } else if (!input_file.empty()) {
        ifstream file(input_file);
        if (!file.is_open()) {
            cerr << "Error: cannot open " << input_file << endl;
            return 1;
        }
        stringstream buf;
        buf << file.rdbuf();
        source = buf.str();
        filename = input_file;
    } else {
        cerr << "Error: no input. Use 'yonac file.yona' or 'yonac -e \"expr\"'" << endl;
        return 1;
    }

    bool is_module = is_module_source(source);
    if (emit_accelerator_report_with_types && !is_module) {
        cerr << "Error: --emit-accelerator-report-with-types is only for module sources" << endl;
        return 1;
    }

    // Set default output
    if (output_file.empty()) {
        if (is_module || emit_obj) {
            if (!input_file.empty())
                output_file = filesystem::path(input_file).stem().string() + ".o";
            else
                output_file = "a.o";
        } else {
#ifdef _WIN32
            output_file = "a.exe";
#else
            output_file = "a.out";
#endif
        }
    }

    // Set up diagnostics
    DiagnosticEngine diag;
    diag.set_source(source, filename);
    if (flag_w)      diag.suppress_all_warnings();
    if (flag_wall)   diag.enable_wall();
    if (flag_wextra) diag.enable_wextra();
    if (flag_werror) diag.set_warnings_as_errors(true);

    // Codegen
    string module_name = is_module ? "yona_module" : "yona_program";
    Codegen codegen(module_name, &diag);

    if (flag_debug) codegen.set_debug_info(true, filename);
    codegen.set_opt_level(opt_level);

    vector<filesystem::path> sysroots = discover_sysroots(argc > 0 ? argv[0] : nullptr, sysroot_path);
    yona::toolchain::LinkerPlan linker_selection;
    string linker_mode_raw = linker_mode_opt;
    if (linker_mode_raw.empty()) {
        if (const char* env_mode = getenv("YONAC_LINKER_MODE")) {
            if (*env_mode) linker_mode_raw = env_mode;
        }
    }
    string linker_error;
    if (!yona::toolchain::resolve_linker_plan(linker_mode_raw, sysroots, linker_selection, linker_error)) {
        cerr << "Error: " << linker_error << endl;
        return 1;
    }
    const bool require_inprocess = yona::toolchain::require_inprocess_lld_from_env();
    if (linker_selection.use_inprocess_lld && !yona::toolchain::inprocess_lld_available()) {
        if (require_inprocess) {
            cerr << "Error: inprocess linker mode required but unavailable: "
                 << yona::toolchain::inprocess_lld_unavailable_reason() << endl;
            return 1;
        }
        cerr << "Warning: inprocess linker mode requested but unavailable: "
             << yona::toolchain::inprocess_lld_unavailable_reason()
             << ". Falling back to external linker path." << endl;
    }

    // Set module search paths for import resolution.
    unordered_set<string> module_seen;
    auto add_module_path = [&](const filesystem::path& p) {
        auto c = canonical_if_exists(p);
        if (c.empty()) return;
        string s = c.string();
        if (module_seen.insert(s).second) codegen.module_paths_.push_back(s);
    };
    for (const auto& inc : include_paths) add_module_path(inc);
    if (!input_file.empty()) {
        auto parent = filesystem::path(input_file).parent_path();
        if (!parent.empty()) add_module_path(parent);
    }
    add_module_path(".");
    for (const auto& root : sysroots) {
        add_module_path(root / "lib");
        add_module_path(root / "share" / "yona" / "lib");
    }
    // Backward-compatible relative probing.
    for (auto& candidate : {"lib", "../lib", "../../lib", "../../../lib"}) {
        auto c = canonical_if_exists(filesystem::path(candidate));
        if (!c.empty() && filesystem::exists(c / "Prelude.yonai")) {
            add_module_path(c);
            break;
        }
    }

    llvm::Module* llvm_mod = nullptr;

    if (is_module) {
        parser::Parser parser;
        if (emit_accelerator_report && emit_accelerator_report_with_types) {
            typechecker::TypeChecker type_checker(diag);
            codegen.load_prelude(&parser, &type_checker);
            auto result = parser.parse_module(source, filename);
            if (!result.has_value()) {
                for (auto& e : result.error())
                    diag.error(e.location, compiler::ErrorCode::E0301, e.message);
                return 1;
            }
            if (!typecheck_module_for_accelerator_report(result.value().get(), type_checker))
                return 1;
            emit_accelerator_diagnostic_report_for_module(std::cout, result.value().get(), filename,
                                                           &type_checker);
            return 0;
        }
        codegen.load_prelude(&parser);  // registers constructors in parser
        auto result = parser.parse_module(source, filename);
        if (!result.has_value()) {
            for (auto& e : result.error())
                diag.error(e.location, compiler::ErrorCode::E0301, e.message);
            return 1;
        }
        if (emit_accelerator_report) {
            emit_accelerator_diagnostic_report_for_module(std::cout, result.value().get(), filename);
            return 0;
        }
        llvm_mod = codegen.compile_module(result.value().get());
    } else {
        parser::Parser parser;
        typechecker::TypeChecker type_checker(diag);
        codegen.load_prelude(&parser, &type_checker);  // registers everything

        istringstream stream(source);
        auto parse_result = parser.parse_input(stream);
        if (!parse_result.node) {
            auto result = parser.parse_expression(source, filename);
            if (!result.has_value()) {
                for (auto& e : result.error())
                    diag.error(e.location, compiler::ErrorCode::E0301, e.message);
            } else {
                diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0301, "parse error");
            }
            return 1;
        }

        type_checker.check(parse_result.node.get());
        if (type_checker.has_direct_errors()) {
            return 1;
        }
        codegen.set_type_checker(&type_checker);

        if (emit_accelerator_report) {
            if (!type_checker.solve_constraints() || type_checker.has_errors())
                return 1;
            emit_accelerator_diagnostic_report(std::cout, parse_result.node.get(), &type_checker,
                                               filename);
            return 0;
        }

        // Refinement checking (non-blocking)
        typechecker::RefinementChecker refinement_checker(diag, &type_checker);
        refinement_checker.check(parse_result.node.get());

        // Linearity checking (non-blocking)
        typechecker::LinearityChecker linearity_checker(diag);
        linearity_checker.check(parse_result.node.get());

        llvm_mod = codegen.compile(parse_result.node.get());
    }

    if (!llvm_mod) {
        // Errors already printed by DiagnosticEngine
        return 1;
    }

    // Print summary if there were warnings
    if (diag.warning_count() > 0) {
        cerr << diag.warning_count() << " warning"
             << (diag.warning_count() != 1 ? "s" : "") << " generated." << endl;
    }

    if (emit_ir) {
        cout << codegen.emit_ir();
        return 0;
    }

    // Emit object file
    string obj_file = (is_module || emit_obj) ? output_file : (output_file + ".o");
    if (!codegen.emit_object_file(obj_file)) {
        diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0400, "failed to emit object file");
        return 1;
    }

    // For modules, also emit interface file (.yonai)
    if (is_module) {
        auto yonai_path = filesystem::path(output_file).replace_extension(".yonai");
        codegen.emit_interface_file(yonai_path.string());
        return 0;
    }

    if (emit_obj) return 0;

    // Link expression into executable.
    auto exe_dir = canonical_if_exists(filesystem::path(argv[0]).parent_path());
    if (exe_dir.empty()) exe_dir = filesystem::current_path();
    string rt_obj = (exe_dir / "compiled_runtime.o").string();
    string rt_bc = (exe_dir / "compiled_runtime.bc").string();
    bool rt_obj_is_archive = false;
    vector<string> rt_extra_objs; /* platform .o files linked alongside rt_obj */

    auto find_packaged_runtime_objects = [&]() -> bool {
        for (const auto& root : sysroots) {
            for (const auto& base : {root / "runtime", root / "lib" / "yona" / "runtime"}) {
                for (const auto& archive_name : {"yona_runtime.lib", "libyona_runtime.lib", "libyona_runtime.a"}) {
                    auto archive = canonical_if_exists(base / archive_name);
                    if (!archive.empty()) {
                        rt_obj = archive.string();
                        rt_obj_is_archive = true;
                        rt_extra_objs.clear();
                        return true;
                    }
                }
                auto main_o = canonical_if_exists(base / "compiled_runtime.o");
                if (main_o.empty()) continue;
                rt_obj = main_o.string();
                rt_obj_is_archive = false;
                rt_extra_objs.clear();
                for (const char* pf : platform_runtime_sources) {
                    auto a = canonical_if_exists(base / ("crt_" + string(pf) + ".o"));
                    auto b = canonical_if_exists(base / (string(pf) + ".o"));
                    if (!a.empty()) rt_extra_objs.push_back(a.string());
                    else if (!b.empty()) rt_extra_objs.push_back(b.string());
                }
                return true;
            }
        }
        return false;
    };

    bool have_packaged_runtime = find_packaged_runtime_objects();

    // Find runtime source and compile to both .o (for linking) and .bc (for LTO) if needed.
    if (!have_packaged_runtime) {
        rt_obj_is_archive = false;
        for (const auto& root : sysroots) {
            auto candidate = root / "src" / "compiled_runtime.c";
            if (!filesystem::exists(candidate)) continue;
            filesystem::path src_dir_p = root / "src";
            filesystem::path inc_dir_p = root / "include";
            string i_flags =
                " -I" + q_cmd_path(src_dir_p) + " -I" + q_cmd_path(inc_dir_p) + yona_runtime_vulkan_cflags();

            vector<string> plat_pf;
            vector<string> plat_obj_paths;
            for (const char* pf : platform_runtime_sources) {
                auto plat_src = root / "src" / "runtime" / "platform" / pf;
                if (filesystem::exists(plat_src)) {
                    plat_pf.push_back(pf);
                    plat_obj_paths.push_back((exe_dir / ("crt_" + string(pf) + ".o")).string());
                }
            }

            auto runtime_sources = embedded_runtime_sources(root);
            bool need_rt = artifact_stale_against_sources(filesystem::path(rt_obj), runtime_sources);
            for (size_t i = 0; i < plat_obj_paths.size(); ++i) {
                auto po = filesystem::path(plat_obj_paths[i]);
                auto ps = root / "src" / "runtime" / "platform" / plat_pf[i];
                if (!filesystem::exists(po) ||
                    (filesystem::exists(ps) && filesystem::last_write_time(ps) > filesystem::last_write_time(po))) {
                    need_rt = true;
                    break;
                }
            }

            if (need_rt) {
                const char* cc = yonac_cc_exe();
                string main_cmd = string(cc) + " -c " + q_cmd_path(candidate) + i_flags + " -o " +
                                   q_cmd_path(filesystem::path(rt_obj)) + shell_stderr_null();
                if (system(main_cmd.c_str()) != 0) {
                    cerr << "Error: failed to compile compiled_runtime.c (set YONAC_CC or install clang in PATH)"
                         << endl;
                    return 1;
                }
                for (size_t i = 0; i < plat_pf.size(); ++i) {
                    auto plat_src = root / "src" / "runtime" / "platform" / plat_pf[i];
                    string plat_cmd = string(cc) + " -c " + q_cmd_path(plat_src) + i_flags + " -o " +
                                       q_cmd_path(filesystem::path(plat_obj_paths[i])) + shell_stderr_null();
                    if (system(plat_cmd.c_str()) != 0) {
                        cerr << "Error: failed to compile runtime platform " << plat_pf[i] << endl;
                        return 1;
                    }
                }
#ifndef _WIN32
                for (size_t i = 0; i < plat_obj_paths.size(); ++i) {
                    string merged = rt_obj + ".merged";
                    string merge_cmd = string(cc) + " -r " + q_cmd_path(filesystem::path(rt_obj)) + " " +
                                       q_cmd_path(filesystem::path(plat_obj_paths[i])) + " -o " +
                                       q_cmd_path(filesystem::path(merged)) + shell_stderr_null();
                    if (system(merge_cmd.c_str()) != 0) {
                        cerr << "Error: failed to merge runtime objects" << endl;
                        return 1;
                    }
                    filesystem::remove(rt_obj);
                    filesystem::rename(merged, rt_obj);
                    filesystem::remove(plat_obj_paths[i]);
                }
#else
                rt_extra_objs = plat_obj_paths;
#endif
            }
#ifdef _WIN32
            else {
                rt_extra_objs = plat_obj_paths;
            }
#endif

            // Compile to LLVM bitcode for LTO (enables runtime function inlining).
            // Merge all runtime sources (main + platform) into one bitcode.
            bool need_bc = artifact_stale_against_sources(filesystem::path(rt_bc), runtime_sources);
            if (need_bc) {
                string bc_main = rt_bc + ".main";
                string bc_cmd = string(yonac_cc_exe()) + " -emit-llvm -O2 -c " + q_cmd_path(candidate) +
                    i_flags + " -o " + q_cmd_path(filesystem::path(bc_main)) + shell_stderr_null();
                system(bc_cmd.c_str());

                vector<string> bc_files = {bc_main};
                for (const char* pf : platform_runtime_sources) {
                    auto plat_src = root / "src" / "runtime" / "platform" / pf;
                    if (filesystem::exists(plat_src)) {
                        string plat_bc = rt_bc + "." + string(pf) + ".bc";
                        system((string(yonac_cc_exe()) + " -emit-llvm -O2 -c " + q_cmd_path(plat_src) +
                                   i_flags + " -o " + q_cmd_path(filesystem::path(plat_bc)) + shell_stderr_null())
                                   .c_str());
                        bc_files.push_back(plat_bc);
                    }
                }
                string link_bc = llvm_link_executable();
                for (const auto& f : bc_files) link_bc += " " + q_cmd_path(filesystem::path(f));
                link_bc += " -o " + q_cmd_path(filesystem::path(rt_bc)) + shell_stderr_null();
                system(link_bc.c_str());
                for (const auto& f : bc_files) filesystem::remove(f);
            }
            break;
        }
    }

    // LTO: link runtime bitcode into the module before emitting object code.
    // This enables LLVM to inline seq_head, seq_tail, etc.
    bool lto_active = false;
    bool rt_bc_usable = filesystem::exists(rt_bc);
    if (rt_bc_usable && have_packaged_runtime && filesystem::exists(filesystem::path(rt_obj)) &&
        filesystem::last_write_time(filesystem::path(rt_obj)) > filesystem::last_write_time(filesystem::path(rt_bc))) {
        rt_bc_usable = false;
    }
    if (rt_bc_usable) {
        lto_active = codegen.link_runtime_bitcode(rt_bc);
        if (lto_active) {
            codegen.optimize();
            if (!codegen.emit_object_file(obj_file)) {
                diag.error(SourceLocation::unknown(), "failed to emit LTO object file");
                return 1;
            }
        }
    }

    // Find Prelude.o for linking
    string prelude_obj;
    for (auto& dir : codegen.module_paths_) {
        auto candidate = filesystem::path(dir) / "Prelude.o";
        if (filesystem::exists(candidate)) {
            prelude_obj = candidate.string();
            break;
        }
    }

    // When LTO merged the runtime, don't link rt_obj separately (avoid dups).
    // Unix: -rdynamic exports symbols for backtrace_symbols() stack traces.
    auto append_link_objects = [&](auto&& append_one) {
        append_one(obj_file);
        if (!lto_active) {
            append_one(rt_obj);
            if (!rt_obj_is_archive) {
#ifdef _WIN32
                for (const auto& ex : rt_extra_objs) append_one(ex);
#endif
            }
        }
        if (!prelude_obj.empty()) append_one(prelude_obj);
    };

    int link_result = 1;
    bool used_inprocess = false;
    if (linker_selection.use_inprocess_lld && yona::toolchain::inprocess_lld_available()) {
        vector<string> lld_args;
#ifdef _WIN32
        lld_args.push_back("lld-link");
        lld_args.push_back("/NOLOGO");
        append_link_objects([&](const string& s) { lld_args.push_back(s); });
        lld_args.push_back("/OUT:" + filesystem::path(output_file).string());
        lld_args.push_back("ws2_32.lib");
        lld_args.push_back("dbghelp.lib");
        {
            string vk_lib = yona_windows_vulkan_import_lib_path();
            if (!vk_lib.empty())
                lld_args.push_back(vk_lib);
        }
#else
        lld_args.push_back("ld.lld");
        append_link_objects([&](const string& s) { lld_args.push_back(s); });
        lld_args.push_back("-o");
        lld_args.push_back(filesystem::path(output_file).string());
#ifdef __APPLE__
        lld_args.push_back("-lSystem");
#else
        lld_args.push_back("-lm");
        lld_args.push_back("-lpthread");
        lld_args.push_back("-rdynamic");
#endif
#ifdef YONAC_EXE_LINK_POSIX_VULKAN
        lld_args.push_back("-lvulkan");
#endif
#endif
        yona::toolchain::InProcessLldResult lld_res;
        used_inprocess = true;
        if (yona::toolchain::run_inprocess_lld(lld_args, lld_res)) {
            link_result = 0;
        } else {
            if (require_inprocess) {
                diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0401,
                           "in-process LLD link failed and fallback is disabled");
                if (!lld_res.stderr_text.empty()) cerr << lld_res.stderr_text << endl;
                return 1;
            }
            cerr << "Warning: in-process LLD link failed, falling back to external linker path.";
            if (!lld_res.stderr_text.empty()) cerr << " details: " << lld_res.stderr_text;
            cerr << endl;
            link_result = 1;
        }
    }

    if (!used_inprocess || link_result != 0) {
        const char* cc_link = yonac_cc_exe();
        string link_cmd = string(cc_link) + " " + q_cmd_path(filesystem::path(obj_file));
        if (linker_selection.use_bundled_lld) {
            link_cmd += " -fuse-ld=lld -B" + q_cmd_path(linker_selection.bundled_lld_path.parent_path());
        }
        if (!lto_active) {
            link_cmd += " " + q_cmd_path(filesystem::path(rt_obj));
            if (!rt_obj_is_archive) {
#ifdef _WIN32
                for (const auto& ex : rt_extra_objs) link_cmd += " " + q_cmd_path(filesystem::path(ex));
#endif
            }
        }
        if (!prelude_obj.empty()) link_cmd += " " + q_cmd_path(filesystem::path(prelude_obj));
#ifdef _WIN32
        link_cmd += " -o " + q_cmd_path(filesystem::path(output_file)) + " -lws2_32 -ldbghelp";
        {
            string vk_lib = yona_windows_vulkan_import_lib_path();
            if (!vk_lib.empty())
                link_cmd += " " + q_cmd_path(filesystem::path(vk_lib));
        }
#else
        link_cmd += " -lm -lpthread -rdynamic";
#ifdef YONAC_EXE_LINK_POSIX_VULKAN
        link_cmd += " -lvulkan";
#endif
        link_cmd += " -o " + q_cmd_path(filesystem::path(output_file));
#endif
        link_result = system(link_cmd.c_str());
    }
    filesystem::remove(obj_file);

    if (link_result != 0) {
        diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0401, "linking failed");
        return 1;
    }

    return 0;
}
