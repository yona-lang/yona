#include "yona/Model/ModuleIdentity.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>

TEST_CASE("module identity owns canonical paths and export names") {
  const yona::model::ModuleIdentity Identity("Std\\Net");

  CHECK(Identity.fqn() == "Std\\Net");
  CHECK(Identity.relativePath() == std::filesystem::path("Std") / "Net");
  CHECK(Identity.mangle("tcpConnect") == "YonaStdNetTcpConnect");
  CHECK(Identity.mangle("Eq_String__eq") == "YonaStdNetEqStringEq");
  CHECK(Identity.mangle("GPU_status") == "YonaStdNetGpuStatus");
}

TEST_CASE("module identity rejects noncanonical names") {
  CHECK_THROWS_AS(yona::model::ModuleIdentity("std\\Net"),
                  std::invalid_argument);
  CHECK_THROWS_AS(yona::model::ModuleIdentity("Std\\..\\Net"),
                  std::invalid_argument);
  CHECK_THROWS_AS(yona::model::ModuleIdentity("Std\\GPU"),
                  std::invalid_argument);
  CHECK_THROWS_AS(yona::model::mangleExport("Std\\Net", ""),
                  std::invalid_argument);
}
