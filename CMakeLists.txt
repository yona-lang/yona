# CMakeList.txt : CMake project for yonac-llvm, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.10..4.0)

# Set policy for MSVC runtime library selection
if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()

# Ensure consistent MSVC runtime library usage across all targets
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

	# Force all targets to use the same runtime
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MD$<$<CONFIG:Debug>:d>")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MD$<$<CONFIG:Debug>:d>")

	# Enable RTTI and exception handling consistently
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR /EHsc")

	# Remove any conflicting runtime flags
	string(REGEX REPLACE "/M[TD]d?" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
	string(REGEX REPLACE "/M[TD]d?" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
	string(REGEX REPLACE "/M[TD]d?" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	string(REGEX REPLACE "/M[TD]d?" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
endif()

if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
	set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)
endif()

if(POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

project(YONAC_LLVM LANGUAGES CXX C)

if(WIN32)
	# Prevent Windows.h min/max macros from breaking std::min / std::max in LLVM and libc++.
	add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Read version from VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_STRING)
string(STRIP "${VERSION_STRING}" VERSION_STRING)

# Parse version components
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" VERSION_MATCH "${VERSION_STRING}")
set(YONAC_LLVM_VERSION_MAJOR ${CMAKE_MATCH_1})
set(YONAC_LLVM_VERSION_MINOR ${CMAKE_MATCH_2})
set(YONAC_LLVM_VERSION_PATCH ${CMAKE_MATCH_3})
set(YONAC_LLVM_VERSION_STRING "${VERSION_STRING}")

# Configure version header
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/version.h"
	@ONLY
)

include(FetchContent)

FetchContent_Declare(
	cli11
	QUIET
	GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
	GIT_TAG v2.6.2
)

FetchContent_MakeAvailable(cli11)

option(YONA_FETCH_LIBXML2 "Fetch/build libxml2 when system package is incompatible for embedded LLD" ON)

# PCRE2 for regex module (optional)
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(PCRE2 libpcre2-8)
endif()
if(PCRE2_FOUND)
    message(STATUS "Found PCRE2: ${PCRE2_LIBRARIES}")
else()
    message(STATUS "PCRE2 not found — Std\\Regex module will not be available")
endif()

# Vulkan for Std\GPU runtime probe (optional; probe implementation is Unix-only in gpu_stub.c)
find_package(Vulkan QUIET)
if(Vulkan_FOUND AND UNIX)
    message(STATUS "Found Vulkan — Std\\GPU will probe physical devices on this platform")
elseif(Vulkan_FOUND)
    message(STATUS "Found Vulkan — Std\\GPU probe is not enabled on non-Unix hosts yet")
else()
    message(STATUS "Vulkan not found — Std\\GPU uses zeroed stub counters only")
endif()

find_package(LLVM CONFIG REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaInProcessLld.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaVulkan.cmake")

# Verify LLVM version is at least 16
if(${LLVM_VERSION_MAJOR} LESS 16)
  message(FATAL_ERROR "LLVM version must be 16 or higher, found ${LLVM_VERSION_MAJOR}")
endif()

# Setup LLVM - use SYSTEM to suppress warnings from LLVM headers
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})


#set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
#set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

file(GLOB_RECURSE all_lib_SRCS
		"${PROJECT_SOURCE_DIR}/include/*.h"
		"${PROJECT_SOURCE_DIR}/src/*.cpp"
		"${PROJECT_SOURCE_DIR}/src/*.c"
)
# Exclude files #included from compiled_runtime.c
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/seq\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/hamt\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/exceptions\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/closures\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan_device\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan_compute\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan_ops\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_cpu\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_stub\\.c$")
# Platform layer: OS TUs + async/channel sources (#included from compiled_runtime.c only).
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/platform/.*\\.c$")
set(YONA_PLATFORM_RUNTIME_SOURCES)
if(WIN32)
	list(APPEND YONA_PLATFORM_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/file_windows.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/os_windows.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/net_windows.c"
	)
else()
	list(APPEND YONA_PLATFORM_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/file_linux.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/os_linux.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/net_linux.c"
	)
endif()
list(APPEND all_lib_SRCS ${YONA_PLATFORM_RUNTIME_SOURCES})
if(NOT PCRE2_FOUND)
    list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/regex\\.c$")
endif()

file(GLOB all_TESTS
		"${PROJECT_SOURCE_DIR}/test/*.h"
		"${PROJECT_SOURCE_DIR}/test/*.cpp"
		"${PROJECT_SOURCE_DIR}/test/*.c"
)

file(GLOB all_yonac_SRCS
		"${PROJECT_SOURCE_DIR}/include/*.h"
		"${PROJECT_SOURCE_DIR}/cli/*.cpp"
		"${PROJECT_SOURCE_DIR}/cli/*.c"
)

file(GLOB all_repl_SRCS
		"${PROJECT_SOURCE_DIR}/include/*.h"
		"${PROJECT_SOURCE_DIR}/repl/*.cpp"
)

# Find the libraries that correspond to the LLVM components that we wish to use
llvm_map_components_to_libnames(llvm_libs Support Core IRReader Passes Target native nativecodegen)

# Add source to this project's executable.
add_library(yona_lib SHARED ${all_lib_SRCS})

# Create a static version of the library for tests only
add_library(yona_lib_static STATIC ${all_lib_SRCS})
target_include_directories(yona_lib_static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yona_lib_static PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(yona_lib_static PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(yona_lib_static PRIVATE ${llvm_libs})
if(YONA_INPROCESS_LLD_AVAILABLE)
	target_link_libraries(yona_lib_static PRIVATE ${YONA_INPROCESS_LLD_LIBS})
	target_compile_definitions(yona_lib_static PRIVATE YONA_ENABLE_INPROCESS_LLD=1)
endif()
if(WIN32)
	target_link_libraries(yona_lib_static PRIVATE ws2_32 dbghelp)
endif()
if(PCRE2_FOUND)
    target_link_libraries(yona_lib_static PRIVATE ${PCRE2_LIBRARIES})
    target_include_directories(yona_lib_static PRIVATE ${PCRE2_INCLUDE_DIRS})
endif()
if(Vulkan_FOUND AND UNIX)
    target_compile_definitions(yona_lib_static PRIVATE YONA_HAS_VULKAN=1)
    target_link_libraries(yona_lib_static PRIVATE Vulkan::Vulkan)
endif()
target_compile_definitions(yona_lib_static PRIVATE YONA_STATIC_BUILD)
if(YONA_ENABLE_VULKAN)
	target_include_directories(yona_lib_static PRIVATE ${YONA_VULKAN_INCLUDE_DIR})
	target_compile_definitions(yona_lib_static PRIVATE YONA_COMPILE_GPU_VULKAN=1)
endif()

target_include_directories(yona_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yona_lib PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(yona_lib PRIVATE ${LLVM_INCLUDE_DIRS})

target_link_libraries(yona_lib PRIVATE ${llvm_libs})
if(YONA_INPROCESS_LLD_AVAILABLE)
	target_link_libraries(yona_lib PRIVATE ${YONA_INPROCESS_LLD_LIBS})
	target_compile_definitions(yona_lib PRIVATE YONA_ENABLE_INPROCESS_LLD=1)
endif()
if(WIN32)
	target_link_libraries(yona_lib PRIVATE ws2_32 dbghelp)
endif()
if(PCRE2_FOUND)
    target_link_libraries(yona_lib PRIVATE ${PCRE2_LIBRARIES})
    target_include_directories(yona_lib PRIVATE ${PCRE2_INCLUDE_DIRS})
endif()
if(Vulkan_FOUND AND UNIX)
    target_compile_definitions(yona_lib PRIVATE YONA_HAS_VULKAN=1)
    target_link_libraries(yona_lib PRIVATE Vulkan::Vulkan)
endif()
if(YONA_ENABLE_VULKAN)
	target_include_directories(yona_lib PRIVATE ${YONA_VULKAN_INCLUDE_DIR})
	target_compile_definitions(yona_lib PRIVATE YONA_COMPILE_GPU_VULKAN=1)
endif()

# yona_lib is always SHARED; Windows needs explicit export for C++ symbols linked by yonac/yona.
if(WIN32)
	set_target_properties(yona_lib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if (CMAKE_VERSION VERSION_GREATER 3.12)
	set_property(TARGET yona_lib PROPERTY CXX_STANDARD 23)
endif ()

# Compiler executable
add_executable(yonac ${all_yonac_SRCS})
target_include_directories(yonac PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yonac PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
if(WIN32)
	# Avoid STL/DLL boundary ownership issues on MSVC/UCRT by linking the
	# compiler driver against the static runtime library.
	target_link_libraries(yonac PRIVATE yona_lib_static CLI11::CLI11)
	target_compile_definitions(yonac PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yonac PRIVATE yona_lib CLI11::CLI11)
endif()

# Prebuilt runtime artifacts for packaged/sysroot-first toolchain flow.
set(YONA_RUNTIME_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/runtime")
set(YONA_RUNTIME_MAIN_OBJ "${YONA_RUNTIME_ARTIFACT_DIR}/compiled_runtime.o")
set(YONA_EMBEDDED_RUNTIME_SOURCES
	"${PROJECT_SOURCE_DIR}/src/compiled_runtime.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/seq.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/hamt.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/exceptions.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/closures.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan_device.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan_compute.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan_ops.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_cpu.c"
)
if(WIN32)
	list(APPEND YONA_EMBEDDED_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/async_win32.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/channel_win32.c"
	)
else()
	list(APPEND YONA_EMBEDDED_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/async_posix.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/channel_posix.c"
	)
endif()
set(YONA_RUNTIME_PLATFORM_OBJS)
foreach(_pf IN LISTS YONA_PLATFORM_RUNTIME_SOURCES)
	get_filename_component(_pf_name "${_pf}" NAME)
	list(APPEND YONA_RUNTIME_PLATFORM_OBJS "${YONA_RUNTIME_ARTIFACT_DIR}/crt_${_pf_name}.o")
endforeach()

set(_YONA_CR_CCFLAGS -I${PROJECT_SOURCE_DIR}/src -I${PROJECT_SOURCE_DIR}/include)
if(YONA_ENABLE_VULKAN)
	list(APPEND _YONA_CR_CCFLAGS -I${YONA_VULKAN_INCLUDE_DIR} -DYONA_COMPILE_GPU_VULKAN=1)
endif()
add_custom_command(
	OUTPUT "${YONA_RUNTIME_MAIN_OBJ}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${YONA_RUNTIME_ARTIFACT_DIR}"
	COMMAND ${CMAKE_C_COMPILER} -c "${PROJECT_SOURCE_DIR}/src/compiled_runtime.c"
		${_YONA_CR_CCFLAGS}
		-o "${YONA_RUNTIME_MAIN_OBJ}"
	DEPENDS ${YONA_EMBEDDED_RUNTIME_SOURCES}
	COMMENT "Building packaged runtime object: compiled_runtime.o"
	VERBATIM
)

set(_runtime_obj_idx 0)
foreach(_pf IN LISTS YONA_PLATFORM_RUNTIME_SOURCES)
	list(GET YONA_RUNTIME_PLATFORM_OBJS ${_runtime_obj_idx} _po)
	add_custom_command(
		OUTPUT "${_po}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${YONA_RUNTIME_ARTIFACT_DIR}"
		COMMAND ${CMAKE_C_COMPILER} -c "${_pf}"
			-I${PROJECT_SOURCE_DIR}/src -I${PROJECT_SOURCE_DIR}/include
			-o "${_po}"
		DEPENDS "${_pf}"
		COMMENT "Building packaged runtime object: ${_po}"
		VERBATIM
	)
	math(EXPR _runtime_obj_idx "${_runtime_obj_idx} + 1")
endforeach()

add_custom_target(yona_runtime_objects
	DEPENDS "${YONA_RUNTIME_MAIN_OBJ}" ${YONA_RUNTIME_PLATFORM_OBJS}
)

add_library(yona_runtime_static STATIC
	"${PROJECT_SOURCE_DIR}/src/compiled_runtime.c"
	${YONA_PLATFORM_RUNTIME_SOURCES}
)
set_target_properties(yona_runtime_static PROPERTIES
	OUTPUT_NAME yona_runtime
	ARCHIVE_OUTPUT_DIRECTORY "${YONA_RUNTIME_ARTIFACT_DIR}"
)
target_include_directories(yona_runtime_static PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_BINARY_DIR}/include
)
# Library path YONA_VULKAN_LIBRARY is set by YonaVulkan.cmake (Option A) but
# we do not link it into the main executable yet, so yonac-linked programs run
# without vulkan-1 at load time. P1 will use delay-load, dlopen, or an aux DSO.
if(YONA_ENABLE_VULKAN)
	target_include_directories(yona_runtime_static PRIVATE ${YONA_VULKAN_INCLUDE_DIR})
	target_compile_definitions(yona_runtime_static PRIVATE YONA_COMPILE_GPU_VULKAN=1)
endif()
if(WIN32)
	target_link_libraries(yona_runtime_static PRIVATE ws2_32 dbghelp)
endif()

add_dependencies(yonac yona_runtime_objects yona_runtime_static)

# REPL executable
add_executable(yona ${all_repl_SRCS})
target_include_directories(yona PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yona PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
if(WIN32)
	target_link_libraries(yona PRIVATE yona_lib_static)
	target_compile_definitions(yona PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yona PRIVATE yona_lib)
endif()
add_dependencies(yona yona_runtime_objects yona_runtime_static)

if(WIN32)
	# repl uses popen; map to _popen on UCRT (same header as tests).
	target_compile_options(yona PRIVATE
		"-include" "${CMAKE_CURRENT_BINARY_DIR}/test_config/yona_test_config.h")
endif()

enable_testing()

FetchContent_Declare(
	doctest
	GIT_REPOSITORY https://github.com/doctest/doctest.git
	GIT_TAG        v2.4.12
)

FetchContent_MakeAvailable(doctest)

# Absolute repo path for tests (fixtures, lib/, src/) regardless of process CWD (e.g. CTest).
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/test/yona_test_config.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/test_config/yona_test_config.h"
	@ONLY)

add_executable(
	tests
	${all_TESTS}
)

# We provide our own main for consistency across all platforms
target_sources(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test/doctest_main.cpp)

# Link with doctest and yona_lib_static for tests
target_link_libraries(tests PRIVATE doctest::doctest yona_lib_static)

# Define YONA_STATIC_BUILD for tests to avoid DLL export/import issues
target_compile_definitions(tests PRIVATE YONA_STATIC_BUILD)
if(YONA_ENABLE_VULKAN)
	target_compile_definitions(tests PRIVATE YONA_COMPILE_GPU_VULKAN=1)
endif()

# Include CTest
include(CTest)
target_include_directories(tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/test
	${CMAKE_CURRENT_BINARY_DIR}/test_config)

if(WIN32)
	# Tests use popen/pclose; force-include maps them to _popen/_pclose on UCRT.
	target_compile_options(tests PRIVATE
		"-include" "${CMAKE_CURRENT_BINARY_DIR}/test_config/yona_test_config.h")
endif()

# Register the test executable with CTest
add_test(NAME doctest_tests COMMAND tests)

# Set working directory for the test to the binary directory
set(_yona_path_env "YONA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/test/code:${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(_ctest_base_env "${_yona_path_env}")
if(WIN32)
	# Shell-out compile/link in tests uses YONAC_CC; CTest often runs without LLVM on PATH.
	get_filename_component(_cxx_bindir "${CMAKE_CXX_COMPILER}" DIRECTORY)
	if(EXISTS "${_cxx_bindir}/clang.exe")
		string(APPEND _ctest_base_env ";YONAC_CC=${_cxx_bindir}/clang.exe")
	endif()
endif()
# Full suite: test harness recompiles `compiled_runtime.c`; do not inherit a user/global
# YONA_COMPILE_GPU_VULKAN=1 + VULKAN_SDK session (would desync with default objects).
set(_ctest_env "${_ctest_base_env};YONA_COMPILE_GPU_VULKAN=0")

set_tests_properties(doctest_tests PROPERTIES
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	LABELS "unit"
	ENVIRONMENT "${_ctest_env}"
)

# Set up test environment
if(APPLE)
	set_tests_properties(doctest_tests PROPERTIES
		ENVIRONMENT "DYLD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{DYLD_LIBRARY_PATH};${_ctest_env}"
	)
elseif(UNIX)
	set_tests_properties(doctest_tests PROPERTIES
		ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{LD_LIBRARY_PATH};${_ctest_env}"
	)
endif()

# Vulkan-only slice: same binary, no YONA_COMPILE_GPU_VULKAN=0 so local SDK matches the
# Vulkan-enabled objects; filters to gpu/vulkan doctest cases.
if(YONA_ENABLE_VULKAN)
	add_test(NAME doctest_gpu_vulkan COMMAND tests -tc=*gpu?vulkan*)
	set_tests_properties(doctest_gpu_vulkan PROPERTIES
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		LABELS "vulkan;gpu"
		ENVIRONMENT "${_ctest_base_env}"
	)
	if(APPLE)
		set_tests_properties(doctest_gpu_vulkan PROPERTIES
			ENVIRONMENT "DYLD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{DYLD_LIBRARY_PATH};${_ctest_base_env}"
		)
	elseif(UNIX)
		set_tests_properties(doctest_gpu_vulkan PROPERTIES
			ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{LD_LIBRARY_PATH};${_ctest_base_env}"
		)
	endif()
endif()

# Enable doctest discovery for Visual Studio Test Explorer
set_target_properties(tests PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	VS_DEBUGGER_COMMAND_ARGUMENTS "--reporters=xml"
)

# Removed temporary test executables - tests are now using doctest
