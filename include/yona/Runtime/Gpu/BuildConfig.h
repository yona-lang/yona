/*
 * Build-time switch for the Vulkan path in the runtime. CMake defines
 * YONA_GPU_VULKAN_ENABLED=1 on the GPU runtime component when configured with
 * -DYONA_ENABLE_VULKAN=ON. Other consumers default to the CPU-only path.
 */
#ifndef YONA_RUNTIME_GPU_BUILDCONFIG_H
#define YONA_RUNTIME_GPU_BUILDCONFIG_H

#ifndef YONA_GPU_VULKAN_ENABLED
#define YONA_GPU_VULKAN_ENABLED 0
#endif

#endif /* YONA_RUNTIME_GPU_BUILDCONFIG_H */
