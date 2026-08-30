# Toolchain file for building with LLVM on Windows

# Set LLVM installation path from cache or environment, falling back to the
# conventional official Windows installer location.
if(NOT DEFINED LLVM_INSTALL_PREFIX AND DEFINED ENV{LLVM_INSTALL_PREFIX})
    set(LLVM_INSTALL_PREFIX "$ENV{LLVM_INSTALL_PREFIX}")
endif()

if(NOT DEFINED LLVM_DIR AND DEFINED ENV{LLVM_DIR})
    set(LLVM_DIR "$ENV{LLVM_DIR}" CACHE PATH "Path to LLVMConfig.cmake")
endif()

if(NOT LLVM_INSTALL_PREFIX AND NOT LLVM_DIR)
    set(LLVM_INSTALL_PREFIX "C:/Program Files/LLVM" CACHE PATH "LLVM installation root")
    message(STATUS "LLVM location unset; defaulting LLVM_INSTALL_PREFIX to ${LLVM_INSTALL_PREFIX}")
endif()

if(LLVM_INSTALL_PREFIX)
    message(STATUS "Using LLVM toolchain for Windows from ${LLVM_INSTALL_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_PREFIX}/lib/cmake/llvm")
    list(APPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_PREFIX}/lib/cmake")
    if(NOT LLVM_DIR OR NOT EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
        set(LLVM_DIR "${LLVM_INSTALL_PREFIX}/lib/cmake/llvm" CACHE PATH "Path to LLVMConfig.cmake" FORCE)
        message(STATUS "Setting LLVM_DIR to ${LLVM_DIR}")
    endif()
else()
    message(STATUS "Using explicitly configured LLVM_DIR: ${LLVM_DIR}")
endif()

if(NOT EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
    message(FATAL_ERROR
        "A complete Windows LLVM development tree is required. Expected "
        "${LLVM_DIR}/LLVMConfig.cmake. LLVM_INSTALL_PREFIX must name the "
        "root of an official clang+llvm-*-<arch>-pc-windows-msvc archive, "
        "and LLVM_DIR must name its lib/cmake/llvm directory. Clang-only "
        "installations from Visual Studio or the standalone installer do not "
        "provide the LLVM CMake libraries Yona links against.")
endif()

# The Windows presets are Clang builds.  Keep an explicit CC/CXX or CMake
# compiler selection intact, but otherwise use the compiler bundled with the
# complete LLVM tree rather than letting CMake choose MSVC's cl.exe.
if(NOT DEFINED CMAKE_C_COMPILER AND EXISTS "${LLVM_INSTALL_PREFIX}/bin/clang.exe")
    set(CMAKE_C_COMPILER "${LLVM_INSTALL_PREFIX}/bin/clang.exe" CACHE FILEPATH
        "C compiler" FORCE)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER AND EXISTS "${LLVM_INSTALL_PREFIX}/bin/clang++.exe")
    set(CMAKE_CXX_COMPILER "${LLVM_INSTALL_PREFIX}/bin/clang++.exe" CACHE FILEPATH
        "C++ compiler" FORCE)
endif()
