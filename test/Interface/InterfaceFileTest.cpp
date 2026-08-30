#include "Support/RepoPaths.h"
#include "yona/Interface/Reader.h"
#include "yona/Interface/Writer.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view CanonicalInput = R"(MODULE Std\Example
FN map 2 FUNCTION(VAR(a),VAR(b)) SEQ(VAR(a)) -> SEQ(VAR(b)) effects | hof
ADT Maybe 2 1 params a
  CTOR Some 1 1 fields value:VAR(a)
  CTOR None 0 0
TRAIT Equal a 1
  METHOD equals FUNCTION(VAR(a),VAR(a),BOOL)
INSTANCE Equal ADT(Maybe,VAR(a))
  PARAM a
  CONSTRAINT Equal a
  IMPL equals Std\Example equalsMaybe
FN equalsMaybe 2 ADT(Maybe,VAR(a)) ADT(Maybe,VAR(a)) -> BOOL effects -
IO fetch 1 STRING -> STRING borrow 1 effects Net.connect,Fs.read|
GENFN_DEP map itemHash NATIVE YonaRuntimeHash FN 1 VAR(a) -> INT effects -
GENFN_DEP map fetchValue YONA Std\Example fetch IO 1 STRING -> STRING effects Net.connect
GENFN_CTOR map Some Maybe 1 1 2 1 fields value:VAR(a)
GENFN_BEGIN map map
map f values = values
GENFN_END
)";

} // namespace

TEST_CASE("canonical interface round-trips deterministically") {
  auto Parsed = yona::interface::parseModule(CanonicalInput);
  REQUIRE(Parsed.has_value());
  CHECK(Parsed->Identity.fqn() == "Std\\Example");

  const auto *Fetch = yona::interface::findFunction(*Parsed, "fetch");
  REQUIRE(Fetch != nullptr);
  CHECK(Fetch->exportName(Parsed->Identity) == "YonaStdExampleFetch");
  CHECK(Fetch->Effects.IsKnown);
  CHECK(Fetch->Effects.IsOpen);

  REQUIRE(Parsed->Instances.size() == 1);
  REQUIRE(Parsed->Instances.front().Implementations.size() == 1);
  CHECK(Parsed->Instances.front().Implementations.front().Target.exportName() ==
        "YonaStdExampleEqualsMaybe");

  REQUIRE(Parsed->GenericFunctions.size() == 1);
  REQUIRE(Parsed->GenericFunctions.front().Dependencies.size() == 2);

  auto Serialized = yona::interface::serializeModule(*Parsed);
  REQUIRE(Serialized.has_value());
  CHECK(Serialized->starts_with("MODULE Std\\Example\n"));
  CHECK(Serialized->find("IO fetch 1 STRING -> STRING") != std::string::npos);
  CHECK(Serialized->find("FN YonaStdExample") == std::string::npos);
  CHECK(Serialized->find("effects Fs.read,Net.connect|") != std::string::npos);

  auto Reparsed = yona::interface::parseModule(*Serialized);
  REQUIRE(Reparsed.has_value());
  auto Reserialized = yona::interface::serializeModule(*Reparsed);
  REQUIRE(Reserialized.has_value());
  CHECK(*Reserialized == *Serialized);
}

TEST_CASE("canonical interface rejects malformed and ambiguous input") {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"missing module", "FN fetch 0 -> STRING effects -\n"},
      {"module is not first", "FN fetch 0 -> STRING\nMODULE Std\\Example\n"},
      {"duplicate module", "MODULE Std\\Example\nMODULE Std\\Example\n"},
      {"noncanonical identity", "MODULE Std/Example\n"},
      {"generated export used as key",
       "MODULE Std\\Example\nFN YonaStdExampleFetch 0 -> STRING\n"},
      {"arity mismatch", "MODULE Std\\Example\nFN fetch 2 STRING -> STRING\n"},
      {"borrow mask mismatch",
       "MODULE Std\\Example\nFN fetch 1 STRING -> STRING borrow 00\n"},
      {"duplicate effect operation",
       "MODULE Std\\Example\nFN fetch 0 -> STRING effects Fs.read,Fs.read\n"},
      {"generated implementation target",
       "MODULE Std\\Example\nINSTANCE Equal STRING\n"
       "  IMPL equals YonaStdExampleEquals\n"},
      {"constructor count mismatch",
       "MODULE Std\\Example\nADT Maybe 2 0\nCTOR None 0 0\n"},
      {"generic metadata without source",
       "MODULE Std\\Example\n"
       "GENFN_DEP map hash NATIVE YonaRuntimeHash FN 1 INT -> INT\n"},
      {"unterminated generic source",
       "MODULE Std\\Example\nGENFN_BEGIN map map\nmap x = x\n"},
      {"unknown record", "MODULE Std\\Example\nFORMAT 1\n"},
  };

  for (const auto &[Name, Input] : Cases) {
    CAPTURE(Name);
    const auto Parsed = yona::interface::parseModule(Input);
    CHECK_FALSE(Parsed.has_value());
    REQUIRE_FALSE(Parsed.error().empty());
  }
}

TEST_CASE("interface search validates the requested module identity") {
  const std::filesystem::path Root =
      yona::test::repo_root() / "test" / "_scratch" / "interface-reader";
  const std::filesystem::path CorrectPath = Root / "Std" / "Example.yonai";
  std::filesystem::create_directories(CorrectPath.parent_path());

  auto Parsed = yona::interface::parseModule(CanonicalInput);
  REQUIRE(Parsed.has_value());
  REQUIRE(yona::interface::writeModule(CorrectPath, *Parsed).has_value());

  const std::vector<std::string> Roots = {Root.string()};
  const yona::model::ModuleIdentity Expected("Std\\Example");
  auto Found = yona::interface::readModuleFromSearchPaths(Roots, Expected);
  REQUIRE(Found.has_value());
  REQUIRE(Found->has_value());
  CHECK((*Found)->Identity.fqn() == Expected.fqn());

  const yona::model::ModuleIdentity Missing("Std\\Missing");
  auto NotFound = yona::interface::readModuleFromSearchPaths(Roots, Missing);
  REQUIRE(NotFound.has_value());
  CHECK_FALSE(NotFound->has_value());
}

TEST_CASE("packaged interfaces use the canonical deterministic schema") {
  const std::filesystem::path Library = yona::test::repo_root() / "lib";
  for (const auto &Entry :
       std::filesystem::recursive_directory_iterator(Library)) {
    if (!Entry.is_regular_file() || Entry.path().extension() != ".yonai")
      continue;

    CAPTURE(Entry.path().string());
    auto Parsed = yona::interface::readModule(Entry.path());
    REQUIRE(Parsed.has_value());

    auto Relative = std::filesystem::relative(Entry.path(), Library);
    Relative.replace_extension();
    std::string ExpectedIdentity;
    for (const auto &Segment : Relative) {
      if (!ExpectedIdentity.empty())
        ExpectedIdentity += '\\';
      ExpectedIdentity += Segment.string();
    }
    CHECK(Parsed->Identity.fqn() == ExpectedIdentity);

    auto Serialized = yona::interface::serializeModule(*Parsed);
    REQUIRE(Serialized.has_value());
    std::ifstream Input(Entry.path(), std::ios::binary);
    const std::string Stored{std::istreambuf_iterator<char>(Input),
                             std::istreambuf_iterator<char>()};
    CHECK(*Serialized == Stored);
  }
}
