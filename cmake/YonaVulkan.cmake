# Vulkan loader + headers (Option A: find_path / find_library, link-time).
# Enable with: cmake -DYONA_ENABLE_VULKAN=ON (expects VULKAN_SDK or system paths).

set(YONA_ENABLE_VULKAN_DESC
	"Build runtime with Khronos vulkan.h: optional device init uses runtime dlopen/LoadLibrary (no app link to the loader)"
)
option(YONA_ENABLE_VULKAN "${YONA_ENABLE_VULKAN_DESC}" OFF)

if(YONA_ENABLE_VULKAN)
	if(WIN32)
		find_path(
			YONA_VULKAN_INCLUDE_DIR
			NAMES vulkan/vulkan.h
			HINTS
				"$ENV{VULKAN_SDK}/Include"
				"$ENV{VULKAN_SDK}/include"
			NO_CMAKE_PATH
			NO_CMAKE_ENVIRONMENT_PATH
		)
		find_library(
			YONA_VULKAN_LIBRARY
			NAMES vulkan-1
			PATHS
				"$ENV{VULKAN_SDK}/Lib"
				"$ENV{VULKAN_SDK}/Lib32"
			NO_DEFAULT_PATH
		)
		# Non-SDK side-by-side (rare for development):
		if(NOT YONA_VULKAN_LIBRARY)
			find_library(YONA_VULKAN_LIBRARY NAMES vulkan-1 vulkan)
		endif()
	else()
		set(_yona_vk_inc_hints
			"$ENV{VULKAN_SDK}/include"
			"$ENV{VULKAN_SDK}/Include"
		)
		set(_yona_vk_lib_hints
			"$ENV{VULKAN_SDK}/lib"
			"$ENV{VULKAN_SDK}/lib32"
		)
		if(NOT YONA_HOMEBREW_PREFIX)
			include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/YonaHomebrew.cmake")
			yona_homebrew_prefix(YONA_HOMEBREW_PREFIX)
		endif()
		if(YONA_HOMEBREW_PREFIX)
			list(APPEND _yona_vk_inc_hints "${YONA_HOMEBREW_PREFIX}/include")
			list(APPEND _yona_vk_lib_hints "${YONA_HOMEBREW_PREFIX}/lib")
		endif()
		find_path(
			YONA_VULKAN_INCLUDE_DIR
			NAMES vulkan/vulkan.h
			HINTS ${_yona_vk_inc_hints}
		)
		# MoltenVK can be used as the ICD via the Vulkan loader, or linked
		# directly as libMoltenVK when no libvulkan dylib is installed.
		find_library(
			YONA_VULKAN_LIBRARY
			NAMES vulkan MoltenVK
			HINTS ${_yona_vk_lib_hints}
		)
		if(NOT YONA_VULKAN_LIBRARY)
			find_library(YONA_VULKAN_LIBRARY NAMES vulkan MoltenVK)
		endif()
		if(NOT YONA_VULKAN_INCLUDE_DIR AND Vulkan_INCLUDE_DIR)
			set(YONA_VULKAN_INCLUDE_DIR "${Vulkan_INCLUDE_DIR}")
		endif()
		if(NOT YONA_VULKAN_LIBRARY AND Vulkan_LIBRARIES)
			list(GET Vulkan_LIBRARIES 0 YONA_VULKAN_LIBRARY)
		endif()
	endif()

	if(NOT YONA_VULKAN_INCLUDE_DIR)
		message(
			FATAL_ERROR
			"YONA_ENABLE_VULKAN is ON but vulkan/vulkan.h was not found. Set VULKAN_SDK, or on macOS: brew install vulkan-headers molten-vk vulkan-loader (CMake uses HOMEBREW_PREFIX or `brew --prefix`)."
		)
	endif()
	if(NOT YONA_VULKAN_LIBRARY)
		message(
			FATAL_ERROR
			"YONA_ENABLE_VULKAN is ON but the Vulkan loader library was not found (Windows: vulkan-1; Unix: libvulkan; macOS: libvulkan or libMoltenVK). Check VULKAN_SDK / Homebrew."
		)
	endif()

	message(STATUS "YONA_ENABLE_VULKAN: ON (include: ${YONA_VULKAN_INCLUDE_DIR}, lib: ${YONA_VULKAN_LIBRARY})")
else()
	set(YONA_VULKAN_INCLUDE_DIR "")
	set(YONA_VULKAN_LIBRARY "")
endif()
