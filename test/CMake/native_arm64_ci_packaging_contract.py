#!/usr/bin/env python3
"""Static contract for native ARM64 CI and Windows MSI packaging."""

from __future__ import annotations

import hashlib
import pathlib
import re
import sys
import tempfile

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
    sjlj = (ROOT / "include/yona/Runtime/Platform/SjLj.h").read_text()
    codegen_expr = (ROOT / "src/Codegen/CodegenExpr.cpp").read_text()
    cmake = (ROOT / "CMakeLists.txt").read_text()
    prelude_test = (ROOT / "test/Interface/PreludeInterfaceTest.cpp").read_text()
    codegen_test = (ROOT / "test/Codegen/CodegenTest.cpp").read_text()
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
        sjlj,
        r"stp x19, x20.*?stp x27, x28.*?str d8.*?str d15.*?"
        r"ldr d8.*?ldr d15.*?ldp x19, x20.*?ldp x27, x28",
        "AArch64 runtime SJLJ must preserve all ABI-callee-saved registers.",
        failures,
    )
    require(
        sjlj,
        r'mov x16, %0.*?ldr d8, \[x16, #104\].*?'
        r'ldp x27, x28, \[x16, #88\].*?ldr x17, \[x16, #8\].*?br x17',
        'AArch64 longjmp must keep the restore base in caller-saved x16.',
        failures,
    )
    if re.search(r'yonaSjLjLongJump.*?\[%0', sjlj, re.DOTALL):
        failures.append(
            'AArch64 longjmp must not address through an unconstrained operand '
            'after restoring x19-x28.'
        )
    require(
        codegen_expr,
        r"stp x19, x20.*?stp x27, x28.*?str d8.*?str d15",
        "AArch64 generated try blocks must save all ABI-callee-saved registers.",
        failures,
    )

    require(
        cmake,
        r'set\(YONA_LEGACY_ORACLE_PRELUDE_SOURCE\s+'
        r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/test/Fixtures/LegacyOracle/Prelude\.yona"\)'
        r'.*?set\(YONA_REPLACEMENT_PRELUDE_SOURCE\s+'
        r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/lib/Prelude\.yona"\)',
        "CMake must keep literal, disjoint legacy-oracle and replacement Prelude paths.",
        failures,
    )
    prelude_command = re.search(
        r'add_custom_command\(\s+OUTPUT "\$\{YONA_BUILD_PRELUDE_OBJECT\}".*?\n\)',
        cmake,
        re.DOTALL,
    )
    if prelude_command is None:
        failures.append("CMake must retain the legacy Prelude custom command.")
    else:
        command = prelude_command.group(0)
        if command.count('${YONA_LEGACY_ORACLE_PRELUDE_SOURCE}') != 2:
            failures.append(
                "The legacy Prelude command and dependency must use only its frozen source."
            )
        if (
            '${YONA_REPLACEMENT_PRELUDE_SOURCE}' in command
            or '${CMAKE_CURRENT_SOURCE_DIR}/lib/Prelude.yona"' in command
        ):
            failures.append(
                "The live replacement Prelude source must not enter the legacy target graph."
            )
    require(
        cmake,
        r'add_custom_target\(yona_test_prelude_object\s+'
        r'DEPENDS yona_build_prelude_object\)',
        "The test Prelude target must remain on the frozen legacy target graph.",
        failures,
    )
    require(
        prelude_test,
        r'const auto source_path = repo / "test" / "Fixtures" / '
        r'"LegacyOracle" /\s+"Prelude\.yona";',
        "PreludeInterfaceTest must name the literal legacy-oracle Prelude path.",
        failures,
    )
    require(
        codegen_test,
        r'auto regex_yona = yona::test::repo_root\(\) / "test" / "Fixtures" /\s+'
        r'"LegacyOracle" / "Std" / "Regex\.yona";',
        "CodegenTest must name the literal legacy-oracle Regex path.",
        failures,
    )

    oracle_hashes = {
        "test/Fixtures/LegacyOracle/Prelude.yona":
            "a864cf8e7457e1036b1701ab220341abc663aafe65c67131422075fd49a41dd7",
        "test/Fixtures/LegacyOracle/Std/Regex.yona":
            "832718332da4210473fb31e017505c9937079122ec40970252c7863395b7a0c1",
    }
    with tempfile.TemporaryDirectory(prefix="yona-legacy-oracle-") as temp_dir:
        manifest = pathlib.Path(temp_dir) / "LegacyOracle.sha256"
        manifest.write_text(
            "".join(f"{digest}  {path}\n" for path, digest in oracle_hashes.items())
        )
        for line in manifest.read_text().splitlines():
            expected, relative_path = line.split("  ", 1)
            source = ROOT / relative_path
            if not source.is_file():
                failures.append(f"Missing frozen legacy-oracle source: {relative_path}")
                continue
            actual = hashlib.sha256(source.read_bytes()).hexdigest()
            if actual != expected:
                failures.append(
                    f"Frozen legacy-oracle source changed: {relative_path} "
                    f"(expected {expected}, got {actual})."
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
