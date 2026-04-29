/*
 * Build-time switch for the Vulkan path in the runtime. When the compiler
 * command line does not set YONA_COMPILE_GPU_VULKAN, it defaults to off (CPU
 * path only). CMake with -DYONA_ENABLE_VULKAN=ON sets -DYONA_COMPILE_GPU_VULKAN=1
 * and adds the Vulkan include path for the Khronos vulkan.h when enabled.
 */
#ifndef YONA_RUNTIME_GPU_BUILD_CONFIG_H
#define YONA_RUNTIME_GPU_BUILD_CONFIG_H

#if defined(YONA_COMPILE_GPU_VULKAN) && (YONA_COMPILE_GPU_VULKAN + 0) == 1
#define YONA_GPU_VULKAN_ENABLED 1
#else
#define YONA_GPU_VULKAN_ENABLED 0
#endif

#endif /* YONA_RUNTIME_GPU_BUILD_CONFIG_H */
