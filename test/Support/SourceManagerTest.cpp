#include "yona/Support/SourceManager.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

TEST_CASE("source manager owns text and resolves stable ranges") {
  yona::SourceManager Sources;
  std::string Text = "alpha\nbeta\n";
  const yona::SourceId Id = Sources.addSource("sample.yona", Text);
  Text.assign("changed");

  CHECK(Sources.size() == 1);
  CHECK(Sources.text(Id) == "alpha\nbeta\n");
  CHECK(Sources.line(Id, 1) == "alpha");
  CHECK(Sources.line(Id, 2) == "beta");

  const yona::SourceRange Start{Id, 1, 2, 1, 2};
  const yona::SourceRange End{Id, 1, 5, 4, 1};
  const yona::SourceRange Range = yona::SourceRange::span(Start, End);
  CHECK(Range.Offset == 1);
  CHECK(Range.Length == 4);
  CHECK(Sources.format(Range) == "sample.yona:1:2");
}

TEST_CASE("source manager rejects foreign source identities") {
  const yona::SourceManager Sources;
  CHECK_THROWS_AS(Sources.text(yona::SourceId(42)), std::out_of_range);
  CHECK_THROWS_AS(yona::SourceRange::span({}, {}), std::invalid_argument);
}

TEST_CASE("source manager loads and retains a file buffer") {
  const auto Suffix = std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count();
  const auto Path = std::filesystem::temp_directory_path() /
                    ("yona-source-manager-" + std::to_string(Suffix) +
                     ".yona");
  {
    std::ofstream Output(Path, std::ios::binary);
    REQUIRE(Output);
    Output << "module Test\\Loaded\nexport value\nvalue = 42\n";
  }

  yona::SourceManager Sources;
  const auto Loaded = Sources.loadFile(Path);
  std::filesystem::remove(Path);

  REQUIRE(Loaded);
  CHECK(Sources.name(*Loaded) == Path.string());
  CHECK(Sources.line(*Loaded, 1) == "module Test\\Loaded");
  CHECK(Sources.text(*Loaded).ends_with("value = 42\n"));
}
