#!/usr/bin/env python3
"""Generate or verify Yona's embedded Vulkan compute shaders."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "src" / "Runtime" / "Generated"
GLSLANG_VERSION = "16.1.0"

SHADERS = (
    "FilterFlagsToInt32",
    "FilterFlagsToInt64",
    "FilterInclusiveToExclusive",
    "FilterInclusiveToExclusiveInt32",
    "FilterMarkInt32",
    "FilterMarkInt64",
    "FilterMarkLessThanInt32",
    "FilterMarkLessThanInt64",
    "FilterPrefixInclusiveStep",
    "FilterPrefixInclusiveStepInt32",
    "FilterScatterInt32",
    "FilterScatterInt64",
    "Float32Reduce",
    "Float32Scale",
    "Float64MultiplyTwo",
    "Float64Reduce",
    "MapAddInt32",
    "MapAddInt64",
    "MapMultiplyInt32",
    "MapMultiplyInt64",
    "MapSquareInt32",
    "MapSquareInt64",
    "Nop",
    "ReduceBlockInt32",
    "ReduceBlockInt64",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--check",
        action="store_true",
        help="verify that every checked-in fragment is reproducible (default)",
    )
    mode.add_argument(
        "--write",
        action="store_true",
        help="replace every checked-in fragment",
    )
    parser.add_argument(
        "--compiler",
        type=Path,
        help="path to glslang or glslangValidator 16.1.0",
    )
    return parser.parse_args()


def resolve_compiler(requested: Path | None) -> Path:
    environment = os.environ.get("GLSLANG")
    candidates = [
        requested,
        Path(environment) if environment else None,
        Path(found) if (found := shutil.which("glslangValidator")) else None,
        Path(found) if (found := shutil.which("glslang")) else None,
    ]
    compiler = next(
        (
            candidate
            for candidate in candidates
            if candidate and candidate.is_file()
        ),
        None,
    )
    if compiler is None:
        raise RuntimeError(
            "glslang 16.1.0 was not found; pass --compiler or set GLSLANG"
        )

    result = subprocess.run(
        [compiler, "--version"],
        check=False,
        capture_output=True,
        text=True,
    )
    reported = result.stdout + result.stderr
    if result.returncode or not re.search(
        rf"(?m)^Glslang Version: \d+:{re.escape(GLSLANG_VERSION)}$",
        reported,
    ):
        raise RuntimeError(
            f"glslang must be {GLSLANG_VERSION}; reported:\n{reported.strip()}"
        )
    return compiler.resolve()


def compile_shader(compiler: Path, source: Path, output: Path) -> bytes:
    result = subprocess.run(
        [
            compiler,
            "-V",
            "--target-env",
            "vulkan1.0",
            "-e",
            "main",
            source,
            "-o",
            output,
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise RuntimeError(
            f"failed to compile {source.relative_to(ROOT)}:\n"
            f"{result.stdout}{result.stderr}"
        )
    data = output.read_bytes()
    if len(data) % 4:
        raise RuntimeError(f"{source}: SPIR-V byte count is not word aligned")
    if not data.startswith(b"\x03\x02\x23\x07"):
        raise RuntimeError(f"{source}: compiler did not produce SPIR-V")
    return data


def render_fragment(stem: str, data: bytes) -> str:
    symbol = f"YonaGpu{stem}Spv"
    guard = f"YONA_RUNTIME_GENERATED_{re.sub(r'(?<!^)(?=[A-Z])', '_', stem).upper()}_SPV_INC"
    words = struct.unpack(f"<{len(data) // 4}I", data)
    lines = [
        f"/* Generated from {stem}.comp by glslang {GLSLANG_VERSION}; do not edit. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdint.h>",
        "",
        f"static const uint32_t {symbol}[] = {{",
    ]
    for begin in range(0, len(words), 4):
        row = ", ".join(f"0x{word:08x}u" for word in words[begin : begin + 4])
        lines.append(f"  {row},")
    lines.extend(
        (
            "};",
            "",
            f"static const uint32_t {symbol}WordCount =",
            f"    (uint32_t)(sizeof({symbol}) / sizeof({symbol}[0]));",
            "",
            f"#endif /* {guard} */",
            "",
        )
    )
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    compiler = resolve_compiler(args.compiler)
    mismatches: list[Path] = []
    with tempfile.TemporaryDirectory(prefix="yona-gpu-shaders-") as directory:
        temporary = Path(directory)
        for stem in SHADERS:
            source = GENERATED / f"{stem}.comp"
            destination = GENERATED / f"{stem}Spv.inc"
            if not source.is_file():
                raise RuntimeError(f"missing shader source: {source}")
            data = compile_shader(compiler, source, temporary / f"{stem}.spv")
            rendered = render_fragment(stem, data)
            if args.write:
                destination.write_text(rendered, encoding="utf-8", newline="\n")
            elif (
                not destination.is_file()
                or destination.read_text(encoding="utf-8") != rendered
            ):
                mismatches.append(destination)

    if mismatches:
        for path in mismatches:
            print(
                f"generated shader is stale: {path.relative_to(ROOT)}",
                file=sys.stderr,
            )
        print(
            "run scripts/generate_gpu_shaders.py --write with "
            f"glslang {GLSLANG_VERSION}",
            file=sys.stderr,
        )
        return 1
    action = "wrote" if args.write else "verified"
    print(f"{action} {len(SHADERS)} deterministic shader fragments")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"gpu shaders: error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
