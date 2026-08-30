# Installing Yona

## Packages (recommended)

| Platform | Command |
|----------|---------|
| Fedora / RHEL | `sudo dnf copr enable kovariadam/yona && sudo dnf install yona` |
| Ubuntu / Debian | `sudo add-apt-repository ppa:kovariadam/yona && sudo apt update && sudo apt install yona` |
| Arch | `yay -S yona-bin` |
| macOS / Linuxbrew | `brew install akovari/tap/yona` |
| Windows | Native x64 or ARM64 MSI/ZIP from [GitHub Releases](https://github.com/yona-lang/yona/releases/latest) |

These install `yonac`, `yona`, `yona-repl`, and `yls` on `PATH`. Distro packages put the compiler sysroot under `/usr/lib/yona` or `/usr/lib64/yona`; Homebrew uses `$(brew --prefix)/lib/yona`.

Ubuntu PPA builds compile from source on Launchpad (Noble `llvm-dev` is LLVM 18). If a series has no published package yet, build a binary `.deb` from the release tarball:

```bash
./dist/debian/build-deb-from-release.sh 0.1.3 amd64
sudo apt install ./dist/debian/yona_0.1.3-1_amd64.deb
```

**Maintainer one-time setup** (create Copr project, AUR SSH key, Launchpad PPA, GitHub secrets): [dist/RELEASING.md](dist/RELEASING.md).

The rest of this file is for **building from source**.

## Prerequisites (source build)

All platforms require:
- **LLVM** for the codegen backend: **22+** recommended (see `CLAUDE.md`); **16+** may work if `find_package(LLVM)` succeeds with your toolchain. **Windows:** use the official **`clang+llvm-*-windows-msvc`** bundle. CI (`.github/actions/setup-llvm`) uses the runner image where it is enough: Ubuntu 26.04 already has Clang 22 and only needs `llvm-22-dev`; macOS needs Homebrew `llvm` (Apple Clang has no `LLVMConfig.cmake`); Windows downloads the latest official archive because the image’s Chocolatey LLVM is not a complete tree.
- **CMake 3.15+** and **Ninja** (for building from source; on **Windows** use the **Windows** section: Ninja + Clang from a prebuilt LLVM + MSVC toolset)
- **C++23 capable compiler** (clang recommended; Windows presets use Clang with the MSVC linker)
- **PCRE2** for `Std\Regex`. CMake prefers a normal platform package; with the
  default `-DYONA_FETCH_DEPS=ON`, it otherwise builds the project-pinned
  PCRE2 10.47 fallback. This project never uses vcpkg.

## Linux (Fedora/RHEL)

```bash
# Install dependencies
sudo dnf install llvm llvm-devel llvm-libs llvm-static \
    clang lld lld-devel cmake ninja-build pcre2-devel cli11-devel \
    libxml2-devel doctest-devel pkgconf

# Build
git clone https://github.com/yona-lang/yona.git
cd yona
cmake --preset x64-release-linux
cmake --build --preset build-release-linux

# Install (optional)
sudo install -m755 out/build/x64-release-linux/yonac /usr/local/bin/
sudo install -m755 out/build/x64-release-linux/yona /usr/local/bin/
sudo install -m755 out/build/x64-release-linux/yona-repl /usr/local/bin/
sudo install -m755 out/build/x64-release-linux/yls /usr/local/bin/
sudo cp -r lib/Std /usr/local/lib/yona/lib/
```

## Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt install llvm-dev clang lld liblld-dev libpolly-dev cmake ninja-build \
    libpcre2-dev libcli11-dev libxml2-dev doctest-dev pkg-config

# Build
git clone https://github.com/yona-lang/yona.git
cd yona
cmake --preset x64-release-linux
cmake --build --preset build-release-linux
```

## macOS (Homebrew)

```bash
brew install akovari/tap/yona
```

This is a **source** formula: it compiles against Homebrew `llvm`, `lld`, `pcre2`, and `cli11` (Apple Silicon and Intel; Linuxbrew too). Wrappers on `PATH` set `YONA_HOME` and `YONAC_CC` so keg-only LLVM is used when compiling Yona programs.

```bash
# Optional Std\GPU Vulkan (MoltenVK on macOS)
brew install akovari/tap/yona --with-vulkan

# Build the git master branch
brew install --HEAD akovari/tap/yona
```

The tap is updated from GitHub Releases (`dist/ci/generate-homebrew-formula.sh`). First formula publish is the next `v*` tag after `HOMEBREW_TAP_SSH_KEY` is set on `yona-lang/yona` (same deploy key as [akovari/winetop](https://github.com/akovari/winetop)).

### Build from a source checkout

```bash
# Install dependencies
brew install llvm lld cmake ninja pcre2 cli11 doctest pkgconf

git clone https://github.com/yona-lang/yona.git
cd yona
# Native Apple Silicon build
cmake --preset arm64-release-macos
cmake --build --preset build-release-macos-arm64
```

Optional **`Std\GPU` Vulkan** (MoltenVK): `brew install molten-vk vulkan-headers vulkan-loader`, then configure with `-DYONA_ENABLE_VULKAN=ON`. CMake finds `vulkan/vulkan.h` and `libvulkan` / `libMoltenVK` via `VULKAN_SDK` and `HOMEBREW_PREFIX` or `brew --prefix`. The runtime `dlopen`s those discovered dirs (and bare loader names) and, if unset, hints `VK_ICD_FILENAMES` at a MoltenVK ICD json CMake or the env prefix located. Metal typically lacks `shaderInt64`; `hasGpu` is still true when the device is ready, and IntArray `mapAdd` / `mapMul` / `reduceSum` / `filterGreaterThan` use i32 when values fit. `vulkanStatus` can be `vulkan-device`. See `docs/gpu-architecture.md` and `docs/gpu-vulkan-implementation-plan.md` §11.

## Windows

Windows presets (`x64-debug`, `x64-release`, `arm64-debug`, `arm64-release`)
use **Ninja** and expect native **Clang** (same as CI). **MSVC** with the
**Desktop development with C++** workload (or Build Tools + Windows SDK) must
be installed for the matching x64 or ARM64 linker and libraries.

For tagged releases, Windows has architecture-labelled portable ZIP and MSI
artifacts: `yona-<version>-windows-x64.{zip,msi}` and
`yona-<version>-windows-arm64.{zip,msi}`. Apple Silicon releases use
`yona-<version>-macos-arm64.tar.gz`; hosted releases do not publish a separate
Intel macOS artifact. The MSI flow is defined under
`packaging/windows/` (`YonaInstaller.wxs`, `build-msi.ps1`).

### 1. Install tools

- **CMake** 3.29+ (3.30+ recommended if you enable newer CMake policies elsewhere). [cmake.org/download](https://cmake.org/download/)
- **Ninja**: [ninja-build.org](https://github.com/ninja-build/ninja/releases) or `winget install Ninja-build.Ninja`
- **LLVM for Windows**: use the complete official prebuilt archive matching
  your native target: `clang+llvm-*-x86_64-pc-windows-msvc` for x64 or
  `clang+llvm-*-aarch64-pc-windows-msvc` for ARM64, from the
  [LLVM project releases](https://github.com/llvm/llvm-project/releases).
  Extract it to a short path such as `C:\LLVM`. This avoids link errors from
  LLVM builds produced against unrelated Visual Studio paths (see
  *Troubleshooting* below).
- **PCRE2** for `Std\Regex` is bundled from the project-pinned 10.47 source
  fallback by the default `-DYONA_FETCH_DEPS=ON` configuration. The resulting
  static archive is placed in the runtime sysroot, so installed `yonac` can
  link Regex programs without vcpkg or a separate system installation. If you
  deliberately build with `-DYONA_FETCH_DEPS=OFF`, provide a normal
  CMake-discoverable PCRE2 installation instead.

The CMake toolchain reads `LLVM_INSTALL_PREFIX` and `LLVM_DIR` from the CMake
cache or environment (CI sets the prefix explicitly). If neither is set, the
Windows presets default `LLVM_INSTALL_PREFIX` to `C:\Program Files\LLVM` and
derive `LLVM_DIR` as `C:\Program Files\LLVM\lib\cmake\llvm`. `LLVM_DIR` names
the directory containing `LLVMConfig.cmake`, not the LLVM installation root.
Override the prefix when the complete archive is extracted elsewhere (for
example `C:\LLVM`). **Spell the path correctly** in user and machine
environment variables; a typo leaves `find_package(LLVM)` searching an empty
prefix. On a fresh Windows build directory, the presets select `clang.exe` and
`clang++.exe` from that LLVM tree unless `CC`/`CXX` or CMake compiler cache
variables explicitly choose another compiler.

The official LLVM package declares Zlib and DIA SDK targets in its CMake
metadata. Yona resolves Zlib through ordinary `find_package(ZLIB)` discovery;
when no package is installed and `YONA_FETCH_DEPS=ON` (the default), CMake
builds Yona's pinned Zlib source dependency. The Visual Studio workload supplies
the DIA SDK; CMake discovers it automatically. If it is installed outside the
normal Visual Studio locations, pass its exact library with
`-DYONA_DIA_SDK_LIBRARY=C:\path\to\diaguids.lib`. Neither dependency requires a
separate package manager.

#### Complete Windows LLVM tree (CMake + `find_package(LLVM)`)

Use the official complete **`clang+llvm-*-<native-target>-pc-windows-msvc`**
archive from the [LLVM releases](https://github.com/llvm/llvm-project/releases):
`x86_64` for x64 or `aarch64` for ARM64. Do not use a Clang-only installer or
partial extract. `LLVM_INSTALL_PREFIX` must be the **root** of that tree (the
folder that contains `bin`, `lib`, `include`).

CMake’s imported targets expect a coherent install, including at minimum under `bin\`: **`clang.exe`**, **`clang++.exe`**, **`llvm-ar.exe`**, and other tools referenced from `LLVMExports.cmake`; under `lib\`: the **`LLVM*.lib`** set plus **`LTO.lib`**; under `bin\` (or next to those libs as shipped): **`LTO.dll`** when the export set references it. If `find_package(LLVM)` fails with “imported target … references the file … but this file does not exist”, the prefix is incomplete—re-download the matching `clang+llvm` archive or repair the install.

If **`LLVM_INSTALL_PREFIX`** points at an LLVM **library** tree **without** Clang in `bin\`, set **`CC`** / **`CXX`** (or CMake **`-D CMAKE_C_COMPILER=…` `-D CMAKE_CXX_COMPILER=…`**) to a separate **`clang.exe` / `clang++.exe`** from another full install, and keep **`LLVM_INSTALL_PREFIX`** on the tree that contains **`lib/cmake/llvm/LLVMConfig.cmake`**.

#### `YONAC_CC` and doctest (`tests.exe`)

The C++ **doctest** harness compiles `src/compiled_runtime.c` and platform sources via `system()` / `cmd`. Set **`YONAC_CC`** to the full path of **`clang.exe`** used for those subprocesses (CTest on Windows should inherit the same env you use for CMake, or set it explicitly). The harness **quotes** `YONAC_CC` when building commands; if you run an **older** `tests.exe` without that fix, use an **8.3 short path** (e.g. `C:\PROGRA~1\LLVM\bin\clang.exe`) or put **`clang.exe`** on **`PATH`** so the default compiler name resolves. Optional Vulkan scratch builds also honor **`YONA_COMPILE_GPU_VULKAN`** and **`VULKAN_SDK`** (see `CLAUDE.md` / `docs/gpu-architecture.md`).
When CMake finds Vulkan, **`yonac`**-linked programmes use **`gpu_stub`** Vulkan entry points for **`Std\GPU` float** natives. On Windows, **`yonac`** prefers the **`vulkan-1.lib`** path recorded at CMake configure time (same as the **`Vulkan::Vulkan`** target); if that file is missing at link time, set **`VULKAN_SDK`** to the LunarG root so **`Lib/vulkan-1.lib`** resolves. Unix builds that define **`YONAC_EXE_LINK_POSIX_VULKAN`** pass **`-L`** (CMake-recorded lib dir, else **`VULKAN_SDK/lib`** or **`$HOMEBREW_PREFIX/lib`**) and **`-lvulkan`**. On macOS they also set **`rpath`** to that directory so **`libvulkan`** resolves at launch.

### 2. Configure and build (PowerShell)

Open the native **x64** or **ARM64** Developer PowerShell for Visual Studio so
the matching linker and SDK paths are on `PATH`. If you have **both Visual
Studio 2022 and 2026** (folder `18`), use a developer shell compatible with
the LLVM archive's import libraries.

Then:

```powershell
git clone https://github.com/yona-lang/yona.git
cd yona

# Point at your extracted LLVM (example: C:\LLVM)
$env:LLVM_INSTALL_PREFIX = "C:\LLVM"
$env:CC  = "C:\LLVM\bin\clang.exe"
$env:CXX = "C:\LLVM\bin\clang++.exe"

cmake --preset x64-release
cmake --build --preset build-release

# Or, from native ARM64 Developer PowerShell with the ARM64 LLVM archive:
cmake --preset arm64-release
cmake --build --preset build-release-arm64
```

Binaries are written under the selected preset directory (for example,
`out\build\x64-release\` or `out\build\arm64-release\`), including
`yonac.exe`, `yona.exe`, and `yona_lib.dll`.

**`yonac` linking a full executable (not `--emit-obj` / `--emit-ir`):** the CLI shells out to compile `src/compiled_runtime.c` and the platform layer, then link. On Windows it uses **`clang`** by default (or **`YONAC_CC`** if set) and links **`ws2_32`** / **`dbghelp`**. Put the same LLVM `bin` directory on `PATH`, or set `YONAC_CC` to the full path of `clang.exe` so the subprocess can find the compiler. Full paths containing spaces, including `C:\Program Files\LLVM\bin\clang.exe`, are supported. Optional LTO uses **`llvm-link.exe`** next to that `clang` when `YONAC_CC` is set.

**Linker mode selection:** `yonac` supports `--linker-mode auto|bundled|system|inprocess` (or `YONAC_LINKER_MODE`). In `auto`, it prefers bundled `lld` when found under discovered sysroots (`bin/` or `llvm/bin/`) and falls back to the system toolchain linker. Use `bundled` to require packaged `lld` (hard error if missing), `system` to force external linker behavior, or `inprocess` to request embedded-linker mode when available. Embedded-linker wiring is gated by CMake option `YONA_ENABLE_INPROCESS_LLD` (default `ON`), but CMake may auto-disable it when required dependencies are missing on the current toolchain (for example, MSVC-compatible LibXml2 for LLVM Windows manifest support). When unavailable, `inprocess` falls back to the external path with a warning unless `YONAC_REQUIRE_INPROCESS_LLD=1` is set, in which case compile/link fails hard. The REPL (`yona`) currently reads `YONAC_LINKER_MODE` as well.

**Prebuilt runtime artifacts:** build outputs now include precompiled runtime files under `out\build\<preset>\runtime\`:
- `compiled_runtime.o`
- `crt_<platform-file>.o` objects (for per-OS runtime TUs)
- `yona_runtime.lib` (Windows) or `libyona_runtime.a` (Unix)

Release packaging and distro packages copy this `runtime/` directory into the Yona sysroot so `yonac`/`yona` can consume prebuilt runtime artifacts directly.

**Distribution policy assumption:** standard end-user workflows should not require
an external C compiler. Runtime/stdlib artifacts are expected to be prebuilt and
packaged. External system compiler usage is considered an explicit advanced
fallback for development/customization scenarios.

### 3. Tests (Windows)

```powershell
$env:YONA_PATH = "$(Resolve-Path test/code);$(Resolve-Path lib)"
# Optional but recommended if clang is not already on PATH:
$env:YONAC_CC = "$env:LLVM_INSTALL_PREFIX\bin\clang.exe"   # e.g. C:\LLVM\bin\clang.exe
ctest --preset unit-tests-windows
```

### Troubleshooting (Windows)

- **`DIASDK::Diaguids` / `diaguids.lib` missing during `find_package(LLVM)`**
  Install the **Desktop development with C++** workload (including the DIA SDK)
  for Visual Studio or Build Tools. Yona creates the required CMake target before
  it loads LLVM and locates it through the active Visual Studio installation or
  `vswhere`. If it lives elsewhere, configure with
  `-DYONA_DIA_SDK_LIBRARY=C:\path\to\diaguids.lib`; do not patch LLVM's exported
  target files or create Visual Studio-directory junctions.

- **`ZLIB::ZLIB` missing during `find_package(LLVM)`**
  Install a normal Zlib CMake package, or leave `YONA_FETCH_DEPS=ON` so CMake
  builds Yona's pinned Zlib dependency. For offline/package builds with
  `YONA_FETCH_DEPS=OFF`, supplying the system CMake package is required.

- **`Policy CMP0167 is not known`**
  Use a newer CMake (3.30+), or use a checkout that guards optional policies for older CMake (supported on 3.29).

- **`LLVMConfig.cmake` not found**
  Set `LLVM_INSTALL_PREFIX` to the **root** of the extracted LLVM tree (the directory that contains `bin`, `lib`, `include`).

- **`find_package(LLVM)` / `LLVMExports.cmake`: imported target references missing `LTO.lib`, `LTO.dll`, `llvm-ar.exe`, etc.**
  Your prefix is not a full **`clang+llvm-…-windows-msvc`** tree, or files were deleted. Re-extract the official archive to a clean directory and point **`LLVM_INSTALL_PREFIX`** (and **`CC`/`CXX`**, if separate) at it.

## Docker

```bash
docker build -t yona .
docker run --rm yona yona -e '"hello world"'
docker run --rm -it yona  # interactive REPL
```

## Verifying the Installation

```bash
# Compile and run a simple expression
yona -e '1 + 2'
# Output: 3

# Compile a file
echo 'let fib n = if n <= 1 then n else fib (n-1) + fib (n-2) in fib 10' > fib.yona
yonac fib.yona -o fib
./fib
# Output: 55

# Interactive REPL
yona
```

On Windows, use the same commands after adding `out\build\x64-release` (or your preset’s output directory) to `PATH`, or invoke the full path, for example `.\out\build\x64-release\yonac.exe -e "1 + 2"`.

## Running Tests

```bash
# Via CTest (Linux)
ctest --preset unit-tests-linux

# Via CTest (Windows)
ctest --preset unit-tests-windows

# Via CTest (Windows ARM64)
ctest --preset unit-tests-windows-arm64

# Via CTest (macOS Apple Silicon)
ctest --preset unit-tests-macos-arm64

# Directly (Linux / macOS)
./out/build/x64-release-linux/tests
```

Directly on **Windows** (after configuring the matching preset), from PowerShell in the repo root:

```powershell
$env:YONA_PATH = "$(Resolve-Path test/code);$(Resolve-Path lib)"
$env:YONAC_CC = "${env:LLVM_INSTALL_PREFIX}\bin\clang.exe"   # or any full path to clang.exe; optional if clang is on PATH
.\out\build\x64-release\tests.exe
```

If **`clang`** is not on **`PATH`**, set **`YONAC_CC`** as above so codegen/link tests can compile the runtime objects (see **Complete Windows LLVM tree** and **`YONAC_CC` and doctest** in the Windows section).
