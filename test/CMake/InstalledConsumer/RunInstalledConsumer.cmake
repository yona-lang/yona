if(NOT DEFINED YONA_BUILD_DIRECTORY)
  message(FATAL_ERROR "YONA_BUILD_DIRECTORY is required")
endif()
if(NOT DEFINED YONA_SOURCE_DIRECTORY)
  message(FATAL_ERROR "YONA_SOURCE_DIRECTORY is required")
endif()
if(NOT DEFINED YONA_INSTALL_DIRECTORY)
  message(FATAL_ERROR "YONA_INSTALL_DIRECTORY is required")
endif()

set(ConsumerSource
    "${YONA_SOURCE_DIRECTORY}/test/CMake/InstalledConsumer")
set(ConsumerBuild
    "${YONA_BUILD_DIRECTORY}/test-cmake/installed-consumer")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${YONA_BUILD_DIRECTORY}" --target
          yona_lib yonac yona-repl yls yona_runtime
  RESULT_VARIABLE ProducerBuildResult)
if(NOT ProducerBuildResult EQUAL 0)
  message(FATAL_ERROR "building the installable Yona targets failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${YONA_BUILD_DIRECTORY}" --prefix
          "${YONA_INSTALL_DIRECTORY}"
  RESULT_VARIABLE InstallResult)
if(NOT InstallResult EQUAL 0)
  message(FATAL_ERROR "installing the Yona development component failed")
endif()

set(ConfigureCommand
    "${CMAKE_COMMAND}"
    -S "${ConsumerSource}"
    -B "${ConsumerBuild}"
    -G "${YONA_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${YONA_INSTALL_DIRECTORY}"
    "-DCMAKE_C_COMPILER=${YONA_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${YONA_CXX_COMPILER}")
if(DEFINED YONA_GENERATOR_PLATFORM AND NOT YONA_GENERATOR_PLATFORM STREQUAL "")
  list(APPEND ConfigureCommand -A "${YONA_GENERATOR_PLATFORM}")
endif()
if(DEFINED YONA_GENERATOR_TOOLSET AND NOT YONA_GENERATOR_TOOLSET STREQUAL "")
  list(APPEND ConfigureCommand -T "${YONA_GENERATOR_TOOLSET}")
endif()
if(DEFINED YONA_BUILD_TYPE AND NOT YONA_BUILD_TYPE STREQUAL "")
  list(APPEND ConfigureCommand "-DCMAKE_BUILD_TYPE=${YONA_BUILD_TYPE}")
endif()

execute_process(COMMAND ${ConfigureCommand} RESULT_VARIABLE ConfigureResult)
if(NOT ConfigureResult EQUAL 0)
  message(FATAL_ERROR "configuring installed consumers failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${ConsumerBuild}" --config
          "${YONA_BUILD_CONFIG}"
  RESULT_VARIABLE BuildResult)
if(NOT BuildResult EQUAL 0)
  message(FATAL_ERROR "building installed consumers failed")
endif()

set(YonaLanguageConsumer
    "${ConsumerBuild}/yona_language_consumer${CMAKE_EXECUTABLE_SUFFIX}")
execute_process(
  COMMAND "${YonaLanguageConsumer}"
  RESULT_VARIABLE YonaLanguageConsumerResult
  OUTPUT_VARIABLE YonaLanguageConsumerOutput
  ERROR_VARIABLE YonaLanguageConsumerError)
if(NOT YonaLanguageConsumerResult EQUAL 0)
  message(FATAL_ERROR
    "running installed Yona CMake consumer failed: ${YonaLanguageConsumerError}")
endif()
if(NOT YonaLanguageConsumerOutput STREQUAL
   "installed CMake tool integration\n()\n")
  message(FATAL_ERROR
    "installed Yona CMake consumer produced unexpected output: ${YonaLanguageConsumerOutput}")
endif()
