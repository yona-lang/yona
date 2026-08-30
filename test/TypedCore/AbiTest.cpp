/// Producer/consumer tests for the canonical typed-core C adapter and the
/// example non-LLVM backend (`YonaTypedCorePrettyPrint`). Independent of LSP.

#include "yona/TypedCore/Abi.h"

#if defined(LLVM_VERSION_MAJOR) || defined(llvm)
#error "typed-core C ABI must not pull LLVM headers into consumers"
#endif

#include "Support/RepoPaths.h"
#include "Toolchain/YonaLinkUtil.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

const char *lib_path() {
  static const std::string p = yona::test::lib_dir().string();
  return p.c_str();
}

YonaTypedCoreModule *analyze(const char *source,
                             const char *filename = "t.yona") {
  const char *paths[] = {lib_path()};
  return YonaTypedCoreAnalyze(source, filename, paths, 1);
}

const YonaTypedCoreNode *find_kind(const YonaTypedCoreNode *nodes,
                                   uint32_t count, YonaTypedCoreNodeKind kind,
                                   const char *name = nullptr) {
  for (uint32_t i = 0; i < count; ++i) {
    const YonaTypedCoreNode *n = &nodes[i];
    if (n->Kind == kind &&
        (!name || (n->Name && std::string_view(n->Name) == name)))
      return n;
    if (const YonaTypedCoreNode *c =
            find_kind(n->Children, n->ChildCount, kind, name))
      return c;
  }
  return nullptr;
}

const YonaTypedCoreNode *find_kind(const YonaTypedCoreModule *m,
                                   YonaTypedCoreNodeKind kind,
                                   const char *name = nullptr) {
  return m ? find_kind(m->Nodes, m->NodeCount, kind, name) : nullptr;
}

std::string dump(const YonaTypedCoreModule *m) {
  char *s = YonaTypedCorePrettyPrint(m);
  REQUIRE(s != nullptr);
  std::string out(s);
  YonaTypedCoreDisposeString(s);
  return out;
}

std::filesystem::path bin_dir() {
#ifdef YONA_BINARY_DIR
  return std::filesystem::path(YONA_BINARY_DIR);
#else
  return std::filesystem::current_path();
#endif
}

std::string exe_name(const char *stem) {
  return std::string(stem) + yona::test::link::exe_suffix();
}

std::filesystem::path tool(const char *stem) {
  return bin_dir() / exe_name(stem);
}

struct CmdResult {
  int status = -1;
  std::string out;
};

CmdResult runProcess(const std::filesystem::path &Executable,
                     const std::vector<std::string> &Arguments) {
  const auto Result = yona::support::executeProcess(
      Executable, Arguments, {.SuppressStderr = true, .CaptureStdout = true});
  return {.status = Result.ExecutionFailed ? -1 : Result.ExitCode,
          .out = Result.StandardOutput};
}

} // namespace

TEST_SUITE("TypedCoreC") {

  TEST_CASE("analyze null source returns null") {
    CHECK(YonaTypedCoreAnalyze(nullptr, "t.yona", nullptr, 0) == nullptr);
  }

  TEST_CASE(
      "function binding has resolved name, Int type, empty effects, span") {
    YonaTypedCoreModule *m = analyze("let add x y = x + y in add 1 2\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *fn =
        find_kind(m, YonaTypedCoreNodeKindFunction, "add");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->Type != nullptr);
    CHECK(std::string_view(fn->Type).find("->") != std::string_view::npos);
    REQUIRE(fn->Effects != nullptr);
    CHECK(std::string_view(fn->Effects).find('{') != std::string_view::npos);
    REQUIRE(fn->Linearity != nullptr);
    CHECK(std::string_view(fn->Linearity) == "unrestricted");
    const bool has_span =
        fn->SourceRange.End.Line > fn->SourceRange.Start.Line ||
        fn->SourceRange.End.Character > fn->SourceRange.Start.Character;
    CHECK(has_span);
    const YonaTypedCoreNode *x = find_kind(fn->Children, fn->ChildCount,
                                           YonaTypedCoreNodeKindBinding, "x");
    REQUIRE(x != nullptr);
    REQUIRE(x->Type != nullptr);
    CHECK_FALSE(std::string_view(x->Type).empty());
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("generic identity keeps a type variable") {
    YonaTypedCoreModule *m = analyze("let id x = x in id\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *fn =
        find_kind(m, YonaTypedCoreNodeKindFunction, "id");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->Type != nullptr);
    // Principal type is a -> a (pretty-printer may use a, t0, …).
    CHECK(std::string_view(fn->Type).find("->") != std::string_view::npos);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("ADT constructors and type params are exported") {
    YonaTypedCoreModule *m = analyze("module Test\\Box\n"
                                     "type Box a = MkBox a\n"
                                     "box x = MkBox x\n");
    REQUIRE(m != nullptr);
    REQUIRE(m->ModuleName != nullptr);
    CHECK(std::string_view(m->ModuleName).find("Box") !=
          std::string_view::npos);
    const YonaTypedCoreNode *adt =
        find_kind(m, YonaTypedCoreNodeKindAdt, "Box");
    REQUIRE(adt != nullptr);
    REQUIRE(adt->Detail != nullptr);
    CHECK(std::string_view(adt->Detail).find('a') != std::string_view::npos);
    const YonaTypedCoreNode *ctor =
        find_kind(m, YonaTypedCoreNodeKindConstructor, "MkBox");
    REQUIRE(ctor != nullptr);
    const YonaTypedCoreNode *fn =
        find_kind(m, YonaTypedCoreNodeKindFunction, "box");
    REQUIRE(fn != nullptr);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("monomorphic let binding has Int type") {
    YonaTypedCoreModule *m = analyze("let n = 1 + 2 in n\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *n =
        find_kind(m, YonaTypedCoreNodeKindBinding, "n");
    REQUIRE(n != nullptr);
    REQUIRE(n->Type != nullptr);
    CHECK(std::string_view(n->Type).find("Int") != std::string_view::npos);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("case expression exports constructor patterns") {
    YonaTypedCoreModule *m = analyze("case Some 1 of\n"
                                     "  Some x -> x\n"
                                     "  None -> 0\n"
                                     "end\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *cse =
        find_kind(m, YonaTypedCoreNodeKindCase, nullptr);
    REQUIRE(cse != nullptr);
    const YonaTypedCoreNode *some = find_kind(
        cse->Children, cse->ChildCount, YonaTypedCoreNodeKindPattern, "Some");
    REQUIRE(some != nullptr);
    REQUIRE(some->Detail != nullptr);
    CHECK(std::string_view(some->Detail).find('x') != std::string_view::npos);
    const YonaTypedCoreNode *none = find_kind(
        cse->Children, cse->ChildCount, YonaTypedCoreNodeKindPattern, "None");
    REQUIRE(none != nullptr);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("perform exports inferred effect row") {
    YonaTypedCoreModule *m = analyze("let f x = perform Fs.read x in f\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *fn =
        find_kind(m, YonaTypedCoreNodeKindFunction, "f");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->Effects != nullptr);
    CHECK(std::string_view(fn->Effects).find("Fs.read") !=
          std::string_view::npos);
    const YonaTypedCoreNode *eff =
        find_kind(m, YonaTypedCoreNodeKindEffect, "Fs.read");
    REQUIRE(eff != nullptr);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("Linear binding is marked linear") {
    YonaTypedCoreModule *m =
        analyze("let h = Linear 1 in case h of Linear x -> x end\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *h =
        find_kind(m, YonaTypedCoreNodeKindBinding, "h");
    REQUIRE(h != nullptr);
    REQUIRE(h->Linearity != nullptr);
    CHECK(std::string_view(h->Linearity) == "linear");
    REQUIRE(h->Type != nullptr);
    CHECK(std::string_view(h->Type).find("Linear") != std::string_view::npos);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("import from Std\\List records module identity") {
    YonaTypedCoreModule *m = analyze("import foldl from Std\\List in foldl "
                                     "(\\acc x -> acc + x) 0 [1, 2, 3]\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *imp =
        find_kind(m, YonaTypedCoreNodeKindImport, "foldl");
    REQUIRE(imp != nullptr);
    REQUIRE(imp->Module != nullptr);
    CHECK(std::string_view(imp->Module).find("List") != std::string_view::npos);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("try/catch is an explicit unsupported node") {
    YonaTypedCoreModule *m = analyze("try 42 catch _ -> 0 end\n");
    REQUIRE(m != nullptr);
    const YonaTypedCoreNode *u =
        find_kind(m, YonaTypedCoreNodeKindUnsupported, nullptr);
    REQUIRE(u != nullptr);
    REQUIRE(u->Detail != nullptr);
    CHECK(std::string_view(u->Detail).find("try") != std::string_view::npos);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("undefined name produces a diagnostic without LLVM") {
    YonaTypedCoreModule *m = analyze("no_such_name\n");
    REQUIRE(m != nullptr);
    REQUIRE(m->DiagnosticCount > 0);
    REQUIRE(m->Diagnostics[0].Message != nullptr);
    CHECK(std::string(m->Diagnostics[0].Message).size() > 0);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("example backend dump is deterministic and names kinds") {
    YonaTypedCoreModule *m =
        analyze("let add x y = x + y in add 1 2\n", "add.yona");
    REQUIRE(m != nullptr);
    const std::string a = dump(m);
    const std::string b = dump(m);
    CHECK(a == b);
    CHECK(a.starts_with("typed-core\n"));
    CHECK(a.find("file=add.yona") != std::string::npos);
    CHECK(a.find("function add") != std::string::npos);
    CHECK(a.find("->") != std::string::npos);
    CHECK(a.find("effects=") != std::string::npos);
    CHECK(a.find("linearity=") != std::string::npos);
    CHECK(a.find("span=") != std::string::npos);
    YonaTypedCoreDisposeModule(m);
  }

  TEST_CASE("pretty-print of null module is empty") {
    CHECK(YonaTypedCorePrettyPrint(nullptr) == nullptr);
  }

  TEST_CASE("yonac --emit-typed-core dumps typed-core and skips LLVM emit") {
    REQUIRE(std::filesystem::exists(tool("yonac")));
    const auto src = yona::test::link::scratch_root() / "emit_typed_core.yona";
    {
      std::ofstream o(src);
      o << "let add x y = x + y in add 1 2\n";
    }
    CmdResult r = runProcess(tool("yonac"), {"--emit-typed-core", "--sysroot",
                                             bin_dir().string(), src.string()});
    CHECK(r.status == 0);
    CHECK(r.out.starts_with("typed-core\n"));
    CHECK(r.out.find("function add") != std::string::npos);
    CHECK(r.out.find("define ") == std::string::npos);
  }

  TEST_CASE("yonac --emit-typed-core cannot combine with --emit-ir") {
    REQUIRE(std::filesystem::exists(tool("yonac")));
    const auto src =
        yona::test::link::scratch_root() / "emit_typed_core_conflict.yona";
    {
      std::ofstream o(src);
      o << "1 + 2\n";
    }
    CmdResult r = runProcess(tool("yonac"),
                             {"--emit-typed-core", "--emit-ir", src.string()});
    CHECK(r.status != 0);
  }

} // TEST_SUITE
