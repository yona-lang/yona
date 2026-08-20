Name:           yona
Version:        0.1.4
Release:        1%{?dist}
Summary:        Yona programming language compiler targeting LLVM
License:        GPL-3.0-only
URL:            https://github.com/yona-lang/yonac-llvm
Source0:        https://github.com/yona-lang/yonac-llvm/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  cmake >= 3.10
BuildRequires:  ninja-build
BuildRequires:  clang
BuildRequires:  lld
BuildRequires:  lld-devel
BuildRequires:  llvm-devel >= 16
BuildRequires:  gcc-c++
BuildRequires:  pcre2-devel
BuildRequires:  pkgconf
BuildRequires:  libxml2-devel
# cmake(CLI11) — do not FetchContent in mock.
BuildRequires:  cli11-devel
# Strip leftover DT_RUNPATH from the v0.1.2 tarball's shared CLI.
BuildRequires:  patchelf

Requires:       llvm-libs >= 16
Requires:       clang
Requires:       lld
Requires:       pcre2

%description
Yona is a compiled functional programming language that targets LLVM.
Features include algebraic data types, pattern matching, traits,
persistent data structures, async I/O via io_uring, and a comprehensive
standard library.

%prep
%autosetup -n yonac-llvm-%{version}

%build
# SKIP_BUILD_RPATH: v0.1.2 CMake links yonac to in-tree libyona_lib.so and
# embeds the mock BUILD dir as DT_RUNPATH (Fedora check-rpaths 0x0002).
# YONA_LINK_STATIC_CLI is used from v0.1.3; ignored as unused on v0.1.2.
cmake --preset x64-release-linux \
    -DBUILD_TESTING=OFF \
    -DYONA_FETCH_DEPS=OFF \
    -DYONA_FETCH_LIBXML2=OFF \
    -DYONA_LINK_STATIC_CLI=ON \
    -DCMAKE_SKIP_BUILD_RPATH=ON
cmake --build --preset build-release-linux

%install
install -Dm755 out/build/x64-release-linux/yonac %{buildroot}%{_bindir}/yonac
install -Dm755 out/build/x64-release-linux/yona %{buildroot}%{_bindir}/yona
# v0.1.2 Source0 links the CLI to in-tree libyona_lib.so; ship it in %%{_libdir}
# so ld.so finds it after RUNPATH is stripped.
install -Dm755 out/build/x64-release-linux/libyona_lib.so \
    %{buildroot}%{_libdir}/libyona_lib.so
patchelf --remove-rpath %{buildroot}%{_bindir}/yonac
patchelf --remove-rpath %{buildroot}%{_bindir}/yona

install -d %{buildroot}%{_libdir}/yona/lib/Std
cp -a lib/Std/. %{buildroot}%{_libdir}/yona/lib/Std/

install -d %{buildroot}%{_libdir}/yona/src/runtime/platform
install -d %{buildroot}%{_libdir}/yona/include/yona/runtime
install -d %{buildroot}%{_libdir}/yona/runtime
if [ -d out/build/x64-release-linux/runtime ]; then
    cp -a out/build/x64-release-linux/runtime/. %{buildroot}%{_libdir}/yona/runtime/
fi
install -Dm644 src/compiled_runtime.c %{buildroot}%{_libdir}/yona/src/compiled_runtime.c
cp -a src/runtime/. %{buildroot}%{_libdir}/yona/src/runtime/
cp -a include/yona/runtime/. %{buildroot}%{_libdir}/yona/include/yona/runtime/

%files
%license LICENSE.txt
%doc README.md
%{_bindir}/yonac
%{_bindir}/yona
%{_libdir}/libyona_lib.so
%{_libdir}/yona/

%changelog
* Thu Aug 20 2026 Adam Kovari <adam@kovari.eu> - 0.1.4-1
- Version 0.1.4; let-if-else no longer consumes closing in as membership

* Tue Aug 18 2026 Adam Kovari <adam@kovari.eu> - 0.1.3-1
- Version 0.1.3; ship libyona_lib.so and strip build-dir RUNPATH for Fedora check-rpaths

* Tue Aug 18 2026 Adam Kovari <adam@kovari.eu> - 0.1.2-2
- Install libyona_lib.so when the CLI is dynamically linked; strip build-dir RUNPATH (Copr check-rpaths)

* Tue Aug 18 2026 Adam Kovari <adam@kovari.eu> - 0.1.2-1
- Version 0.1.2; native CLI11/LLD packaging and in-process LLD ELF/Darwin fixes

* Tue Aug 18 2026 Adam Kovari <adam@kovari.eu> - 0.1.1-2
- BuildRequires cli11-devel, libxml2-devel, and lld-devel; configure with -DYONA_FETCH_DEPS=OFF
- Pass -DBUILD_TESTING=OFF so doctest is not required

* Mon Aug 17 2026 Adam Kovari <adam@kovari.eu> - 0.1.1-1
- Copr/AUR/PPA packaging; sysroot under %%{_libdir}/yona

* Sun Apr 06 2025 Yona Team <team@yona-lang.org> - 0.1.0-1
- Initial package
