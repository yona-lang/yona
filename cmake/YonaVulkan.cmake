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
		find_path(
			YONA_VULKAN_INCLUDE_DIR
			NAMES vulkan/vulkan.h
			HINTS
				"$ENV{VULKAN_SDK}/include"
				"$ENV{VULKAN_SDK}/Include"
				/usr/include
		)
		find_library(
			YONA_VULKAN_LIBRARY
			NAMES vulkan
			PATHS
				"$ENV{VULKAN_SDK}/lib"
				"$ENV{VULKAN_SDK}/lib32"
		)
		if(NOT YONA_VULKAN_LIBRARY)
			find_library(YONA_VULKAN_LIBRARY NAMES vulkan)
		endif()
	endif()

	if(NOT YONA_VULKAN_INCLUDE_DIR)
		message(
			FATAL_ERROR
			"YONA_ENABLE_VULKAN is ON but vulkan/vulkan.h was not found. Set VULKAN_SDK to the LunarG SDK root or install system Vulkan headers."
		)
	endif()
	if(NOT YONA_VULKAN_LIBRARY)
		message(
			FATAL_ERROR
			"YONA_ENABLE_VULKAN is ON but the Vulkan loader library was not found (Windows: vulkan-1; Unix: libvulkan). Check VULKAN_SDK."
		)
	endif()

	set(YONA_COMPILE_GPU_VULKAN 1)
	message(STATUS "YONA_ENABLE_VULKAN: ON (include: ${YONA_VULKAN_INCLUDE_DIR}, lib: ${YONA_VULKAN_LIBRARY})")
else()
	set(YONA_COMPILE_GPU_VULKAN 0)
	set(YONA_VULKAN_INCLUDE_DIR "")
	set(YONA_VULKAN_LIBRARY "")
endif()
