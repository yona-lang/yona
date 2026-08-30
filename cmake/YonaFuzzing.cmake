include_guard(GLOBAL)

option(YONA_BUILD_FUZZERS "Build local libFuzzer targets" OFF)
set(YONA_FUZZ_SECONDS "60" CACHE STRING
    "Seconds to run each harness from the fuzz target")
set(YONA_FUZZ_CHECK_RUNS "100" CACHE STRING
    "Iterations per harness in the short fuzz-check target")

if(NOT YONA_BUILD_FUZZERS)
  return()
endif()

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR
   NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR
      "YONA_BUILD_FUZZERS requires Clang for libFuzzer, ASan, and UBSan")
endif()

include(CheckCXXSourceCompiles)
set(YonaSavedRequiredFlags "${CMAKE_REQUIRED_FLAGS}")
set(YonaSavedRequiredLinkOptions "${CMAKE_REQUIRED_LINK_OPTIONS}")
set(CMAKE_REQUIRED_FLAGS
    "-fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined")
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer,address,undefined)
check_cxx_source_compiles(
    "#include <cstddef>
     extern \"C\" int LLVMFuzzerTestOneInput(const unsigned char *,
                                               std::size_t) { return 0; }"
    YONA_HAS_REQUIRED_FUZZER_RUNTIMES)
set(CMAKE_REQUIRED_FLAGS "${YonaSavedRequiredFlags}")
set(CMAKE_REQUIRED_LINK_OPTIONS "${YonaSavedRequiredLinkOptions}")
if(NOT YONA_HAS_REQUIRED_FUZZER_RUNTIMES)
  message(FATAL_ERROR
      "The selected Clang installation does not provide compatible "
      "libFuzzer, ASan, and UBSan runtimes for this target architecture")
endif()

set(YONA_FUZZ_COMPILE_OPTIONS
    -fno-omit-frame-pointer
    -fsanitize=fuzzer-no-link,address,undefined)
set(YONA_FUZZ_HARNESS_OPTIONS
    -fno-omit-frame-pointer
    -fsanitize=fuzzer,address,undefined)

# Instrument the production component objects consumed by the harnesses. The
# executable alone is insufficient: parser, interface, LSP, regex, and UTF
# implementation code must all participate in sanitizer coverage.
foreach(YonaTarget IN ITEMS
    yona_support
    yona_model
    yona_syntax
    yona_semantics
    yona_codegen_llvm
    yona_toolchain
    yona_typed_core
    yona_lsp
    yona_runtime_core
    yona_runtime_collections
    yona_runtime_codecs
    yona_runtime_concurrency
    yona_runtime_gpu
    yona_runtime_platform_io
    yona_runtime_stdlib)
  if(TARGET ${YonaTarget})
    target_compile_options(${YonaTarget} PRIVATE
        ${YONA_FUZZ_COMPILE_OPTIONS})
  endif()
endforeach()

set(YONA_FUZZ_RUNTIME_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fuzz")
set(YONA_FUZZ_WORK_DIRECTORY "${YONA_FUZZ_RUNTIME_DIRECTORY}/Work")
set(YONA_FUZZ_ARTIFACT_DIRECTORY
    "${YONA_FUZZ_RUNTIME_DIRECTORY}/Artifacts")

function(yona_add_fuzzer Target Source Corpus)
  add_executable(${Target} "${PROJECT_SOURCE_DIR}/${Source}")
  target_include_directories(${Target} PRIVATE
      "${PROJECT_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_BINARY_DIR}/include")
  target_compile_definitions(${Target} PRIVATE YONA_STATIC_BUILD)
  target_compile_options(${Target} PRIVATE ${YONA_FUZZ_HARNESS_OPTIONS})
  target_link_options(${Target} PRIVATE
      -fsanitize=fuzzer,address,undefined)
  target_link_libraries(${Target} PRIVATE ${ARGN})
  set_target_properties(${Target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${YONA_FUZZ_RUNTIME_DIRECTORY}")

  set(SeedDirectory "${PROJECT_SOURCE_DIR}/fuzz/Corpus/${Corpus}")
  set(WorkingCorpus "${YONA_FUZZ_WORK_DIRECTORY}/${Corpus}")
  set(ArtifactDirectory "${YONA_FUZZ_ARTIFACT_DIRECTORY}/${Corpus}")

  add_custom_target(${Target}_check
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${WorkingCorpus}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${WorkingCorpus}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SeedDirectory}" "${WorkingCorpus}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ArtifactDirectory}"
    COMMAND "$<TARGET_FILE:${Target}>"
        "${WorkingCorpus}"
        -runs=${YONA_FUZZ_CHECK_RUNS}
        -max_len=65536
        -timeout=10
        -rss_limit_mb=2048
        -print_final_stats=1
        "-artifact_prefix=${ArtifactDirectory}/"
    DEPENDS ${Target}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM)

  add_custom_target(${Target}_run
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${WorkingCorpus}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${WorkingCorpus}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SeedDirectory}" "${WorkingCorpus}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ArtifactDirectory}"
    COMMAND "$<TARGET_FILE:${Target}>"
        "${WorkingCorpus}"
        -max_total_time=${YONA_FUZZ_SECONDS}
        -max_len=65536
        -timeout=10
        -rss_limit_mb=2048
        -print_final_stats=1
        "-artifact_prefix=${ArtifactDirectory}/"
    DEPENDS ${Target}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM)

  set_property(GLOBAL APPEND PROPERTY YONA_FUZZ_CHECK_TARGETS
      ${Target}_check)
  set_property(GLOBAL APPEND PROPERTY YONA_FUZZ_RUN_TARGETS
      ${Target}_run)
endfunction()

yona_add_fuzzer(yona_fuzz_parser
    "fuzz/Syntax/ParserFuzzer.cpp" Parser yona_lib_static)
yona_add_fuzzer(yona_fuzz_interface
    "fuzz/Interface/InterfaceFuzzer.cpp" Interface yona_interface)
yona_add_fuzzer(yona_fuzz_json_rpc
    "fuzz/Lsp/JsonRpcFuzzer.cpp" JsonRpc yona_lib_static)
target_sources(yona_fuzz_json_rpc PRIVATE $<TARGET_OBJECTS:yona_lsp>)
yona_add_fuzzer(yona_fuzz_regex
    "fuzz/Runtime/RegexFuzzer.cpp" Regex yona_runtime)
yona_add_fuzzer(yona_fuzz_utf_codec
    "fuzz/Runtime/UtfCodecFuzzer.cpp" UtfCodec
    yona_lib_static yona_runtime)
target_sources(yona_fuzz_utf_codec PRIVATE $<TARGET_OBJECTS:yona_lsp>)

get_property(YonaFuzzCheckTargets GLOBAL PROPERTY YONA_FUZZ_CHECK_TARGETS)
get_property(YonaFuzzRunTargets GLOBAL PROPERTY YONA_FUZZ_RUN_TARGETS)

add_custom_target(fuzz-check)
add_dependencies(fuzz-check ${YonaFuzzCheckTargets})

add_custom_target(fuzz)
add_dependencies(fuzz ${YonaFuzzRunTargets})
