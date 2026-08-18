#include "LinkerPlan.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
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
static std::string macos_sdkroot() {
    if (const char* e = std::getenv("SDKROOT")) {
        if (*e)
            return std::string(e);
    }
    FILE* pipe = popen("xcrun --show-sdk-path 2>/dev/null", "r");
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
#endif

std::vector<std::string> inprocess_lld_system_args() {
#ifdef _WIN32
    // Clang-as-linker adds oldnames.lib (POSIX open/read/write/close/isatty ->
    // _open/_read/...). Raw lld-link does not, so in-process COFF links fail.
    return {"/SUBSYSTEM:CONSOLE", "oldnames.lib", "ws2_32.lib", "dbghelp.lib"};
#elif defined(__APPLE__)
    std::vector<std::string> args;
    const std::string sdk = macos_sdkroot();
    if (!sdk.empty()) {
        args.push_back("-syslibroot");
        args.push_back(sdk);
    }
#if defined(__aarch64__)
    args.push_back("-arch");
    args.push_back("arm64");
#else
    args.push_back("-arch");
    args.push_back("x86_64");
#endif
    args.push_back("-lSystem");
    args.push_back("-U");
    args.push_back("_yona_regex_free_code");
    return args;
#else
    return {"-lm", "-lpthread", "-rdynamic"};
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
