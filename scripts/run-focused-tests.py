#!/usr/bin/env python3
"""Run doctest filters only after proving every positive pattern matches."""

from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Filter:
    option: str
    value: str


ALIASES = {
    "-tc": "-tc",
    "--test-case": "-tc",
    "-ts": "-ts",
    "--test-suite": "-ts",
}
SUBCASES = {"-sc", "--subcase"}


def parse_filters(arguments: list[str]) -> list[Filter]:
    result: list[Filter] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        name, separator, inline = argument.partition("=")
        if name in SUBCASES:
            raise ValueError("focused test runner rejects subcase filters")
        if name in ALIASES:
            if separator:
                value = inline
            else:
                index += 1
                if index == len(arguments):
                    raise ValueError(f"{name} requires a value")
                value = arguments[index]
            for pattern in value.split(","):
                if pattern and not pattern.startswith("~"):
                    result.append(Filter(ALIASES[name], pattern))
        index += 1
    return result


def count_arguments(arguments: list[str], selected: Filter) -> list[str]:
    result: list[str] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        name, separator, _ = argument.partition("=")
        if name in ALIASES:
            canonical = ALIASES[name]
            if not separator:
                index += 1
            if canonical == selected.option:
                index += 1
                continue
        result.append(argument)
        index += 1
    result.append(f"{selected.option}={selected.value}")
    result.extend(["--count", "--no-version"])
    return result


def matching_count(output: str) -> int | None:
    match = re.search(r"passing the current filters:\s*(\d+)\s*$", output)
    if match:
        return int(match.group(1))
    stripped = output.strip()
    return int(stripped) if stripped.isdecimal() else None


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: run-focused-tests.py <test-executable> <doctest-filters...>", file=sys.stderr)
        return 2
    executable, arguments = sys.argv[1], sys.argv[2:]
    try:
        filters = parse_filters(arguments)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    for selected in filters:
        completed = subprocess.run(
            [executable, *count_arguments(arguments, selected)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        count = matching_count(completed.stdout)
        if completed.returncode != 0 or count is None:
            print(f"could not enumerate focused filter {selected.value!r}", file=sys.stderr)
            print(completed.stdout, end="", file=sys.stderr)
            return completed.returncode or 2
        if count == 0:
            print(f"focused filter {selected.value!r} matched zero test cases", file=sys.stderr)
            return 2
    return subprocess.run([executable, *arguments], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
