#include "yona/Toolchain/InProcessLld.h"
#include "yona/Toolchain/LinkerPlan.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_SUITE("LinkerPlan") {

  TEST_CASE("parse linker mode accepts valid values") {
    using yona::toolchain::LinkerMode;
    LinkerMode mode = LinkerMode::Auto;
    CHECK(yona::toolchain::parseLinkerMode("auto", mode));
    CHECK(mode == LinkerMode::Auto);
    CHECK(yona::toolchain::parseLinkerMode("bundled", mode));
    CHECK(mode == LinkerMode::Bundled);
    CHECK(yona::toolchain::parseLinkerMode("system", mode));
    CHECK(mode == LinkerMode::System);
    CHECK(yona::toolchain::parseLinkerMode("inprocess", mode));
    CHECK(mode == LinkerMode::InProcess);
    CHECK(yona::toolchain::parseLinkerMode("in-process", mode));
    CHECK(mode == LinkerMode::InProcess);
  }

  TEST_CASE("parse linker mode rejects unknown value") {
    using yona::toolchain::LinkerMode;
    LinkerMode mode = LinkerMode::Auto;
    CHECK_FALSE(yona::toolchain::parseLinkerMode("unknown", mode));
  }

  TEST_CASE("linker mode name round-trips expected spellings") {
    using yona::toolchain::LinkerMode;
    CHECK(yona::toolchain::linkerModeName(LinkerMode::Auto) == "auto");
    CHECK(yona::toolchain::linkerModeName(LinkerMode::Bundled) == "bundled");
    CHECK(yona::toolchain::linkerModeName(LinkerMode::System) == "system");
    CHECK(yona::toolchain::linkerModeName(LinkerMode::InProcess) ==
          "inprocess");
  }

  TEST_CASE("parse linker mode is case-insensitive") {
    using yona::toolchain::LinkerMode;
    LinkerMode mode = LinkerMode::Auto;
    CHECK(yona::toolchain::parseLinkerMode("InPrOcEsS", mode));
    CHECK(mode == LinkerMode::InProcess);
  }

  TEST_CASE("resolve system mode never enables bundled linker") {
    yona::toolchain::LinkerPlan plan;
    std::string error;
    const std::vector<fs::path> roots = {};
    REQUIRE(yona::toolchain::resolveLinkerPlan("system", roots, plan, error));
    CHECK_FALSE(plan.UseBundledLld);
    CHECK(plan.RequestedMode == yona::toolchain::LinkerMode::System);
  }

  TEST_CASE("resolve bundled mode requires bundled lld in sysroots") {
    yona::toolchain::LinkerPlan plan;
    std::string error;
    const std::vector<fs::path> roots = {};
    CHECK_FALSE(
        yona::toolchain::resolveLinkerPlan("bundled", roots, plan, error));
    CHECK(error.find("bundled") != std::string::npos);
  }

  TEST_CASE("auto mode picks bundled lld when discovered") {
    auto tmp_base = fs::temp_directory_path() / "yona_linker_plan_test_auto";
    fs::remove_all(tmp_base);
    fs::create_directories(tmp_base / "bin");

    const auto candidates = yona::toolchain::bundledLldCandidateNames();
    REQUIRE(!candidates.empty());
    const fs::path lld_path = tmp_base / "bin" / candidates.front();
    {
      std::ofstream out(lld_path.string(), std::ios::binary);
      REQUIRE(out.good());
      out << "stub";
    }

    yona::toolchain::LinkerPlan plan;
    std::string error;
    const std::vector<fs::path> roots = {tmp_base};
    REQUIRE(yona::toolchain::resolveLinkerPlan("auto", roots, plan, error));
    CHECK(plan.RequestedMode == yona::toolchain::LinkerMode::Auto);
    CHECK(plan.UseBundledLld);
    CHECK_FALSE(plan.BundledLldPath.empty());

    fs::remove_all(tmp_base);
  }

  TEST_CASE("discover bundled lld also checks llvm/bin") {
    auto tmp_base =
        fs::temp_directory_path() / "yona_linker_plan_test_llvm_bin";
    fs::remove_all(tmp_base);
    fs::create_directories(tmp_base / "llvm" / "bin");

    const auto candidates = yona::toolchain::bundledLldCandidateNames();
    REQUIRE(!candidates.empty());
    const fs::path lld_path = tmp_base / "llvm" / "bin" / candidates.front();
    {
      std::ofstream out(lld_path.string(), std::ios::binary);
      REQUIRE(out.good());
      out << "stub";
    }

    const fs::path found = yona::toolchain::discoverBundledLld({tmp_base});
    CHECK_FALSE(found.empty());
    CHECK(found.filename() == lld_path.filename());
    fs::remove_all(tmp_base);
  }

  TEST_CASE("inprocess mode resolves and keeps external fallback policy") {
    yona::toolchain::LinkerPlan plan;
    std::string error;
    const std::vector<fs::path> roots = {};
    REQUIRE(
        yona::toolchain::resolveLinkerPlan("inprocess", roots, plan, error));
    CHECK(plan.RequestedMode == yona::toolchain::LinkerMode::InProcess);
    CHECK(plan.UseInProcessLld);
    CHECK_FALSE(plan.UseBundledLld);
  }

  TEST_CASE("inprocess mode keeps bundled fallback when bundled lld exists") {
    auto tmp_base =
        fs::temp_directory_path() / "yona_linker_plan_test_inprocess_bundled";
    fs::remove_all(tmp_base);
    fs::create_directories(tmp_base / "bin");
    const auto candidates = yona::toolchain::bundledLldCandidateNames();
    REQUIRE(!candidates.empty());
    {
      std::ofstream out((tmp_base / "bin" / candidates.front()).string(),
                        std::ios::binary);
      REQUIRE(out.good());
      out << "stub";
    }

    yona::toolchain::LinkerPlan plan;
    std::string error;
    REQUIRE(yona::toolchain::resolveLinkerPlan("inprocess", {tmp_base}, plan,
                                                 error));
    CHECK(plan.UseInProcessLld);
    CHECK(plan.UseBundledLld);
    CHECK_FALSE(plan.BundledLldPath.empty());
    fs::remove_all(tmp_base);
  }

  TEST_CASE("discoverSysroots prefers explicit sysroot then YONA_HOME") {
    auto tmp_sys = fs::temp_directory_path() / "yona_sysroot_opt";
    auto tmp_home = fs::temp_directory_path() / "yona_sysroot_home";
    fs::remove_all(tmp_sys);
    fs::remove_all(tmp_home);
    fs::create_directories(tmp_sys);
    fs::create_directories(tmp_home);

    const char *old_home = std::getenv("YONA_HOME");
    const std::string saved_home = old_home ? old_home : "";
#ifdef _WIN32
    _putenv_s("YONA_HOME", tmp_home.string().c_str());
#else
    setenv("YONA_HOME", tmp_home.string().c_str(), 1);
#endif
    const auto roots =
        yona::toolchain::discoverSysroots(nullptr, tmp_sys.string());
#ifdef _WIN32
    _putenv_s("YONA_HOME", saved_home.c_str());
#else
    if (saved_home.empty())
      unsetenv("YONA_HOME");
    else
      setenv("YONA_HOME", saved_home.c_str(), 1);
#endif

    REQUIRE(roots.size() >= 2);
    CHECK(roots[0] == fs::weakly_canonical(tmp_sys));
    CHECK(roots[1] == fs::weakly_canonical(tmp_home));
    fs::remove_all(tmp_sys);
    fs::remove_all(tmp_home);
  }

  TEST_CASE("discoverSysroots uses HOMEBREW_PREFIX when set") {
    auto tmp_brew = fs::temp_directory_path() / "yona_sysroot_brew";
    auto yona_root = tmp_brew / "lib" / "yona";
    fs::remove_all(tmp_brew);
    fs::create_directories(yona_root);

    const char *old_brew = std::getenv("HOMEBREW_PREFIX");
    const std::string saved_brew = old_brew ? old_brew : "";
#ifdef _WIN32
    _putenv_s("HOMEBREW_PREFIX", tmp_brew.string().c_str());
#else
    setenv("HOMEBREW_PREFIX", tmp_brew.string().c_str(), 1);
#endif
    const auto roots = yona::toolchain::discoverSysroots(nullptr, {});
#ifdef _WIN32
    _putenv_s("HOMEBREW_PREFIX", saved_brew.c_str());
#else
    if (saved_brew.empty())
      unsetenv("HOMEBREW_PREFIX");
    else
      setenv("HOMEBREW_PREFIX", saved_brew.c_str(), 1);
#endif

    bool found = false;
    const auto want = fs::weakly_canonical(yona_root);
    for (const auto &r : roots) {
      if (r == want)
        found = true;
    }
    CHECK(found);
    fs::remove_all(tmp_brew);
  }

} // TEST_SUITE("LinkerPlan")

TEST_SUITE("InProcessLld") {

  TEST_CASE("inprocess system args include Windows POSIX CRT aliases") {
#ifdef _WIN32
    const auto args = yona::toolchain::inProcessLldSystemArgs();
    bool has_oldnames = false;
    bool has_ws2 = false;
    bool has_dbghelp = false;
    for (const auto &a : args) {
      if (a.find("oldnames") != std::string::npos)
        has_oldnames = true;
      if (a.find("ws2_32") != std::string::npos)
        has_ws2 = true;
      if (a.find("dbghelp") != std::string::npos)
        has_dbghelp = true;
    }
    CHECK(has_oldnames);
    CHECK(has_ws2);
    CHECK(has_dbghelp);
#else
    CHECK_FALSE(yona::toolchain::inProcessLldSystemArgs().empty());
#endif
  }

#ifdef __linux__
  TEST_CASE("inprocess system args use lld flags not clang-driver -rdynamic") {
    const auto args = yona::toolchain::inProcessLldSystemArgs();
    bool saw_rdynamic = false;
    bool saw_export = false;
    bool saw_lm = false;
    bool saw_L = false;
    for (const auto &a : args) {
      if (a == "-rdynamic")
        saw_rdynamic = true;
      if (a == "--export-dynamic" || a == "-export-dynamic")
        saw_export = true;
      if (a == "-lm")
        saw_lm = true;
      if (a.rfind("-L", 0) == 0 && a.size() > 2)
        saw_L = true;
    }
    CHECK(saw_export);
    CHECK_FALSE(saw_rdynamic);
    CHECK(saw_lm);
    CHECK(saw_L);
  }
#endif

  TEST_CASE("inprocess LLD diagnostics include stdout when stderr is empty") {
    yona::toolchain::InProcessLldResult res;
    res.stdout_text = "lld stdout";
    CHECK(res.diagnostic_text() == "lld stdout");
    res.stderr_text = "lld stderr";
    CHECK(res.diagnostic_text().find("lld stderr") != std::string::npos);
    CHECK(res.diagnostic_text().find("lld stdout") != std::string::npos);
  }

#ifdef __APPLE__
  TEST_CASE("inprocess system args pass syslibroot when SDKROOT is set") {
    const char *old_sdk = std::getenv("SDKROOT");
    const std::string saved_sdk = old_sdk ? old_sdk : "";
    const char *old_dt = std::getenv("MACOSX_DEPLOYMENT_TARGET");
    const std::string saved_dt = old_dt ? old_dt : "";
    setenv("SDKROOT", "/tmp/yona-fake-sdk", 1);
    setenv("MACOSX_DEPLOYMENT_TARGET", "12.0", 1);
    const auto args = yona::toolchain::inProcessLldSystemArgs();
    bool saw_syslibroot = false;
    bool saw_platform = false;
    bool saw_arch = false;
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] == "-syslibroot" && i + 1 < args.size() &&
          args[i + 1] == "/tmp/yona-fake-sdk")
        saw_syslibroot = true;
      if (args[i] == "-platform_version" && i + 3 < args.size() &&
          args[i + 1] == "macos" && args[i + 2] == "12.0")
        saw_platform = true;
      if (args[i] == "-arch" && i + 1 < args.size() &&
          (args[i + 1] == "arm64" || args[i + 1] == "x86_64"))
        saw_arch = true;
    }
    CHECK(saw_syslibroot);
    CHECK(saw_platform);
    CHECK(saw_arch);
    if (saved_sdk.empty())
      unsetenv("SDKROOT");
    else
      setenv("SDKROOT", saved_sdk.c_str(), 1);
    if (saved_dt.empty())
      unsetenv("MACOSX_DEPLOYMENT_TARGET");
    else
      setenv("MACOSX_DEPLOYMENT_TARGET", saved_dt.c_str(), 1);
  }
#endif

  TEST_CASE("inprocess availability API is self-consistent") {
    if (yona::toolchain::inProcessLldAvailable()) {
      CHECK(yona::toolchain::inProcessLldUnavailableReason().empty());
    } else {
      CHECK_FALSE(yona::toolchain::inProcessLldUnavailableReason().empty());
    }
  }

  TEST_CASE("inprocess runner reports unavailable backend clearly") {
    if (yona::toolchain::inProcessLldAvailable()) {
      // Availability is toolchain-dependent; this branch is validated by CI
      // smoke tests that compile+run with --linker-mode inprocess.
      CHECK(true);
      return;
    }
    yona::toolchain::InProcessLldResult res;
    const bool ok = yona::toolchain::run_inprocess_lld({}, res);
    CHECK_FALSE(ok);
    CHECK_FALSE(res.stderr_text.empty());
  }

  TEST_CASE("require inprocess env parser accepts common truthy values") {
#ifdef _WIN32
    _putenv_s("YONAC_REQUIRE_INPROCESS_LLD", "1");
#else
    setenv("YONAC_REQUIRE_INPROCESS_LLD", "1", 1);
#endif
    CHECK(yona::toolchain::requireInProcessLldFromEnv());
#ifdef _WIN32
    _putenv_s("YONAC_REQUIRE_INPROCESS_LLD", "true");
#else
    setenv("YONAC_REQUIRE_INPROCESS_LLD", "true", 1);
#endif
    CHECK(yona::toolchain::requireInProcessLldFromEnv());
#ifdef _WIN32
    _putenv_s("YONAC_REQUIRE_INPROCESS_LLD", "on");
#else
    setenv("YONAC_REQUIRE_INPROCESS_LLD", "on", 1);
#endif
    CHECK(yona::toolchain::requireInProcessLldFromEnv());
  }

  TEST_CASE("require inprocess env parser treats missing/false as disabled") {
#ifdef _WIN32
    _putenv_s("YONAC_REQUIRE_INPROCESS_LLD", "0");
#else
    setenv("YONAC_REQUIRE_INPROCESS_LLD", "0", 1);
#endif
    CHECK_FALSE(yona::toolchain::requireInProcessLldFromEnv());
#ifdef _WIN32
    _putenv_s("YONAC_REQUIRE_INPROCESS_LLD", "");
#else
    unsetenv("YONAC_REQUIRE_INPROCESS_LLD");
#endif
    CHECK_FALSE(yona::toolchain::requireInProcessLldFromEnv());
  }

} // TEST_SUITE("InProcessLld")
