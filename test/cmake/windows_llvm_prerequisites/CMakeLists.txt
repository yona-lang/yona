cmake_minimum_required(VERSION 3.15...4.0)
project(YonaWindowsLlvmPrerequisitesTest LANGUAGES NONE)

if(NOT DEFINED YONA_SOURCE_DIR)
  message(FATAL_ERROR "YONA_SOURCE_DIR is required")
endif()

function(yona_require_file_contains file_path expected)
  file(READ "${file_path}" _contents)
  string(FIND "${_contents}" "${expected}" _position)
  if(_position LESS 0)
    message(FATAL_ERROR "${file_path} must contain '${expected}'")
  endif()
endfunction()

function(yona_require_file_omits file_path unexpected)
  file(READ "${file_path}" _contents)
  string(FIND "${_contents}" "${unexpected}" _position)
  if(NOT _position LESS 0)
    message(FATAL_ERROR "${file_path} must not contain '${unexpected}'")
  endif()
endfunction()

# The ARM64 CI jobs select these presets and pass their architecture through
# setup-llvm. Keep this check text-based so it runs on every host without an
# ARM64 Windows toolchain or YAML parser.
set(_presets_file "${YONA_SOURCE_DIR}/CMakePresets.json")
foreach(_required_preset
    "arm64-debug"
    "arm64-release"
    "arm64-debug-macos"
    "arm64-release-macos"
    "build-debug-arm64"
    "build-release-arm64"
    "build-debug-macos-arm64"
    "build-release-macos-arm64"
    "unit-tests-windows-arm64"
    "unit-tests-macos-arm64")
  yona_require_file_contains("${_presets_file}" "\"${_required_preset}\"")
endforeach()
yona_require_file_contains("${_presets_file}" "\"configurePreset\": \"arm64-debug\"")
yona_require_file_contains("${_presets_file}" "\"configurePreset\": \"arm64-release\"")
yona_require_file_contains("${_presets_file}" "\"CMAKE_OSX_ARCHITECTURES\": \"arm64\"")
yona_require_file_omits("${_presets_file}" "\"x64-debug-macos\"")
yona_require_file_omits("${_presets_file}" "\"x64-release-macos\"")

set(_setup_llvm_action "${YONA_SOURCE_DIR}/.github/actions/setup-llvm/action.yml")
yona_require_file_contains("${_setup_llvm_action}" "windows-architecture:")
yona_require_file_contains("${_setup_llvm_action}" "default: x64")
yona_require_file_contains("${_setup_llvm_action}" "\"x64\" { $targetTriple = \"x86_64-pc-windows-msvc\" }")
yona_require_file_contains("${_setup_llvm_action}" "\"arm64\" { $targetTriple = \"aarch64-pc-windows-msvc\" }")
yona_require_file_contains("${_setup_llvm_action}" "clang+llvm-$ver-$targetTriple.tar.xz")
yona_require_file_contains("${_setup_llvm_action}" "llvm-windows-\${{ steps.win-llvm.outputs.release }}-\${{ steps.win-llvm.outputs.target-triple }}")
yona_require_file_contains("${_setup_llvm_action}" "Resolved LLVM archive '$archive' does not match requested target")
yona_require_file_contains("${_setup_llvm_action}" "-print-target-triple")
yona_require_file_omits("${_setup_llvm_action}" "vcpkg")

file(READ "${YONA_SOURCE_DIR}/CMakeLists.txt" _top_level_cmake)
foreach(_pcre_contract
    "find_package(PCRE2 QUIET CONFIG COMPONENTS 8BIT)"
    "PCRE2::8BIT"
    "PCRE2Project/pcre2"
    "pcre2-10.47"
    "YONA_PCRE2_RUNTIME_ARCHIVE"
    "PCRE2_STATIC"
    "yona_runtime_pcre2"
    "YONAC_EXE_LINK_PCRE2")
  yona_require_file_contains("${YONA_SOURCE_DIR}/CMakeLists.txt" "${_pcre_contract}")
endforeach()
set(_yonac_cli "${YONA_SOURCE_DIR}/cli/main.cpp")
yona_require_file_contains("${_yonac_cli}" "const string pcre2_lld_arg = packaged_pcre2.empty()")
yona_require_file_contains("${_yonac_cli}" "lld_args.push_back(pcre2_lld_arg);")
string(FIND "${_top_level_cmake}" "find_package(ZLIB QUIET)" _zlib_position)
string(FIND "${_top_level_cmake}" "find_package(zstd CONFIG QUIET)" _zstd_position)
string(FIND "${_top_level_cmake}" "zstd::libzstd_static" _zstd_target_position)
string(FIND "${_top_level_cmake}" "yona_prepare_llvm_dia_sdk()" _dia_position)
string(FIND "${_top_level_cmake}" "find_package(LLVM CONFIG REQUIRED)" _llvm_position)
if(_zlib_position LESS 0 OR _zstd_position LESS 0 OR _zstd_target_position LESS 0 OR
   _dia_position LESS 0 OR _llvm_position LESS 0 OR
   _zlib_position GREATER _llvm_position OR _zstd_position GREATER _llvm_position OR
   _zstd_target_position GREATER _llvm_position OR
   _dia_position GREATER _llvm_position)
  message(FATAL_ERROR
    "Windows LLVM prerequisite targets must be prepared before find_package(LLVM)")
endif()

set(_fake_dia "${CMAKE_CURRENT_BINARY_DIR}/diaguids.lib")
file(WRITE "${_fake_dia}" "")
set(YONA_DIA_SDK_LIBRARY "${_fake_dia}" CACHE FILEPATH "" FORCE)
add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
add_library(zstd::libzstd_static INTERFACE IMPORTED GLOBAL)

include("${YONA_SOURCE_DIR}/cmake/YonaLlvmWindowsDia.cmake")
yona_dia_sdk_arch(_arm64_dia_arch "ARM64")
if(NOT _arm64_dia_arch STREQUAL "arm64")
  message(FATAL_ERROR "The ARM64 DIA SDK lookup must use DIA SDK/lib/arm64")
endif()
yona_prepare_llvm_dia_sdk()

if(NOT TARGET DIASDK::Diaguids)
  message(FATAL_ERROR "DIASDK::Diaguids must exist before LLVMConfig.cmake is loaded")
endif()

get_target_property(_dia_location DIASDK::Diaguids IMPORTED_LOCATION)
if(NOT _dia_location STREQUAL _fake_dia)
  message(FATAL_ERROR
    "DIASDK::Diaguids location mismatch: expected '${_fake_dia}', got '${_dia_location}'")
endif()

# This is a contract test of our mock package, not a probe of whichever LLVM
# the host happens to export through CMAKE_PREFIX_PATH/LLVM_DIR.  Loading the
# fixture directly is intentional: `find_package` may prefer an already loaded
# host package in an enclosing configure, and host LLVMConfig files can run
# compiler checks which this LANGUAGES NONE test deliberately does not enable.
# The source-order assertion above independently verifies that the production
# build uses find_package(LLVM CONFIG REQUIRED) after preparing its targets.
include("${CMAKE_CURRENT_SOURCE_DIR}/mock-llvm/LLVMConfig.cmake")
if(NOT LLVM_PACKAGE_VERSION STREQUAL "test")
  message(FATAL_ERROR
    "The Windows LLVM prerequisite contract must load its mock LLVMConfig.cmake, "
    "not a host LLVM installation")
endif()
