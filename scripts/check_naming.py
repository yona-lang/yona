#!/usr/bin/env python3
"""Enforce repository path, header, module, and retired-name conventions."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

NATIVE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
NATIVE_ROOTS = {"bench", "cli", "include", "repl", "src", "test", "tools"}
PYTHON_SUFFIXES = {".py"}
SCRIPT_SUFFIXES = {".ps1", ".psd1", ".psm1", ".sh"}
SHADER_SUFFIXES = {".comp", ".inc", ".inl"}

UPPER_CAMEL = re.compile(r"^[A-Z][A-Za-z0-9]*$")
LOWER_SNAKE = re.compile(r"^[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")
LOWER_KEBAB = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
MODULE_DECL = re.compile(
    r"(?m)^\s*module\s+"
    r"([A-Za-z][A-Za-z0-9]*(?:\\[A-Za-z][A-Za-z0-9]*)*)\s*$"
)
INTERFACE_MODULE_DECL = re.compile(
    r"(?m)^MODULE\s+"
    r"([A-Za-z][A-Za-z0-9]*(?:\\[A-Za-z][A-Za-z0-9]*)*)\s*$"
)
USING_NAMESPACE = re.compile(r"(?m)^\s*using\s+namespace\s+")
NONCANONICAL_ACRONYM = re.compile(
    r"(?:ABI|CFFI|GPU|IO|JSON|LLVM|LSP|RPC|UTF(?:8|16|32)?)(?=[A-Z0-9_]|$)"
)
NONCANONICAL_EXPORT = re.compile(r"\byona_(?:Std|rt|typed_core)[A-Za-z0-9_]*")
NONCANONICAL_PUBLIC_ACRONYM = re.compile(
    r"\bYona[A-Za-z0-9]*(?:ABI|CFFI|GPU|IO|JSON|LLVM|LSP|RPC|UTF(?:8|16|32)?)[A-Za-z0-9]*\b"
)
NONCANONICAL_TASK_MARKER = re.compile(r"\bTO" r"DO\b(?!\([^)]+\):)")

# Spell these as split literals so this check can enforce their complete
# removal without matching its own source.
RETIRED_ANALYZERS = (
    re.compile("qod" r"ana", re.IGNORECASE),
    re.compile("code" r"ql", re.IGNORECASE),
)
RETIRED_YONA_NAMES = (
    (re.compile(r"\bStd\\G" r"PU\b"), r"use Std\Gpu"),
    (re.compile(r"\bStd\\I" r"O\b"), r"use Std\Io"),
    (re.compile(r"\bmapG" r"PU\b"), "use mapGpu"),
    (re.compile(r"\breduceG" r"PU\b"), "use reduceGpu"),
)
RETIRED_ABI_VERSIONING = (
    re.compile(r"\b[A-Z0-9_]*ABI_" r"VERSION(?:_STRING)?\b"),
    re.compile(r"\babi_" r"version\b"),
    re.compile(r"\babi" r"Version\b"),
    re.compile(r"\bversioned\s+(?:C\s+)?A" r"BI\b", re.IGNORECASE),
    re.compile(r"\bA" r"BI\s+v[0-9]+\b", re.IGNORECASE),
)

CODE_SUFFIXES = NATIVE_SUFFIXES | {
    ".cmake",
    ".js",
    ".mjs",
    ".py",
    ".ps1",
    ".sh",
    ".ts",
    ".tsx",
    ".yona",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        help="files to check; defaults to every tracked file",
    )
    parser.add_argument(
        "--max-errors",
        type=int,
        default=200,
        help="maximum diagnostics to print (0 means unlimited)",
    )
    return parser.parse_args()


def tracked_files(paths: list[str]) -> list[Path]:
    if paths:
        candidates = [Path(item) for item in paths]
    else:
        result = subprocess.run(
            [
                "git",
                "ls-files",
                "--cached",
                "--others",
                "--exclude-standard",
                "-z",
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
        )
        candidates = [
            Path(item.decode("utf-8"))
            for item in result.stdout.split(b"\0")
            if item
        ]

    files: set[Path] = set()
    for candidate in candidates:
        absolute = candidate if candidate.is_absolute() else ROOT / candidate
        if not absolute.is_file():
            continue
        try:
            files.add(absolute.resolve().relative_to(ROOT))
        except ValueError:
            continue
    return sorted(files, key=lambda item: item.as_posix())


def split_words(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").upper()


def expected_guard(path: Path) -> str:
    parts = list(path.with_suffix("").parts)
    if parts and parts[0] == "include":
        parts.pop(0)
    if parts and parts[0] == "yona":
        parts.pop(0)
    words = [split_words(part) for part in parts]
    suffix = split_words(path.suffix.lstrip("."))
    return "_".join(["YONA", *words, suffix])


def check_upper_camel(value: str, description: str, emit) -> None:
    if not UPPER_CAMEL.fullmatch(value):
        emit(f"{description} must be UpperCamelCase: {value}")
    elif NONCANONICAL_ACRONYM.search(value):
        emit(f"{description} contains an all-caps acronym: {value}")


def check_path(path: Path, emit) -> None:
    suffix = path.suffix.lower()
    stem = path.stem
    parts = path.parts

    if suffix == ".spv":
        emit(
            "checked-in SPIR-V binaries are forbidden; generate an .inc fragment"
        )

    if suffix in NATIVE_SUFFIXES:
        check_upper_camel(stem, "native filename", emit)

        if parts[0] in NATIVE_ROOTS:
            for index, component in enumerate(parts[1:-1], start=1):
                if parts[0] == "include" and index == 1 and component == "yona":
                    continue
                check_upper_camel(component, "native component directory", emit)

    # This mock must retain LLVM's externally defined package filename.
    is_llvm_config_mock = (
        path.parts[:3]
        == (
            "test",
            "CMake",
            "WindowsLlvmPrerequisites",
        )
        and stem == "LLVMConfig"
    )
    if (
        path.name != "CMakeLists.txt"
        and suffix == ".cmake"
        and not is_llvm_config_mock
    ):
        check_upper_camel(stem, "CMake module filename", emit)

    if suffix in PYTHON_SUFFIXES and not LOWER_SNAKE.fullmatch(stem):
        emit(f"Python filename must be lower_snake_case: {path.name}")

    if (
        parts[:2] == ("test", "Fixtures")
        and suffix in {".expected", ".yona"}
        and not LOWER_SNAKE.fullmatch(stem)
    ):
        emit(f"fixture filename must be lower_snake_case: {path.name}")

    if suffix in SCRIPT_SUFFIXES and not LOWER_KEBAB.fullmatch(stem):
        emit(f"script filename must be lower-kebab-case: {path.name}")

    if (
        parts[:2] == (".github", "workflows")
        and suffix in {".yaml", ".yml"}
        and not LOWER_KEBAB.fullmatch(stem)
    ):
        emit(f"workflow filename must be lower-kebab-case: {path.name}")

    if suffix in SHADER_SUFFIXES:
        if "Generated" not in parts:
            emit(
                f"generated shader must live under a Generated directory: {path}"
            )
        check_upper_camel(stem, "generated shader filename", emit)


def read_text(path: Path) -> str | None:
    try:
        return (ROOT / path).read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return None


def check_header(path: Path, text: str, emit) -> None:
    if "#pragma once" in text:
        emit("headers must use a path-derived guard, not #pragma once")

    guard = expected_guard(path)
    if not re.search(rf"(?m)^#ifndef\s+{re.escape(guard)}\s*$", text):
        emit(f"missing canonical #ifndef {guard}")
    if not re.search(rf"(?m)^#define\s+{re.escape(guard)}\s*$", text):
        emit(f"missing canonical #define {guard}")

    match = USING_NAMESPACE.search(text)
    if match:
        line = text.count("\n", 0, match.start()) + 1
        emit(f"using namespace is forbidden in headers (line {line})")


def canonical_module_segment(segment: str) -> bool:
    canonical_case = bool(UPPER_CAMEL.fullmatch(segment))
    return canonical_case and not NONCANONICAL_ACRONYM.search(segment)


def check_yona_module(path: Path, text: str, emit) -> None:
    if path.parts[0] != "lib":
        return

    if path.suffix == ".yonai":
        match = INTERFACE_MODULE_DECL.search(text)
        if not match:
            emit("stdlib interface has no canonical MODULE record")
            return
        module = match.group(1)
        expected = "\\".join(path.with_suffix("").parts[1:])
        if module != expected:
            emit(f"MODULE identity {module!r} must match path {expected!r}")
        for segment in module.split("\\"):
            if not canonical_module_segment(segment):
                emit(f"module segment must use canonical PascalCase: {segment}")
        return

    if path.suffix != ".yona":
        return

    match = MODULE_DECL.search(text)
    if not match:
        emit("stdlib source has no module declaration")
        return

    module = match.group(1)
    expected = "\\".join(path.with_suffix("").parts[1:])
    if module != expected:
        emit(f"module declaration {module!r} must match path {expected!r}")

    for segment in module.split("\\"):
        if not canonical_module_segment(segment):
            emit(f"module segment must use canonical PascalCase: {segment}")


def check_content(path: Path, text: str, emit) -> None:
    for analyzer in RETIRED_ANALYZERS:
        if analyzer.search(text):
            emit("reference to a retired hosted analyzer")

    for retired_name, guidance in RETIRED_YONA_NAMES:
        if retired_name.search(text):
            emit(f"reference to a retired Yona name; {guidance}")

    for version_marker in RETIRED_ABI_VERSIONING:
        if version_marker.search(text):
            emit("ABI format numbering is forbidden in the clean-slate API")

    public_acronym = NONCANONICAL_PUBLIC_ACRONYM.search(text)
    if public_acronym:
        emit(
            "public Yona symbol contains a noncanonical acronym: "
            f"{public_acronym.group(0)}"
        )

    if path.suffix.lower() in NATIVE_SUFFIXES:
        match = NONCANONICAL_EXPORT.search(text)
        if match:
            line = text.count("\n", 0, match.start()) + 1
            emit(
                f"noncanonical public/export symbol {match.group(0)!r} "
                f"(line {line})"
            )

    if path.suffix.lower() in CODE_SUFFIXES or path.name == "CMakeLists.txt":
        match = NONCANONICAL_TASK_MARKER.search(text)
        if match:
            line = text.count("\n", 0, match.start()) + 1
            marker = "".join(("TO", "DO"))
            emit(
                f"{marker} must be {marker}(issue-or-owner): reason "
                f"(line {line})"
            )


def main() -> int:
    args = parse_args()
    files = tracked_files(args.paths)
    errors: list[str] = []

    for path in files:

        def emit(message: str, current: Path = path) -> None:
            errors.append(f"{current.as_posix()}: {message}")

        check_path(path, emit)
        text = read_text(path)
        if text is None:
            continue
        if path.suffix.lower() in HEADER_SUFFIXES:
            check_header(path, text, emit)
        check_yona_module(path, text, emit)
        check_content(path, text, emit)

    limit = args.max_errors
    displayed = errors if limit == 0 else errors[:limit]
    for error in displayed:
        print(error, file=sys.stderr)
    if len(displayed) < len(errors):
        omitted = len(errors) - len(displayed)
        print(
            f"... {omitted} additional diagnostic(s) omitted", file=sys.stderr
        )

    if errors:
        print(
            f"naming check failed with {len(errors)} diagnostic(s)",
            file=sys.stderr,
        )
        return 1
    print(f"naming check passed ({len(files)} tracked file(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
