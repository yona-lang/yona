#!/usr/bin/env python3
"""Render CTest/JUnit XML for the GitHub step summary. Always exits 0.

Lists every test case grouped by suite. CTest's own --output-junit only has
one wrapper case per add_test(); prefer doctest-results.xml from the
YONA_DOCTEST_JUNIT listener when present.
"""

from __future__ import annotations

import argparse
import os
import sys
import xml.etree.ElementTree as ET
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Case:
    name: str
    suite: str
    status: str  # passed | failed | error | skipped
    message: str = ""


@dataclass
class Report:
    label: str
    cases: list[Case] = field(default_factory=list)

    @property
    def passed(self) -> int:
        return sum(1 for c in self.cases if c.status == "passed")

    @property
    def failed(self) -> int:
        return sum(1 for c in self.cases if c.status == "failed")

    @property
    def errors(self) -> int:
        return sum(1 for c in self.cases if c.status == "error")

    @property
    def skipped(self) -> int:
        return sum(1 for c in self.cases if c.status == "skipped")

    @property
    def total(self) -> int:
        return len(self.cases)


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _text(el: ET.Element | None) -> str:
    if el is None:
        return ""
    msg = (el.attrib.get("message") or "").strip()
    body = (el.text or "").strip()
    if msg and body:
        return f"{msg}: {body}" if body not in msg else msg
    return msg or body


def _case_status(el: ET.Element) -> tuple[str, str]:
    error = fail = skip = None
    for child in el:
        name = _local(child.tag)
        if name == "error":
            error = child
        elif name == "failure":
            fail = child
        elif name == "skipped":
            skip = child
    if error is not None:
        return "error", _text(error)
    if fail is not None:
        return "failed", _text(fail)
    if skip is not None:
        return "skipped", _text(skip)
    return "passed", ""


def parse_cases(path: Path) -> list[Case]:
    tree = ET.parse(path)
    root = tree.getroot()
    cases: list[Case] = []
    suites = (
        [root]
        if _local(root.tag) == "testsuite"
        else [el for el in root if _local(el.tag) == "testsuite"]
    )
    if not suites and _local(root.tag) == "testsuites":
        suites = [
            el
            for el in root.iter()
            if el is not root and _local(el.tag) == "testsuite"
        ]
    for suite in suites:
        suite_name = suite.attrib.get("name") or "(ungrouped)"
        for el in suite:
            if _local(el.tag) != "testcase":
                continue
            name = el.attrib.get("name") or "(unnamed)"
            classname = el.attrib.get("classname") or ""
            group = classname if classname and classname != name else suite_name
            status, message = _case_status(el)
            cases.append(
                Case(name=name, suite=group, status=status, message=message)
            )
    return cases


def is_ctest_wrapper(cases: list[Case]) -> bool:
    if not cases or len(cases) > 4:
        return False
    wrappers = {"doctest_tests", "doctest_gpu_vulkan"}
    return all(c.name in wrappers for c in cases)


def label_for(path: Path, title: str) -> str:
    if title:
        return title
    label = path.parent.name or path.name
    if label.startswith("test-results-"):
        label = label[len("test-results-") :]
    return label


def load_reports(paths: list[Path], title: str) -> list[Report]:
    by_label: OrderedDict[str, Report] = OrderedDict()
    leftovers: list[tuple[str, list[Case]]] = []
    for path in paths:
        if not path.is_file():
            print(f"missing {path}", file=sys.stderr)
            continue
        try:
            cases = parse_cases(path)
        except ET.ParseError as exc:
            print(f"unreadable {path}: {exc}", file=sys.stderr)
            continue
        label = label_for(path, title)
        leftovers.append((label, cases))

    detailed_labels = {
        label
        for label, cases in leftovers
        if cases and not is_ctest_wrapper(cases)
    }
    for label, cases in leftovers:
        if is_ctest_wrapper(cases) and label in detailed_labels:
            continue
        report = by_label.get(label)
        if report is None:
            report = Report(label=label)
            by_label[label] = report
        report.cases.extend(cases)
    return list(by_label.values())


def group_suites(cases: list[Case]) -> OrderedDict[str, list[Case]]:
    suites: OrderedDict[str, list[Case]] = OrderedDict()
    for case in cases:
        suites.setdefault(case.suite, []).append(case)
    return suites


def md_cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ").strip()


def render_suite_tables(cases: list[Case]) -> list[str]:
    lines: list[str] = []
    for suite, suite_cases in group_suites(cases).items():
        show_detail = any(c.message for c in suite_cases)
        lines.append(f"\n#### {suite}\n\n")
        if show_detail:
            lines.append(
                "| Test | Status | Detail |\n|------|--------|--------|\n"
            )
            for c in suite_cases:
                lines.append(
                    f"| {md_cell(c.name)} | {c.status} | {md_cell(c.message)} |\n"
                )
        else:
            lines.append("| Test | Status |\n|------|--------|\n")
            for c in suite_cases:
                lines.append(f"| {md_cell(c.name)} | {c.status} |\n")
    return lines


def render_counts(report: Report, heading: str) -> list[str]:
    status = "ok" if report.failed == 0 and report.errors == 0 else "fail"
    return [
        f"{heading}\n\n",
        "| Status | Count |\n|--------|-------|\n",
        f"| Passed | {report.passed} |\n",
        f"| Failed | {report.failed} |\n",
        f"| Errors | {report.errors} |\n",
        f"| Skipped | {report.skipped} |\n",
        f"| Total | {report.total} |\n",
        f"\nResult: **{status}**\n",
    ]


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

    reports = load_reports(args.files, args.title if not args.table else "")
    if not reports:
        append_summary(
            f"⚠️ No test results found for {args.title or 'this job'}\n"
        )
        return 0

    if args.table:
        lines = [
            "# Test Results Summary\n\n",
            "| Platform | Total | Passed | Failed | Errors | Skipped | Status |\n",
            "|----------|-------|--------|--------|--------|---------|--------|\n",
        ]
        tot = passed = fail = err = skipped = 0
        for report in reports:
            tot += report.total
            passed += report.passed
            fail += report.failed
            err += report.errors
            skipped += report.skipped
            status = (
                "ok" if report.failed == 0 and report.errors == 0 else "fail"
            )
            lines.append(
                f"| {report.label} | {report.total} | {report.passed} | {report.failed} | "
                f"{report.errors} | {report.skipped} | {status} |\n"
            )
        lines.append(
            f"| **TOTAL** | **{tot}** | **{passed}** | **{fail}** | **{err}** | **{skipped}** | |\n"
        )
        executed = tot - skipped
        if executed:
            lines.append(
                f"\nSuccess rate: {(passed * 100) // executed}% of executed cases\n"
            )
        for report in reports:
            lines.append(f"\n## {report.label}\n")
            lines.extend(render_counts(report, "")[1:])
            lines.extend(render_suite_tables(report.cases))
        append_summary("".join(lines))
        return 0

    report = reports[0]
    for extra in reports[1:]:
        report.cases.extend(extra.cases)
    lines = render_counts(report, f"### Test Results for {report.label}")
    lines.extend(render_suite_tables(report.cases))
    append_summary("".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
