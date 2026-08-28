# Prepare the DIA SDK target referenced by official Windows LLVM packages.
# This file is included only from the WIN32 prerequisite block in the top-level
# CMakeLists. Keeping target creation separate also makes its ordering testable.

set(YONA_DIA_SDK_LIBRARY "" CACHE FILEPATH
    "Full path to the Visual Studio DIA SDK diaguids.lib used by LLVM")

function(yona_dia_sdk_arch out_var)
  # An explicit platform lets the CMake-only contract test cover ARM64 without
  # changing the already-selected Visual Studio generator platform. Production
  # callers intentionally omit it and use CMake's target-platform metadata.
  set(_platform "${ARGV1}")
  if(NOT _platform)
    set(_platform "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(NOT _platform AND DEFINED CMAKE_VS_PLATFORM_NAME AND CMAKE_VS_PLATFORM_NAME)
    set(_platform "${CMAKE_VS_PLATFORM_NAME}")
  endif()
  if(NOT _platform AND DEFINED CMAKE_SYSTEM_PROCESSOR AND CMAKE_SYSTEM_PROCESSOR)
    set(_platform "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  if(NOT _platform AND DEFINED MSVC_CXX_ARCHITECTURE_ID AND MSVC_CXX_ARCHITECTURE_ID)
    set(_platform "${MSVC_CXX_ARCHITECTURE_ID}")
  endif()
  string(TOLOWER "${_platform}" _platform)

  if(_platform MATCHES "arm64")
    set(_arch "arm64")
  elseif(_platform MATCHES "win32|x86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    # The x86 DIA import library lives directly under DIA SDK/lib.
    set(_arch "")
  else()
    set(_arch "amd64")
  endif()
  set(${out_var} "${_arch}" PARENT_SCOPE)
endfunction()

function(yona_find_diaguids_lib out_var)
  if(YONA_DIA_SDK_LIBRARY)
    if(EXISTS "${YONA_DIA_SDK_LIBRARY}")
      get_filename_component(_configured "${YONA_DIA_SDK_LIBRARY}" ABSOLUTE)
      set(${out_var} "${_configured}" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR
      "YONA_DIA_SDK_LIBRARY does not exist: '${YONA_DIA_SDK_LIBRARY}'. "
      "Point it at the diaguids.lib installed by the Visual Studio DIA SDK.")
  endif()

  yona_dia_sdk_arch(_dia_arch)
  set(_roots "")
  if(DEFINED CMAKE_GENERATOR_INSTANCE AND CMAKE_GENERATOR_INSTANCE)
    list(APPEND _roots "${CMAKE_GENERATOR_INSTANCE}")
  endif()
  if(DEFINED ENV{VSINSTALLDIR} AND NOT "$ENV{VSINSTALLDIR}" STREQUAL "")
    list(APPEND _roots "$ENV{VSINSTALLDIR}")
  endif()

  set(_vswhere "")
  if(DEFINED ENV{ProgramFiles\(x86\)} AND
     EXISTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
    set(_vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
  endif()
  if(_vswhere)
    execute_process(
      COMMAND "${_vswhere}" -products * -property installationPath
      OUTPUT_VARIABLE _vs_roots
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    string(REPLACE "\r\n" ";" _vs_roots "${_vs_roots}")
    string(REPLACE "\n" ";" _vs_roots "${_vs_roots}")
    list(APPEND _roots ${_vs_roots})
  endif()

  set(_program_files_roots "")
  if(DEFINED ENV{ProgramFiles} AND NOT "$ENV{ProgramFiles}" STREQUAL "")
    list(APPEND _program_files_roots "$ENV{ProgramFiles}/Microsoft Visual Studio")
  endif()
  if(DEFINED ENV{ProgramFiles\(x86\)} AND NOT "$ENV{ProgramFiles\(x86\)}" STREQUAL "")
    list(APPEND _program_files_roots "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio")
  endif()
  foreach(_base IN LISTS _program_files_roots)
    file(GLOB _installations LIST_DIRECTORIES TRUE "${_base}/*/*")
    list(APPEND _roots ${_installations})
  endforeach()
  list(REMOVE_DUPLICATES _roots)

  set(_found "")
  foreach(_root IN LISTS _roots)
    string(STRIP "${_root}" _root)
    if(NOT _root)
      continue()
    endif()
    if(_dia_arch)
      set(_candidate "${_root}/DIA SDK/lib/${_dia_arch}/diaguids.lib")
    else()
      set(_candidate "${_root}/DIA SDK/lib/diaguids.lib")
    endif()
    if(EXISTS "${_candidate}")
      set(_found "${_candidate}")
      break()
    endif()
  endforeach()

  set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

function(yona_prepare_llvm_dia_sdk)
  if(TARGET DIASDK::Diaguids)
    return()
  endif()

  yona_find_diaguids_lib(_yona_dia)
  if(NOT _yona_dia)
    message(FATAL_ERROR
      "LLVM's Windows CMake package requires DIASDK::Diaguids, but "
      "diaguids.lib was not found. Install the Visual Studio 'Desktop "
      "development with C++' workload (including the DIA SDK), or configure "
      "with -DYONA_DIA_SDK_LIBRARY=C:/path/to/DIA SDK/lib/amd64/diaguids.lib.")
  endif()

  add_library(DIASDK::Diaguids UNKNOWN IMPORTED GLOBAL)
  set_target_properties(DIASDK::Diaguids PROPERTIES IMPORTED_LOCATION "${_yona_dia}")
  message(STATUS "DIA SDK: ${_yona_dia}")
endfunction()
