# CMake integration for an installed Yona toolchain.
#
#   find_package(Yona CONFIG REQUIRED)
#   yona_add_executable(app SOURCE main.yona)
#   yona_add_module(math SOURCE Math.yona)
#
# Both functions create normal CMake custom targets. Their generated files are
# properties on the target: YONA_EXECUTABLE for programs and YONA_OBJECT /
# YONA_INTERFACE for modules.

include(CMakeParseArguments)

function(yona_resolve_source Output Source)
  if(IS_ABSOLUTE "${Source}")
    set(SourcePath "${Source}")
  else()
    set(SourcePath "${CMAKE_CURRENT_SOURCE_DIR}/${Source}")
  endif()
  get_filename_component(SourcePath "${SourcePath}" ABSOLUTE)
  if(NOT EXISTS "${SourcePath}")
    message(FATAL_ERROR "Yona source does not exist: ${SourcePath}")
  endif()
  set("${Output}" "${SourcePath}" PARENT_SCOPE)
endfunction()

function(yona_include_arguments Output)
  set(Arguments)
  foreach(Directory IN LISTS ARGN)
    if(IS_ABSOLUTE "${Directory}")
      set(IncludeDirectory "${Directory}")
    else()
      set(IncludeDirectory "${CMAKE_CURRENT_SOURCE_DIR}/${Directory}")
    endif()
    get_filename_component(IncludeDirectory "${IncludeDirectory}" ABSOLUTE)
    list(APPEND Arguments -I "${IncludeDirectory}")
  endforeach()
  set("${Output}" "${Arguments}" PARENT_SCOPE)
endfunction()

function(yona_validate_arguments Target Prefix)
  if(NOT DEFINED ${Prefix}_SOURCE OR "${${Prefix}_SOURCE}" STREQUAL "")
    message(FATAL_ERROR "${Target} requires SOURCE <file.yona>")
  endif()
  if(NOT "${${Prefix}_UNPARSED_ARGUMENTS}" STREQUAL "")
    message(FATAL_ERROR
      "${Target} received unsupported arguments: ${${Prefix}_UNPARSED_ARGUMENTS}")
  endif()
endfunction()

function(yona_add_executable Target)
  cmake_parse_arguments(Yona "" "SOURCE;OUTPUT_NAME" "INCLUDE_DIRECTORIES;OPTIONS;DEPENDS" ${ARGN})
  yona_validate_arguments("yona_add_executable(${Target})" Yona)
  yona_resolve_source(SourcePath "${Yona_SOURCE}")
  yona_include_arguments(IncludeArguments ${Yona_INCLUDE_DIRECTORIES})
  if(NOT Yona_OUTPUT_NAME)
    set(Yona_OUTPUT_NAME "${Target}")
  endif()
  set(OutputPath
    "${CMAKE_CURRENT_BINARY_DIR}/bin/${Yona_OUTPUT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
  get_filename_component(OutputDirectory "${OutputPath}" DIRECTORY)

  add_custom_command(
    OUTPUT "${OutputPath}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OutputDirectory}"
    COMMAND "${Yona_COMPILER_EXECUTABLE}"
      --sysroot "${Yona_SYSROOT}"
      ${IncludeArguments}
      ${Yona_OPTIONS}
      -o "${OutputPath}"
      "${SourcePath}"
    DEPENDS "${SourcePath}" ${Yona_DEPENDS}
    COMMENT "Compiling Yona executable ${Yona_OUTPUT_NAME}"
    VERBATIM)
  add_custom_target("${Target}" ALL DEPENDS "${OutputPath}")
  set_property(TARGET "${Target}" PROPERTY YONA_EXECUTABLE "${OutputPath}")
endfunction()

function(yona_add_module Target)
  cmake_parse_arguments(Yona "" "SOURCE;OUTPUT_NAME" "INCLUDE_DIRECTORIES;OPTIONS;DEPENDS" ${ARGN})
  yona_validate_arguments("yona_add_module(${Target})" Yona)
  yona_resolve_source(SourcePath "${Yona_SOURCE}")
  yona_include_arguments(IncludeArguments ${Yona_INCLUDE_DIRECTORIES})
  if(NOT Yona_OUTPUT_NAME)
    set(Yona_OUTPUT_NAME "${Target}")
  endif()
  set(OutputPath
    "${CMAKE_CURRENT_BINARY_DIR}/${Yona_OUTPUT_NAME}${CMAKE_C_OUTPUT_EXTENSION}")
  get_filename_component(OutputDirectory "${OutputPath}" DIRECTORY)
  get_filename_component(OutputStem "${OutputPath}" NAME_WE)
  set(InterfacePath "${OutputDirectory}/${OutputStem}.yonai")

  add_custom_command(
    OUTPUT "${OutputPath}"
    BYPRODUCTS "${InterfacePath}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OutputDirectory}"
    COMMAND "${Yona_COMPILER_EXECUTABLE}"
      --sysroot "${Yona_SYSROOT}"
      ${IncludeArguments}
      ${Yona_OPTIONS}
      --emit-obj
      -o "${OutputPath}"
      "${SourcePath}"
    DEPENDS "${SourcePath}" ${Yona_DEPENDS}
    COMMENT "Compiling Yona module ${Yona_OUTPUT_NAME}"
    VERBATIM)
  add_custom_target("${Target}" ALL DEPENDS "${OutputPath}")
  set_property(TARGET "${Target}" PROPERTY YONA_OBJECT "${OutputPath}")
  set_property(TARGET "${Target}" PROPERTY YONA_INTERFACE "${InterfacePath}")
endfunction()
