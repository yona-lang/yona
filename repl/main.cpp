// yona — Yona REPL (compile-and-run)
//
// Reads expressions, compiles to native code via yonac, runs, shows output.

#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <array>
#include <vector>

#include "Parser.h"
#include "Codegen.h"
#include "InProcessLld.h"
#include "LinkerPlan.h"

using namespace std;
using namespace yona;
using namespace yona::compiler::codegen;
namespace fs = std::filesystem;

static const char* const platform_runtime_sources[] = {
#ifdef _WIN32
    "file_windows.c", "net_windows.c", "os_windows.c",
#elif defined(__APPLE__)
    "kqueue_macos.c", "file_macos.c", "net_macos.c", "os_macos.c",
#else
    "uring_linux.c", "file_linux.c", "net_linux.c", "os_linux.c",
#endif
};

static fs::path repl_temp_dir() {
    if (const char* t = getenv("TMPDIR"))
        return fs::path(t);
    if (const char* t = getenv("TEMP"))
        return fs::path(t);
    if (const char* t = getenv("TMP"))
        return fs::path(t);
    return fs::temp_directory_path();
}

static const char* repl_cc() {
    if (const char* e = getenv("YONAC_CC"))
        if (*e) return e;
#ifdef _WIN32
    return "clang";
#else
    return "cc";
#endif
}

static string q(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

static string err_null() {
#ifdef _WIN32
    return " 2>nul";
#else
    return " 2>/dev/null";
#endif
}

static string exe_suffix() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

static fs::path canonical_if_exists(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return {};
    auto c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

static vector<fs::path> discover_sysroots(const char* argv0) {
    return yona::toolchain::discover_sysroots(argv0);
}

static string compile_and_run(const string& expr,
                              const string& rt_obj,
                              const vector<string>& rt_extra_objs,
                              const yona::toolchain::LinkerPlan& linker_selection,
                              bool require_inprocess) {
    Codegen codegen("yona_repl");
    parser::Parser parser;
    istringstream stream(expr);
    auto parse_result = parser.parse_input(stream);
    if (!parse_result.node) return "Parse error";

    auto* llvm_mod = codegen.compile(parse_result.node.get());
    if (!llvm_mod) return ""; // errors already printed

    fs::path base = repl_temp_dir();
    fs::path obj = base / "yona_repl.o";
    fs::path exe = base / ("yona_repl" + exe_suffix());
    if (!codegen.emit_object_file(obj.string())) return "Codegen error";

    ostringstream link;
    int link_result = 1;
    bool used_inprocess = false;
    if (linker_selection.use_inprocess_lld && yona::toolchain::inprocess_lld_available()) {
        std::vector<std::string> lld_args;
#ifdef _WIN32
        lld_args.push_back("lld-link");
        lld_args.push_back("/NOLOGO");
        lld_args.push_back(obj.string());
        lld_args.push_back(fs::path(rt_obj).string());
        for (const auto& ex : rt_extra_objs) lld_args.push_back(fs::path(ex).string());
        lld_args.push_back("/OUT:" + exe.string());
        for (const auto& a : yona::toolchain::inprocess_lld_system_args())
            lld_args.push_back(a);
#else
#ifdef __APPLE__
        lld_args.push_back("ld64.lld");
#else
        lld_args.push_back("ld.lld");
#endif
        lld_args.push_back(obj.string());
        lld_args.push_back(fs::path(rt_obj).string());
        for (const auto& ex : rt_extra_objs) lld_args.push_back(fs::path(ex).string());
        lld_args.push_back("-o");
        lld_args.push_back(exe.string());
        for (const auto& a : yona::toolchain::inprocess_lld_system_args())
            lld_args.push_back(a);
#endif
        yona::toolchain::InProcessLldResult lld_res;
        used_inprocess = true;
        if (yona::toolchain::run_inprocess_lld(lld_args, lld_res)) {
            link_result = 0;
        } else {
            if (require_inprocess) {
                if (!lld_res.diagnostic_text().empty()) cerr << lld_res.diagnostic_text() << endl;
                return "Link error";
            }
            cerr << "Warning: in-process LLD link failed in REPL, falling back to external linker path.";
            if (!lld_res.diagnostic_text().empty()) cerr << " details: " << lld_res.diagnostic_text();
            cerr << endl;
        }
    }
    if (!used_inprocess || link_result != 0) {
        link << repl_cc() << " " << q(obj) << " " << q(fs::path(rt_obj));
        if (linker_selection.use_bundled_lld) {
            link << " -fuse-ld=lld -B" << q(linker_selection.bundled_lld_path.parent_path());
        }
        for (const auto& ex : rt_extra_objs) link << " " << q(fs::path(ex));
        link << " -o " << q(exe);
#ifdef _WIN32
        link << " -lws2_32 -ldbghelp";
#elif defined(__APPLE__)
        link << " -lpthread -Wl,-U,_yona_regex_free_code";
#else
        link << " -lm -lpthread -rdynamic";
#endif
        link << err_null();
        if (system(link.str().c_str()) != 0) return "Link error";
    }
    fs::remove(obj);

    // Run and capture output
    array<char, 4096> buf;
    string output;
    FILE* pipe = popen(q(exe).c_str(), "r");
    if (!pipe) return "Exec error";
    while (fgets(buf.data(), buf.size(), pipe)) output += buf.data();
    pclose(pipe);
    fs::remove(exe);

    // Trim trailing newline
    if (!output.empty() && output.back() == '\n') output.pop_back();
    return output;
}

int main(int argc, char* argv[]) {
    vector<fs::path> sysroots = discover_sysroots(argc > 0 ? argv[0] : nullptr);
    yona::toolchain::LinkerPlan linker_selection;
    string linker_error;
    string linker_mode_raw = "auto";
    if (const char* env_mode = getenv("YONAC_LINKER_MODE")) {
        if (*env_mode) linker_mode_raw = env_mode;
    }
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
    string rt_obj;
    bool rt_obj_is_archive = false;
    vector<string> rt_extra_objs;

    // Prefer packaged runtime objects from distribution roots.
    for (const auto& root : sysroots) {
        for (const auto& base : {root / "runtime", root / "lib" / "yona" / "runtime"}) {
            for (const auto& archive_name : {"yona_runtime.lib", "libyona_runtime.lib", "libyona_runtime.a"}) {
                auto archive = canonical_if_exists(base / archive_name);
                if (!archive.empty()) {
                    rt_obj = archive.string();
                    rt_obj_is_archive = true;
                    rt_extra_objs.clear();
                    break;
                }
            }
            if (!rt_obj.empty()) break;
            auto main_o = canonical_if_exists(base / "compiled_runtime.o");
            if (main_o.empty()) continue;
            rt_obj = main_o.string();
            rt_obj_is_archive = false;
            for (const char* pf : platform_runtime_sources) {
                auto a = canonical_if_exists(base / ("crt_" + string(pf) + ".o"));
                auto b = canonical_if_exists(base / (string(pf) + ".o"));
                if (!a.empty()) rt_extra_objs.push_back(a.string());
                else if (!b.empty()) rt_extra_objs.push_back(b.string());
            }
            break;
        }
        if (!rt_obj.empty()) break;
    }

    if (rt_obj.empty()) {
        // Fall back to source compile from discovered roots.
        auto out_dir = canonical_if_exists(fs::path(argv[0]).parent_path());
        if (out_dir.empty()) out_dir = repl_temp_dir();
        rt_obj_is_archive = false;
        for (const auto& root : sysroots) {
            auto src = root / "src" / "compiled_runtime.c";
            if (fs::exists(src)) {
                fs::path out_o = out_dir / "compiled_runtime.o";
                ostringstream cmd;
                cmd << repl_cc() << " -c " << q(src) << " -I" << q(root / "src") << " -I"
                    << q(root / "include") << " -o " << q(out_o) << err_null();
                if (system(cmd.str().c_str()) == 0) {
                    rt_obj = out_o.string();
                    for (const char* pf : platform_runtime_sources) {
                        fs::path plat_src = root / "src" / "runtime" / "platform" / pf;
                        if (!fs::exists(plat_src)) continue;
                        fs::path plat_o = out_dir / ("crt_" + string(pf) + ".o");
                        ostringstream pcmd;
                        pcmd << repl_cc() << " -c " << q(plat_src) << " -I" << q(root / "src")
                             << " -I" << q(root / "include") << " -o " << q(plat_o) << err_null();
                        if (system(pcmd.str().c_str()) == 0) {
                            rt_extra_objs.push_back(plat_o.string());
                        }
                    }
                }
                break;
            }
        }
    }

    if (rt_obj.empty()) {
        cerr << "Error: cannot find compiled_runtime.o" << endl;
        return 1;
    }

    cout << "Yona REPL (type expressions, Ctrl-D to exit)" << endl;

    string line;
    while (true) {
        cout << "yona> " << flush;
        if (!getline(cin, line)) break;
        if (line.empty()) continue;
        if (line == ":q" || line == ":quit") break;

        vector<string> link_extra = rt_obj_is_archive ? vector<string>{} : rt_extra_objs;
        string result = compile_and_run(line, rt_obj, link_extra, linker_selection, require_inprocess);
        if (!result.empty()) cout << result << endl;
    }

    return 0;
}
