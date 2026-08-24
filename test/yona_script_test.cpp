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
                               "import getArgs from Std\\Process in\n"
                               "case getArgs of [_|t] -> t; [] -> [] end\n");
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

static CmdResult run_yonac_ir(const fs::path& src, const std::vector<std::string>& extra = {}) {
    std::ostringstream cmd;
    cmd << yona::test::link::qpath(tool("yonac")) << " --sysroot "
        << yona::test::link::qpath(bin_dir()) << " -I "
        << yona::test::link::qpath(yona::test::lib_dir()) << " --emit-ir";
    for (const auto& a : extra)
        cmd << " " << shell_quote(a);
    cmd << " " << yona::test::link::qpath(src) << " 2>&1";
    return run_cmd(cmd.str());
}

TEST_CASE("yonac fails E0500 on unproven head") {
    auto src = write_temp_yona(
        "e0500_head",
        "import head from Std\\List in let f x = x in let xs = f [1, 2] in head xs\n");
    auto r = run_yonac_ir(src);
    CHECK(r.status != 0);
    CHECK(r.out.find("E0500") != std::string::npos);
}

TEST_CASE("yonac --Wno-refinement allows unproven head") {
    auto src = write_temp_yona(
        "e0500_head_allow",
        "import head from Std\\List in let f x = x in let xs = f [1, 2] in head xs\n");
    auto r = run_yonac_ir(src, {"--Wno-refinement"});
    CHECK(r.status == 0);
}

TEST_CASE("yonac fails E0600 on use-after-consume") {
    auto src = write_temp_yona(
        "e0600_uac",
        "let makeHandle x = Linear x, conn = makeHandle 0, conn2 = conn, conn3 = conn in conn3\n");
    auto r = run_yonac_ir(src);
    CHECK(r.status != 0);
    CHECK(r.out.find("E0600") != std::string::npos);
}

TEST_CASE("yonac fails E0600 in a module function") {
    auto src = write_temp_yona(
        "e0600_mod",
        "module Test\\LinFail\n\nexport bad\n\n"
        "bad x =\n"
        "  let makeHandle y = Linear y, conn = makeHandle x, conn2 = conn, conn3 = conn in conn3\n");
    auto r = run_yonac_ir(src);
    CHECK(r.status != 0);
    CHECK(r.out.find("E0600") != std::string::npos);
}

TEST_CASE("yonac leak warning is E0602 not unhandled-effect") {
    auto src = write_temp_yona("e0602_leak", "let conn = Linear 0 in 42\n");
    auto r = run_yonac_ir(src);
    CHECK(r.status == 0);
    CHECK(r.out.find("E0602") != std::string::npos);
    CHECK(r.out.find("unhandled-effect") == std::string::npos);
}

TEST_CASE("yonac --Wno-linear permits a linear use-after-consume") {
    auto src = write_temp_yona(
        "e0600_uac_allow",
        "let makeHandle x = Linear x, conn = makeHandle 0, conn2 = conn, conn3 = conn in conn3\n");
    auto r = run_yonac_ir(src, {"--Wno-linear"});
    CHECK(r.status == 0);
    CHECK(r.out.find("E0600") == std::string::npos);
}

TEST_CASE("yonac --Wincomplete-patterns warns without failing") {
    auto src = write_temp_yona("incomplete_patterns", "case Some 1 of Some x -> x end\n");
    auto r = run_yonac_ir(src, {"--Wincomplete-patterns"});
    CHECK(r.status == 0);
    CHECK(r.out.find("Wincomplete-patterns") != std::string::npos);
    CHECK(r.out.find("None") != std::string::npos);
}

TEST_CASE("yonac --Werror promotes incomplete pattern warnings") {
    auto src = write_temp_yona("incomplete_patterns_werror", "case Some 1 of Some x -> x end\n");
    auto r = run_yonac_ir(src, {"--Werror", "--Wincomplete-patterns"});
    CHECK(r.status != 0);
    CHECK(r.out.find("Wincomplete-patterns") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free accepts a pure expression") {
    auto src = write_temp_yona("effect_free_pure", "let add x y = x + y in add 20 22\n");
    auto r = run_yonac_ir(src, {"--require-effect-free"});
    CHECK(r.status == 0);
    CHECK(r.out.find("E0203") == std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects incomplete finite ADT cases") {
    auto src = write_temp_yona("effect_free_incomplete_case", "case Some 1 of Some x -> x end\n");
    auto r = run_yonac_ir(src, {"--require-effect-free"});
    CHECK(r.status != 0);
    CHECK(r.out.find("E0203") != std::string::npos);
    CHECK(r.out.find("None") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free accepts wildcard finite ADT cases") {
    auto src = write_temp_yona("effect_free_wildcard_case",
                               "case Some 1 of Some x -> x; _ -> 0 end\n");
    auto r = run_yonac_ir(src, {"--require-effect-free"});
    CHECK(r.status == 0);
    CHECK(r.out.find("E0203") == std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects guarded finite ADT cases") {
    auto src = write_temp_yona("effect_free_guarded_case",
                               "case Some 1 of Some x if x > 0 -> x end\n");
    auto r = run_yonac_ir(src, {"--require-effect-free"});
    CHECK(r.status != 0);
    CHECK(r.out.find("E0203") != std::string::npos);
    CHECK(r.out.find("None") != std::string::npos);
    CHECK(r.out.find("Some") != std::string::npos);
}

TEST_CASE("yonac warns for overlapping patterns and rejects incomplete Bool in strict mode") {
    auto overlap = write_temp_yona("overlapping_patterns",
                                   "case true of _ -> 1; true -> 2 end\n");
    auto warning = run_yonac_ir(overlap, {"--Woverlapping-patterns"});
    CHECK(warning.status == 0);
    CHECK(warning.out.find("unreachable pattern") != std::string::npos);
    auto werror = run_yonac_ir(overlap, {"--Woverlapping-patterns", "--Werror"});
    CHECK(werror.status != 0);

    auto incomplete = write_temp_yona("incomplete_bool", "case true of true -> 1 end\n");
    auto strict = run_yonac_ir(incomplete, {"--require-effect-free"});
    CHECK(strict.status != 0);
    CHECK(strict.out.find("E0203") != std::string::npos);
    CHECK(strict.out.find("False") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects incomplete module finite ADT cases") {
    auto src = write_temp_yona("effect_free_module_incomplete_case",
        "module Test\\StrictCase\n"
        "export type Choice\n"
        "export choose\n"
        "type Choice = First Int | Second Int\n"
        "choose value = case value of First x -> x end\n");
    auto r = run_yonac_ir(src, {"--require-effect-free"});
    CHECK(r.status != 0);
    CHECK(r.out.find("E0203") != std::string::npos);
    CHECK(r.out.find("Second") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free requires structural recursion") {
    auto structural = write_temp_yona("effect_free_structural_recursion",
        "module Test\\StructuralRecursion\n"
        "export sum\n"
        "type Nat = Zero | Succ Nat\n"
        "sum n = case n of Zero -> 0; Succ rest -> let next = rest in sum next end\n");
    auto structural_result = run_yonac_ir(structural, {"--require-effect-free"});
    CHECK(structural_result.status == 0);

    auto direct = write_temp_yona("effect_free_direct_recursion",
        "module Test\\DirectRecursion\n"
        "export loop\n"
        "loop n = loop n\n");
    auto direct_result = run_yonac_ir(direct, {"--require-effect-free"});
    CHECK(direct_result.status != 0);
    CHECK(direct_result.out.find("E0203") != std::string::npos);

    auto mutual = write_temp_yona("effect_free_mutual_recursion",
        "module Test\\MutualRecursion\n"
        "export even\n"
        "even n = odd n\n"
        "odd n = even n\n");
    auto mutual_result = run_yonac_ir(mutual, {"--require-effect-free"});
    CHECK(mutual_result.status != 0);
    CHECK(mutual_result.out.find("E0203") != std::string::npos);
    CHECK(mutual_result.out.find("mutual recursion") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects imported functions without effect rows") {
    fs::path modules = yona::test::link::scratch_root() / "unknown_effect_rows";
    fs::create_directories(modules / "Test");
    {
        std::ofstream iface(modules / "Test" / "Unknown.yonai");
        iface << "FN yona_Test_Unknown__f 0 -> INT\n";
    }
    auto src = write_temp_yona("effect_free_unknown_import", "import f from Test\\Unknown in f\n");
    auto r = run_yonac_ir(src, {"--require-effect-free", "-I", modules.string()});
    CHECK(r.status != 0);
    CHECK(r.out.find("E0203") != std::string::npos);
}
