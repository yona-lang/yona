include_guard(GLOBAL)

set(YONA_COMPONENT_INCLUDE_DIRECTORIES
  "${PROJECT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/include"
)

function(yona_configure_object_component Target)
  target_include_directories(${Target} PRIVATE
    ${YONA_COMPONENT_INCLUDE_DIRECTORIES}
    ${LLVM_INCLUDE_DIRS}
  )
  target_compile_definitions(${Target} PRIVATE YONA_STATIC_BUILD)
  set_target_properties(${Target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

add_library(yona_support OBJECT
  "${PROJECT_SOURCE_DIR}/src/Support/Common.cpp"
  "${PROJECT_SOURCE_DIR}/src/Support/Diagnostic.cpp"
  "${PROJECT_SOURCE_DIR}/src/Support/Process.cpp"
  "${PROJECT_SOURCE_DIR}/src/Support/SourceManager.cpp"
  "${PROJECT_SOURCE_DIR}/src/Support/ThreadPool.cpp"
)
yona_configure_object_component(yona_support)
target_link_libraries(yona_support PRIVATE ${llvm_libs})

# Canonical type storage and binding identities live below both syntax and
# semantic analysis.  The effect solver is included here because TypeArena
# owns its solver-local EffectRef values.
add_library(yona_model OBJECT
  "${PROJECT_SOURCE_DIR}/src/Model/EffectSolver.cpp"
  "${PROJECT_SOURCE_DIR}/src/Model/ModuleIdentity.cpp"
  "${PROJECT_SOURCE_DIR}/src/Model/TypeArena.cpp"
  "${PROJECT_SOURCE_DIR}/src/Model/TypeEnv.cpp"
)
yona_configure_object_component(yona_model)
target_link_libraries(yona_model PRIVATE yona_support)

add_library(yona_syntax OBJECT
  "${PROJECT_SOURCE_DIR}/src/Syntax/Ast.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/Lexer.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/Parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/ParserExpr.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/ParserModule.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/ParserPattern.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/ParserType.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/Utils.cpp"
  "${PROJECT_SOURCE_DIR}/src/Syntax/YonaStyle.cpp"
)
yona_configure_object_component(yona_syntax)
target_link_libraries(yona_syntax PRIVATE yona_support yona_model)

# The canonical, unversioned .yonai representation is intentionally independent
# of syntax and LLVM. It stores structural module/local identities and derives
# generated exports through yona_model.
add_library(yona_interface OBJECT
  "${PROJECT_SOURCE_DIR}/src/Interface/Module.cpp"
  "${PROJECT_SOURCE_DIR}/src/Interface/Reader.cpp"
  "${PROJECT_SOURCE_DIR}/src/Interface/Writer.cpp"
)
target_include_directories(yona_interface PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
)
target_compile_definitions(yona_interface PRIVATE YONA_STATIC_BUILD)
set_target_properties(yona_interface PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(yona_interface PRIVATE yona_model)

add_library(yona_semantics OBJECT
  "${PROJECT_SOURCE_DIR}/src/Semantics/AcceleratorDiag.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/BorrowEscapeAnalysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/GenericFunctionSource.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/InterfaceCatalog.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/PatternAnalysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/TerminationAnalysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/LinearityChecker.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/ModuleFunctionDependencies.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/RefinementChecker.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/SemanticModel.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/TypeChecker.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/Unification.cpp"
  "${PROJECT_SOURCE_DIR}/src/Semantics/UnionFind.cpp"
)
yona_configure_object_component(yona_semantics)
target_link_libraries(yona_semantics PRIVATE
  yona_interface
  yona_syntax
  yona_model
  yona_support
)

# Typed IR owns the typed, ownership-explicit representation consumed by
# backends. Syntax and semantics must never depend on this target.
add_library(yona_typed_ir OBJECT
  "${PROJECT_SOURCE_DIR}/src/TypedIr/Builder.cpp"
  "${PROJECT_SOURCE_DIR}/src/TypedIr/TypedIr.cpp"
)
yona_configure_object_component(yona_typed_ir)
target_link_libraries(yona_typed_ir PRIVATE yona_semantics)

add_library(yona_codegen_llvm OBJECT
  "${PROJECT_SOURCE_DIR}/src/Codegen/AcceleratorLowering.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/Codegen.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenApply.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenCase.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenCollections.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenEffects.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenExpr.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenFunction.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenModule.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenSession.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/CodegenUtils.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/DeriveEngine.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/EscapeAnalysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/LastUseAnalysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/Codegen/TypedIrLowering.cpp"
)
yona_configure_object_component(yona_codegen_llvm)
target_link_libraries(yona_codegen_llvm PRIVATE
  yona_typed_ir
  ${llvm_libs}
)

add_library(yona_toolchain OBJECT
  "${PROJECT_SOURCE_DIR}/src/Toolchain/InProcessLld.cpp"
  "${PROJECT_SOURCE_DIR}/src/Toolchain/LinkerPlan.cpp"
)
yona_configure_object_component(yona_toolchain)
target_link_libraries(yona_toolchain PRIVATE yona_codegen_llvm ${llvm_libs})
if(YONA_INPROCESS_LLD_AVAILABLE)
  target_compile_definitions(yona_toolchain PRIVATE YONA_ENABLE_INPROCESS_LLD=1)
endif()

add_library(yona_typed_core OBJECT
  "${PROJECT_SOURCE_DIR}/src/TypedCore/Analyze.cpp"
  "${PROJECT_SOURCE_DIR}/src/TypedCore/PrettyPrint.c"
)
yona_configure_object_component(yona_typed_core)
target_link_libraries(yona_typed_core PRIVATE
  yona_semantics
)

add_library(yona_lsp OBJECT
  "${PROJECT_SOURCE_DIR}/src/Lsp/Analysis.cpp"
  "${PROJECT_SOURCE_DIR}/src/Lsp/Json.cpp"
  "${PROJECT_SOURCE_DIR}/src/Lsp/JsonRpc.cpp"
  "${PROJECT_SOURCE_DIR}/src/Lsp/Server.cpp"
  "${PROJECT_SOURCE_DIR}/src/Lsp/Utf16.cpp"
)
yona_configure_object_component(yona_lsp)
target_link_libraries(yona_lsp PRIVATE yona_semantics)

set(YONA_COMPILER_COMPONENT_OBJECTS
  $<TARGET_OBJECTS:yona_support>
  $<TARGET_OBJECTS:yona_model>
  $<TARGET_OBJECTS:yona_syntax>
  $<TARGET_OBJECTS:yona_interface>
  $<TARGET_OBJECTS:yona_semantics>
  $<TARGET_OBJECTS:yona_typed_ir>
  $<TARGET_OBJECTS:yona_codegen_llvm>
  $<TARGET_OBJECTS:yona_toolchain>
  $<TARGET_OBJECTS:yona_typed_core>
)

add_library(yona_lib SHARED ${YONA_COMPILER_COMPONENT_OBJECTS})
add_library(yona_lib_static STATIC ${YONA_COMPILER_COMPONENT_OBJECTS})
target_include_directories(yona_lib PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_include_directories(yona_lib_static PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
set_target_properties(yona_lib PROPERTIES
  DEFINE_SYMBOL YONA_LIB_EXPORTS
  EXPORT_NAME Compiler
)
target_link_libraries(yona_lib PRIVATE ${llvm_libs})
target_link_libraries(yona_lib_static PUBLIC ${llvm_libs})
if(YONA_INPROCESS_LLD_AVAILABLE)
  target_link_libraries(yona_lib PRIVATE ${YONA_INPROCESS_LLD_LIBS})
  target_link_libraries(yona_lib_static PUBLIC ${YONA_INPROCESS_LLD_LIBS})
endif()
if(WIN32)
  target_link_libraries(yona_lib PRIVATE ws2_32 dbghelp)
  target_link_libraries(yona_lib_static PUBLIC ws2_32 dbghelp)
  set_target_properties(yona_lib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_definitions(yona_lib_static PUBLIC YONA_STATIC_BUILD)

set(YONA_RUNTIME_PLATFORM_IO_SOURCES)
set(YONA_RUNTIME_CONCURRENCY_SOURCES)
if(WIN32)
  list(APPEND YONA_RUNTIME_PLATFORM_IO_SOURCES
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/FileWindows.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/NetWindows.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/OsWindows.c"
  )
  list(APPEND YONA_RUNTIME_CONCURRENCY_SOURCES
    "${PROJECT_SOURCE_DIR}/src/Runtime/Concurrency/AsyncWin32.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Concurrency/ChannelWin32.c"
  )
elseif(APPLE)
  list(APPEND YONA_RUNTIME_PLATFORM_IO_SOURCES
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/KqueueMacOs.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/FileMacOs.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/NetMacOs.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/OsMacOs.c"
  )
  list(APPEND YONA_RUNTIME_CONCURRENCY_SOURCES
    "${PROJECT_SOURCE_DIR}/src/Runtime/Concurrency/AsyncPosix.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Concurrency/ChannelPosix.c"
  )
else()
  list(APPEND YONA_RUNTIME_PLATFORM_IO_SOURCES
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/IoUringLinux.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/FileLinux.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/NetLinux.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Platform/OsLinux.c"
  )
  list(APPEND YONA_RUNTIME_CONCURRENCY_SOURCES
    "${PROJECT_SOURCE_DIR}/src/Runtime/Concurrency/AsyncPosix.c"
    "${PROJECT_SOURCE_DIR}/src/Runtime/Concurrency/ChannelPosix.c"
  )
endif()

function(yona_configure_runtime_object Target)
  target_include_directories(${Target} PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
  )
  set_target_properties(${Target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

add_library(yona_runtime_core OBJECT
  "${PROJECT_SOURCE_DIR}/src/Runtime/Core/Runtime.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Core/Closures.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Core/Exceptions.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Core/Value.c"
)
yona_configure_runtime_object(yona_runtime_core)

add_library(yona_runtime_collections OBJECT
  "${PROJECT_SOURCE_DIR}/src/Runtime/Collections/Arrays.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Collections/DictionarySet.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Collections/Hamt.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Collections/Sequence.c"
)
yona_configure_runtime_object(yona_runtime_collections)

add_library(yona_runtime_codecs OBJECT
  "${PROJECT_SOURCE_DIR}/src/Runtime/Codecs/Json.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Codecs/Utf16.c"
)
yona_configure_runtime_object(yona_runtime_codecs)
if(PCRE2_FOUND)
  target_sources(yona_runtime_codecs PRIVATE
    "${PROJECT_SOURCE_DIR}/src/Runtime/Codecs/Regex.c"
  )
  target_include_directories(yona_runtime_codecs PRIVATE ${YONA_PCRE2_INCLUDE_DIRS})
  target_link_libraries(yona_runtime_codecs PRIVATE ${YONA_PCRE2_TARGET})
endif()

add_library(yona_runtime_concurrency OBJECT
  ${YONA_RUNTIME_CONCURRENCY_SOURCES}
)
yona_configure_runtime_object(yona_runtime_concurrency)

add_library(yona_runtime_gpu OBJECT
  "${PROJECT_SOURCE_DIR}/src/Runtime/Gpu/Cpu.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Gpu/Stub.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Gpu/VulkanLoader.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Gpu/VulkanDevice.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Gpu/VulkanCompute.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Gpu/VulkanOperations.c"
)
yona_configure_runtime_object(yona_runtime_gpu)

add_library(yona_runtime_platform_io OBJECT
  ${YONA_RUNTIME_PLATFORM_IO_SOURCES}
)
yona_configure_runtime_object(yona_runtime_platform_io)

add_library(yona_runtime_stdlib OBJECT
  "${PROJECT_SOURCE_DIR}/src/Runtime/Stdlib/Iterator.c"
  "${PROJECT_SOURCE_DIR}/src/Runtime/Stdlib/Native.c"
)
yona_configure_runtime_object(yona_runtime_stdlib)

if(Vulkan_FOUND)
  target_compile_definitions(yona_runtime_gpu PRIVATE YONA_HAS_VULKAN=1)
  target_include_directories(yona_runtime_gpu PRIVATE ${YONA_VK_HEADER_DIR})
endif()
if(YONA_ENABLE_VULKAN)
  target_compile_definitions(yona_runtime_gpu PRIVATE YONA_GPU_VULKAN_ENABLED=1)
  target_include_directories(yona_runtime_gpu PRIVATE ${YONA_VULKAN_INCLUDE_DIR})
endif()

set(YONA_RUNTIME_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/runtime")
add_library(yona_runtime STATIC
  $<TARGET_OBJECTS:yona_runtime_core>
  $<TARGET_OBJECTS:yona_runtime_collections>
  $<TARGET_OBJECTS:yona_runtime_codecs>
  $<TARGET_OBJECTS:yona_runtime_concurrency>
  $<TARGET_OBJECTS:yona_runtime_gpu>
  $<TARGET_OBJECTS:yona_runtime_platform_io>
  $<TARGET_OBJECTS:yona_runtime_stdlib>
)
set_target_properties(yona_runtime PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${YONA_RUNTIME_ARTIFACT_DIR}"
)
target_include_directories(yona_runtime PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)
if(PCRE2_FOUND)
  target_link_libraries(yona_runtime PUBLIC ${YONA_PCRE2_TARGET})
endif()
if(Vulkan_FOUND)
  target_link_libraries(yona_runtime PUBLIC Vulkan::Vulkan)
endif()
if(WIN32)
  target_link_libraries(yona_runtime PUBLIC ws2_32 dbghelp)
else()
  target_link_libraries(yona_runtime PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  if(NOT APPLE)
    target_link_libraries(yona_runtime PUBLIC m)
  endif()
endif()
