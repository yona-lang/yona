# Compile a Yona expression program to a native executable using the
# just-built yonac. Output sits in CMAKE_BINARY_DIR next to yonac.
#
# These are data dependencies, not target source lists: a tool may import any
# standard-library module, directly or transitively. CONFIGURE_DEPENDS keeps
# the dependency set deterministic and refreshes it when a new module is
# added, without asking contributors to update this file.
file(GLOB_RECURSE YonaStdlibInputs CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/lib/*.yona"
  "${CMAKE_SOURCE_DIR}/lib/*.yonai")
list(SORT YonaStdlibInputs)
#
#   yona_add_executable(<name> SOURCE <main.yona> [OUTPUT_NAME <name>])
#
function(yona_add_executable Name)
  cmake_parse_arguments(YonaExecutable "" "SOURCE;OUTPUT_NAME" "" ${ARGN})
  if(NOT YonaExecutable_SOURCE)
    message(FATAL_ERROR
      "yona_add_executable(${Name}) requires SOURCE <file.yona>")
  endif()
  if(NOT YonaExecutable_OUTPUT_NAME)
    set(YonaExecutable_OUTPUT_NAME "${Name}")
  endif()
  if(IS_ABSOLUTE "${YonaExecutable_SOURCE}")
    set(SourcePath "${YonaExecutable_SOURCE}")
  else()
    set(SourcePath "${CMAKE_CURRENT_SOURCE_DIR}/${YonaExecutable_SOURCE}")
  endif()
  set(OutputPath
    "${CMAKE_BINARY_DIR}/${YonaExecutable_OUTPUT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
  set(CCompiler "$ENV{YONAC_CC}")
  if(NOT CCompiler)
    set(CCompiler "${CMAKE_C_COMPILER}")
  endif()
  add_custom_command(
    OUTPUT "${OutputPath}"
    COMMAND ${CMAKE_COMMAND} -E env
      "YONAC_CC=${CCompiler}"
      $<TARGET_FILE:yonac>
      --sysroot "${CMAKE_BINARY_DIR}"
      -I "${CMAKE_SOURCE_DIR}/lib"
      -o "${OutputPath}"
      "${SourcePath}"
    DEPENDS yonac yona_runtime "${SourcePath}" ${YonaStdlibInputs}
    COMMENT "Building Yona tool ${YonaExecutable_OUTPUT_NAME}"
    VERBATIM)
  add_custom_target(${Name} ALL DEPENDS "${OutputPath}")
endfunction()
