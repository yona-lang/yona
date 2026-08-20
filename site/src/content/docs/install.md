---
title: Installation
description: Install the Yona compiler and REPL from packages, or build from source.
---

Every installation provides these executables:

- **`yonac`** — the compiler. Compiles `.yona` source to native executables,
  object files, or LLVM IR. It never runs the result. Source is a file or
  `-` (stdin).
- **`yona`** — compile-and-run a file, stdin, or `-e` expression. Shebang
  target (`#!/usr/bin/env yona`). No arguments on a TTY starts the REPL.
- **`yona-repl`** — the interactive REPL binary; `yona` execs it when you
  want a prompt.
- **`yls`** — the language server (`yls --stdio`). See
  [Editor and language server](/guides/editor/).

## Packages (recommended)

| Platform | Command |
|----------|---------|
| Fedora / RHEL | `sudo dnf copr enable kovariadam/yona && sudo dnf install yona` |
| Ubuntu / Debian | `sudo add-apt-repository ppa:kovariadam/yona && sudo apt update && sudo apt install yona` |
| Arch Linux | `yay -S yona-bin` |
| macOS / Linuxbrew | `brew install akovari/tap/yona` |
| Windows | MSI or ZIP from [GitHub Releases](https://github.com/yona-lang/yona/releases/latest) |

Distro packages place the compiler sysroot (standard library sources,
interface files, runtime objects) under `/usr/lib/yona` or `/usr/lib64/yona`;
Homebrew uses `$(brew --prefix)/lib/yona`. The compiler locates its sysroot
automatically; `--sysroot` overrides it.

Verify the installation:

```bash
yona -e 'let fib n = if n <= 1 then n else fib (n-1) + fib (n-2) in fib 10'
# => 55
```

### macOS notes

The Homebrew formula builds from source against Homebrew `llvm`, `lld`,
`pcre2`, and `cli11` (Apple Silicon, Intel, and Linuxbrew). Wrappers on
`PATH` set `YONA_HOME` and `YONAC_CC` so the keg-only LLVM is used when
compiling Yona programs.

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
- **CMake 3.10+** and **Ninja**
- A **C++23** compiler (Clang recommended)
- **PCRE2** (optional — enables `Std\Regex`)

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
cmake --preset x64-release-macos
cmake --build --preset build-release-macos
```

### Windows

Windows presets (`x64-debug`, `x64-release`) use Ninja with Clang from a
prebuilt **`clang+llvm-*-x86_64-pc-windows-msvc`** archive, plus the MSVC
toolset for the linker and Windows SDK. Extract the LLVM archive to a short
path and set `LLVM_INSTALL_PREFIX` to its root (the directory containing
`bin`, `lib`, `include`). The archive must be the complete tree — a
Clang-only installer omits libraries that `find_package(LLVM)` requires.

See
[INSTALL.md](https://github.com/yona-lang/yona/blob/master/INSTALL.md)
in the repository for the full Windows walkthrough, GPU/Vulkan options, and
troubleshooting.

## Optional: GPU runtime

`Std\GPU` works everywhere with a CPU fallback. For Vulkan execution,
configure with `-DYONA_ENABLE_VULKAN=ON` and install a Vulkan loader
(Fedora: `vulkan-devel vulkan-loader-devel`; macOS: MoltenVK via Homebrew).
Details: [Accelerators](/guides/accelerators/).

## Next

Continue to the [quick start](/learn/quick-start/).
