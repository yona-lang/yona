---
title: Installation
description:
  Install the Yona compiler and REPL from packages, or build from source.
---

Every installation provides these executables:

- **`yonac`** — the compiler. Compiles `.yona` source to native executables,
  object files, or LLVM IR. It never runs the result. Source is a file or `-`
  (stdin).
- **`yona`** — compile-and-run a file, stdin, or `-e` expression. Shebang target
  (`#!/usr/bin/env yona`). No arguments on a TTY starts the REPL.
- **`yona-repl`** — the interactive REPL binary; `yona` execs it when you want a
  prompt.
- **`yls`** — the language server (`yls --stdio`). See
  [Editor and language server](/guides/editor/).

## Packages (recommended)

| Platform          | Command                                                                                               |
| ----------------- | ----------------------------------------------------------------------------------------------------- |
| Fedora / RHEL     | `sudo dnf copr enable kovariadam/yona && sudo dnf install yona`                                       |
| Ubuntu / Debian   | `sudo add-apt-repository ppa:kovariadam/yona && sudo apt update && sudo apt install yona`             |
| Arch Linux        | `yay -S yona-bin`                                                                                     |
| macOS / Linuxbrew | `brew install akovari/tap/yona`                                                                       |
| Windows           | Native x64 or ARM64 MSI/ZIP from [GitHub Releases](https://github.com/yona-lang/yona/releases/latest) |

Distro packages place the compiler sysroot (standard library sources, interface
files, and the canonical runtime archive) under `/usr/lib/yona` or
`/usr/lib64/yona`; Homebrew uses `$(brew --prefix)/lib/yona`. The compiler
locates its sysroot automatically; `--sysroot` overrides it.

Verify the installation:

```bash
yona -e 'let fib n = if n <= 1 then n else fib (n-1) + fib (n-2) in fib 10'
# => 55
```

## CMake package

An installed Yona distribution exports `YonaConfig.cmake` alongside `yonac`, the
runtime archive, and standard library. Downstream CMake projects can use the
compiler without hard-coding a sysroot or constructing a shell command:

```cmake
find_package(Yona CONFIG REQUIRED)
yona_add_executable(app SOURCE App.yona)
```

See [CMake integration](/reference/cmake/) for module builds, interface
dependencies, and target properties.

### macOS notes

The Homebrew formula builds from source against Homebrew `llvm`, `lld`, `pcre2`,
and `cli11` (Apple Silicon, Intel, and Linuxbrew). Wrappers on `PATH` set
`YONA_HOME` and `YONAC_CC` so the keg-only LLVM is used when compiling Yona
programs.

Tagged binary releases include a native Apple Silicon archive named
`yona-<version>-macos-arm64.tar.gz`; the hosted CI/release matrix does not build
a separate Intel macOS artifact.

Optional GPU support via MoltenVK:

```bash
brew install akovari/tap/yona --with-vulkan
```

### Ubuntu note

PPA builds compile from source on Launchpad. If your series has no published
package yet, build a binary `.deb` from a release tarball:

```bash
./dist/debian/build-deb-from-release.sh 0.1.3 amd64
sudo apt install ./dist/debian/yona_0.1.3-1_amd64.deb
```

## Building from source

Prerequisites on all platforms:

- **LLVM 22+** recommended (16+ may work when `find_package(LLVM)` succeeds)
- **CMake 3.15+** and **Ninja**
- A **C++23** compiler (Clang recommended)
- **PCRE2** for `Std\Regex`. CMake uses a normal system package when present,
  otherwise its default dependency fallback builds pinned PCRE2 10.47; no vcpkg
  is used.

### Fedora / RHEL

```bash
sudo dnf install llvm llvm-devel llvm-libs llvm-static \
    clang lld lld-devel cmake ninja-build pcre2-devel cli11-devel \
    libxml2-devel doctest-devel pkgconf

git clone https://github.com/yona-lang/yona.git
cd yona
cmake --preset x64-release-linux
cmake --build --preset build-release-linux
```

### Ubuntu / Debian

```bash
sudo apt install llvm-dev clang lld liblld-dev libpolly-dev cmake ninja-build \
    libpcre2-dev libcli11-dev libxml2-dev doctest-dev pkg-config

git clone https://github.com/yona-lang/yona.git
cd yona
cmake --preset x64-release-linux
cmake --build --preset build-release-linux
```

### macOS

```bash
brew install llvm lld cmake ninja pcre2 cli11 doctest pkgconf

git clone https://github.com/yona-lang/yona.git
cd yona
cmake --preset arm64-release-macos
cmake --build --preset build-release-macos-arm64
```

### Windows

Windows has native x64 and ARM64 presets. Use Ninja with the matching official
complete LLVM archive — **`clang+llvm-*-x86_64-pc-windows-msvc`** for x64 or
**`clang+llvm-*-aarch64-pc-windows-msvc`** for ARM64 — plus the matching MSVC
toolset and Windows SDK. Extract it to a short path and set
`LLVM_INSTALL_PREFIX` to its root (the directory containing `bin`, `lib`,
`include`). A Clang-only installer omits libraries that `find_package(LLVM)`
requires. If neither `LLVM_INSTALL_PREFIX` nor `LLVM_DIR` is set, Windows
presets assume `C:\Program Files\LLVM`; `LLVM_DIR`, when set, must be
`C:\Program Files\LLVM\lib\cmake\llvm` (the directory that contains
`LLVMConfig.cmake`), not the installation root.

On a fresh build directory the Windows presets use `clang.exe` and `clang++.exe`
from that complete LLVM installation. An ARM64 Visual Studio Developer
PowerShell remains required to provide the matching Windows SDK and linker
environment.

Tagged releases publish `yona-<version>-windows-x64.{zip,msi}` and
`yona-<version>-windows-arm64.{zip,msi}`.

The MSVC **Desktop development with C++** workload supplies the DIA SDK used by
the official LLVM CMake package. Yona discovers that SDK before loading LLVM; if
it is installed in a custom location, set `YONA_DIA_SDK_LIBRARY` to its
`diaguids.lib`. Zlib is resolved through normal CMake discovery, with Yona's
pinned CMake source fallback enabled by default. No external package manager is
needed for this setup.

See [INSTALL.md](https://github.com/yona-lang/yona/blob/master/INSTALL.md) in
the repository for the full Windows walkthrough, GPU/Vulkan options, and
troubleshooting.

## Optional: GPU runtime

`Std\Gpu` works everywhere with a CPU fallback. For Vulkan execution, configure
with `-DYONA_ENABLE_VULKAN=ON` and install a Vulkan loader (Fedora:
`vulkan-devel vulkan-loader-devel`; macOS: MoltenVK via Homebrew). Details:
[Accelerators](/guides/accelerators/).

## Next

Continue to the [quick start](/learn/quick-start/).
