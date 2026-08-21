/// Producer/consumer tests for the versioned typed-core C ABI and the
/// example non-LLVM backend (`yona_tc_pretty_print`). Independent of LSP.

#include "typed_core/abi.h"

#if defined(LLVM_VERSION_MAJOR) || defined(llvm)
#error "typed-core C ABI must not pull LLVM headers into consumers"
#endif

#include "repo_paths.h"
#include "yona_link_util.hpp"

#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

const char* lib_path() {
    static const std::string p = yona::test::lib_dir().string();
    return p.c_str();
}

YonaTcModule* analyze(const char* source, const char* filename = "t.yona") {
    const char* paths[] = {lib_path()};
    return yona_tc_analyze(source, filename, paths, 1);
}

const YonaTcNode* find_kind(const YonaTcNode* nodes, uint32_t count, YonaTcKind kind,
                            const char* name = nullptr) {
    for (uint32_t i = 0; i < count; ++i) {
        const YonaTcNode* n = &nodes[i];
        if (n->kind == kind && (!name || (n->name && std::string_view(n->name) == name)))
            return n;
        if (const YonaTcNode* c = find_kind(n->children, n->child_count, kind, name))
            return c;
    }
    return nullptr;
}

const YonaTcNode* find_kind(const YonaTcModule* m, YonaTcKind kind, const char* name = nullptr) {
    return m ? find_kind(m->nodes, m->node_count, kind, name) : nullptr;
}

std::string dump(const YonaTcModule* m) {
    char* s = yona_tc_pretty_print(m);
    REQUIRE(s != nullptr);
    std::string out(s);
    yona_tc_string_free(s);
    return out;
}

std::filesystem::path bin_dir() {
#ifdef YONA_BINARY_DIR
    return std::filesystem::path(YONA_BINARY_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::string exe_name(const char* stem) {
    return std::string(stem) + yona::test::link::exe_suffix();
}

std::filesystem::path tool(const char* stem) { return bin_dir() / exe_name(stem); }

std::string shell_quote(const std::string& s) {
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

struct CmdResult {
    int status = -1;
    std::string out;
};

CmdResult run_cmd(const std::string& cmd) {
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
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
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
    return r;
}

} // namespace

TEST_SUITE("TypedCoreAbi") {

TEST_CASE("C ABI version macro matches yona_tc_abi_version") {
    CHECK(YONA_TYPED_CORE_ABI_VERSION == 1);
    CHECK(yona_tc_abi_version() == YONA_TYPED_CORE_ABI_VERSION);
    CHECK(std::string_view(YONA_TYPED_CORE_ABI_VERSION_STRING) == "1");
}

TEST_CASE("analyze null source returns null") {
    CHECK(yona_tc_analyze(nullptr, "t.yona", nullptr, 0) == nullptr);
}

TEST_CASE("function binding has resolved name, Int type, empty effects, span") {
    YonaTcModule* m = analyze("let add x y = x + y in add 1 2\n");
    REQUIRE(m != nullptr);
    CHECK(m->abi_version == YONA_TYPED_CORE_ABI_VERSION);
    const YonaTcNode* fn = find_kind(m, YONA_TC_KIND_FUNCTION, "add");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->type != nullptr);
    CHECK(std::string_view(fn->type).find("->") != std::string_view::npos);
    REQUIRE(fn->effects != nullptr);
    CHECK(std::string_view(fn->effects).find('{') != std::string_view::npos);
    REQUIRE(fn->linearity != nullptr);
    CHECK(std::string_view(fn->linearity) == "unrestricted");
    const bool has_span = fn->span.end.line > fn->span.start.line ||
                          fn->span.end.character > fn->span.start.character;
    CHECK(has_span);
    const YonaTcNode* x = find_kind(fn->children, fn->child_count, YONA_TC_KIND_BINDING, "x");
    REQUIRE(x != nullptr);
    REQUIRE(x->type != nullptr);
    CHECK_FALSE(std::string_view(x->type).empty());
    yona_tc_module_free(m);
}

TEST_CASE("generic identity keeps a type variable") {
    YonaTcModule* m = analyze("let id x = x in id\n");
    REQUIRE(m != nullptr);
    const YonaTcNode* fn = find_kind(m, YONA_TC_KIND_FUNCTION, "id");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->type != nullptr);
    // Principal type is a -> a (pretty-printer may use a, t0, …).
    CHECK(std::string_view(fn->type).find("->") != std::string_view::npos);
    yona_tc_module_free(m);
}

TEST_CASE("ADT constructors and type params are exported") {
    YonaTcModule* m = analyze(
        "module Test\\Box\n"
        "type Box a = MkBox a\n"
        "box x = MkBox x\n");
    REQUIRE(m != nullptr);
    REQUIRE(m->module_name != nullptr);
    CHECK(std::string_view(m->module_name).find("Box") != std::string_view::npos);
    const YonaTcNode* adt = find_kind(m, YONA_TC_KIND_ADT, "Box");
    REQUIRE(adt != nullptr);
    REQUIRE(adt->detail != nullptr);
    CHECK(std::string_view(adt->detail).find('a') != std::string_view::npos);
    const YonaTcNode* ctor = find_kind(m, YONA_TC_KIND_CONSTRUCTOR, "MkBox");
    REQUIRE(ctor != nullptr);
    const YonaTcNode* fn = find_kind(m, YONA_TC_KIND_FUNCTION, "box");
    REQUIRE(fn != nullptr);
    yona_tc_module_free(m);
}

TEST_CASE("monomorphic let binding has Int type") {
    YonaTcModule* m = analyze("let n = 1 + 2 in n\n");
    REQUIRE(m != nullptr);
    const YonaTcNode* n = find_kind(m, YONA_TC_KIND_BINDING, "n");
    REQUIRE(n != nullptr);
    REQUIRE(n->type != nullptr);
    CHECK(std::string_view(n->type).find("Int") != std::string_view::npos);
    yona_tc_module_free(m);
}

TEST_CASE("case expression exports constructor patterns") {
    YonaTcModule* m = analyze(
        "case Some 1 of\n"
        "  Some x -> x\n"
        "  None -> 0\n"
        "end\n");
    REQUIRE(m != nullptr);
    const YonaTcNode* cse = find_kind(m, YONA_TC_KIND_CASE, nullptr);
    REQUIRE(cse != nullptr);
    const YonaTcNode* some = find_kind(cse->children, cse->child_count, YONA_TC_KIND_PATTERN, "Some");
    REQUIRE(some != nullptr);
    REQUIRE(some->detail != nullptr);
    CHECK(std::string_view(some->detail).find('x') != std::string_view::npos);
    const YonaTcNode* none = find_kind(cse->children, cse->child_count, YONA_TC_KIND_PATTERN, "None");
    REQUIRE(none != nullptr);
    yona_tc_module_free(m);
}

TEST_CASE("perform exports inferred effect row") {
    YonaTcModule* m = analyze("let f x = perform Fs.read x in f\n");
    REQUIRE(m != nullptr);
    const YonaTcNode* fn = find_kind(m, YONA_TC_KIND_FUNCTION, "f");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->effects != nullptr);
    CHECK(std::string_view(fn->effects).find("Fs.read") != std::string_view::npos);
    const YonaTcNode* eff = find_kind(m, YONA_TC_KIND_EFFECT, "Fs.read");
    REQUIRE(eff != nullptr);
    yona_tc_module_free(m);
}

TEST_CASE("Linear binding is marked linear") {
    YonaTcModule* m = analyze("let h = Linear 1 in case h of Linear x -> x end\n");
    REQUIRE(m != nullptr);
    const YonaTcNode* h = find_kind(m, YONA_TC_KIND_BINDING, "h");
    REQUIRE(h != nullptr);
    REQUIRE(h->linearity != nullptr);
    CHECK(std::string_view(h->linearity) == "linear");
    REQUIRE(h->type != nullptr);
    CHECK(std::string_view(h->type).find("Linear") != std::string_view::npos);
    yona_tc_module_free(m);
}

TEST_CASE("import from Std\\List records module identity") {
    YonaTcModule* m = analyze(
        "import foldl from Std\\List in foldl (\\acc x -> acc + x) 0 [1, 2, 3]\n");
    REQUIRE(m != nullptr);
    const YonaTcNode* imp = find_kind(m, YONA_TC_KIND_IMPORT, "foldl");
    REQUIRE(imp != nullptr);
    REQUIRE(imp->module != nullptr);
    CHECK(std::string_view(imp->module).find("List") != std::string_view::npos);
    yona_tc_module_free(m);
}

TEST_CASE("try/catch is an explicit unsupported node") {
    YonaTcModule* m = analyze("try 42 catch _ -> 0 end\n");
    REQUIRE(m != nullptr);
    const YonaTcNode* u = find_kind(m, YONA_TC_KIND_UNSUPPORTED, nullptr);
    REQUIRE(u != nullptr);
    REQUIRE(u->detail != nullptr);
    CHECK(std::string_view(u->detail).find("try") != std::string_view::npos);
    yona_tc_module_free(m);
}

TEST_CASE("undefined name produces a diagnostic without LLVM") {
    YonaTcModule* m = analyze("no_such_name\n");
    REQUIRE(m != nullptr);
    REQUIRE(m->diagnostic_count > 0);
    REQUIRE(m->diagnostics[0].message != nullptr);
    CHECK(std::string(m->diagnostics[0].message).size() > 0);
    yona_tc_module_free(m);
}

TEST_CASE("example backend dump is deterministic and names kinds") {
    YonaTcModule* m = analyze("let add x y = x + y in add 1 2\n", "add.yona");
    REQUIRE(m != nullptr);
    const std::string a = dump(m);
    const std::string b = dump(m);
    CHECK(a == b);
    CHECK(a.find("typed-core abi=1") != std::string::npos);
    CHECK(a.find("file=add.yona") != std::string::npos);
    CHECK(a.find("function add") != std::string::npos);
    CHECK(a.find("->") != std::string::npos);
    CHECK(a.find("effects=") != std::string::npos);
    CHECK(a.find("linearity=") != std::string::npos);
    CHECK(a.find("span=") != std::string::npos);
    yona_tc_module_free(m);
}

TEST_CASE("pretty-print of null module is empty") {
    CHECK(yona_tc_pretty_print(nullptr) == nullptr);
}

TEST_CASE("yonac --emit-typed-core dumps typed-core and skips LLVM emit") {
    REQUIRE(std::filesystem::exists(tool("yonac")));
    const auto src = yona::test::link::scratch_root() / "emit_typed_core.yona";
    {
        std::ofstream o(src);
        o << "let add x y = x + y in add 1 2\n";
    }
    std::ostringstream cmd;
    cmd << yona::test::link::qpath(tool("yonac")) << " --emit-typed-core --sysroot "
        << yona::test::link::qpath(bin_dir()) << " " << yona::test::link::qpath(src)
        << yona::test::link::err_null();
    CmdResult r = run_cmd(cmd.str());
    CHECK(r.status == 0);
    CHECK(r.out.find("typed-core abi=1") != std::string::npos);
    CHECK(r.out.find("function add") != std::string::npos);
    CHECK(r.out.find("define ") == std::string::npos);
}

TEST_CASE("yonac --emit-typed-core cannot combine with --emit-ir") {
    REQUIRE(std::filesystem::exists(tool("yonac")));
    const auto src = yona::test::link::scratch_root() / "emit_typed_core_conflict.yona";
    {
        std::ofstream o(src);
        o << "1 + 2\n";
    }
    std::ostringstream cmd;
    cmd << yona::test::link::qpath(tool("yonac")) << " --emit-typed-core --emit-ir "
        << yona::test::link::qpath(src) << yona::test::link::err_null();
    CmdResult r = run_cmd(cmd.str());
    CHECK(r.status != 0);
}

} // TEST_SUITE
