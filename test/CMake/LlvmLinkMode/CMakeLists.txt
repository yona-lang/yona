cmake_minimum_required(VERSION 3.15)

project(YonaLlvmLinkModeContract NONE)

if(NOT YONA_SOURCE_DIR)
	message(FATAL_ERROR "YONA_SOURCE_DIR is required")
endif()

add_library(LLVM INTERFACE IMPORTED)
set(LLVM_LINK_LLVM_DYLIB ON)
set(LLVM_DYLIB_COMPONENTS all)

function(llvm_map_components_to_libnames OutputVariable)
	set(_mapped)
	foreach(_component IN LISTS ARGN)
		list(APPEND _mapped "LLVM${_component}")
	endforeach()
	set(${OutputVariable} ${_mapped} PARENT_SCOPE)
endfunction()

include("${YONA_SOURCE_DIR}/cmake/YonaLlvmLinkMode.cmake")
yona_map_llvm_components(_selected Support Core nativecodegen)

if(NOT "${_selected}" STREQUAL "LLVM")
	message(FATAL_ERROR
		"expected the LLVM shared aggregate, got '${_selected}'")
endif()

set(LLVM_DYLIB_COMPONENTS Support Core)
yona_map_llvm_components(_subset_selected Support Core nativecodegen)
if(NOT "${_subset_selected}" STREQUAL "LLVM;LLVMnativecodegen")
	message(FATAL_ERROR
		"expected shared LLVM plus the component outside the subset dylib, got '${_subset_selected}'")
endif()

# Public compiler headers instantiate LLVM's ABI compatibility check in each
# executable that includes them.  Shared-library consumers therefore need a
# direct LLVM link instead of relying on libyona_lib's transitive dependency.
file(READ "${YONA_SOURCE_DIR}/CMakeLists.txt" _yona_root_cmake)
foreach(_tool IN ITEMS yonac yona-repl)
	string(REGEX MATCH
		"target_link_libraries\\(${_tool} PRIVATE[ \t\r\n]+yona_lib[ \t\r\n]+\\$\\{llvm_libs\\}[^\\)]*\\)"
		_yona_tool_llvm_link "${_yona_root_cmake}")
	if(NOT _yona_tool_llvm_link)
		message(FATAL_ERROR
			"${_tool} must link llvm_libs directly when consuming yona_lib")
	endif()
endforeach()
