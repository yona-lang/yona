#include "repo_paths.h"
#include "yona_link_util.hpp"
#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

static fs::path bin_dir() {
#ifdef YONA_BINARY_DIR
    return fs::path(YONA_BINARY_DIR);
#else
    return fs::current_path();
#endif
}

static std::string exe_name(const char* stem) {
    return std::string(stem) + yona::test::link::exe_suffix();
}

static fs::path tool(const char* stem) { return bin_dir() / exe_name(stem); }

struct CmdResult {
    int status = -1;
    std::string out;
};

static std::string shell_quote(const std::string& s) {
#ifdef _WIN32
    return yona::test::link::qarg(s);
#else
    std::string o = "'";
    for (char c : s) {
        if (c == '\'')
            o += "'\\''";
        else
            o += c;
    }
    o += "'";
    return o;
#endif
}

static CmdResult run_cmd(const std::string& cmd) {
    CmdResult r;
#ifdef _WIN32
    const std::string line = yona::test::link::wrap_for_cmd_c(cmd);
#else
    const std::string& line = cmd;
#endif
    FILE* pipe = popen(line.c_str(), "r");
    if (!pipe) {
        r.out = "POPEN_ERROR";
        return r;
    }
    std::array<char, 256> buf{};
    while (fgets(buf.data(), (int)buf.size(), pipe) != nullptr)
        r.out += buf.data();
    int st = pclose(pipe);
#ifdef _WIN32
    r.status = st;
#else
    if (WIFEXITED(st))
        r.status = WEXITSTATUS(st);
    else
        r.status = st;
#endif
    while (!r.out.empty() && (r.out.back() == '\n' || r.out.back() == '\r'))
        r.out.pop_back();
    return r;
}

static CmdResult run_yona(const std::vector<std::string>& args, const std::string& stdin_text = "") {
    std::ostringstream cmd;
#ifndef _WIN32
    if (!stdin_text.empty())
        cmd << "printf '%s' " << shell_quote(stdin_text) << " | ";
#endif
    cmd << yona::test::link::qpath(tool("yona"));
    for (const auto& a : args)
        cmd << " " << shell_quote(a);
#ifdef _WIN32
    if (!stdin_text.empty()) {
        fs::path in = yona::test::link::scratch_root() / "yona_script_stdin.txt";
        {
            std::ofstream o(in, std::ios::binary);
            o << stdin_text;
        }
        cmd << " < " << yona::test::link::qpath(in);
    }
#endif
    cmd << yona::test::link::err_null();
    return run_cmd(cmd.str());
}

static fs::path write_temp_yona(const std::string& stem, const std::string& body) {
    fs::path p = yona::test::link::scratch_root() / (stem + ".yona");
    std::ofstream o(p);
    o << body;
    return p;
}

TEST_CASE("wrap_for_cmd_c preserves quoted argv for MSVC popen") {
    using yona::test::link::wrap_for_cmd_c;
    CHECK(wrap_for_cmd_c("\"D:/yona.exe\" --version 2>nul") ==
          "\"\"D:/yona.exe\" --version\" 2>nul");
    CHECK(wrap_for_cmd_c("\"D:/yona.exe\" \"file.yona\" 2>nul") ==
          "\"\"D:/yona.exe\" \"file.yona\"\" 2>nul");
    CHECK(wrap_for_cmd_c("\"D:/yonac.exe\" --sysroot \"D:/build\" - -o \"D:/out.exe\" < \"D:/in.yona\" 2>nul") ==
          "\"\"D:/yonac.exe\" --sysroot \"D:/build\" - -o \"D:/out.exe\" < \"D:/in.yona\"\" 2>nul");
    CHECK(wrap_for_cmd_c("D:/yona.exe -e \"1 + 2\" 2>nul") == "D:/yona.exe -e \"1 + 2\" 2>nul");
}

TEST_CASE("qarg quotes cmd argv without filesystem-normalizing backslashes") {
    using yona::test::link::qarg;
    const std::string expr = "import getArgs from Std\\Process in getArgs";
    CHECK(qarg(expr) == "\"" + expr + "\"");
    CHECK(qarg("1 + 2") == "\"1 + 2\"");
    CHECK(qarg("say \"hi\"") == "\"say \"\"hi\"\"\"");
#ifdef _WIN32
    CHECK(qarg(expr) != yona::test::link::qpath(expr));
#endif
}

TEST_CASE("yona runner is built") {
    REQUIRE(fs::exists(tool("yona")));
    REQUIRE(fs::exists(tool("yonac")));
    REQUIRE(fs::exists(tool("yona-repl")));
}

TEST_CASE("yona runs a file") {
    auto src = write_temp_yona("script_literal", "1 + 2\n");
    auto r = run_yona({src.string()});
    CHECK(r.status == 0);
    CHECK(r.out == "3");
}

#ifndef _WIN32
TEST_CASE("yona shebang script is executable") {
    auto src = write_temp_yona("script_shebang", "#!/usr/bin/env yona\n1 + 2\n");
    fs::permissions(src, fs::perms::owner_exec, fs::perm_options::add);
    auto r = run_yona({src.string()});
    CHECK(r.status == 0);
    CHECK(r.out == "3");
}
#endif

TEST_CASE("yona getArgs uses the script path") {
    auto src = write_temp_yona("script_args",
                               "import getArgs from Std\\Process, tail from Std\\List in\n"
                               "tail getArgs\n");
    auto r = run_yona({src.string(), "foo", "bar"});
    CHECK(r.status == 0);
    CHECK(r.out == "[foo, bar]");
}

TEST_CASE("yona compiles piped stdin") {
    auto r = run_yona({}, "1 + 2\n");
    CHECK(r.status == 0);
    CHECK(r.out == "3");
}

TEST_CASE("yona -e compiles and runs an expression") {
    auto r = run_yona({"-e", "1 + 2"});
    CHECK(r.status == 0);
    CHECK(r.out == "3");
}

TEST_CASE("yona -e getArgs argv0 is -e") {
    auto r = run_yona({"-e", "import getArgs from Std\\Process in getArgs", "foo"});
    CHECK(r.status == 0);
    CHECK(r.out == "[-e, foo]");
}

TEST_CASE("yona rejects a module file") {
    auto src = write_temp_yona("script_module", "module Foo\nexport x\nx = 1\n");
    auto r = run_yona({src.string()});
    CHECK(r.status != 0);
}

TEST_CASE("yona missing file is non-zero") {
    auto r = run_yona({(yona::test::link::scratch_root() / "no_such_script.yona").string()});
    CHECK(r.status != 0);
}

TEST_CASE("yona unknown flag is non-zero") {
    auto r = run_yona({"--not-a-real-flag"});
    CHECK(r.status == 2);
}

TEST_CASE("yona --version matches yonac --version") {
    auto yona_v = run_cmd(yona::test::link::qpath(tool("yona")) + " --version" + yona::test::link::err_null());
    auto yonac_v = run_cmd(yona::test::link::qpath(tool("yonac")) + " --version" + yona::test::link::err_null());
    CHECK(yona_v.status == 0);
    CHECK(yonac_v.status == 0);
    CHECK(yona_v.out == yonac_v.out);
    CHECK(!yona_v.out.empty());
}

TEST_CASE("yonac - reads stdin") {
    std::ostringstream cmd;
    fs::path out = yona::test::link::scratch_root() / ("yonac_stdin" + yona::test::link::exe_suffix());
#ifndef _WIN32
    cmd << "printf '%s' '1 + 2' | ";
    cmd << yona::test::link::qpath(tool("yonac")) << " --sysroot " << yona::test::link::qpath(bin_dir())
        << " - -o " << yona::test::link::qpath(out);
#else
    fs::path in = yona::test::link::scratch_root() / "yonac_stdin_src.yona";
    {
        std::ofstream o(in, std::ios::binary);
        o << "1 + 2\n";
    }
    cmd << yona::test::link::qpath(tool("yonac")) << " --sysroot " << yona::test::link::qpath(bin_dir())
        << " - -o " << yona::test::link::qpath(out) << " < " << yona::test::link::qpath(in);
#endif
    cmd << yona::test::link::err_null();
    auto compile = run_cmd(cmd.str());
    CHECK(compile.status == 0);
    REQUIRE(fs::exists(out));
    auto run = run_cmd(yona::test::link::qpath(out) + yona::test::link::err_null());
    CHECK(run.status == 0);
    CHECK(run.out == "3");
}

TEST_CASE("yonac -e is rejected") {
    auto r = run_cmd(yona::test::link::qpath(tool("yonac")) + " -e \"1 + 2\"" + yona::test::link::err_null());
    CHECK(r.status != 0);
}
