# Toolchain file for building with LLVM on Windows

# Set LLVM installation path from cache or environment. Do not guess a machine
# prefix; find_package(LLVM) uses CMAKE_PREFIX_PATH / LLVM_DIR otherwise.
if(NOT DEFINED LLVM_INSTALL_PREFIX AND DEFINED ENV{LLVM_INSTALL_PREFIX})
    set(LLVM_INSTALL_PREFIX "$ENV{LLVM_INSTALL_PREFIX}")
endif()

if(LLVM_INSTALL_PREFIX)
    message(STATUS "Using LLVM toolchain for Windows from ${LLVM_INSTALL_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_PREFIX}/lib/cmake/llvm")
    list(APPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_PREFIX}/lib/cmake")
    if(EXISTS "${LLVM_INSTALL_PREFIX}/lib/cmake/llvm/LLVMConfig.cmake")
        set(LLVM_DIR "${LLVM_INSTALL_PREFIX}/lib/cmake/llvm" CACHE PATH "Path to LLVMConfig.cmake" FORCE)
        message(STATUS "Setting LLVM_DIR to ${LLVM_DIR}")
    endif()
else()
    message(STATUS "LLVM_INSTALL_PREFIX unset; find_package(LLVM) will search CMAKE_PREFIX_PATH / LLVM_DIR")
endif()
