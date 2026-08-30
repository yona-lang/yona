#!/usr/bin/env python3
"""Verify the canonical compiler and runtime component graph."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPONENT_FILE = ROOT / "cmake" / "YonaComponents.cmake"

COMPILER_DEPENDENCIES = {
    "yona_support": set(),
    "yona_model": {"yona_support"},
    "yona_syntax": {"yona_model", "yona_support"},
    "yona_interface": {"yona_model"},
    "yona_semantics": {
        "yona_interface",
        "yona_model",
        "yona_support",
        "yona_syntax",
    },
    "yona_typed_ir": {"yona_semantics"},
    "yona_codegen_llvm": {"yona_typed_ir"},
    "yona_toolchain": {"yona_codegen_llvm"},
    "yona_lsp": {"yona_semantics"},
    "yona_typed_core": {"yona_semantics"},
}

SOURCE_ROOTS = {
    "yona_support": "Support",
    "yona_model": "Model",
    "yona_syntax": "Syntax",
    "yona_interface": "Interface",
    "yona_semantics": "Semantics",
    "yona_typed_ir": "TypedIr",
    "yona_codegen_llvm": "Codegen",
    "yona_toolchain": "Toolchain",
    "yona_lsp": "Lsp",
    "yona_typed_core": "TypedCore",
    "yona_runtime_core": "Runtime/Core",
    "yona_runtime_collections": "Runtime/Collections",
    "yona_runtime_codecs": "Runtime/Codecs",
    "yona_runtime_concurrency": "Runtime/Concurrency",
    "yona_runtime_gpu": "Runtime/Gpu",
    "yona_runtime_platform_io": "Runtime/Platform",
    "yona_runtime_stdlib": "Runtime/Stdlib",
}

RUNTIME_COMPONENTS = {
    target for target in SOURCE_ROOTS if target.startswith("yona_runtime_")
}


def cmake_calls(text: str, command: str) -> list[tuple[str, str]]:
    calls: list[tuple[str, str]] = []
    pattern = re.compile(rf"(?m)^\s*{re.escape(command)}\s*\(")
    for match in pattern.finditer(text):
        begin = match.end()
        depth = 1
        quoted = False
        escaped = False
        end = begin
        while end < len(text) and depth:
            character = text[end]
            if escaped:
                escaped = False
            elif character == "\\" and quoted:
                escaped = True
            elif character == '"':
                quoted = not quoted
            elif not quoted and character == "(":
                depth += 1
            elif not quoted and character == ")":
                depth -= 1
            end += 1
        if depth:
            raise ValueError(
                f"unterminated {command} call at byte {match.start()}"
            )
        body = text[begin : end - 1]
        target_match = re.match(r"\s*([A-Za-z0-9_]+)(.*)", body, re.DOTALL)
        if target_match:
            calls.append((target_match.group(1), target_match.group(2)))
    return calls


def target_dependencies(text: str) -> dict[str, set[str]]:
    dependencies: dict[str, set[str]] = {}
    for target, body in cmake_calls(text, "target_link_libraries"):
        dependencies.setdefault(target, set()).update(
            re.findall(r"\byona_[a-z0-9_]+\b", body)
        )
    return dependencies


def object_sources(text: str) -> dict[str, list[str]]:
    sources: dict[str, list[str]] = {}
    for target, body in cmake_calls(text, "add_library"):
        if not re.match(r"\s+OBJECT\b", body):
            continue
        sources[target] = re.findall(
            r'"\$\{PROJECT_SOURCE_DIR\}/src/([^"$]+)"', body
        )
    return sources


def verify_dependencies(text: str, errors: list[str]) -> None:
    actual = target_dependencies(text)
    for target, expected in COMPILER_DEPENDENCIES.items():
        found = actual.get(target, set())
        if found != expected:
            errors.append(
                f"{target}: project dependencies {sorted(found)}; "
                f"expected {sorted(expected)}"
            )


def verify_source_ownership(text: str, errors: list[str]) -> None:
    sources = object_sources(text)
    owners: dict[str, str] = {}
    for target, root in SOURCE_ROOTS.items():
        owned = sources.get(target)
        if owned is None:
            errors.append(f"{target}: missing explicit OBJECT source list")
            continue
        if not owned and target not in {
            "yona_runtime_concurrency",
            "yona_runtime_platform_io",
        }:
            errors.append(f"{target}: explicit source list is empty")
        for source in owned:
            if not (source == root or source.startswith(f"{root}/")):
                errors.append(
                    f"{target}: src/{source} belongs outside src/{root}/"
                )
            previous = owners.setdefault(source, target)
            if previous != target:
                errors.append(
                    f"src/{source}: compiled by both {previous} and {target}"
                )

    aggregate_match = re.search(
        r"(?ms)add_library\s*\(\s*yona_runtime\s+STATIC(.*?)^\s*\)",
        text,
    )
    if not aggregate_match:
        errors.append("yona_runtime: missing aggregate static archive")
        return
    aggregate = set(
        re.findall(
            r"TARGET_OBJECTS:(yona_runtime_[a-z0-9_]+)",
            aggregate_match.group(1),
        )
    )
    if aggregate != RUNTIME_COMPONENTS:
        errors.append(
            f"yona_runtime: object components {sorted(aggregate)}; "
            f"expected {sorted(RUNTIME_COMPONENTS)}"
        )


def main() -> int:
    text = COMPONENT_FILE.read_text(encoding="utf-8")
    errors: list[str] = []
    verify_dependencies(text, errors)
    verify_source_ownership(text, errors)
    if errors:
        for error in errors:
            print(f"architecture: {error}", file=sys.stderr)
        return 1
    print(
        "architecture graph passed "
        f"({len(COMPILER_DEPENDENCIES)} compiler targets, "
        f"{len(RUNTIME_COMPONENTS)} runtime components)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
