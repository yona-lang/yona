# Toolchain file for building with Homebrew LLVM on macOS.
# Prefixes come from env, then `brew --prefix`; do not hardcode /opt/homebrew.

set(_yona_llvm_prefix "")
if(DEFINED ENV{LLVM_INSTALL_PREFIX} AND EXISTS "$ENV{LLVM_INSTALL_PREFIX}/bin/clang")
	set(_yona_llvm_prefix "$ENV{LLVM_INSTALL_PREFIX}")
endif()

if(NOT _yona_llvm_prefix)
	set(_yona_llvm_formulas)
	if(DEFINED ENV{LLVM_MAJOR} AND NOT "$ENV{LLVM_MAJOR}" STREQUAL "")
		list(APPEND _yona_llvm_formulas "llvm@$ENV{LLVM_MAJOR}")
	endif()
	list(APPEND _yona_llvm_formulas llvm)
	foreach(_yona_formula IN LISTS _yona_llvm_formulas)
		execute_process(
			COMMAND brew --prefix "${_yona_formula}"
			OUTPUT_VARIABLE _yona_brew_llvm
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET
		)
		if(_yona_brew_llvm AND EXISTS "${_yona_brew_llvm}/bin/clang")
			set(_yona_llvm_prefix "${_yona_brew_llvm}")
			break()
		endif()
	endforeach()
endif()

if(_yona_llvm_prefix)
	set(CMAKE_C_COMPILER "${_yona_llvm_prefix}/bin/clang" CACHE FILEPATH "")
	set(CMAKE_CXX_COMPILER "${_yona_llvm_prefix}/bin/clang++" CACHE FILEPATH "")
	if(EXISTS "${_yona_llvm_prefix}/lib")
		set(CMAKE_EXE_LINKER_FLAGS "-L${_yona_llvm_prefix}/lib -Wl,-rpath,${_yona_llvm_prefix}/lib" CACHE STRING "")
		set(CMAKE_SHARED_LINKER_FLAGS "-L${_yona_llvm_prefix}/lib -Wl,-rpath,${_yona_llvm_prefix}/lib" CACHE STRING "")
	endif()
	if(EXISTS "${_yona_llvm_prefix}/lib/cmake/llvm")
		set(LLVM_DIR "${_yona_llvm_prefix}/lib/cmake/llvm" CACHE PATH "LLVM CMake directory")
	endif()
endif()

set(CMAKE_CXX_FLAGS "-stdlib=libc++" CACHE STRING "")

execute_process(
	COMMAND xcrun --show-sdk-path
	OUTPUT_VARIABLE _YONA_OSX_SYSROOT
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)
if(_YONA_OSX_SYSROOT)
	set(CMAKE_OSX_SYSROOT "${_YONA_OSX_SYSROOT}" CACHE PATH "")
endif()
# Match the active Command Line Tools / Xcode SDK.
# A stale 15.0 target made LLD warn on every Homebrew LLVM object (built for 26).
set(CMAKE_OSX_DEPLOYMENT_TARGET "" CACHE STRING "")
