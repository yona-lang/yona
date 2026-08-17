# Toolchain file for building with LLVM/Clang on Linux.
# Prefer CI / user env (LLVM_INSTALL_PREFIX, CC, CXX), then llvm-config on PATH.

if(DEFINED ENV{CC} AND NOT "$ENV{CC}" STREQUAL "")
	set(CMAKE_C_COMPILER "$ENV{CC}" CACHE FILEPATH "C compiler")
else()
	set(CMAKE_C_COMPILER "clang" CACHE FILEPATH "C compiler")
endif()
if(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
	set(CMAKE_CXX_COMPILER "$ENV{CXX}" CACHE FILEPATH "C++ compiler")
else()
	set(CMAKE_CXX_COMPILER "clang++" CACHE FILEPATH "C++ compiler")
endif()

set(_yona_llvm_config "")
if(DEFINED ENV{LLVM_INSTALL_PREFIX} AND EXISTS "$ENV{LLVM_INSTALL_PREFIX}/bin/llvm-config")
	set(_yona_llvm_config "$ENV{LLVM_INSTALL_PREFIX}/bin/llvm-config")
endif()
if(NOT _yona_llvm_config)
	find_program(_yona_llvm_config llvm-config)
endif()
if(_yona_llvm_config)
	execute_process(
		COMMAND "${_yona_llvm_config}" --cmakedir
		OUTPUT_VARIABLE _LLVM_CMAKE_DIR
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	if(_LLVM_CMAKE_DIR)
		set(LLVM_DIR "${_LLVM_CMAKE_DIR}" CACHE PATH "LLVM CMake directory")
	endif()
endif()

set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
set(CMAKE_MODULE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
