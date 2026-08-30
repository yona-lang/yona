#!/usr/bin/env python3
"""Static contract for native ARM64 CI and Windows MSI packaging."""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def require(
    text: str, pattern: str, description: str, failures: list[str]
) -> None:
    if not re.search(pattern, text, re.MULTILINE | re.DOTALL):
        failures.append(description)


def main() -> int:
    ci = (ROOT / ".github/workflows/cmake-multi-platform.yml").read_text()
    release = (ROOT / ".github/workflows/release.yml").read_text()
    msi = (ROOT / "packaging/windows/build-msi.ps1").read_text()
    async_win32 = (ROOT / "src/Runtime/Concurrency/AsyncWin32.c").read_text()
    failures: list[str] = []

    require(
        ci,
        r"os: windows-11-vs2026-arm\s+name: Windows arm64 Debug\s+"
        r"configure_preset: arm64-debug\s+build_preset: build-debug-arm64\s+"
        r"build_dir: arm64-debug\s+arch: arm64",
        "CI must build Windows ARM64 Debug natively with the ARM64 presets.",
        failures,
    )
    require(
        ci,
        r"os: windows-11-vs2026-arm\s+name: Windows arm64 Release\s+"
        r"configure_preset: arm64-release\s+build_preset: build-release-arm64\s+"
        r"build_dir: arm64-release\s+arch: arm64",
        "CI must build Windows ARM64 Release natively with the ARM64 presets.",
        failures,
    )
    require(
        ci,
        r"os: macos-26\s+name: macOS arm64 Debug\s+"
        r"configure_preset: arm64-debug-macos\s+"
        r"build_preset: build-debug-macos-arm64\s+build_dir: arm64-debug-macos",
        "CI must build macOS ARM64 Debug explicitly on macos-26.",
        failures,
    )
    require(
        ci,
        r"os: macos-26\s+name: macOS arm64 Release\s+"
        r"configure_preset: arm64-release-macos\s+"
        r"build_preset: build-release-macos-arm64\s+build_dir: arm64-release-macos",
        "CI must build macOS ARM64 Release explicitly on macos-26.",
        failures,
    )
    if re.search(r"x64-(?:debug|release)-macos", ci):
        failures.append("CI must not contain Intel macOS presets.")
    require(
        ci,
        r"uses: \./\.github/actions/setup-llvm\s+with:\s+windows-architecture: \$\{\{ matrix\.arch \}\}",
        "CI must pass the Windows architecture to setup-llvm.",
        failures,
    )

    require(
        release,
        r"build-macos:.*?matrix:\s+arch: \[arm64\].*?runs-on: macos-26.*?"
        r"cmake --preset arm64-release-macos.*?cmake --build --preset build-release-macos-arm64.*?"
        r"ctest --test-dir out/build/arm64-release-macos --output-on-failure.*?"
        r"DIST=yona-\$\{VERSION\}-macos-\$\{\{ matrix\.arch \}\}.*?"
        r"name: macos-\$\{\{ matrix\.arch \}\}.*?"
        r"yona-\*-macos-\$\{\{ matrix\.arch \}\}\.tar\.gz",
        "Release must build, test, and package a named macOS ARM64 artifact.",
        failures,
    )
    require(
        release,
        r"build-windows:.*?matrix:\s+include:.*?arch: x64.*?runner: windows-2025.*?"
        r"arch: arm64\s+runner: windows-11-vs2026-arm\s+"
        r"configure_preset: arm64-release\s+build_preset: build-release-arm64\s+"
        r"build_dir: arm64-release\s+runs-on: \$\{\{ matrix\.runner \}\}.*?"
        r"windows-architecture: \$\{\{ matrix\.arch \}\}.*?"
        r"yona-\$version-windows-\$\{\{ matrix\.arch \}\}\.zip.*?"
        r"yona-\$version-windows-\$\{\{ matrix\.arch \}\}\.msi",
        "Release must build and package Windows x64 and ARM64 artifacts on matching runners.",
        failures,
    )

    require(
        msi,
        r"\[ValidateSet\(\"x64\", \"arm64\"\)\]\s*\[string\]\$Architecture",
        "MSI script must validate the x64|arm64 architecture parameter.",
        failures,
    )

    require(
        async_win32,
        r'#include "yona/Runtime/Platform/SjLj\.h".*?YONA_SJLJ_SETJMP\(Jmp\)',
        "Windows async workers must use the shared target-aware SJLJ helper.",
        failures,
    )
    if re.search(r"\b__builtin_setjmp\s*\(", async_win32):
        failures.append(
            "Windows async workers must not call the x64-only __builtin_setjmp directly."
        )
    require(
        msi,
        r"\$architectureOutDir = Join-Path \$absOutDir \$Architecture.*?"
        r'Join-Path \$architectureOutDir "stage".*?'
        r'"yona-\$Version-windows-\$Architecture\.msi".*?'
        r"-arch \$Architecture",
        "MSI script must isolate staging, name the MSI, and select WiX architecture by architecture.",
        failures,
    )

    if failures:
        print("Native ARM64 CI/packaging contract failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("Native ARM64 CI/packaging contract passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
