#include "LinkerPlan.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#endif

namespace yona::toolchain {

static std::string lowercase_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

static std::filesystem::path canonical_if_exists(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return {};
    auto c = std::filesystem::weakly_canonical(p, ec);
    return ec ? p : c;
}

std::string linker_mode_name(LinkerMode mode) {
    switch (mode) {
        case LinkerMode::Auto: return "auto";
        case LinkerMode::Bundled: return "bundled";
        case LinkerMode::System: return "system";
        case LinkerMode::InProcess: return "inprocess";
    }
    return "auto";
}

bool parse_linker_mode(const std::string& raw, LinkerMode& out_mode) {
    std::string mode = lowercase_copy(raw);
    if (mode == "auto") {
        out_mode = LinkerMode::Auto;
        return true;
    }
    if (mode == "bundled") {
        out_mode = LinkerMode::Bundled;
        return true;
    }
    if (mode == "system") {
        out_mode = LinkerMode::System;
        return true;
    }
    if (mode == "inprocess" || mode == "in-process") {
        out_mode = LinkerMode::InProcess;
        return true;
    }
    return false;
}

std::vector<std::string> bundled_lld_candidate_names() {
#ifdef _WIN32
    return {"lld-link.exe", "ld.lld.exe"};
#else
    return {"ld.lld", "lld"};
#endif
}

std::filesystem::path discover_bundled_lld(const std::vector<std::filesystem::path>& sysroots) {
    const auto names = bundled_lld_candidate_names();
    for (const auto& root : sysroots) {
        for (const auto& name : names) {
            for (const auto& rel : {std::filesystem::path("bin"), std::filesystem::path("llvm") / "bin"}) {
                auto c = canonical_if_exists(root / rel / name);
                if (!c.empty()) return c;
            }
        }
    }
    return {};
}

bool resolve_linker_plan(const std::string& mode_raw,
                         const std::vector<std::filesystem::path>& sysroots,
                         LinkerPlan& out_plan,
                         std::string& error) {
    std::string normalized = mode_raw.empty() ? "auto" : lowercase_copy(mode_raw);

    LinkerMode requested = LinkerMode::Auto;
    if (!parse_linker_mode(normalized, requested)) {
        error = "invalid linker mode '" + mode_raw + "' (expected auto|bundled|system|inprocess)";
        return false;
    }

    out_plan = {};
    out_plan.requested_mode = requested;
    out_plan.bundled_lld_path = discover_bundled_lld(sysroots);

    if (requested == LinkerMode::Bundled) {
        if (out_plan.bundled_lld_path.empty()) {
            error = "requested bundled linker mode but no bundled lld was found under sysroots";
            return false;
        }
        out_plan.use_bundled_lld = true;
        return true;
    }

    if (requested == LinkerMode::System) {
        out_plan.use_bundled_lld = false;
        return true;
    }

    if (requested == LinkerMode::InProcess) {
        out_plan.use_inprocess_lld = true;
        // External fallback policy mirrors auto mode.
        out_plan.use_bundled_lld = !out_plan.bundled_lld_path.empty();
        return true;
    }

    out_plan.use_bundled_lld = !out_plan.bundled_lld_path.empty();
    return true;
}

bool inprocess_lld_available() {
#if defined(YONA_ENABLE_INPROCESS_LLD) && YONA_ENABLE_INPROCESS_LLD
    return true;
#else
    return false;
#endif
}

std::string inprocess_lld_unavailable_reason() {
    if (inprocess_lld_available()) return {};
    return "this build was compiled without embedded LLD support";
}

bool require_inprocess_lld_from_env() {
    const char* e = std::getenv("YONAC_REQUIRE_INPROCESS_LLD");
    if (!e || !*e) return false;
    std::string v = lowercase_copy(std::string(e));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

#ifdef __APPLE__
static std::string macos_popen_trim(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe)
        return {};
    char buf[1024];
    std::string out;
    while (fgets(buf, sizeof(buf), pipe))
        out += buf;
    pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

static std::string macos_sdkroot() {
    if (const char* e = std::getenv("SDKROOT")) {
        if (*e)
            return std::string(e);
    }
    return macos_popen_trim("xcrun --show-sdk-path 2>/dev/null");
}

static std::string macos_sdk_version() {
    std::string v = macos_popen_trim("xcrun --show-sdk-version 2>/dev/null");
    return v.empty() ? "11.0" : v;
}

static std::string macos_deployment_target() {
    if (const char* e = std::getenv("MACOSX_DEPLOYMENT_TARGET")) {
        if (*e)
            return std::string(e);
    }
    return "11.0";
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
static std::string trim_copy(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

static std::string resolve_c_compiler() {
    std::string cc = "clang";
    if (const char* e = std::getenv("YONAC_CC"); e && *e)
        cc = e;
    else if (const char* e = std::getenv("CC"); e && *e)
        cc = e;
    if (llvm::sys::path::is_absolute(cc) && llvm::sys::fs::can_execute(cc))
        return cc;
    if (auto found = llvm::sys::findProgramByName(cc))
        return *found;
    if (auto found = llvm::sys::findProgramByName("clang"))
        return *found;
    if (auto found = llvm::sys::findProgramByName("cc"))
        return *found;
    return {};
}

static std::string run_cc_stdout(const std::vector<std::string>& extra_args) {
    const std::string cc = resolve_c_compiler();
    if (cc.empty())
        return {};
    llvm::SmallString<256> out_path;
    if (llvm::sys::fs::createTemporaryFile("yona-cc", "txt", out_path))
        return {};
    std::vector<llvm::StringRef> argv;
    argv.reserve(extra_args.size() + 1);
    argv.push_back(cc);
    for (const auto& a : extra_args)
        argv.push_back(a);
    llvm::SmallVector<std::optional<llvm::StringRef>, 3> redirects;
    redirects.push_back(llvm::StringRef(""));
    redirects.push_back(llvm::StringRef(out_path));
    redirects.push_back(llvm::StringRef(""));
    std::string err;
    const int rc = llvm::sys::ExecuteAndWait(cc, argv, std::nullopt, redirects, 0, 0, &err);
    std::string out;
    if (auto mb = llvm::MemoryBuffer::getFile(out_path.str()))
        out = trim_copy((*mb)->getBuffer().str());
    llvm::sys::fs::remove(out_path);
    if (rc != 0)
        return {};
    return out;
}

static std::string cc_print_file_name(const std::string& name) {
    const std::string out = run_cc_stdout({"-print-file-name=" + name});
    if (out.empty() || out == name)
        return {};
    std::error_code ec;
    if (!std::filesystem::exists(out, ec))
        return {};
    return out;
}

static void append_colon_dirs(std::vector<std::string>& dirs, std::string_view spec) {
    if (!spec.empty() && spec.front() == '=')
        spec.remove_prefix(1);
    while (!spec.empty()) {
        const auto colon = spec.find(':');
        const std::string_view one = spec.substr(0, colon);
        if (!one.empty()) {
            std::error_code ec;
            if (std::filesystem::is_directory(std::filesystem::path(one), ec))
                dirs.emplace_back(one);
        }
        if (colon == std::string_view::npos)
            break;
        spec.remove_prefix(colon + 1);
    }
}

static std::vector<std::string> cc_library_dirs() {
    std::vector<std::string> dirs;
    const std::string printed = run_cc_stdout({"-print-search-dirs"});
    const auto pos = printed.find("libraries:");
    if (pos != std::string::npos) {
        auto line = std::string_view(printed).substr(pos);
        const auto nl = line.find('\n');
        if (nl != std::string_view::npos)
            line = line.substr(0, nl);
        const auto eq = line.find('=');
        if (eq != std::string_view::npos)
            append_colon_dirs(dirs, line.substr(eq + 1));
    }
    if (const char* lp = std::getenv("LIBRARY_PATH"); lp && *lp)
        append_colon_dirs(dirs, lp);
    return dirs;
}

static std::string linux_dynamic_linker() {
#if defined(__aarch64__)
    const char* soname = "ld-linux-aarch64.so.1";
    const char* fallback = "/lib/ld-linux-aarch64.so.1";
#elif defined(__x86_64__)
    const char* soname = "ld-linux-x86-64.so.2";
    const char* fallback = "/lib64/ld-linux-x86-64.so.2";
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
    const char* soname = "ld-linux-riscv64-lp64d.so.1";
    const char* fallback = "/lib/ld-linux-riscv64-lp64d.so.1";
#else
    const char* soname = nullptr;
    const char* fallback = nullptr;
#endif
    if (soname) {
        std::string p = cc_print_file_name(soname);
        if (!p.empty())
            return p;
    }
    if (fallback) {
        std::error_code ec;
        if (std::filesystem::exists(fallback, ec))
            return fallback;
    }
    return {};
}

struct ElfLldArgs {
    std::vector<std::string> before;
    std::vector<std::string> after;
};

static ElfLldArgs make_elf_lld_args() {
    ElfLldArgs out;
    out.before.push_back("--eh-frame-hdr");
    const std::string interp = linux_dynamic_linker();
    if (!interp.empty()) {
        out.before.push_back("-dynamic-linker");
        out.before.push_back(interp);
    }
    const std::string scrt1 = cc_print_file_name("Scrt1.o");
    const std::string crt1 = cc_print_file_name("crt1.o");
    const std::string crti = cc_print_file_name("crti.o");
    const std::string crtbeginS = cc_print_file_name("crtbeginS.o");
    const std::string crtbegin = cc_print_file_name("crtbegin.o");
    const std::string crtendS = cc_print_file_name("crtendS.o");
    const std::string crtend = cc_print_file_name("crtend.o");
    const std::string crtn = cc_print_file_name("crtn.o");
    std::vector<std::string> crt_end;
    if (!scrt1.empty()) {
        out.before.push_back("-pie");
        out.before.push_back(scrt1);
        if (!crti.empty())
            out.before.push_back(crti);
        if (!crtbeginS.empty())
            out.before.push_back(crtbeginS);
        else if (!crtbegin.empty())
            out.before.push_back(crtbegin);
        if (!crtendS.empty())
            crt_end.push_back(crtendS);
        else if (!crtend.empty())
            crt_end.push_back(crtend);
        if (!crtn.empty())
            crt_end.push_back(crtn);
    } else if (!crt1.empty()) {
        out.before.push_back(crt1);
        if (!crti.empty())
            out.before.push_back(crti);
        if (!crtbegin.empty())
            out.before.push_back(crtbegin);
        if (!crtend.empty())
            crt_end.push_back(crtend);
        if (!crtn.empty())
            crt_end.push_back(crtn);
    }
    for (const auto& d : cc_library_dirs())
        out.after.push_back("-L" + d);
    out.after.push_back("--export-dynamic");
    out.after.push_back("-lm");
    out.after.push_back("-lpthread");
    const std::string libgcc = trim_copy(run_cc_stdout({"-print-libgcc-file-name"}));
    if (!libgcc.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(libgcc, ec))
            out.after.push_back(libgcc);
    }
    const std::string gcc_s = cc_print_file_name("libgcc_s.so.1");
    if (!gcc_s.empty())
        out.after.push_back(gcc_s);
    out.after.push_back("-lc");
    out.after.insert(out.after.end(), crt_end.begin(), crt_end.end());
    return out;
}

static const ElfLldArgs& elf_lld_args() {
    static const ElfLldArgs cached = make_elf_lld_args();
    return cached;
}
#endif

std::vector<std::string> inprocess_lld_before_input_args() {
#ifdef _WIN32
    return {};
#elif defined(__APPLE__)
    std::vector<std::string> args;
#if defined(__aarch64__)
    args.push_back("-arch");
    args.push_back("arm64");
#else
    args.push_back("-arch");
    args.push_back("x86_64");
#endif
    args.push_back("-platform_version");
    args.push_back("macos");
    args.push_back(macos_deployment_target());
    args.push_back(macos_sdk_version());
    const std::string sdk = macos_sdkroot();
    if (!sdk.empty()) {
        args.push_back("-syslibroot");
        args.push_back(sdk);
    }
    return args;
#else
    return elf_lld_args().before;
#endif
}

std::vector<std::string> inprocess_lld_after_input_args() {
#ifdef _WIN32
    return {"/SUBSYSTEM:CONSOLE", "oldnames.lib", "ws2_32.lib", "dbghelp.lib"};
#elif defined(__APPLE__)
    return {"-lSystem", "-U", "_yona_regex_free_code"};
#else
    return elf_lld_args().after;
#endif
}

std::vector<std::string> inprocess_lld_system_args() {
#ifdef _WIN32
    // Clang-as-linker adds oldnames.lib (POSIX open/read/write/close/isatty ->
    // _open/_read/...). Raw lld-link does not, so in-process COFF links fail.
    return inprocess_lld_after_input_args();
#else
    auto args = inprocess_lld_before_input_args();
    const auto after = inprocess_lld_after_input_args();
    args.insert(args.end(), after.begin(), after.end());
    return args;
#endif
}

static std::filesystem::path discover_executable_dir(const char* argv0) {
#ifdef _WIN32
    wchar_t wbuf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        auto c = canonical_if_exists(std::filesystem::path(wbuf).parent_path());
        if (!c.empty())
            return c;
    }
#elif defined(__APPLE__)
    uint32_t size = 1024;
    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        buf.assign(size, '\0');
        if (_NSGetExecutablePath(buf.data(), &size) != 0)
            buf.clear();
    }
    if (!buf.empty()) {
        auto c = canonical_if_exists(std::filesystem::path(buf.data()).parent_path());
        if (!c.empty())
            return c;
    }
#else
    auto exe_file = canonical_if_exists(std::filesystem::path("/proc/self/exe"));
    if (!exe_file.empty()) {
        auto c = canonical_if_exists(exe_file.parent_path());
        if (!c.empty())
            return c;
    }
#endif
    if (argv0 && *argv0) {
        std::filesystem::path p(argv0);
        if (p.has_parent_path()) {
            auto c = canonical_if_exists(p.parent_path());
            if (!c.empty())
                return c;
        }
    }
    return {};
}

std::vector<std::filesystem::path> discover_sysroots(const char* argv0,
                                                     const std::string& sysroot_opt) {
    std::vector<std::filesystem::path> roots;
    std::unordered_set<std::string> seen;

    auto push_unique = [&](const std::filesystem::path& p) {
        auto c = canonical_if_exists(p);
        if (c.empty())
            return;
        if (seen.insert(c.string()).second)
            roots.push_back(c);
    };

    if (!sysroot_opt.empty())
        push_unique(std::filesystem::path(sysroot_opt));
    if (const char* h = std::getenv("YONA_HOME")) {
        if (*h)
            push_unique(std::filesystem::path(h));
    }
    const auto exe = discover_executable_dir(argv0);
    if (!exe.empty()) {
        push_unique(exe);
        auto prefix = exe.parent_path();
        push_unique(prefix);
        // Homebrew / FHS: binaries in PREFIX/bin, sysroot in PREFIX/lib/yona
        push_unique(prefix / "lib" / "yona");
        push_unique(prefix / "lib64" / "yona");
    }
    push_unique(std::filesystem::current_path());
    push_unique(std::filesystem::current_path().parent_path());
    if (const char* bp = std::getenv("HOMEBREW_PREFIX")) {
        if (*bp)
            push_unique(std::filesystem::path(bp) / "lib" / "yona");
    }
    return roots;
}

} // namespace yona::toolchain
