# Toolchain file for building with Homebrew LLVM on macOS

# Use libc++ from LLVM
set(CMAKE_CXX_FLAGS "-stdlib=libc++" CACHE STRING "")

# Link with LLVM's libc++
set(CMAKE_EXE_LINKER_FLAGS "-L/opt/homebrew/opt/llvm/lib -Wl,-rpath,/opt/homebrew/opt/llvm/lib" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS "-L/opt/homebrew/opt/llvm/lib -Wl,-rpath,/opt/homebrew/opt/llvm/lib" CACHE STRING "")

# Use CommandLineTools SDK instead of Xcode SDK to avoid header conflicts
set(CMAKE_OSX_SYSROOT "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk" CACHE PATH "")
# Match the active Command Line Tools SDK (MacOSX.sdk → 26+/27+).
# A stale 15.0 target made LLD warn on every Homebrew LLVM object (built for 26).
set(CMAKE_OSX_DEPLOYMENT_TARGET "" CACHE STRING "")
