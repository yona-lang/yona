#include "yona/Support/Process.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifndef YONA_TEST_CMAKE_EXECUTABLE
#define YONA_TEST_CMAKE_EXECUTABLE "cmake"
#endif

TEST_SUITE("Process execution") {

  TEST_CASE("shell metacharacters remain one literal argument") {
    const auto UniqueValue =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Sentinel =
        std::filesystem::temp_directory_path() /
        ("yona-process-injection-" + std::to_string(UniqueValue));
    const std::filesystem::path CMake = YONA_TEST_CMAKE_EXECUTABLE;

    std::error_code Error;
    std::filesystem::remove(Sentinel, Error);
    REQUIRE_FALSE(std::filesystem::exists(Sentinel));

    const std::string Injection = "literal & \"" + CMake.string() +
                                  "\" -E touch \"" + Sentinel.string() +
                                  "\"; $(touch \"" + Sentinel.string() + "\")";
    const yona::support::ProcessResult Result = yona::support::executeProcess(
        CMake, {"-E", "echo", Injection}, {.SuppressStderr = true});

    const bool ShellInterpretedArgument = std::filesystem::exists(Sentinel);
    if (ShellInterpretedArgument)
      std::filesystem::remove(Sentinel, Error);

    CHECK_FALSE(Result.ExecutionFailed);
    CHECK(Result.ExitCode == 0);
    CHECK_FALSE(ShellInterpretedArgument);
  }

  TEST_CASE("stdout capture preserves an exact argument") {
    const std::filesystem::path CMake = YONA_TEST_CMAKE_EXECUTABLE;
    const std::string Expected = "literal & ; $() text";
    const yona::support::ProcessResult Result = yona::support::executeProcess(
        CMake, {"-E", "echo", Expected}, {.CaptureStdout = true});

    CHECK_FALSE(Result.ExecutionFailed);
    CHECK(Result.ExitCode == 0);
    CHECK(Result.StandardOutput.find(Expected) != std::string::npos);
  }

  TEST_CASE("stdin content is supplied without redirection syntax") {
    const std::filesystem::path CMake = YONA_TEST_CMAKE_EXECUTABLE;
    const std::string Expected = "literal stdin & ; $()\n";
    const yona::support::ProcessResult Result = yona::support::executeProcess(
        CMake, {"-E", "cat", "-"},
        {.CaptureStdout = true, .StandardInput = Expected});

    CHECK_FALSE(Result.ExecutionFailed);
    CHECK(Result.ExitCode == 0);
    CHECK(Result.StandardOutput == Expected);
  }

  TEST_CASE("environment overrides preserve the inherited environment") {
    const std::filesystem::path CMake = YONA_TEST_CMAKE_EXECUTABLE;
    const std::string Expected = "literal & ; $()";
    const yona::support::ProcessResult Result = yona::support::executeProcess(
        CMake, {"-E", "environment"},
        {.CaptureStdout = true,
         .EnvironmentOverrides = {{"YONA_PROCESS_TEST_VALUE", Expected}}});

    CHECK_FALSE(Result.ExecutionFailed);
    CHECK(Result.ExitCode == 0);
    CHECK(Result.StandardOutput.find("YONA_PROCESS_TEST_VALUE=" + Expected) !=
          std::string::npos);
  }

  TEST_CASE("stderr is captured independently") {
    const auto UniqueValue =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path Missing =
        std::filesystem::temp_directory_path() /
        ("yona-process-missing-" + std::to_string(UniqueValue));
    const std::filesystem::path CMake = YONA_TEST_CMAKE_EXECUTABLE;
    const yona::support::ProcessResult Result = yona::support::executeProcess(
        CMake, {"-E", "chdir", Missing.string(), CMake.string(), "-E", "true"},
        {.CaptureStderr = true});

    CHECK_FALSE(Result.ExecutionFailed);
    CHECK(Result.ExitCode != 0);
    CHECK_FALSE(Result.StandardError.empty());
  }

} // TEST_SUITE
