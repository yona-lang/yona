#pragma once

/*
 * Portable compile/link helpers for doctest binaries that shell out to compile
 * compiled_runtime.c plus platform C sources and link small Yona executables.
 * Windows: YONAC_CC (default clang), link multiple .o files (lld-link has no -r);
 * Unix: same multi-.o link (clang -r is optional; not relied on).
 */

#include "repo_paths.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>
#include <string>
#include <vector>

namespace yona::test::link {

inline std::filesystem::path scratch_root() {
    auto p = yona::test::repo_root() / "test" / "_scratch";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

inline std::string qpath(const std::filesystem::path& p) {
    /* Always forward slashes: mixed C:/...\\foo from path.string() breaks cmd.exe system(). */
    return "\"" + p.lexically_normal().generic_string() + "\"";
}

/** Quote a cmd.exe argv token. Unlike qpath, this is not a filesystem path:
 * backslashes in `-e` source stay `\`, and `"` is escaped as `""`. */
inline std::string qarg(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"')
            o += "\"\"";
        else
            o += c;
    }
    o += "\"";
    return o;
}

/** MSVC `popen`/`system` is `cmd.exe /c <command>`. If `<command>` starts with `"`,
 * cmd.exe strips the first and last quote on the line (unless the two-quote
 * executable-name exception applies). Extra quoted argv then becomes
 * `exe" "arg` and the child never starts. Wrap so inner quotes survive;
 * keep a trailing ` 2>nul` outside the wrap so it stays a redirection.
 * No-op when the command does not start with `"`. */
inline std::string wrap_for_cmd_c(std::string command) {
    std::string tail;
    static const char kNul[] = " 2>nul";
    const size_t n = sizeof(kNul) - 1;
    if (command.size() >= n && command.compare(command.size() - n, n, kNul) == 0) {
        tail = kNul;
        command.resize(command.size() - n);
    }
    if (!command.empty() && command.front() == '"')
        command = "\"" + command + "\"";
    return command + tail;
}

inline const char* cc() {
    const char* e = std::getenv("YONAC_CC");
    if (e && *e) return e;
#ifdef _WIN32
    return "clang";
#else
    return "cc";
#endif
}

/** Compiler argv0 for `system()` / `cmd.exe`. Quote only if the path contains spaces — a
 * leading `"` breaks `cmd /c` parsing when MSVC `system()` wraps the whole command. */
inline std::string cc_quoted() {
    namespace fs = std::filesystem;
    std::string g = fs::path(cc()).lexically_normal().generic_string();
    if (g.find(' ') != std::string::npos)
        return "\"" + g + "\"";
    return g;
}

inline const char* err_null() {
#ifdef _WIN32
    return " 2>nul";
#else
    return " 2>/dev/null";
#endif
}

inline std::string include_flags() {
    std::ostringstream o;
    o << " -I" << qpath(yona::test::src_dir()) << " -I" << qpath(yona::test::repo_root() / "include");
#ifdef YONA_BUILD_INCLUDE_DIR
    o << " -I" << qpath(YONA_BUILD_INCLUDE_DIR);
#endif
    return o.str();
}

inline std::string trim_cmd_line(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

inline std::string popen_trim(const char* cmd) {
#if defined(_WIN32)
    FILE* f = _popen(cmd, "r");
#else
    FILE* f = popen(cmd, "r");
#endif
    if (!f) return "";
    char buf[512];
    std::string out;
    if (std::fgets(buf, sizeof buf, f)) out = trim_cmd_line(buf);
#if defined(_WIN32)
    _pclose(f);
#else
    pclose(f);
#endif
    return out;
}

inline std::string discovered_homebrew_prefix() {
    if (const char* p = std::getenv("HOMEBREW_PREFIX"); p && *p) return p;
#ifndef _WIN32
    return popen_trim("brew --prefix 2>/dev/null");
#else
    return "";
#endif
}

/** Match CMake yonac when YONA_ENABLE_VULKAN: headers + -D, no vulkan-1 on link.
 * Do **not** add `-DYONA_HAS_VULKAN` here: scratch `compiled_runtime` must stay on the
 * **`gpu_stub` non-Vulkan TU** so codegen subprocesses link without `vulkan-1` (see
 * `docs/gpu-architecture.md` / `CLAUDE.md` scratch-compile note). */
inline std::string vulkan_runtime_cflags() {
    const char* on = std::getenv("YONA_COMPILE_GPU_VULKAN");
    if (!on || std::string(on) == "0")
        return "";
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (const char* sdk = std::getenv("VULKAN_SDK"); sdk && *sdk) {
        candidates.push_back(fs::path(sdk) / "Include");
        candidates.push_back(fs::path(sdk) / "include");
    }
    const std::string brew = discovered_homebrew_prefix();
    if (!brew.empty())
        candidates.push_back(fs::path(brew) / "include");
    for (const auto& inc : candidates) {
        if (fs::exists(inc / "vulkan" / "vulkan.h"))
            return std::string(" -DYONA_COMPILE_GPU_VULKAN=1 -I") + qpath(inc);
    }
    return "";
}

inline std::vector<std::string> platform_sources() {
#ifdef _WIN32
    return {"file_windows.c", "net_windows.c", "os_windows.c"};
#elif defined(__APPLE__)
    return {"kqueue_macos.c", "file_macos.c", "net_macos.c", "os_macos.c"};
#else
    return {"uring_linux.c", "file_linux.c", "net_linux.c", "os_linux.c"};
#endif
}

inline int sh(const std::string& cmd) {
#ifdef _WIN32
    return std::system(wrap_for_cmd_c(cmd).c_str());
#else
    return std::system(cmd.c_str());
#endif
}

/* Paths of compiled_runtime.o then each platform .o (only existing platform sources). */
inline void runtime_object_paths(std::vector<std::filesystem::path>& out) {
    namespace fs = std::filesystem;
    out.clear();
    out.push_back(scratch_root() / "compiled_runtime_test_cr.o");
    for (const auto& pf : platform_sources()) {
        auto ps = yona::test::src_dir() / "runtime" / "platform" / pf;
        if (fs::exists(ps))
            out.push_back(scratch_root() / ("compiled_runtime_test_plat_" + pf + ".o"));
    }
}

inline bool compile_c_file(const std::filesystem::path& src, const std::filesystem::path& dst_o,
                           const std::string& extra_flags = "") {
    std::ostringstream cmd;
    cmd << cc_quoted() << " -c " << qpath(src) << include_flags();
    if (!extra_flags.empty()) cmd << " " << extra_flags;
    cmd << " -o " << qpath(dst_o) << err_null();
    return sh(cmd.str()) == 0;
}

/* True if any .c/.h under dir is newer than `than` (compiled_runtime.c includes
 * seq/hamt/async/…; platform TUs include headers below yona/runtime). */
inline bool runtime_tree_newer_than(const std::filesystem::path& dir,
                                    const std::filesystem::file_time_type& than) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return false;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension();
        if (ext != ".c" && ext != ".h") continue;
        if (it->last_write_time(ec) > than) return true;
    }
    return false;
}

/* (Re)compile runtime + platform objects when sources are newer than outputs. */
inline bool ensure_runtime_objects() {
    namespace fs = std::filesystem;
    auto cr_src = yona::test::src_dir() / "compiled_runtime.c";
    if (!fs::exists(cr_src)) return false;

    fs::path cr_o = scratch_root() / "compiled_runtime_test_cr.o";
    std::vector<std::pair<fs::path, fs::path>> plat; // source, object
    for (const auto& pf : platform_sources()) {
        auto ps = yona::test::src_dir() / "runtime" / "platform" / pf;
        if (!fs::exists(ps)) continue;
        plat.push_back({ps, scratch_root() / ("compiled_runtime_test_plat_" + pf + ".o")});
    }

    bool need = !fs::exists(cr_o);
    if (!need && fs::last_write_time(cr_src) > fs::last_write_time(cr_o)) need = true;
    if (!need) {
        const auto cr_t = fs::last_write_time(cr_o);
        if (runtime_tree_newer_than(yona::test::src_dir() / "runtime", cr_t) ||
            runtime_tree_newer_than(yona::test::repo_root() / "include" / "yona" / "runtime", cr_t))
            need = true;
    }
    for (const auto& [ps, po] : plat) {
        if (!fs::exists(po) || fs::last_write_time(ps) > fs::last_write_time(po)) {
            need = true;
            break;
        }
    }
    if (!need) return true;

    if (!compile_c_file(cr_src, cr_o, vulkan_runtime_cflags())) return false;
    for (const auto& [ps, po] : plat) {
        if (!compile_c_file(ps, po)) return false;
    }
    return true;
}

inline bool append_runtime_objects(std::vector<std::filesystem::path>& objs) {
    if (!ensure_runtime_objects()) return false;
    std::vector<std::filesystem::path> rt;
    runtime_object_paths(rt);
    objs.insert(objs.end(), rt.begin(), rt.end());
    return true;
}

inline bool append_prelude_object(std::vector<std::filesystem::path>& objs) {
    const auto prelude = yona::test::prelude_object();
    if (!std::filesystem::exists(prelude)) return false;
    objs.push_back(prelude);
    return true;
}

/** @deprecated Use append_runtime_objects; kept for a few call sites. */
inline bool ensure_merged_runtime_obj() {
    return ensure_runtime_objects();
}

inline std::string exe_suffix() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

inline bool link_objs_to_exe(const std::vector<std::filesystem::path>& objs,
                             const std::filesystem::path& exe_out, const std::string& extra_libs = "") {
    std::ostringstream cmd;
    cmd << cc_quoted();
    for (const auto& o : objs) cmd << " " << qpath(o);
    cmd << " -o " << qpath(exe_out);
#ifdef _WIN32
    /* lld-link (Clang's default Windows linker) requires an explicit subsystem.
     * Embed an asInvoker manifest as well: without requestedExecutionLevel,
     * Windows' installer-detection heuristic can classify ordinary generated
     * fixture executables as requiring elevation based on their code/data. */
    cmd << " -lws2_32 -ldbghelp -Xlinker /SUBSYSTEM:CONSOLE"
        << " -Xlinker /MANIFEST:EMBED"
        << " -Xlinker " << qarg("/MANIFESTUAC:level='asInvoker' uiAccess='false'");
#elif defined(__APPLE__)
    /* ELF treats undefined weak symbols as NULL; ld64 needs an explicit allow. */
    cmd << " -lpthread -Wl,-U,_yona_regex_free_code";
#else
    cmd << " -lm -lpthread -rdynamic";
#endif
    if (!extra_libs.empty()) cmd << " " << extra_libs;
    const std::string command = cmd.str();
    if (sh(command + err_null()) == 0)
        return true;

    // Fixture failures used to be reported only as LINK_ERROR because the first
    // invocation intentionally hides noisy linker output.  Keep successful
    // runs quiet, but make the failed command actionable on every platform.
    std::cerr << "Yona fixture link failed; rerunning with linker diagnostics:\n";
    (void)sh(command);
    return false;
}

inline std::filesystem::path regex_obj_path() {
    return scratch_root() / "yona_regex_test.o";
}

inline std::string pcre_cflags() {
    const std::filesystem::path configured_archive(YONA_TEST_PCRE2_ARCHIVE);
    if (!configured_archive.empty() && std::filesystem::exists(configured_archive)) {
        std::string configured_flags(YONA_TEST_PCRE2_CFLAGS);
        std::replace(configured_flags.begin(), configured_flags.end(), ';', ' ');
        if (!configured_flags.empty()) return " " + configured_flags;
        const std::filesystem::path configured_include(YONA_TEST_PCRE2_INCLUDE_DIR);
        if (!configured_include.empty() && std::filesystem::exists(configured_include / "pcre2.h"))
            return std::string(" -I") + qpath(configured_include);
    }
    std::string pc = popen_trim("pkg-config --cflags libpcre2-8 2>/dev/null");
    if (!pc.empty()) return " " + pc;
    const std::string brew = discovered_homebrew_prefix();
    if (!brew.empty()) {
        auto inc = std::filesystem::path(brew) / "include";
        if (std::filesystem::exists(inc / "pcre2.h"))
            return std::string(" -I") + qpath(inc);
    }
    return "";
}

inline bool ensure_regex_obj(std::filesystem::path& out_path) {
    namespace fs = std::filesystem;
    auto regex_src = yona::test::src_dir() / "runtime" / "regex.c";
    if (!fs::exists(regex_src)) {
        out_path.clear();
        return false;
    }
    out_path = regex_obj_path();
    bool need = !fs::exists(out_path);
    if (!need && fs::last_write_time(regex_src) > fs::last_write_time(out_path)) need = true;
    if (!need) return true;
    if (!compile_c_file(regex_src, out_path, pcre_cflags())) {
        out_path.clear();
        return false;
    }
    return true;
}

inline std::string pcre_link_flags() {
    const std::filesystem::path configured_archive(YONA_TEST_PCRE2_ARCHIVE);
    if (!configured_archive.empty() && std::filesystem::exists(configured_archive))
        return qpath(configured_archive);
    std::string pc = popen_trim("pkg-config --libs libpcre2-8 2>/dev/null");
    if (!pc.empty()) return pc;
    const std::string brew = discovered_homebrew_prefix();
    if (!brew.empty()) {
        auto lib = std::filesystem::path(brew) / "lib";
        if (std::filesystem::exists(lib / "libpcre2-8.dylib") ||
            std::filesystem::exists(lib / "libpcre2-8.so") ||
            std::filesystem::exists(lib / "libpcre2-8.a"))
            return std::string("-L") + qpath(lib) + " -lpcre2-8";
    }
    return "-lpcre2-8";
}

inline std::string path_for_yona_literal(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

inline void rewrite_codegen_fixture_tmp_paths(std::string& s) {
    namespace fs = std::filesystem;
    const fs::path base = scratch_root();
    static const struct {
        const char* from;
        const char* rel;
    } repl[] = {
        {"/tmp/yona_iter_gen_lines_test.txt", "yona_iter_gen_lines_test.txt"},
        {"/tmp/yona_binary_test.bin", "yona_binary_test.bin"},
        {"/tmp/yona_binary_chunks.bin", "yona_binary_chunks.bin"},
        {"/tmp/yona_binary_seek.bin", "yona_binary_seek.bin"},
        {"/tmp/yona_test_write.txt", "yona_test_write.txt"},
        {"/tmp/yona_test_file.txt", "yona_test_file.txt"},
        {"/tmp/yona_stdlib_file_contract.txt", "yona_stdlib_file_contract.txt"},
        {"/tmp/yona_readexact_prereq.txt", "yona_readexact_prereq.txt"},
        {"/tmp/yona_linear_file_case.txt", "yona_linear_file_case.txt"},
    };
    for (const auto& r : repl) {
        const std::string to = path_for_yona_literal(base / r.rel);
        for (size_t pos = 0; (pos = s.find(r.from, pos)) != std::string::npos;) {
            s.replace(pos, std::strlen(r.from), to);
            pos += to.size();
        }
    }
#if !defined(__linux__)
    {
        const char* from = "/etc/os-release";
        const std::string to = path_for_yona_literal(base / "yona_stub_os_release.txt");
        for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos;) {
            s.replace(pos, std::strlen(from), to);
            pos += to.size();
        }
    }
#endif
#if defined(_WIN32)
    /* cmd.exe: `;` is not a command separator like in sh; use `&&` between echo commands. */
    {
        const char* from = R"(spawn "echo line1; echo line2; echo line3")";
        const char* to = R"(spawn "echo line1&&echo line2&&echo line3")";
        for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos;) {
            s.replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
    {
        const char* from = R"(run "/bin/sh" ["/bin/sh", "-c", "exit 7"])";
        const char* to = R"(run "cmd.exe" ["cmd.exe", "/c", "exit", "7"])";
        for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos;) {
            s.replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
#endif
}

inline std::string popen_read_all(const std::filesystem::path& exe) {
    std::string cmd = qpath(exe) + err_null();
#ifdef _WIN32
    cmd = wrap_for_cmd_c(cmd);
#endif
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "RUN_ERROR";
    std::string result;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), (int)buffer.size(), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    if (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}

/** Run `exe` with `input` on stdin (codegen fixtures that call `readExact`). */
inline std::string popen_read_all_with_stdin(const std::filesystem::path& exe, const std::string& input) {
    namespace fs = std::filesystem;
    fs::path in = scratch_root() / "yona_codegen_stdin.bin";
    {
        std::ofstream out(in.string(), std::ios::binary);
        out << input;
    }
    std::string cmd = qpath(exe) + " < " + qpath(in) + err_null();
#ifdef _WIN32
    cmd = wrap_for_cmd_c(cmd);
#endif
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "RUN_ERROR";
    std::string result;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), (int)buffer.size(), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    if (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}

/** Run `exe` with one `KEY=VAL` in the child environment (fixture stability / isolation). */
inline std::string popen_read_all_run_with_env(const std::filesystem::path& exe, const char* key,
                                               const char* val) {
    namespace fs = std::filesystem;
    std::string cmd;
#ifdef _WIN32
    fs::path bat = scratch_root() / "yona_codegen_env_run.bat";
    {
        std::ofstream out(bat.string(), std::ios::binary);
        out << "@echo off\r\nset " << key << "=" << val << "\r\n" << qpath(exe) << "\r\n";
    }
    cmd = std::string("cmd /c ") + qpath(bat) + err_null();
#else
    cmd = std::string(key) + "=" + val + " " + qpath(exe) + err_null();
#endif
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "RUN_ERROR";
    std::string result;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), (int)buffer.size(), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    if (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}

inline std::string alloc_stats_cmd(const std::filesystem::path& exe) {
#ifdef _WIN32
    std::filesystem::path bat = scratch_root() / "yona_alloc_stats_wrap.bat";
    {
        std::ofstream out(bat.string(), std::ios::binary);
        out << "@echo off\r\nset YONA_ALLOC_STATS=1\r\n" << qpath(exe) << " 2>&1\r\n";
    }
    return std::string("cmd /c ") + qpath(bat);
#else
    return std::string("YONA_ALLOC_STATS=1 ") + qpath(exe) + " 2>&1";
#endif
}

} // namespace yona::test::link
