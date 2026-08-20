# Compile a Yona expression program to a native executable using the
# just-built yonac. Output sits in CMAKE_BINARY_DIR next to yonac.
#
#   yona_add_executable(<name> SOURCE <main.yona> [OUTPUT_NAME <name>])
#
function(yona_add_executable name)
	cmake_parse_arguments(YAE "" "SOURCE;OUTPUT_NAME" "" ${ARGN})
	if(NOT YAE_SOURCE)
		message(FATAL_ERROR "yona_add_executable(${name}) requires SOURCE <file.yona>")
	endif()
	if(NOT YAE_OUTPUT_NAME)
		set(YAE_OUTPUT_NAME "${name}")
	endif()
	if(IS_ABSOLUTE "${YAE_SOURCE}")
		set(_src "${YAE_SOURCE}")
	else()
		set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${YAE_SOURCE}")
	endif()
	set(_out "${CMAKE_BINARY_DIR}/${YAE_OUTPUT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
	set(_cc "$ENV{YONAC_CC}")
	if(NOT _cc)
		set(_cc "${CMAKE_C_COMPILER}")
	endif()
	add_custom_command(
		OUTPUT "${_out}"
		COMMAND ${CMAKE_COMMAND} -E env
			"YONAC_CC=${_cc}"
			$<TARGET_FILE:yonac>
			--sysroot "${CMAKE_BINARY_DIR}"
			-I "${CMAKE_SOURCE_DIR}/lib"
			-o "${_out}"
			"${_src}"
		DEPENDS yonac yona_runtime_objects "${_src}"
			"${CMAKE_SOURCE_DIR}/lib/Std/Process.yonai"
			"${CMAKE_SOURCE_DIR}/lib/Std/IO.yonai"
			"${CMAKE_SOURCE_DIR}/lib/Std/File.yonai"
			"${CMAKE_SOURCE_DIR}/lib/Std/Path.yonai"
			"${CMAKE_SOURCE_DIR}/lib/Std/String.yonai"
		COMMENT "Building Yona tool ${YAE_OUTPUT_NAME}"
		VERBATIM
	)
	add_custom_target(${name} ALL DEPENDS "${_out}")
endfunction()
