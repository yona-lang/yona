# Toolchain file for building with Homebrew LLVM on macOS.
# Prefixes come from `brew --prefix` / `xcrun`; do not hardcode /opt/homebrew.

execute_process(
	COMMAND brew --prefix llvm
	OUTPUT_VARIABLE _YONA_BREW_LLVM
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)
if(_YONA_BREW_LLVM AND EXISTS "${_YONA_BREW_LLVM}/bin/clang")
	set(CMAKE_C_COMPILER "${_YONA_BREW_LLVM}/bin/clang" CACHE FILEPATH "")
	set(CMAKE_CXX_COMPILER "${_YONA_BREW_LLVM}/bin/clang++" CACHE FILEPATH "")
endif()
if(_YONA_BREW_LLVM AND EXISTS "${_YONA_BREW_LLVM}/lib")
	set(CMAKE_EXE_LINKER_FLAGS "-L${_YONA_BREW_LLVM}/lib -Wl,-rpath,${_YONA_BREW_LLVM}/lib" CACHE STRING "")
	set(CMAKE_SHARED_LINKER_FLAGS "-L${_YONA_BREW_LLVM}/lib -Wl,-rpath,${_YONA_BREW_LLVM}/lib" CACHE STRING "")
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
