# CMakeList.txt : CMake project for yona, include source and define
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

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaHomebrew.cmake")
yona_homebrew_prefix(YONA_HOMEBREW_PREFIX)
if(YONA_HOMEBREW_PREFIX)
	list(PREPEND CMAKE_PREFIX_PATH "${YONA_HOMEBREW_PREFIX}")
endif()

# Prefer distro / Homebrew packages. FetchContent is a last resort for the
# project's pinned third-party build dependencies (including the Windows LLVM
# package's Zlib/zstd prerequisites). Packaging passes -DYONA_FETCH_DEPS=OFF.
option(YONA_FETCH_DEPS "Fetch pinned dependencies when no system CMake package is found" ON)
# Distro packages copy binaries from the build tree (no cmake --install). Linking
# the shared in-tree DSO embeds that directory as DT_RUNPATH; Fedora check-rpaths
# rejects it and the RPM would not ship libyona_lib.so. Packaging passes ON.
option(YONA_LINK_STATIC_CLI "Link yonac/yona against yona_lib_static" OFF)
if(WIN32)
	# Avoid STL/DLL boundary ownership issues on MSVC/UCRT.
	set(YONA_LINK_STATIC_CLI ON)
endif()
if(WIN32)
	option(YONA_FETCH_LIBXML2 "Fetch/build libxml2 when system LibXml2 is not MSVC-compatible" ON)
else()
	option(YONA_FETCH_LIBXML2 "Fetch/build libxml2 when system LibXml2 is missing" OFF)
endif()

find_package(CLI11 2.0 CONFIG QUIET)
if(CLI11_FOUND)
	message(STATUS "Using system CLI11")
elseif(YONA_FETCH_DEPS)
	find_package(Git QUIET)
	if(NOT Git_FOUND)
		message(FATAL_ERROR
			"CLI11 not found and git is unavailable for FetchContent. "
			"Install libcli11-dev (Debian/Ubuntu), cli11-devel (Fedora), "
			"Homebrew cli11, or the CLI11 CMake package.")
	endif()
	message(STATUS "CLI11 not found; fetching v2.7.2")
	FetchContent_Declare(
		cli11
		QUIET
		GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
		GIT_TAG v2.7.2
	)
	FetchContent_MakeAvailable(cli11)
else()
	message(FATAL_ERROR
		"CLI11 not found. Install libcli11-dev, cli11-devel, or Homebrew cli11 "
		"(or pass -DYONA_FETCH_DEPS=ON to clone it).")
endif()

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

# Vulkan for Std\GPU — gpu_stub lazy device + f64 fence path (Unix + Windows when found)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Found Vulkan — Std\\GPU gpu_stub will link the loader on this platform")
    if(Vulkan_INCLUDE_DIRS)
        list(GET Vulkan_INCLUDE_DIRS 0 YONA_VK_HEADER_DIR)
    else()
        set(YONA_VK_HEADER_DIR "${Vulkan_INCLUDE_DIR}")
    endif()
else()
    message(STATUS "Vulkan not found — Std\\GPU uses zeroed stub counters only")
    set(YONA_VK_HEADER_DIR "")
endif()

# yonac: embed resolved vulkan-1.lib path on Windows (fallback: VULKAN_SDK at link time).
set(YONA_CFG_VULKAN_IMP_LIB_DEFINED 0)
set(YONA_CFG_VULKAN_IMP_LIB "")
if(Vulkan_FOUND AND WIN32)
	set(_yona_vk_imp "")
	if(TARGET Vulkan::Vulkan)
		get_target_property(_yona_vk_imp Vulkan::Vulkan IMPORTED_IMPLIB)
		if(_yona_vk_imp STREQUAL "_yona_vk_imp-NOTFOUND" OR NOT _yona_vk_imp)
			set(_yona_vk_imp "")
		endif()
		if(NOT _yona_vk_imp)
			get_target_property(_yona_vk_imp Vulkan::Vulkan IMPORTED_LOCATION)
			if(_yona_vk_imp STREQUAL "_yona_vk_imp-NOTFOUND" OR NOT _yona_vk_imp)
				set(_yona_vk_imp "")
			endif()
		endif()
	endif()
	if(NOT _yona_vk_imp AND DEFINED Vulkan_LIBRARY AND Vulkan_LIBRARY)
		set(_yona_vk_imp "${Vulkan_LIBRARY}")
	endif()
	if(NOT _yona_vk_imp AND Vulkan_LIBRARIES)
		list(GET Vulkan_LIBRARIES 0 _yona_vk_imp)
	endif()
	if(_yona_vk_imp AND NOT _yona_vk_imp MATCHES "::")
		if(EXISTS "${_yona_vk_imp}")
			file(TO_CMAKE_PATH "${_yona_vk_imp}" _yona_vk_norm)
			string(REPLACE "\\" "/" YONA_CFG_VULKAN_IMP_LIB "${_yona_vk_norm}")
			set(YONA_CFG_VULKAN_IMP_LIB_DEFINED 1)
		endif()
	endif()
endif()

if(WIN32)
	# LLVMExports.cmake from the official Windows archive references these
	# imported targets while it is being evaluated. They must exist before the
	# LLVM package is loaded; repairing target link interfaces afterward is too
	# late because CMake validates them during find_package(LLVM).
	find_package(ZLIB QUIET)
	if(NOT TARGET ZLIB::ZLIB)
		if(YONA_FETCH_DEPS)
			find_package(Git QUIET)
			if(NOT Git_FOUND)
				message(FATAL_ERROR
					"ZLIB was not found and git is unavailable for FetchContent. "
					"Install zlib with a CMake package that provides ZLIB::ZLIB, "
					"or install git and configure with -DYONA_FETCH_DEPS=ON.")
			endif()
			message(STATUS "ZLIB not found; fetching zlib v1.3.1 for LLVM")
			set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
			FetchContent_Declare(
				yona_zlib
				QUIET
				GIT_REPOSITORY https://github.com/madler/zlib.git
				GIT_TAG v1.3.1
				GIT_SHALLOW TRUE
			)
			FetchContent_MakeAvailable(yona_zlib)
			if(TARGET zlibstatic)
				add_library(ZLIB::ZLIB ALIAS zlibstatic)
			elseif(TARGET zlib)
				add_library(ZLIB::ZLIB ALIAS zlib)
			else()
				message(FATAL_ERROR
					"Fetched zlib v1.3.1 did not define the expected zlibstatic or zlib target.")
			endif()
		else()
			message(FATAL_ERROR
				"LLVM's Windows CMake package requires ZLIB::ZLIB, but ZLIB was not found. "
				"Install zlib with a CMake package, or configure with -DYONA_FETCH_DEPS=ON "
				"to build the pinned zlib dependency.")
		endif()
	endif()

	# LLVM 23's official Windows exports also name the upstream zstd target
	# directly.  Define it before loading LLVMConfig.cmake: CMake validates
	# imported link interfaces while the package is read, so it is too late to
	# repair this after find_package(LLVM).
	find_package(zstd CONFIG QUIET)
	if(NOT TARGET zstd::libzstd_static)
		if(YONA_FETCH_DEPS)
			find_package(Git QUIET)
			if(NOT Git_FOUND)
				message(FATAL_ERROR
					"zstd was not found and git is unavailable for FetchContent. "
					"Install zstd with a CMake package that provides zstd::libzstd_static, "
					"or install git and configure with -DYONA_FETCH_DEPS=ON.")
			endif()
			message(STATUS "zstd not found; fetching zstd v1.5.7 for LLVM")
			set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
			set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
			set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
			set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
			set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
			FetchContent_Declare(
				yona_zstd
				QUIET
				GIT_REPOSITORY https://github.com/facebook/zstd.git
				GIT_TAG v1.5.7
				GIT_SHALLOW TRUE
				SOURCE_SUBDIR build/cmake
			)
			FetchContent_MakeAvailable(yona_zstd)
			if(TARGET libzstd_static)
				add_library(zstd::libzstd_static ALIAS libzstd_static)
			else()
				message(FATAL_ERROR
					"Fetched zstd v1.5.7 did not define the expected libzstd_static target.")
			endif()
		else()
			message(FATAL_ERROR
				"LLVM's Windows CMake package requires zstd::libzstd_static, but zstd was not found. "
				"Install zstd with a CMake package, or configure with -DYONA_FETCH_DEPS=ON "
				"to build the pinned zstd dependency.")
		endif()
	endif()

	include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaLlvmWindowsDia.cmake")
	yona_prepare_llvm_dia_sdk()
endif()

find_package(LLVM CONFIG REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaInProcessLld.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaVulkan.cmake")

set(YONA_CFG_VULKAN_LIB_DIR_DEFINED 0)
set(YONA_CFG_VULKAN_LIB_DIR "")
set(YONA_CFG_VULKAN_ICD_JSON_DEFINED 0)
set(YONA_CFG_VULKAN_ICD_JSON "")
set(_yona_vk_lib_file "")
if(YONA_VULKAN_LIBRARY AND EXISTS "${YONA_VULKAN_LIBRARY}")
	set(_yona_vk_lib_file "${YONA_VULKAN_LIBRARY}")
elseif(DEFINED Vulkan_LIBRARY AND Vulkan_LIBRARY AND EXISTS "${Vulkan_LIBRARY}")
	set(_yona_vk_lib_file "${Vulkan_LIBRARY}")
endif()
if(_yona_vk_lib_file)
	get_filename_component(_yona_vk_lib_dir "${_yona_vk_lib_file}" DIRECTORY)
	file(TO_CMAKE_PATH "${_yona_vk_lib_dir}" YONA_CFG_VULKAN_LIB_DIR)
	set(YONA_CFG_VULKAN_LIB_DIR_DEFINED 1)
endif()
set(_yona_icd_roots "")
if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
	list(APPEND _yona_icd_roots "$ENV{VULKAN_SDK}")
endif()
if(YONA_HOMEBREW_PREFIX)
	list(APPEND _yona_icd_roots "${YONA_HOMEBREW_PREFIX}")
endif()
foreach(_yona_icd_root IN LISTS _yona_icd_roots)
	foreach(_yona_icd_rel IN ITEMS "etc/vulkan/icd.d/MoltenVK_icd.json" "share/vulkan/icd.d/MoltenVK_icd.json")
		if(EXISTS "${_yona_icd_root}/${_yona_icd_rel}")
			file(TO_CMAKE_PATH "${_yona_icd_root}/${_yona_icd_rel}" YONA_CFG_VULKAN_ICD_JSON)
			set(YONA_CFG_VULKAN_ICD_JSON_DEFINED 1)
		endif()
	endforeach()
endforeach()
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/yona_vulkan_link_cfg.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/yona_vulkan_link_cfg.h"
	@ONLY)

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

file(GLOB_RECURSE all_lib_SRCS CONFIGURE_DEPENDS
		"${PROJECT_SOURCE_DIR}/include/*.h"
		"${PROJECT_SOURCE_DIR}/src/*.cpp"
		"${PROJECT_SOURCE_DIR}/src/*.c"
)
# Exclude files #included from compiled_runtime.c
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/seq\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/hamt\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/exceptions\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/closures\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/utf16\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/json\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan_device\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan_compute\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_vulkan_ops\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_cpu\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/gpu_stub\\.c$")
# Platform layer: OS TUs (Linux io_uring / macOS kqueue / Windows IOCP)
# + async/channel sources (#included from compiled_runtime.c only).
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/runtime/platform/.*\\.c$")
list(FILTER all_lib_SRCS EXCLUDE REGEX "src/lsp/")
list(FILTER all_lib_SRCS EXCLUDE REGEX "include/lsp/")
set(YONA_PLATFORM_RUNTIME_SOURCES)
if(WIN32)
	list(APPEND YONA_PLATFORM_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/file_windows.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/os_windows.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/net_windows.c"
	)
elseif(APPLE)
	list(APPEND YONA_PLATFORM_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/kqueue_macos.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/file_macos.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/os_macos.c"
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/net_macos.c"
	)
else()
	list(APPEND YONA_PLATFORM_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/platform/uring_linux.c"
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
list(FILTER all_yonac_SRCS EXCLUDE REGEX "cli/yls\\.cpp$")

file(GLOB all_repl_SRCS
		"${PROJECT_SOURCE_DIR}/include/*.h"
		"${PROJECT_SOURCE_DIR}/repl/*.cpp"
)

# Find the libraries that correspond to the LLVM components that we wish to use
llvm_map_components_to_libnames(llvm_libs Support Core IRReader Passes Target native nativecodegen)

# Add source to this project's executable.
add_library(yona_lib SHARED ${all_lib_SRCS})

# Static library for tests and for packaged CLI/REPL (YONA_LINK_STATIC_CLI).
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
if(Vulkan_FOUND)
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
if(Vulkan_FOUND)
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

file(GLOB YONA_LSP_LIB_SRCS "${PROJECT_SOURCE_DIR}/src/lsp/*.cpp")
add_library(yona_lsp_static STATIC ${YONA_LSP_LIB_SRCS})
target_include_directories(yona_lsp_static PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_BINARY_DIR}/include
	${LLVM_INCLUDE_DIRS})
target_compile_definitions(yona_lsp_static PRIVATE YONA_STATIC_BUILD)
# Do not link yona_lib here: tests already use yona_lib_static. Pulling the
# shared library onto that executable registers LLVM cl::opt twice.

add_executable(yls "${PROJECT_SOURCE_DIR}/cli/yls.cpp")
target_include_directories(yls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yls PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(yls PRIVATE yona_lsp_static)
if(YONA_LINK_STATIC_CLI)
	target_link_libraries(yls PRIVATE yona_lib_static)
	target_compile_definitions(yls PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yls PRIVATE yona_lib)
endif()

# Compiler executable
add_executable(yonac ${all_yonac_SRCS})
target_include_directories(yonac PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yonac PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
if(YONA_LINK_STATIC_CLI)
	target_link_libraries(yonac PRIVATE yona_lib_static CLI11::CLI11)
	target_compile_definitions(yonac PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yonac PRIVATE yona_lib CLI11::CLI11)
endif()
if(Vulkan_FOUND AND NOT WIN32)
	# cli/main.cpp: yonac-linked exe pulls vk* from gpu_stub → link libvulkan.
	target_compile_definitions(yonac PRIVATE YONAC_EXE_LINK_POSIX_VULKAN=1)
endif()
if(PCRE2_FOUND AND NOT WIN32)
	target_compile_definitions(yonac PRIVATE YONAC_EXE_LINK_PCRE2=1)
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
	"${PROJECT_SOURCE_DIR}/src/runtime/utf16.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/json.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan_device.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan_compute.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_vulkan_ops.c"
	"${PROJECT_SOURCE_DIR}/src/runtime/gpu_cpu.c"
)
if(PCRE2_FOUND)
	list(APPEND YONA_EMBEDDED_RUNTIME_SOURCES
		"${PROJECT_SOURCE_DIR}/src/runtime/regex.c")
endif()
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

set(_YONA_CR_CCFLAGS -I${PROJECT_SOURCE_DIR}/src -I${PROJECT_SOURCE_DIR}/include -I${CMAKE_CURRENT_BINARY_DIR}/include)
if(PCRE2_FOUND)
	list(APPEND _YONA_CR_CCFLAGS ${PCRE2_CFLAGS} -DYONA_EMBEDDED_PCRE2=1)
endif()
if(Vulkan_FOUND)
	list(APPEND _YONA_CR_CCFLAGS -I${YONA_VK_HEADER_DIR} -DYONA_HAS_VULKAN=1)
endif()
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
if(PCRE2_FOUND)
	target_sources(yona_runtime_static PRIVATE
		"${PROJECT_SOURCE_DIR}/src/runtime/regex.c")
	target_include_directories(yona_runtime_static PRIVATE ${PCRE2_INCLUDE_DIRS})
endif()
set_target_properties(yona_runtime_static PROPERTIES
	OUTPUT_NAME yona_runtime
	ARCHIVE_OUTPUT_DIRECTORY "${YONA_RUNTIME_ARTIFACT_DIR}"
)
target_include_directories(yona_runtime_static PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_BINARY_DIR}/include
)
# gpu_stub compiled into this archive uses vk* imports; exe link resolves them.
if(Vulkan_FOUND)
	target_compile_definitions(yona_runtime_static PRIVATE YONA_HAS_VULKAN=1)
	target_include_directories(yona_runtime_static PRIVATE ${YONA_VK_HEADER_DIR})
endif()
if(YONA_ENABLE_VULKAN)
	target_include_directories(yona_runtime_static PRIVATE ${YONA_VULKAN_INCLUDE_DIR})
	target_compile_definitions(yona_runtime_static PRIVATE YONA_COMPILE_GPU_VULKAN=1)
endif()
if(WIN32)
	target_link_libraries(yona_runtime_static PRIVATE ws2_32 dbghelp)
endif()

add_dependencies(yonac yona_runtime_objects yona_runtime_static)

# REPL executable (invoked by the Yona-written `yona` runner)
add_executable(yona-repl ${all_repl_SRCS})
target_include_directories(yona-repl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yona-repl PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
if(YONA_LINK_STATIC_CLI)
	target_link_libraries(yona-repl PRIVATE yona_lib_static)
	target_compile_definitions(yona-repl PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yona-repl PRIVATE yona_lib)
endif()
add_dependencies(yona-repl yona_runtime_objects yona_runtime_static)

# Tests link generated programs against Prelude's exported constructors and
# functions. Build that object deterministically in the binary tree; never
# depend on a developer-local, ignored lib/Prelude.o.
set(YONA_TEST_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_artifacts")
set(YONA_TEST_PRELUDE_OBJECT "${YONA_TEST_ARTIFACT_DIR}/Prelude${CMAKE_C_OUTPUT_EXTENSION}")
set(YONA_TEST_PRELUDE_INTERFACE "${YONA_TEST_ARTIFACT_DIR}/Prelude.yonai")
add_custom_command(
	OUTPUT "${YONA_TEST_PRELUDE_OBJECT}"
	BYPRODUCTS "${YONA_TEST_PRELUDE_INTERFACE}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${YONA_TEST_ARTIFACT_DIR}"
	COMMAND $<TARGET_FILE:yonac>
		--sysroot "${CMAKE_CURRENT_BINARY_DIR}"
		-I "${CMAKE_CURRENT_SOURCE_DIR}/lib"
		--emit-obj
		-o "${YONA_TEST_PRELUDE_OBJECT}"
		"${CMAKE_CURRENT_SOURCE_DIR}/lib/Prelude.yona"
	DEPENDS yonac
		"${CMAKE_CURRENT_SOURCE_DIR}/lib/Prelude.yona"
		"${CMAKE_CURRENT_SOURCE_DIR}/lib/Prelude.yonai"
	COMMENT "Building deterministic Prelude object for tests"
	VERBATIM
)
add_custom_target(yona_test_prelude_object DEPENDS "${YONA_TEST_PRELUDE_OBJECT}")

# Absolute repo path for tests (fixtures, lib/, src/) regardless of process CWD (e.g. CTest).
# Generated even when BUILD_TESTING=OFF: the Windows REPL force-includes this header.
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/test/yona_test_config.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/test_config/yona_test_config.h"
	@ONLY)

if(WIN32)
	# repl uses popen; map to _popen on UCRT (same header as tests).
	target_compile_options(yona-repl PRIVATE
		"-include" "${CMAKE_CURRENT_BINARY_DIR}/test_config/yona_test_config.h")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaTools.cmake")
add_subdirectory(tools)

include(CTest)

if(BUILD_TESTING)
find_package(doctest 2.5 CONFIG QUIET)
if(doctest_FOUND)
	message(STATUS "Using system doctest")
elseif(YONA_FETCH_DEPS)
	find_package(Git QUIET)
	if(NOT Git_FOUND)
		message(FATAL_ERROR
			"doctest not found and git is unavailable for FetchContent. "
			"Install doctest-dev (Debian/Ubuntu), doctest-devel (Fedora), "
			"or Homebrew doctest.")
	endif()
	message(STATUS "doctest not found; fetching v2.5.3")
	FetchContent_Declare(
		doctest
		GIT_REPOSITORY https://github.com/doctest/doctest.git
		GIT_TAG        v2.5.3
	)
	FetchContent_MakeAvailable(doctest)
else()
	message(FATAL_ERROR
		"doctest not found. Install doctest-dev, doctest-devel, or Homebrew doctest "
		"(or pass -DYONA_FETCH_DEPS=ON to clone it).")
endif()

add_executable(
	tests
	${all_TESTS}
)
add_dependencies(tests yona_test_prelude_object)

# We provide our own main for consistency across all platforms
target_sources(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test/doctest_main.cpp)

# Link with doctest and yona_lib_static for tests
target_link_libraries(tests PRIVATE doctest::doctest yona_lib_static yona_lsp_static)

# Define YONA_STATIC_BUILD for tests to avoid DLL export/import issues
target_compile_definitions(tests PRIVATE YONA_STATIC_BUILD)
if(YONA_ENABLE_VULKAN)
	target_compile_definitions(tests PRIVATE YONA_COMPILE_GPU_VULKAN=1)
endif()

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

add_test(NAME cmake_windows_llvm_prerequisites
	COMMAND "${CMAKE_COMMAND}"
		-S "${CMAKE_CURRENT_SOURCE_DIR}/test/cmake/windows_llvm_prerequisites"
		-B "${CMAKE_CURRENT_BINARY_DIR}/test-cmake/windows-llvm-prerequisites"
		-DYONA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR})

# CMake 3.10 predates FindPython3, while newer CMake versions may disable the
# legacy FindPythonInterp module. Resolve a Python 3 interpreter directly so
# this source-contract test runs on every supported host.
find_program(YONA_PYTHON_EXECUTABLE NAMES python3 python)
if(NOT YONA_PYTHON_EXECUTABLE)
	message(FATAL_ERROR "Python 3 is required to run native_arm64_ci_packaging_contract")
endif()
execute_process(
	COMMAND "${YONA_PYTHON_EXECUTABLE}" -c "import sys; raise SystemExit(sys.version_info < (3, 7))"
	RESULT_VARIABLE _yona_python3_result)
if(NOT _yona_python3_result EQUAL 0)
	message(FATAL_ERROR
		"native_arm64_ci_packaging_contract requires Python 3.7 or newer: ${YONA_PYTHON_EXECUTABLE}")
endif()
add_test(NAME native_arm64_ci_packaging_contract
	COMMAND "${YONA_PYTHON_EXECUTABLE}"
		"${CMAKE_CURRENT_SOURCE_DIR}/test/cmake/native_arm64_ci_packaging_contract.py")
set_tests_properties(native_arm64_ci_packaging_contract PROPERTIES
	LABELS "ci-contract")

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
set(_junit_env "YONA_DOCTEST_JUNIT=${CMAKE_CURRENT_BINARY_DIR}/doctest-results.xml")
set(_ctest_env "${_ctest_base_env};YONA_COMPILE_GPU_VULKAN=0;${_junit_env}")

set_tests_properties(doctest_tests PROPERTIES
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	LABELS "unit"
	ENVIRONMENT "${_ctest_env}"
)

# Independently addressable standard-library capability tiers. The complete
# doctest run still executes these fixtures; the labeled slices let capable CI
# and local environments target the public network/GPU contracts directly.
add_test(NAME doctest_stdlib_network
	COMMAND tests -tc=Stdlib?network?conformance?fixtures)
add_test(NAME doctest_stdlib_gpu
	COMMAND tests -tc=Stdlib?GPU?conformance?fixtures)
set(_stdlib_network_env
	"${_ctest_base_env};YONA_COMPILE_GPU_VULKAN=0;YONA_DOCTEST_JUNIT=${CMAKE_CURRENT_BINARY_DIR}/doctest-stdlib-network-results.xml")
set(_stdlib_gpu_env
	"${_ctest_base_env};YONA_COMPILE_GPU_VULKAN=0;YONA_DOCTEST_JUNIT=${CMAKE_CURRENT_BINARY_DIR}/doctest-stdlib-gpu-results.xml")
set_tests_properties(doctest_stdlib_network PROPERTIES
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	LABELS "unit;stdlib-network"
	ENVIRONMENT "${_stdlib_network_env}")
set_tests_properties(doctest_stdlib_gpu PROPERTIES
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	LABELS "unit;stdlib-gpu"
	ENVIRONMENT "${_stdlib_gpu_env}")

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

if(APPLE)
	set_tests_properties(doctest_stdlib_network PROPERTIES
		ENVIRONMENT "DYLD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{DYLD_LIBRARY_PATH};${_stdlib_network_env}")
	set_tests_properties(doctest_stdlib_gpu PROPERTIES
		ENVIRONMENT "DYLD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{DYLD_LIBRARY_PATH};${_stdlib_gpu_env}")
elseif(UNIX)
	set_tests_properties(doctest_stdlib_network PROPERTIES
		ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{LD_LIBRARY_PATH};${_stdlib_network_env}")
	set_tests_properties(doctest_stdlib_gpu PROPERTIES
		ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{LD_LIBRARY_PATH};${_stdlib_gpu_env}")
endif()

# Vulkan-only slice: same binary, no YONA_COMPILE_GPU_VULKAN=0 so local SDK matches the
# Vulkan-enabled objects; filters to gpu/vulkan doctest cases.
if(YONA_ENABLE_VULKAN)
	set(_junit_gpu_env "YONA_DOCTEST_JUNIT=${CMAKE_CURRENT_BINARY_DIR}/doctest-gpu-results.xml")
	add_test(NAME doctest_gpu_vulkan COMMAND tests -tc=*gpu?vulkan*)
	set_tests_properties(doctest_gpu_vulkan PROPERTIES
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		LABELS "vulkan;gpu"
		ENVIRONMENT "${_ctest_base_env};${_junit_gpu_env}"
	)
	if(APPLE)
		set_tests_properties(doctest_gpu_vulkan PROPERTIES
			ENVIRONMENT "DYLD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{DYLD_LIBRARY_PATH};${_ctest_base_env};${_junit_gpu_env}"
		)
	elseif(UNIX)
		set_tests_properties(doctest_gpu_vulkan PROPERTIES
			ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{LD_LIBRARY_PATH};${_ctest_base_env};${_junit_gpu_env}"
		)
	endif()
endif()

# Enable doctest discovery for Visual Studio Test Explorer
set_target_properties(tests PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	VS_DEBUGGER_COMMAND_ARGUMENTS "--reporters=xml"
)
add_test(NAME smoke_yls_yona
	COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ci/smoke-yls-yona.py"
		"${CMAKE_CURRENT_BINARY_DIR}/yls-yona${CMAKE_EXECUTABLE_SUFFIX}")
set_tests_properties(smoke_yls_yona PROPERTIES
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	LABELS "unit"
	ENVIRONMENT "${_ctest_env}"
)

endif() # BUILD_TESTING

# Removed temporary test executables - tests are now using doctest
