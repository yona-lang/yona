# CMakeList.txt : CMake project for yona, include source and define
# project specific logic here.
#
# PCRE2 10.47, our pinned public Std\Regex fallback, requires CMake 3.15.
# Keep the project requirement aligned with the dependency used by the default
# configuration instead of advertising a version that cannot configure it.
cmake_minimum_required(VERSION 3.15..4.0)

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
	"${CMAKE_CURRENT_SOURCE_DIR}/include/yona/Support/Version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/yona/Support/Version.h"
	@ONLY
)

include(FetchContent)

# Dependencies are implementation details of the Yona build. Their install
# rules must never leak into the Yona package; only the explicitly declared
# Yona install rules below define the distribution surface.
macro(yona_fetchcontent_make_available Dependency)
	set(_yona_saved_skip_install_rules "${CMAKE_SKIP_INSTALL_RULES}")
	set(CMAKE_SKIP_INSTALL_RULES ON)
	FetchContent_MakeAvailable(${Dependency})
	# CMake still emits a parent-level include for every FetchContent binary
	# directory.  With install rules disabled it does not create that child
	# script, so provide an explicit no-op instead of allowing dependency install
	# rules to become part of Yona's package.
	if(DEFINED ${Dependency}_BINARY_DIR)
		file(WRITE "${${Dependency}_BINARY_DIR}/cmake_install.cmake"
			"# ${Dependency} is a build-only dependency; it has no install surface.\n")
	endif()
	set(CMAKE_SKIP_INSTALL_RULES "${_yona_saved_skip_install_rules}")
	unset(_yona_saved_skip_install_rules)
endmacro()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaHomebrew.cmake")
yona_homebrew_prefix(YONA_HOMEBREW_PREFIX)
if(YONA_HOMEBREW_PREFIX)
	list(PREPEND CMAKE_PREFIX_PATH "${YONA_HOMEBREW_PREFIX}")
endif()

# Prefer distro / Homebrew packages. FetchContent is a last resort for the
# project's pinned third-party build dependencies (including the Windows LLVM
# package's Zlib/zstd prerequisites). Packaging passes -DYONA_FETCH_DEPS=OFF.
option(YONA_FETCH_DEPS "Fetch pinned dependencies when no system CMake package is found" ON)
# CI and release verification use this to exercise the same self-contained
# PCRE2 path required by Windows without perturbing other package discovery.
option(YONA_FORCE_BUNDLED_PCRE2 "Use pinned PCRE2 instead of a system package" OFF)
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
	yona_fetchcontent_make_available(cli11)
else()
	message(FATAL_ERROR
		"CLI11 not found. Install libcli11-dev, cli11-devel, or Homebrew cli11 "
		"(or pass -DYONA_FETCH_DEPS=ON to clone it).")
endif()

# PCRE2 backs the public Std\Regex module. Prefer the platform package where
# it is available, but make Windows releases self-contained with one pinned
# static archive. A public .yonai must never outlive the runtime symbols it
# declares; this deliberately does not use vcpkg.
set(YONA_PCRE2_TARGET "")
set(YONA_PCRE2_INCLUDE_DIRS "")
set(YONA_PCRE2_CFLAGS "")
set(YONA_PCRE2_BUNDLED_STATIC OFF)
if(NOT YONA_FORCE_BUNDLED_PCRE2)
    # PCRE2's own config package exposes PCRE2::8BIT.  Prefer it on every
    # platform so a manually installed Windows package is a supported normal
    # CMake dependency too; requesting static linkage makes its ABI safe to
    # copy next to the compiler's runtime sysroot.
    set(PCRE2_USE_STATIC_LIBS ON)
    find_package(PCRE2 QUIET CONFIG COMPONENTS 8BIT)
    if(TARGET PCRE2::8BIT)
        get_target_property(_yona_pcre2_type PCRE2::8BIT TYPE)
        if(WIN32 AND NOT _yona_pcre2_type STREQUAL "STATIC_LIBRARY")
            message(STATUS
                "Found shared PCRE2 CMake target; using the pinned static fallback for a self-contained Windows runtime")
        else()
            set(YONA_PCRE2_TARGET PCRE2::8BIT)
            get_target_property(YONA_PCRE2_INCLUDE_DIRS PCRE2::8BIT
                INTERFACE_INCLUDE_DIRECTORIES)
            if(YONA_PCRE2_INCLUDE_DIRS STREQUAL "YONA_PCRE2_INCLUDE_DIRS-NOTFOUND")
                set(YONA_PCRE2_INCLUDE_DIRS "")
            endif()
            get_target_property(_yona_pcre2_defs PCRE2::8BIT
                INTERFACE_COMPILE_DEFINITIONS)
            if(_yona_pcre2_defs STREQUAL "_yona_pcre2_defs-NOTFOUND")
                set(_yona_pcre2_defs "")
            endif()
            foreach(_yona_pcre2_include IN LISTS YONA_PCRE2_INCLUDE_DIRS)
                list(APPEND YONA_PCRE2_CFLAGS "-I${_yona_pcre2_include}")
            endforeach()
            foreach(_yona_pcre2_def IN LISTS _yona_pcre2_defs)
                # Config packages may expose generator expressions that make
                # sense only for CMake targets.  The packaged runtime is
                # compiled directly, so forward only literal C preprocessor
                # definitions (notably PCRE2_STATIC).
                if(_yona_pcre2_def MATCHES "^[A-Za-z_][A-Za-z0-9_]*(=.*)?$")
                    list(APPEND YONA_PCRE2_CFLAGS "-D${_yona_pcre2_def}")
                endif()
            endforeach()
            if(_yona_pcre2_type STREQUAL "STATIC_LIBRARY")
                set(YONA_PCRE2_BUNDLED_STATIC ON)
            endif()
            message(STATUS "Found CMake PCRE2 8-bit target")
        endif()
    endif()

    # pkg-config is the normal Unix discovery route.  Windows has no
    # dependency on it: its static CMake target/fallback is copied into the
    # runtime sysroot instead.
    if(NOT YONA_PCRE2_TARGET AND NOT WIN32)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(PCRE2 QUIET IMPORTED_TARGET libpcre2-8)
        endif()
        if(PCRE2_FOUND)
            set(YONA_PCRE2_TARGET PkgConfig::PCRE2)
            set(YONA_PCRE2_INCLUDE_DIRS ${PCRE2_INCLUDE_DIRS})
            set(YONA_PCRE2_CFLAGS ${PCRE2_CFLAGS})
            message(STATUS "Found system PCRE2: ${PCRE2_LIBRARIES}")
        endif()
    endif()
endif()
if(YONA_PCRE2_TARGET)
    set(PCRE2_FOUND TRUE)
elseif(YONA_FETCH_DEPS)
    message(STATUS "PCRE2 not found; fetching pinned PCRE2 10.47 for Std\\Regex")
    set(BUILD_STATIC_LIBS ON CACHE BOOL "Build static libraries." FORCE)
    set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "Build the PCRE2 8-bit library." FORCE)
    set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "Disable the PCRE2 16-bit library." FORCE)
    set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "Disable the PCRE2 32-bit library." FORCE)
    set(PCRE2_STATIC_PIC ON CACHE BOOL "Build the static PCRE2 archive with PIC." FORCE)
    set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "Do not build pcre2grep." FORCE)
    set(PCRE2_BUILD_TESTS OFF CACHE BOOL "Do not build upstream PCRE2 tests." FORCE)
    set(PCRE2_SUPPORT_JIT ON CACHE BOOL "Enable PCRE2 JIT support." FORCE)
    FetchContent_Declare(
        yona_pcre2
        URL https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.47/pcre2-10.47.tar.gz
        URL_HASH SHA256=c08ae2388ef333e8403e670ad70c0a11f1eed021fd88308d7e02f596fcd9dc16
    )
    yona_fetchcontent_make_available(yona_pcre2)
    if(NOT TARGET pcre2-8-static)
        message(FATAL_ERROR "Fetched PCRE2 10.47 did not define pcre2-8-static.")
    endif()
    set(PCRE2_FOUND TRUE)
    set(YONA_PCRE2_TARGET pcre2-8-static)
    if(WIN32)
        # Generated Yona programs use Clang's static UCRT default. The runtime
        # dependency shipped beside yonac must use the same CRT in every CMake
        # configuration; a Debug-DLL PCRE archive cannot be linked by users.
        set_property(TARGET pcre2-8-static PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
    set(YONA_PCRE2_INCLUDE_DIRS "${yona_pcre2_BINARY_DIR}/interface")
    # PCRE2's public static target propagates this definition to normal CMake
    # consumers. Carry it explicitly for generated-program tests as well;
    # otherwise Windows headers declare `__imp_` imports that cannot be
    # satisfied by the bundled static archive.
    set(YONA_PCRE2_CFLAGS
        "-I${yona_pcre2_BINARY_DIR}/interface"
        "-DPCRE2_STATIC")
    set(YONA_PCRE2_BUNDLED_STATIC ON)
else()
    message(FATAL_ERROR
        "PCRE2 is required for the public Std\\Regex module. Install a normal PCRE2 package, "
        "or configure with -DYONA_FETCH_DEPS=ON to build pinned PCRE2 10.47 (never vcpkg).")
endif()

# Vulkan for Std\Gpu — gpu_stub lazy device + f64 fence path (Unix + Windows when found)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Found Vulkan — Std\\Gpu gpu_stub will link the loader on this platform")
    if(Vulkan_INCLUDE_DIRS)
        list(GET Vulkan_INCLUDE_DIRS 0 YONA_VK_HEADER_DIR)
    else()
        set(YONA_VK_HEADER_DIR "${Vulkan_INCLUDE_DIR}")
    endif()
else()
    message(STATUS "Vulkan not found — Std\\Gpu uses zeroed stub counters only")
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
			yona_fetchcontent_make_available(yona_zlib)
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
			yona_fetchcontent_make_available(yona_zstd)
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
set(YONA_CFG_VULKAN_LIBRARY_DEFINED 0)
set(YONA_CFG_VULKAN_LIBRARY "")
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
	if(NOT WIN32)
		file(TO_CMAKE_PATH "${_yona_vk_lib_file}" _yona_vk_lib_norm)
		string(REPLACE "\\" "/" YONA_CFG_VULKAN_LIBRARY "${_yona_vk_lib_norm}")
		set(YONA_CFG_VULKAN_LIBRARY_DEFINED 1)
	endif()
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
file(MAKE_DIRECTORY
  "${CMAKE_CURRENT_BINARY_DIR}/include/yona/Runtime/Generated")
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/VulkanLinkConfig.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/yona/Runtime/Generated/VulkanLinkConfig.h"
	@ONLY)

# Verify the compiler backend matches the supported LLVM baseline.
if(${LLVM_VERSION_MAJOR} LESS 22)
  message(FATAL_ERROR "LLVM version must be 22 or higher, found ${LLVM_VERSION_MAJOR}")
endif()

# Setup LLVM - use SYSTEM to suppress warnings from LLVM headers
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Find the libraries that correspond to the LLVM components that we wish to use
llvm_map_components_to_libnames(llvm_libs Support Core IRReader Passes Target native nativecodegen)

find_package(Threads REQUIRED)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaComponents.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaFuzzing.cmake")
include(GNUInstallDirs)

set(YONA_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")

install(TARGETS yona_runtime
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/yona/runtime"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/yona/runtime"
	RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}/yona/runtime")
install(TARGETS yona_lib
	EXPORT YonaTargets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	RUNTIME DESTINATION bin
	INCLUDES DESTINATION include)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/yona/"
	DESTINATION "include/yona"
	FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/yona/Support/Version.h"
	DESTINATION "include/yona/Support")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib/"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/yona/lib"
	FILES_MATCHING
	PATTERN "*.yona"
	PATTERN "*.yonai")
install(EXPORT YonaTargets
	FILE YonaTargets.cmake
	NAMESPACE Yona::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Yona")
include(CMakePackageConfigHelpers)
configure_package_config_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaConfig.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/YonaConfig.cmake"
	INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Yona"
	NO_SET_AND_CHECK_MACRO)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/YonaConfig.cmake"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Yona")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaPackageTools.cmake"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Yona"
	RENAME "YonaTools.cmake")

add_executable(yls "${PROJECT_SOURCE_DIR}/cli/Yls.cpp")
target_sources(yls PRIVATE $<TARGET_OBJECTS:yona_lsp>)
target_include_directories(yls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yls PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
if(YONA_LINK_STATIC_CLI)
	target_link_libraries(yls PRIVATE yona_lib_static)
	target_compile_definitions(yls PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yls PRIVATE yona_lib)
endif()

# Compiler executable
add_executable(yonac "${PROJECT_SOURCE_DIR}/cli/Main.cpp")
target_include_directories(yonac PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yonac PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
if(YONA_LINK_STATIC_CLI)
	target_link_libraries(yonac PRIVATE yona_lib_static CLI11::CLI11)
	target_compile_definitions(yonac PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yonac PRIVATE yona_lib CLI11::CLI11)
endif()
if(Vulkan_FOUND AND NOT WIN32)
	# cli/Main.cpp: yonac-linked exe pulls vk* from gpu_stub → link libvulkan.
	target_compile_definitions(yonac PRIVATE YONAC_EXE_LINK_POSIX_VULKAN=1)
endif()
if(PCRE2_FOUND)
	target_compile_definitions(yonac PRIVATE YONAC_EXE_LINK_PCRE2=1)
endif()

# Copy a bundled PCRE2 archive next to the canonical runtime archive. `yonac`
# links user programs outside CMake and therefore cannot consume an imported
# CMake target after installation.
set(YONA_PCRE2_RUNTIME_ARCHIVE "")
if(YONA_PCRE2_BUNDLED_STATIC)
	set(YONA_PCRE2_RUNTIME_ARCHIVE
		"${YONA_RUNTIME_ARTIFACT_DIR}/yona_pcre2${CMAKE_STATIC_LIBRARY_SUFFIX}")
	add_custom_command(
		OUTPUT "${YONA_PCRE2_RUNTIME_ARCHIVE}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${YONA_RUNTIME_ARTIFACT_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:${YONA_PCRE2_TARGET}>"
			"${YONA_PCRE2_RUNTIME_ARCHIVE}"
		DEPENDS ${YONA_PCRE2_TARGET}
		COMMENT "Bundling pinned PCRE2 runtime archive"
		VERBATIM
	)
	add_custom_target(yona_runtime_pcre2 DEPENDS "${YONA_PCRE2_RUNTIME_ARCHIVE}")
	add_dependencies(yona_runtime yona_runtime_pcre2)
	install(FILES "${YONA_PCRE2_RUNTIME_ARCHIVE}"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/yona/runtime")
endif()
add_dependencies(yonac yona_runtime)

# REPL executable (invoked by the Yona-written `yona` runner)
add_executable(yona-repl "${PROJECT_SOURCE_DIR}/repl/Main.cpp")
target_include_directories(yona-repl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(yona-repl PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
if(YONA_LINK_STATIC_CLI)
	target_link_libraries(yona-repl PRIVATE yona_lib_static)
	target_compile_definitions(yona-repl PRIVATE YONA_STATIC_BUILD)
else()
	target_link_libraries(yona-repl PRIVATE yona_lib)
endif()
add_dependencies(yona-repl yona_runtime)

if(NOT YONA_LINK_STATIC_CLI AND UNIX)
	if(APPLE)
		set(_yona_cli_install_rpath "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
	else()
		set(_yona_cli_install_rpath "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
	endif()
	set_target_properties(yonac yona-repl yls PROPERTIES
		INSTALL_RPATH "${_yona_cli_install_rpath}")
endif()

install(TARGETS yonac yona-repl yls
	RUNTIME DESTINATION bin)

# One local-only quality entry point backs both developer use and CI. These
# targets never install tools or contact analysis services.
find_program(YONA_PYTHON_EXECUTABLE NAMES python3 python)
if(NOT YONA_PYTHON_EXECUTABLE)
	message(FATAL_ERROR "Python 3 is required for repository quality targets")
endif()
if(UNIX)
	find_program(YONA_TEST_DIRNAME_EXECUTABLE NAMES dirname)
	if(NOT YONA_TEST_DIRNAME_EXECUTABLE)
		message(FATAL_ERROR "dirname is required to test repository shell scripts")
	endif()
endif()
foreach(_yona_quality_command IN ITEMS
	format
	format-check
	tidy
	analyze
	sanitize
	coverage
	fuzz-check
	architecture
	generated
	hygiene
	headers
	naming
	symbols
	vulkan
	yona-style
	quality)
	if(TARGET ${_yona_quality_command})
		continue()
	endif()
	add_custom_target(${_yona_quality_command}
		COMMAND "${YONA_PYTHON_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/scripts/quality.py"
			${_yona_quality_command}
			--build-dir "${CMAKE_CURRENT_BINARY_DIR}"
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		USES_TERMINAL
		VERBATIM)
endforeach()

# Tests link generated programs against Prelude's exported constructors and
# functions. Build that object deterministically in the binary tree; never
# depend on a developer-local, ignored lib/Prelude.o.
set(YONA_TEST_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_artifacts")
set(YONA_TEST_PRELUDE_OBJECT "${YONA_TEST_ARTIFACT_DIR}/Prelude${CMAKE_C_OUTPUT_EXTENSION}")
set(YONA_TEST_PRELUDE_INTERFACE "${YONA_TEST_ARTIFACT_DIR}/Prelude.yonai")
set(YONA_TEST_PCRE2_INCLUDE_DIR "")
if(YONA_PCRE2_INCLUDE_DIRS)
	list(GET YONA_PCRE2_INCLUDE_DIRS 0 YONA_TEST_PCRE2_INCLUDE_DIR)
endif()
set(YONA_TEST_PCRE2_ARCHIVE "${YONA_PCRE2_RUNTIME_ARCHIVE}")
set(YONA_TEST_PCRE2_CFLAGS "${YONA_PCRE2_CFLAGS}")
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
# Generated for repository-aware test helpers.
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/test/Support/YonaTestConfig.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/test_config/YonaTestConfig.h"
	@ONLY)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaTools.cmake")
add_subdirectory(tools)

include(CTest)

if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_executable(runtime_io_fallback_leak_probe
		"${PROJECT_SOURCE_DIR}/test/Runtime/IoFallbackLeakProbe.c")
	target_include_directories(runtime_io_fallback_leak_probe PRIVATE
		"${PROJECT_SOURCE_DIR}/include")
	target_link_libraries(runtime_io_fallback_leak_probe PRIVATE yona_runtime)

	add_test(NAME runtime_io_submission_cleanup_string
		COMMAND "${CMAKE_COMMAND}" -E env YONA_ALLOC_STATS=1
			$<TARGET_FILE:runtime_io_fallback_leak_probe>)
	set_tests_properties(runtime_io_submission_cleanup_string PROPERTIES
		LABELS "runtime;linux;ownership"
		PASS_REGULAR_EXPRESSION "tag=STRING allocs=2 frees=2 leaked=0")
	add_test(NAME runtime_io_submission_cleanup_bytes
		COMMAND "${CMAKE_COMMAND}" -E env YONA_ALLOC_STATS=1
			$<TARGET_FILE:runtime_io_fallback_leak_probe>)
	set_tests_properties(runtime_io_submission_cleanup_bytes PROPERTIES
		LABELS "runtime;linux;ownership"
		PASS_REGULAR_EXPRESSION "tag=BYTEARR allocs=4 frees=4 leaked=0")

	find_program(YONA_VALGRIND_EXECUTABLE NAMES valgrind)
	if(YONA_VALGRIND_EXECUTABLE)
		add_test(NAME runtime_io_fallback_leak
			COMMAND "${YONA_VALGRIND_EXECUTABLE}"
				--leak-check=full
				--show-leak-kinds=definite
				--errors-for-leak-kinds=definite
				--error-exitcode=99
				$<TARGET_FILE:runtime_io_fallback_leak_probe>)
		set_tests_properties(runtime_io_fallback_leak PROPERTIES
			LABELS "runtime;linux;valgrind")
	endif()
endif()

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
	yona_fetchcontent_make_available(doctest)
else()
	message(FATAL_ERROR
		"doctest not found. Install doctest-dev, doctest-devel, or Homebrew doctest "
		"(or pass -DYONA_FETCH_DEPS=ON to clone it).")
endif()

add_executable(tests
	"${PROJECT_SOURCE_DIR}/test/Codegen/AcceleratorLoweringTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Codegen/AdtTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Codegen/CodegenTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Codegen/PatternOwnershipTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Codegen/RepeatedOwnershipTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Codegen/TypedIrLoweringTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Support/DoctestMain.cpp"
	"${PROJECT_SOURCE_DIR}/test/Syntax/AstTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Syntax/FormatParserTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/GpuStubTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/GpuVulkanDeviceTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/GpuVulkanMapAddTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/HamtRcTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Syntax/ImportedConstructorParserTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/IoReadExactTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/JsonAbiTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Syntax/LexerTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Toolchain/LinkerPlanTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/TypedIr/TypedIrTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Lsp/LspTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Lsp/ProtocolTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Model/ModuleIdentityTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/NetRuntimeTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Interface/InterfaceFileTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Interface/PreludeInterfaceTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/RuntimeGuardsTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/TaskOwnershipTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Support/SanityTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Support/SemanticSetupTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/SequenceOperationsTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Support/ProcessTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Support/SourceManagerTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Support/TerminalTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Support/ThreadPoolTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Syntax/YonaStyleTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Semantics/TerminationAnalysisTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Semantics/InterfaceCatalogTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Semantics/SemanticModelTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Semantics/TraitTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Syntax/TryCatchParserTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Semantics/TypeCheckerTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/TypedCore/AbiTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Runtime/Utf16AbiTest.cpp"
	"${PROJECT_SOURCE_DIR}/test/Toolchain/YonaScriptTest.cpp"
	$<TARGET_OBJECTS:yona_lsp>
)
add_dependencies(tests yona_test_prelude_object yona_runtime yls-yona)

target_link_libraries(tests PRIVATE
	doctest::doctest
	yona_lib_static
	yona_runtime
)

# Define YONA_STATIC_BUILD for tests to avoid DLL export/import issues
target_compile_definitions(tests PRIVATE
	YONA_STATIC_BUILD
	YONA_TEST_CMAKE_EXECUTABLE="${CMAKE_COMMAND}"
	YONA_TEST_RUNTIME_ARCHIVE="$<TARGET_FILE:yona_runtime>"
)
if(YONA_ENABLE_VULKAN)
	target_compile_definitions(tests PRIVATE YONA_GPU_VULKAN_ENABLED=1)
endif()

target_include_directories(tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/test
	${CMAKE_CURRENT_BINARY_DIR}/test_config)

# Register the test executable with CTest
add_test(NAME doctest_tests COMMAND tests)

add_test(NAME cmake_windows_llvm_prerequisites
	COMMAND "${CMAKE_COMMAND}"
		-S "${CMAKE_CURRENT_SOURCE_DIR}/test/CMake/WindowsLlvmPrerequisites"
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
		"${CMAKE_CURRENT_SOURCE_DIR}/test/CMake/native_arm64_ci_packaging_contract.py")
set_tests_properties(native_arm64_ci_packaging_contract PROPERTIES
	LABELS "ci-contract")

add_test(NAME installed_consumer_contract
	COMMAND "${CMAKE_COMMAND}"
		-DYONA_BUILD_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
		-DYONA_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
		-DYONA_INSTALL_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/test-install
		-DYONA_C_COMPILER=${CMAKE_C_COMPILER}
		-DYONA_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DYONA_GENERATOR=${CMAKE_GENERATOR}
		-DYONA_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
		-DYONA_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
		-DYONA_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-DYONA_BUILD_CONFIG=$<CONFIG>
		-P "${CMAKE_CURRENT_SOURCE_DIR}/test/CMake/InstalledConsumer/RunInstalledConsumer.cmake")
set_tests_properties(installed_consumer_contract PROPERTIES
	LABELS "install-contract")

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
set(_junit_env "YONA_DOCTEST_JUNIT=${CMAKE_CURRENT_BINARY_DIR}/doctest-results.xml")
set(_ctest_env "${_ctest_base_env};${_junit_env}")

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
	"${_ctest_base_env};YONA_DOCTEST_JUNIT=${CMAKE_CURRENT_BINARY_DIR}/doctest-stdlib-network-results.xml")
set(_stdlib_gpu_env
	"${_ctest_base_env};YONA_DOCTEST_JUNIT=${CMAKE_CURRENT_BINARY_DIR}/doctest-stdlib-gpu-results.xml")
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

# Vulkan-only slice for the configured Vulkan-enabled objects.
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
	COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ci/smoke_yls_yona.py"
		"${CMAKE_CURRENT_BINARY_DIR}/yls-yona${CMAKE_EXECUTABLE_SUFFIX}")
set_tests_properties(smoke_yls_yona PROPERTIES
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	LABELS "unit"
	ENVIRONMENT "${_ctest_env}"
)

endif() # BUILD_TESTING

# Removed temporary test executables - tests are now using doctest
