---
title: CMake integration
description: Build Yona programs and modules from a downstream CMake project.
---

Installing Yona exports a CMake package named `Yona`. It includes `yonac`, the
runtime archive, standard-library interfaces, and small helpers that invoke the
compiler using an argument vector. No shell is involved.

## Find the package

```cmake
find_package(Yona CONFIG REQUIRED)
```

This defines `Yona::yonac`, `Yona_COMPILER_EXECUTABLE`, and `Yona_SYSROOT`.
`Yona_SYSROOT` is the installed runtime and standard-library root used for every
helper invocation. Pass `-DYona_DIR=/path/to/lib/cmake/Yona` when CMake cannot
find the package through its normal prefix search.

## Build an executable

```cmake
find_package(Yona CONFIG REQUIRED)

yona_add_executable(report
  SOURCE Report.yona
  INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
  OPTIONS --Wall --Werror
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/Domain.yonai)
```

`report` is a normal build target and is included in the default build. Its
generated program path is available as a target property:

```cmake
get_property(report_program TARGET report PROPERTY YONA_EXECUTABLE)
add_test(NAME report COMMAND "${report_program}")
```

`SOURCE` is required. Relative paths are resolved from the `CMakeLists.txt` that
calls the helper. `INCLUDE_DIRECTORIES` becomes ordered `-I` arguments;
`OPTIONS` is passed to `yonac` unchanged; and `DEPENDS` adds files such as
generated interfaces to the custom-command dependency graph.

## Build a separately compiled module

```cmake
yona_add_module(domain
  SOURCE Domain.yona
  OUTPUT_NAME Domain
  INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/interfaces)

get_property(domain_object TARGET domain PROPERTY YONA_OBJECT)
get_property(domain_interface TARGET domain PROPERTY YONA_INTERFACE)
```

The helper invokes `yonac --emit-obj`. `YONA_OBJECT` names its object output and
`YONA_INTERFACE` names the deterministic `.yonai` output beside it. Add the
module target to another helper's `DEPENDS` list when that command needs the
interface before it runs.

## Direct invocation

For an integration that needs a custom command, use the imported executable and
the package sysroot directly:

```cmake
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Tool${CMAKE_EXECUTABLE_SUFFIX}"
  COMMAND Yona::yonac
    --sysroot "${Yona_SYSROOT}"
    -o "${CMAKE_CURRENT_BINARY_DIR}/Tool${CMAKE_EXECUTABLE_SUFFIX}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Tool.yona"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tool.yona"
  VERBATIM)
```

Prefer `yona_add_executable()` and `yona_add_module()` unless the project needs
a nonstandard output layout. Both helpers keep compiler paths, the runtime
archive, and standard-library imports aligned with the installed toolchain.
