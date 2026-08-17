#!/usr/bin/env python3
"""Summarize CTest/JUnit XML for the GitHub step summary. Always exits 0."""

from __future__ import annotations

import argparse
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def counts(path: Path) -> tuple[int, int, int]:
    tree = ET.parse(path)
    root = tree.getroot()
    if root.tag.endswith("testsuite"):
        return (
            int(root.attrib.get("tests", 0)),
            int(root.attrib.get("failures", 0)),
            int(root.attrib.get("errors", 0)),
        )
    tests = failures = errors = 0
    if "tests" in root.attrib:
        return (
            int(root.attrib.get("tests", 0)),
            int(root.attrib.get("failures", 0)),
            int(root.attrib.get("errors", 0)),
        )
    for suite in root.iter():
        if suite is root or not suite.tag.endswith("testsuite"):
            continue
        tests += int(suite.attrib.get("tests", 0))
        failures += int(suite.attrib.get("failures", 0))
        errors += int(suite.attrib.get("errors", 0))
    return tests, failures, errors


def append_summary(text: str) -> None:
    dest = os.environ.get("GITHUB_STEP_SUMMARY")
    if not dest:
        print(text, end="")
        return
    with open(dest, "a", encoding="utf-8") as fh:
        fh.write(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", default="")
    parser.add_argument("--table", action="store_true")
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()

    rows: list[tuple[str, int, int, int, int]] = []
    for path in args.files:
        if not path.is_file():
            print(f"missing {path}", file=sys.stderr)
            continue
        try:
            tests, failures, errors = counts(path)
        except ET.ParseError as exc:
            print(f"unreadable {path}: {exc}", file=sys.stderr)
            continue
        label = args.title or path.parent.name or path.name
        if not args.title and label.startswith("test-results-"):
            label = label[len("test-results-") :]
        rows.append((label, tests, failures, errors, tests - failures - errors))

    if not rows:
        append_summary(f"⚠️ No test results found for {args.title or 'this job'}\n")
        return 0

    if args.table:
        lines = [
            "# Test Results Summary\n",
            "\n",
            "| Platform | Total | Passed | Failed | Errors | Status |\n",
            "|----------|-------|--------|--------|--------|--------|\n",
        ]
        tot = fail = err = passed = 0
        for name, tests, failures, errors, ok in rows:
            tot += tests
            fail += failures
            err += errors
            passed += ok
            status = "ok" if failures == 0 and errors == 0 else "fail"
            lines.append(f"| {name} | {tests} | {ok} | {failures} | {errors} | {status} |\n")
        lines.append(f"| **TOTAL** | **{tot}** | **{passed}** | **{fail}** | **{err}** | |\n")
        if tot:
            lines.append(f"\nSuccess rate: {(passed * 100) // tot}%\n")
        append_summary("".join(lines))
        return 0

    name, tests, failures, errors, ok = rows[0]
    append_summary(
        f"### Test Results for {name}\n"
        f"\n"
        f"| Status | Count |\n"
        f"|--------|-------|\n"
        f"| Passed | {ok} |\n"
        f"| Failed | {failures} |\n"
        f"| Errors | {errors} |\n"
        f"| Total | {tests} |\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
