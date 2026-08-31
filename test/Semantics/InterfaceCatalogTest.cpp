#include "Support/RepoPaths.h"
#include "yona/Semantics/GenericFunctionSource.h"
#include "yona/Semantics/InterfaceCatalog.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("Semantics interface catalog installs Prelude without Codegen") {
  const auto LibraryPath = yona::test::repo_root() / "lib";
  yona::semantics::InterfaceCatalog Catalog({LibraryPath.string()});
  yona::parser::Parser Parser;
  yona::compiler::DiagnosticEngine Diagnostics;
  yona::compiler::typechecker::TypeChecker Checker(Diagnostics);

  Checker.add_module_path(LibraryPath.string());
  Checker.set_import_type_source(&Catalog);
  const auto Installed = Catalog.installPrelude(Parser, Checker);
  REQUIRE(Installed.has_value());
  REQUIRE(*Installed);

  const auto Identity = Catalog.imported_function_sig("Prelude", "identity");
  REQUIRE(Identity.has_value());
  REQUIRE(Identity->param_descriptors.size() == 1);
  CHECK(Identity->param_descriptors.front() == "VAR(a)");
  CHECK(Identity->return_descriptor == "VAR(a)");

  const auto Some = Catalog.imported_function_sig("Prelude", "Some");
  REQUIRE(Some.has_value());
  REQUIRE(Some->param_descriptors.size() == 1);
  CHECK(Some->param_descriptors.front() == "VAR(a)");
  CHECK(Some->return_descriptor == "ADT(Option,VAR(a))");

  auto Result = Parser.parseExpression(
      "import identity from Prelude in identity 42", "catalog.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());
  Diagnostics.setSources(Parsed.Sources);
  Checker.check(Parsed.Expression.get());
  CHECK(Checker.solve_constraints());
  CHECK_FALSE(Checker.has_errors());
}

TEST_CASE("Std Gpu preserves typed float channel helper contracts") {
  const auto LibraryPath = yona::test::repo_root() / "lib";
  yona::semantics::InterfaceCatalog Catalog({LibraryPath.string()});
  const auto Loaded = Catalog.loadModule("Std\\Gpu");
  REQUIRE(Loaded.has_value());
  REQUIRE(*Loaded != nullptr);

  const auto *Drain =
      yona::interface::findFunction(**Loaded, "drainMapFloatGpu");
  REQUIRE(Drain != nullptr);
  CHECK(Drain->ParameterTypes ==
        std::vector<std::string>{"ADT(FloatMapOp)", "ADT(Receiver,FLOAT_ARRAY)",
                                 "ADT(Sender,FLOAT_ARRAY)"});
  CHECK(Drain->ReturnType == "INT");
}

TEST_CASE("Std File exposes canonical typed resource contracts") {
  const auto LibraryPath = yona::test::repo_root() / "lib";
  yona::semantics::InterfaceCatalog Catalog({LibraryPath.string()});
  const auto Loaded = Catalog.loadModule("Std\\File");
  REQUIRE(Loaded.has_value());
  REQUIRE(*Loaded != nullptr);

  const auto &Functions = (*Loaded)->Functions;
  const auto Find =
      [&](const std::string &Name) -> const yona::interface::Function & {
    const auto Found =
        std::find_if(Functions.begin(), Functions.end(),
                     [&](const auto &Fn) { return Fn.Name == Name; });
    REQUIRE(Found != Functions.end());
    return *Found;
  };

  const auto CheckFunction =
      [&](const std::string &Name, std::vector<std::string> Parameters,
          const std::string &ReturnType, std::vector<bool> BorrowedParameters) {
        const auto &Function = Find(Name);
        CHECK(Function.ParameterTypes == Parameters);
        CHECK(Function.ReturnType == ReturnType);
        CHECK(Function.BorrowedParameters == BorrowedParameters);
      };

  CheckFunction("appendFile", {"STRING", "STRING"}, "BOOL", {true, true});
  CheckFunction("closeFileHandle", {"ADT(FileHandle)"}, "UNIT", {false});
  CheckFunction("exists", {"STRING"}, "BOOL", {true});
  CheckFunction("flush", {"ADT(FileHandle)"}, "BOOL", {true});
  CheckFunction("listDir", {"STRING"}, "Seq(STRING)", {true});
  CheckFunction("openFile", {"STRING", "ADT(FileMode)"},
                "LINEAR(ADT(FileHandle))", {true, true});
  CheckFunction("readBytes", {"ADT(FileHandle)", "INT"}, "BYTE_ARRAY",
                {true, false});
  CheckFunction("readChunks", {"ADT(FileHandle)", "INT"},
                "ADT(Iterator,BYTE_ARRAY)", {true, false});
  CheckFunction("readExact", {"ADT(FileHandle)", "INT"},
                "ADT(Result,STRING,STRING)", {true, false});
  CheckFunction("readExactBytes", {"ADT(FileHandle)", "INT"}, "STRING",
                {true, false});
  CheckFunction("readFile", {"STRING"}, "STRING", {true});
  CheckFunction("readFileBytes", {"STRING"}, "BYTE_ARRAY", {true});
  CheckFunction("readLines", {"STRING"}, "ADT(Iterator,STRING)", {true});
  CheckFunction("remove", {"STRING"}, "BOOL", {true});
  CheckFunction("seek", {"ADT(FileHandle)", "INT", "ADT(Whence)"}, "INT",
                {true, false, true});
  CheckFunction("size", {"STRING"}, "INT", {true});
  CheckFunction("tell", {"ADT(FileHandle)"}, "INT", {true});
  CheckFunction("truncate", {"ADT(FileHandle)", "INT"}, "BOOL", {true, false});
  CheckFunction("writeBytes", {"ADT(FileHandle)", "BYTE_ARRAY"}, "INT",
                {true, true});
  CheckFunction("writeFile", {"STRING", "STRING"}, "BOOL", {true, true});
  CheckFunction("writeFileBytes", {"STRING", "BYTE_ARRAY"}, "BOOL",
                {true, true});
}

TEST_CASE("Std File requires one explicit Linear handle unwrap") {
  const auto LibraryPath = yona::test::repo_root() / "lib";

  const auto CheckSource = [&](const std::string &Source) {
    yona::semantics::InterfaceCatalog Catalog({LibraryPath.string()});
    yona::parser::Parser Parser;
    yona::compiler::DiagnosticEngine Diagnostics;
    yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
    Checker.add_module_path(LibraryPath.string());
    Checker.set_import_type_source(&Catalog);
    const auto Installed = Catalog.installPrelude(Parser, Checker);
    REQUIRE(Installed.has_value());
    REQUIRE(*Installed);

    auto Result = Parser.parseExpression(Source, "file_handle_contract.yona");
    REQUIRE(Result.has_value());
    auto Parsed = std::move(Result.value());
    Diagnostics.setSources(Parsed.Sources);
    Checker.check(Parsed.Expression.get());
    Checker.solve_constraints();
    return Diagnostics.error_count();
  };

  CHECK(CheckSource(R"(
import openFile, tell from Std\File in
tell (openFile "file.bin" Read)
)") > 0);

  CHECK(CheckSource(R"(
import openFile, tell, closeFileHandle from Std\File in
case openFile "file.bin" Read of
  Linear h -> do
    n = tell h
    closeFileHandle h
    n
  end
end
)") == 0);
}

TEST_CASE("Std File and Std Io exact reads reject crossed resource types") {
  const auto LibraryPath = yona::test::repo_root() / "lib";

  const auto CheckSource = [&](const std::string &Source) {
    yona::semantics::InterfaceCatalog Catalog({LibraryPath.string()});
    yona::parser::Parser Parser;
    yona::compiler::DiagnosticEngine Diagnostics;
    yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
    Checker.add_module_path(LibraryPath.string());
    Checker.set_import_type_source(&Catalog);
    const auto Installed = Catalog.installPrelude(Parser, Checker);
    REQUIRE(Installed.has_value());
    REQUIRE(*Installed);

    auto Result = Parser.parseExpression(Source, "exact_read_contract.yona");
    REQUIRE(Result.has_value());
    auto Parsed = std::move(Result.value());
    Diagnostics.setSources(Parsed.Sources);
    Checker.check(Parsed.Expression.get());
    Checker.solve_constraints();
    return Diagnostics.error_count();
  };

  CHECK(CheckSource(R"(
import stdinFd, readExact from Std\Io, isOk from Std\Result in
isOk (readExact stdinFd 0)
)") == 0);

  CHECK(CheckSource(R"(
import openFile, closeFileHandle, readExactBytes from Std\File in
case openFile "file.bin" Read of
  Linear h -> do
    bytes = readExactBytes h 0
    closeFileHandle h
    bytes
  end
end
)") == 0);

  CHECK(CheckSource(R"(
import stdinFd from Std\Io, readExactBytes from Std\File in
readExactBytes stdinFd 0
)") > 0);

  CHECK(CheckSource(R"(
import openFile, closeFileHandle from Std\File,
       readExactBytes from Std\Io in
case openFile "file.bin" Read of
  Linear h -> do
    bytes = readExactBytes h 0
    closeFileHandle h
    bytes
  end
end
)") > 0);
}

TEST_CASE("Semantics generic source service retains GENFN source ownership") {
  yona::semantics::GenericFunctionSourceService Service;
  const std::vector<yona::semantics::GenericConstructorMetadata> Constructors{
      {"Some", "Option", 0, 1, {"value"}}, {"None", "Option", 1, 0, {}}};

  auto Parsed = Service.parseGenericModule(
      "identityOption",
      "identityOption value = case value of\nSome x -> x\nNone -> 0 end",
      Constructors);
  REQUIRE(Parsed.has_value());
  REQUIRE(Parsed->Sources);
  REQUIRE(Parsed->Module);
  REQUIRE(Parsed->Module->functions.size() == 1);
  const auto *Function = Parsed->Module->functions.front();
  CHECK(Function->name == "identityOption");

  const auto FunctionRange = Function->Range;
  REQUIRE(FunctionRange.isValid());
  const auto SourceText = Parsed->Sources->text(FunctionRange.Source);
  REQUIRE(FunctionRange.Offset + FunctionRange.Length <= SourceText.size());
  CHECK(Parsed->Sources->name(FunctionRange.Source) == "<imported>");
  CHECK(SourceText.substr(FunctionRange.Offset, FunctionRange.Length) ==
        "identityOption");
}
