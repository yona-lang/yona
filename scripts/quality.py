#!/usr/bin/env python3
"""Run Yona's recursive, local-only quality checks and formatters."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import sysconfig
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QUALITY_ROOT = ROOT / "out" / "quality"

BINARY_SUFFIXES = {
    ".a",
    ".bin",
    ".bmp",
    ".dll",
    ".dylib",
    ".exe",
    ".gif",
    ".ico",
    ".jpeg",
    ".jpg",
    ".lib",
    ".o",
    ".obj",
    ".pdf",
    ".png",
    ".so",
    ".spv",
    ".vsix",
    ".webp",
    ".zip",
}

GENERATED_PREFIXES = (
    "site/generated/",
    "site/src/content/docs/stdlib/",
)
GENERATED_EXACT = {
    "editors/vscode/package-lock.json",
    "site/pnpm-lock.yaml",
}

NATIVE_FORMAT_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
}
NATIVE_COMPILE_ROOTS = {"bench", "cli", "repl", "src", "test", "tools"}
PRETTIER_SUFFIXES = {
    ".astro",
    ".css",
    ".js",
    ".json",
    ".jsonc",
    ".md",
    ".mdc",
    ".mjs",
    ".ts",
    ".tsx",
    ".yaml",
    ".yml",
}


class QualityError(RuntimeError):
    """A deterministic quality check could not complete or failed."""


@dataclass(frozen=True)
class Formatter:
    name: str
    executable: str
    accepts: Callable[[Path], bool]
    check_args: tuple[str, ...]
    fix_args: tuple[str, ...]
    version_22: bool = False


FORMATTERS = (
    Formatter(
        "clang-format",
        "clang-format",
        lambda path: path.suffix.lower() in NATIVE_FORMAT_SUFFIXES,
        ("--dry-run", "--Werror"),
        ("-i",),
        version_22=True,
    ),
    Formatter(
        "gersemi",
        "gersemi",
        lambda path: path.name == "CMakeLists.txt" or path.suffix == ".cmake",
        ("--check",),
        ("--in-place",),
    ),
    Formatter(
        "ruff",
        "ruff",
        lambda path: path.suffix == ".py",
        ("format", "--check"),
        ("format",),
    ),
    Formatter(
        "shfmt",
        "shfmt",
        lambda path: path.suffix == ".sh",
        ("-d", "-i", "2", "-ci", "-bn"),
        ("-w", "-i", "2", "-ci", "-bn"),
    ),
    Formatter(
        "powershell",
        "pwsh",
        lambda path: path.suffix.lower() in {".ps1", ".psd1", ".psm1"},
        (
            "-NoProfile",
            "-File",
            str(ROOT / "scripts" / "format-powershell.ps1"),
            "check",
        ),
        (
            "-NoProfile",
            "-File",
            str(ROOT / "scripts" / "format-powershell.ps1"),
            "write",
        ),
    ),
    Formatter(
        "prettier",
        "prettier",
        lambda path: path.suffix.lower() in PRETTIER_SUFFIXES,
        ("--check",),
        ("--write",),
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        choices=(
            "analyze",
            "architecture",
            "coverage",
            "format",
            "format-check",
            "fuzz-check",
            "generated",
            "hygiene",
            "headers",
            "naming",
            "quality",
            "sanitize",
            "symbols",
            "tidy",
            "vulkan",
            "yona-style",
        ),
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help="limit file-oriented commands (pre-commit passes staged paths)",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="configured build directory containing compile_commands.json",
    )
    parser.add_argument(
        "--preset",
        help="debug configure preset used for sanitizer/coverage builds",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, os.cpu_count() or 1),
        help="parallel clang-tidy/build jobs",
    )
    parser.add_argument(
        "--only",
        action="append",
        choices=[formatter.name for formatter in FORMATTERS],
        help="limit format/format-check to one or more formatters",
    )
    return parser.parse_args()


def print_command(command: Sequence[object]) -> None:
    rendered = " ".join(str(part) for part in command)
    print(f"+ {rendered}")


def run(
    command: Sequence[object],
    *,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    args = [str(part) for part in command]
    print_command(args)
    result = subprocess.run(
        args,
        cwd=cwd,
        env=env,
        check=False,
        text=True,
        capture_output=capture,
    )
    if result.returncode:
        if capture:
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
        raise QualityError(
            f"command failed with exit code {result.returncode}: {args[0]}"
        )
    return result


def resolve_tool(name: str, *, version_22: bool = False) -> str:
    executable = shutil.which(name)
    if not executable:
        suffix = ".exe" if os.name == "nt" else ""
        command_suffix = ".cmd" if os.name == "nt" else ""
        candidates = (
            Path(sysconfig.get_path("scripts")) / f"{name}{suffix}",
            ROOT / "node_modules" / ".bin" / f"{name}{command_suffix}",
            ROOT / "site" / "node_modules" / ".bin" / f"{name}{command_suffix}",
        )
        executable = next(
            (str(candidate) for candidate in candidates if candidate.is_file()),
            None,
        )
    if not executable:
        raise QualityError(
            f"required local tool {name!r} was not found; see docs/quality.md"
        )
    if version_22:
        result = subprocess.run(
            [executable, "--version"],
            check=False,
            capture_output=True,
            text=True,
        )
        version = result.stdout + result.stderr
        if result.returncode or not re.search(r"\bversion\s+22\.1\.", version):
            raise QualityError(
                f"{name} must be LLVM 22.1.x; reported: {version.strip()}"
            )
    return executable


def relative_path(path: Path) -> Path | None:
    absolute = path if path.is_absolute() else ROOT / path
    if not absolute.is_file():
        return None
    try:
        return absolute.resolve().relative_to(ROOT)
    except ValueError:
        return None


def tracked_files(paths: Sequence[str] = ()) -> list[Path]:
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

    files = {
        path for item in candidates if (path := relative_path(item)) is not None
    }
    return sorted(files, key=lambda path: path.as_posix())


def is_generated(path: Path) -> bool:
    name = path.as_posix()
    return (
        name in GENERATED_EXACT
        or any(name.startswith(prefix) for prefix in GENERATED_PREFIXES)
        or "Generated" in path.parts
        or path.suffix == ".yonai"
        or name.endswith("_spv.inc")
        or name.endswith("_spv.inl")
    )


def chunks(paths: Sequence[Path]) -> Iterable[list[Path]]:
    limit = 24_000 if os.name == "nt" else 100_000
    current: list[Path] = []
    size = 0
    for path in paths:
        item_size = len(str(path)) + 3
        if current and size + item_size > limit:
            yield current
            current = []
            size = 0
        current.append(path)
        size += item_size
    if current:
        yield current


def formatter_command(
    formatter: Formatter, executable: str, check: bool, paths: list[Path]
) -> list[str]:
    options = formatter.check_args if check else formatter.fix_args
    return [executable, *options, *(str(ROOT / path) for path in paths)]


def run_formatters(
    *,
    check: bool,
    paths: Sequence[str] = (),
    selected: Sequence[str] | None = None,
) -> None:
    files = [path for path in tracked_files(paths) if not is_generated(path)]
    enabled = set(selected or (formatter.name for formatter in FORMATTERS))
    plan: list[tuple[Formatter, str, list[Path]]] = []
    for formatter in FORMATTERS:
        if formatter.name not in enabled:
            continue
        accepted = [path for path in files if formatter.accepts(path)]
        if not accepted:
            continue
        executable = resolve_tool(
            formatter.executable, version_22=formatter.version_22
        )
        plan.append((formatter, executable, accepted))

    # Discover every required tool before fix mode changes the first file.
    for formatter, executable, accepted in plan:
        for batch in chunks(accepted):
            run(formatter_command(formatter, executable, check, batch))


def is_probably_binary(path: Path, data: bytes) -> bool:
    return path.suffix.lower() in BINARY_SUFFIXES or b"\0" in data[:8192]


def check_hygiene(paths: Sequence[str] = ()) -> None:
    errors: list[str] = []
    for path in tracked_files(paths):
        data = (ROOT / path).read_bytes()
        if is_probably_binary(path, data):
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as error:
            errors.append(f"{path}: not UTF-8 ({error})")
            continue

        if data.startswith(b"\xef\xbb\xbf"):
            errors.append(f"{path}: UTF-8 BOM is forbidden")
        if b"\r" in data:
            errors.append(f"{path}: CR/CRLF line endings are forbidden")
        if data and not data.endswith(b"\n"):
            errors.append(f"{path}: missing final newline")
        tabs_are_syntax = (
            path.suffix == ".go"
            or path.name == "Makefile"
            or path.as_posix() == "packaging/debian/rules"
        )
        for line_number, line in enumerate(text.splitlines(), start=1):
            if line.endswith((" ", "\t")):
                errors.append(f"{path}:{line_number}: trailing whitespace")
            if "\t" in line and not tabs_are_syntax:
                errors.append(f"{path}:{line_number}: tab character")
        if re.search(r"(?m)^(<<<<<<<|=======|>>>>>>>)", text):
            errors.append(f"{path}: unresolved merge-conflict marker")

        if path.suffix == ".json":
            try:
                json.loads(text)
            except json.JSONDecodeError as error:
                errors.append(
                    f"{path}:{error.lineno}: invalid JSON: {error.msg}"
                )

        large_nonfixture = len(
            data
        ) > 1_000_000 and not path.as_posix().startswith("bench/data/")
        if large_nonfixture:
            errors.append(f"{path}: tracked file exceeds 1 MB")

    if errors:
        for error in errors[:300]:
            print(error, file=sys.stderr)
        if len(errors) > 300:
            print(
                f"... {len(errors) - 300} additional issue(s)", file=sys.stderr
            )
        raise QualityError(f"text hygiene failed with {len(errors)} issue(s)")
    print("text hygiene passed")


def find_compile_database(build_dir: Path | None) -> tuple[Path, Path]:
    if build_dir:
        directory = build_dir if build_dir.is_absolute() else ROOT / build_dir
        database = directory / "compile_commands.json"
        if not database.is_file():
            raise QualityError(f"compile database not found: {database}")
        return directory.resolve(), database.resolve()

    candidates = list((ROOT / "out" / "build").glob("*/compile_commands.json"))
    if not candidates:
        raise QualityError(
            "no compile_commands.json found; configure a debug preset or "
            "pass --build-dir"
        )
    database = max(candidates, key=lambda path: path.stat().st_mtime)
    return database.parent.resolve(), database.resolve()


def project_source(entry: dict[str, object]) -> Path | None:
    raw_source = entry.get("file")
    raw_directory = entry.get("directory")
    if not isinstance(raw_source, str) or not isinstance(raw_directory, str):
        return None
    source = Path(raw_source)
    if not source.is_absolute():
        source = Path(raw_directory) / source
    try:
        relative = source.resolve().relative_to(ROOT)
    except ValueError:
        return None
    if not relative.parts or relative.parts[0] not in NATIVE_COMPILE_ROOTS:
        return None
    if relative.suffix.lower() not in {".c", ".cc", ".cpp", ".cxx"}:
        return None
    return relative if not is_generated(relative) else None


def project_compile_entries(database: Path) -> list[dict[str, object]]:
    entries = json.loads(database.read_text(encoding="utf-8"))
    if not isinstance(entries, list):
        raise QualityError(f"invalid compile database: {database}")
    return [
        entry
        for entry in entries
        if isinstance(entry, dict) and project_source(entry) is not None
    ]


def project_compile_database(database: Path) -> tuple[Path, Path]:
    entries = project_compile_entries(database)
    if not entries:
        raise QualityError(
            f"compile database contains no Yona project sources: {database}"
        )
    directory = QUALITY_ROOT / "compile-database"
    directory.mkdir(parents=True, exist_ok=True)
    filtered = directory / "compile_commands.json"
    filtered.write_text(
        json.dumps(entries, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    return directory, filtered


def compilation_sources(database: Path) -> list[Path]:
    entries = project_compile_entries(database)
    sources: set[Path] = set()
    for entry in entries:
        relative = project_source(entry)
        if relative is not None:
            sources.add(relative)
    return sorted(sources, key=lambda path: path.as_posix())


def run_tidy(build_dir: Path | None, jobs: int) -> None:
    directory, database = find_compile_database(build_dir)
    executable = resolve_tool("clang-tidy", version_22=True)
    sources = compilation_sources(database)
    if not sources:
        raise QualityError(
            f"compile database contains no Yona sources: {database}"
        )

    def invoke(source: Path) -> tuple[Path, subprocess.CompletedProcess[str]]:
        command = [
            executable,
            f"-p={directory}",
            f"--config-file={ROOT / '.clang-tidy'}",
            str(ROOT / source),
        ]
        result = subprocess.run(
            command,
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        return source, result

    failures = 0
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=max(1, jobs)
    ) as pool:
        for source, result in pool.map(invoke, sources):
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
            if result.returncode:
                failures += 1
                print(f"clang-tidy failed: {source}", file=sys.stderr)
    if failures:
        raise QualityError(
            f"clang-tidy failed for {failures} translation unit(s)"
        )


def compile_command_tokens(entry: dict[str, object]) -> list[str]:
    arguments = entry.get("arguments")
    if isinstance(arguments, list):
        return [str(item) for item in arguments]
    command = entry.get("command")
    if not isinstance(command, str):
        raise QualityError("compile database entry has no command or arguments")
    tokens = shlex.split(command, posix=os.name != "nt")
    return [
        token[1:-1]
        if len(token) >= 2 and token[0] == token[-1] == '"'
        else token
        for token in tokens
    ]


def header_compile_flags(
    entries: list[dict[str, object]],
) -> tuple[str, list[str]]:
    if not entries:
        raise QualityError("compile database is empty")
    first = compile_command_tokens(entries[0])
    if not first:
        raise QualityError("compile database command is empty")

    flags: list[str] = []
    seen: set[tuple[str, ...]] = set()

    def add(*parts: str) -> None:
        key = tuple(parts)
        if key not in seen:
            seen.add(key)
            flags.extend(parts)

    for token in first[1:]:
        if token.startswith(("-D", "/D")) or token.startswith(
            ("-std=", "/std:")
        ):
            add(token)

    for entry in entries:
        tokens = compile_command_tokens(entry)
        index = 1
        while index < len(tokens):
            token = tokens[index]
            if token in {"-I", "-isystem", "/I", "-imsvc"} and index + 1 < len(
                tokens
            ):
                add(token, tokens[index + 1])
                index += 2
                continue
            if token.startswith(("-I", "/I")) and len(token) > 2:
                add(token)
            index += 1
    return first[0], flags


def run_header_self_containment(build_dir: Path | None, jobs: int) -> None:
    _, database = find_compile_database(build_dir)
    entries = project_compile_entries(database)
    compiler, flags = header_compile_flags(entries)

    def applies_to_host(path: Path) -> bool:
        name = path.name.lower()
        if name == "kqueue.h":
            return sys.platform == "darwin"
        if name == "iouring.h":
            return sys.platform.startswith("linux")
        return True

    headers = [
        path
        for path in tracked_files()
        if path.suffix.lower() in {".h", ".hh", ".hpp", ".hxx"}
        and path.parts[0] in {"include", "src", "cli", "repl", "test", "tools"}
        and not is_generated(path)
        and applies_to_host(path)
    ]

    def invoke(header: Path) -> tuple[Path, subprocess.CompletedProcess[str]]:
        source = f'#include "{(ROOT / header).as_posix()}"\n'
        result = subprocess.run(
            [compiler, *flags, "-fsyntax-only", "-x", "c++", "-"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
            input=source,
        )
        return header, result

    failures = 0
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=max(1, jobs)
    ) as pool:
        for header, result in pool.map(invoke, headers):
            if not result.returncode:
                continue
            failures += 1
            print(f"header is not self-contained: {header}", file=sys.stderr)
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
    if failures:
        raise QualityError(
            f"header self-containment failed for {failures} header(s)"
        )
    print(f"header self-containment passed ({len(headers)} header(s))")


def tracked_by_suffix(*suffixes: str) -> list[Path]:
    wanted = set(suffixes)
    return [
        path
        for path in tracked_files()
        if path.suffix.lower() in wanted and not is_generated(path)
    ]


def run_analyzers(build_dir: Path | None, jobs: int) -> None:
    _, complete_database = find_compile_database(build_dir)
    directory, database = project_compile_database(complete_database)

    ruff = resolve_tool("ruff")
    python_files = tracked_by_suffix(".py")
    for batch in chunks(python_files):
        run([ruff, "check", *(ROOT / path for path in batch)])

    cppcheck = resolve_tool("cppcheck")
    run(
        [
            cppcheck,
            f"--project={database}",
            "--enable=warning,style,performance,portability",
            "--error-exitcode=1",
            "--inline-suppr",
            "--suppress=missingIncludeSystem",
            f"-j{max(1, jobs)}",
        ]
    )

    scan_deps = resolve_tool("clang-scan-deps", version_22=True)
    QUALITY_ROOT.mkdir(parents=True, exist_ok=True)
    dependencies = QUALITY_ROOT / "dependencies.json"
    result = run(
        [
            scan_deps,
            f"-compilation-database={database}",
            "-format=experimental-full",
        ],
        capture=True,
    )
    dependencies.write_text(result.stdout, encoding="utf-8", newline="\n")

    iwyu_tool = shutil.which("iwyu_tool.py") or shutil.which("iwyu_tool")
    if not iwyu_tool:
        raise QualityError("required local tool 'iwyu_tool.py' was not found")
    run([iwyu_tool, "-p", directory, "-j", max(1, jobs)])

    analyze_build = resolve_tool("analyze-build")
    clang = resolve_tool("clang", version_22=True)
    scan_output = QUALITY_ROOT / "scan-build"
    analyzer_command = [analyze_build]
    if os.name == "nt" and not Path(analyze_build).suffix:
        analyzer_command.insert(0, sys.executable)
    run(
        [
            *analyzer_command,
            "--cdb",
            database,
            "--status-bugs",
            "--output",
            scan_output,
            "--sarif",
            "--use-analyzer",
            clang,
        ]
    )

    shellcheck = resolve_tool("shellcheck")
    for batch in chunks(tracked_by_suffix(".sh")):
        run([shellcheck, *(ROOT / path for path in batch)])

    pwsh = resolve_tool("pwsh")
    ps_command = (
        "$Results = Invoke-ScriptAnalyzer -Path $args[0] "
        f"-Settings '{ROOT / 'ps-script-analyzer-settings.psd1'}'; "
        "$Results | Format-Table -AutoSize | Out-Host; "
        "if ($Results) { exit 1 }"
    )
    for path in tracked_by_suffix(".ps1", ".psd1", ".psm1"):
        run([pwsh, "-NoProfile", "-Command", ps_command, ROOT / path])

    actionlint = resolve_tool("actionlint")
    run([actionlint])

    yamllint = resolve_tool("yamllint")
    yaml_files = tracked_by_suffix(".yaml", ".yml")
    for batch in chunks(yaml_files):
        run(
            [
                yamllint,
                "-c",
                ROOT / ".yamllint.yaml",
                *(ROOT / path for path in batch),
            ]
        )

    markdownlint = resolve_tool("markdownlint-cli2")
    run([markdownlint, "--config", ROOT / ".markdownlint-cli2.yaml"])

    typos = resolve_tool("typos")
    run([typos, ROOT])

    eslint = resolve_tool("eslint")
    web_sources = tracked_by_suffix(".astro", ".js", ".mjs", ".ts", ".tsx")
    if web_sources:
        for batch in chunks(web_sources):
            run(
                [
                    eslint,
                    "--config",
                    ROOT / "site" / "eslint.config.mjs",
                    "--max-warnings=0",
                    *(ROOT / path for path in batch),
                ]
            )


def default_preset() -> str:
    if sys.platform == "win32":
        return "x64-debug"
    if sys.platform == "darwin":
        return "arm64-debug-macos"
    return "x64-debug-linux"


FETCHCONTENT_SOURCES = (
    ("CLI11", "cli11-src"),
    ("DOCTEST", "doctest-src"),
    ("LIBXML2_SRC", "libxml2_src-src"),
    ("YONA_PCRE2", "yona_pcre2-src"),
    ("YONA_ZLIB", "yona_zlib-src"),
    ("YONA_ZSTD", "yona_zstd-src"),
)


def dependency_source_overrides(build_dir: Path | None) -> list[str]:
    try:
        dependency_build, _ = find_compile_database(build_dir)
    except QualityError:
        return []
    dependency_sources = dependency_build / "_deps"
    overrides: list[str] = []
    for package, directory_name in FETCHCONTENT_SOURCES:
        source = dependency_sources / directory_name
        if source.is_dir():
            overrides.append(f"-DFETCHCONTENT_SOURCE_DIR_{package}={source}")
    return overrides


def configure_instrumented(
    name: str,
    preset: str,
    compile_flags: str,
    linker_flags: str,
    build_dir: Path | None,
) -> Path:
    directory = QUALITY_ROOT / name
    cmake = resolve_tool("cmake")
    run(
        [
            cmake,
            "--preset",
            preset,
            "-B",
            directory,
            f"-DCMAKE_C_FLAGS={compile_flags}",
            f"-DCMAKE_CXX_FLAGS={compile_flags}",
            f"-DCMAKE_EXE_LINKER_FLAGS={linker_flags}",
            f"-DCMAKE_SHARED_LINKER_FLAGS={linker_flags}",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
            *dependency_source_overrides(build_dir),
        ]
    )
    return directory


def build_and_test(
    directory: Path, jobs: int, env: dict[str, str] | None = None
) -> None:
    cmake = resolve_tool("cmake")
    ctest = resolve_tool("ctest")
    run([cmake, "--build", directory, "--parallel", max(1, jobs)])
    run([ctest, "--test-dir", directory, "--output-on-failure"], env=env)


def run_sanitizers(
    preset: str, jobs: int, build_dir: Path | None = None
) -> None:
    common = "-fno-omit-frame-pointer"
    sanitizers = "address" if sys.platform == "win32" else "address,undefined"
    address_flags = f"{common} -fsanitize={sanitizers}"
    address = configure_instrumented(
        "sanitize-address",
        preset,
        address_flags,
        f"-fsanitize={sanitizers}",
        build_dir,
    )
    address_env = os.environ.copy()
    address_options = "strict_string_checks=1"
    if sys.platform != "win32":
        address_options = f"detect_leaks=1:{address_options}"
    address_env.setdefault("ASAN_OPTIONS", address_options)
    address_env.setdefault(
        "UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1"
    )
    build_and_test(address, jobs, address_env)

    if sys.platform != "win32":
        thread_flags = f"{common} -fsanitize=thread"
        thread = configure_instrumented(
            "sanitize-thread",
            preset,
            thread_flags,
            "-fsanitize=thread",
            build_dir,
        )
        thread_env = os.environ.copy()
        thread_env.setdefault("TSAN_OPTIONS", "halt_on_error=1")
        build_and_test(thread, jobs, thread_env)


def run_fuzz_check(build_dir: Path | None, preset: str) -> None:
    directory = QUALITY_ROOT / "fuzz"
    cmake = resolve_tool("cmake")
    run(
        [
            cmake,
            "--preset",
            preset,
            "-B",
            directory,
            "-DYONA_BUILD_FUZZERS=ON",
            "-DBUILD_TESTING=OFF",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
            *dependency_source_overrides(build_dir),
        ]
    )
    # Each libFuzzer process already uses all sanitizer instrumentation it
    # needs. Run the short harness targets one at a time to bound peak memory.
    run(
        [
            cmake,
            "--build",
            directory,
            "--target",
            "fuzz-check",
            "--parallel",
            "1",
        ]
    )


def run_vulkan_validation(
    preset: str, jobs: int, build_dir: Path | None = None
) -> None:
    """Build the Vulkan backend and reject validation-layer diagnostics."""
    directory = QUALITY_ROOT / "vulkan"
    cmake = resolve_tool("cmake")
    ctest = resolve_tool("ctest")
    run(
        [
            cmake,
            "--preset",
            preset,
            "-B",
            directory,
            "-DYONA_ENABLE_VULKAN=ON",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
            *dependency_source_overrides(build_dir),
        ]
    )
    run(
        [
            cmake,
            "--build",
            directory,
            "--target",
            "tests",
            "--parallel",
            max(1, jobs),
        ]
    )

    environment = os.environ.copy()
    environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
    environment.setdefault("VK_LOADER_DEBUG", "error,warn")
    result = run(
        [
            ctest,
            "--test-dir",
            directory,
            "--output-on-failure",
            "-L",
            "vulkan",
        ],
        env=environment,
        capture=True,
    )
    output = result.stdout + result.stderr
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    findings = sorted(
        set(re.findall(r"(?:Validation Error|VUID-[A-Za-z0-9_-]+)", output))
    )
    if findings:
        raise QualityError(
            "Vulkan validation reported errors: " + ", ".join(findings)
        )


def find_test_executable(directory: Path) -> Path:
    names = ("tests.exe", "tests") if sys.platform == "win32" else ("tests",)
    candidates = [
        path
        for name in names
        for path in directory.rglob(name)
        if path.is_file()
    ]
    if not candidates:
        raise QualityError(f"test executable not found below {directory}")
    return min(candidates, key=lambda path: len(path.parts))


def find_yonac_executable(directory: Path) -> Path:
    names = ("yonac.exe", "yonac") if sys.platform == "win32" else ("yonac",)
    candidates = [
        path
        for name in names
        for path in directory.rglob(name)
        if path.is_file()
    ]
    if not candidates:
        raise QualityError(f"yonac executable not found below {directory}")
    return min(candidates, key=lambda path: len(path.parts))


def run_yona_style(
    build_dir: Path | None, paths: Sequence[str], jobs: int
) -> None:
    directory, _ = find_compile_database(build_dir)
    yonac = find_yonac_executable(directory)
    sources = [
        path
        for path in tracked_files(paths)
        if path.suffix == ".yona" and not is_generated(path)
    ]
    if not sources:
        return

    def invoke(source: Path) -> tuple[Path, subprocess.CompletedProcess[str]]:
        result = subprocess.run(
            [yonac, ROOT / source, "--check-style"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        return source, result

    failures = 0
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=max(1, jobs)
    ) as pool:
        for source, result in pool.map(invoke, sources):
            if result.returncode:
                failures += 1
                print(f"Yona style check failed: {source}", file=sys.stderr)
                if result.stdout:
                    print(result.stdout, end="")
                if result.stderr:
                    print(result.stderr, end="", file=sys.stderr)
    if failures:
        raise QualityError(
            f"Yona style check failed for {failures} source file(s)"
        )
    print(f"Yona style check passed ({len(sources)} source file(s))")


def run_coverage(preset: str, jobs: int, build_dir: Path | None = None) -> None:
    flags = "-fprofile-instr-generate -fcoverage-mapping"
    directory = configure_instrumented(
        "coverage", preset, flags, flags, build_dir
    )
    profiles = directory / "profiles"
    profiles.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["LLVM_PROFILE_FILE"] = str(profiles / "%p-%m.profraw")
    build_and_test(directory, jobs, environment)

    profdata = resolve_tool("llvm-profdata", version_22=True)
    llvm_cov = resolve_tool("llvm-cov", version_22=True)
    raw_profiles = sorted(profiles.glob("*.profraw"))
    if not raw_profiles:
        raise QualityError("coverage tests produced no .profraw files")
    merged = directory / "coverage.profdata"
    run([profdata, "merge", "-sparse", *raw_profiles, "-o", merged])
    test_executable = find_test_executable(directory)
    yonac_executable = find_yonac_executable(directory)
    objects = [f"-object={yonac_executable}"]
    run(
        [
            llvm_cov,
            "report",
            test_executable,
            *objects,
            f"-instr-profile={merged}",
            "-ignore-filename-regex=(/out/|\\\\out\\\\|/test/|\\\\test\\\\)",
        ]
    )

    exported = run(
        [
            llvm_cov,
            "export",
            "-summary-only",
            test_executable,
            *objects,
            f"-instr-profile={merged}",
        ],
        capture=True,
    )
    try:
        report = json.loads(exported.stdout)
        files = report["data"][0]["files"]
    except (json.JSONDecodeError, KeyError, IndexError, TypeError) as error:
        raise QualityError(
            f"llvm-cov returned an invalid summary: {error}"
        ) from error

    critical_groups = {
        "parser": ("/src/Syntax/Parser",),
        "interface": ("/src/Interface/",),
        "ownership": (
            "/src/Semantics/LinearityChecker.",
            "/src/Codegen/EscapeAnalysis.",
            "/src/Codegen/LastUseAnalysis.",
        ),
        "runtime-lifecycle": (
            "/src/Runtime/Concurrency/",
            "/src/Runtime/Platform/",
            "/src/Runtime/Gpu/VulkanDevice.",
            "/src/Runtime/Codecs/",
        ),
    }
    threshold = 80.0
    failures: list[str] = []
    for group, fragments in critical_groups.items():
        covered = 0
        count = 0
        matched = 0
        for entry in files:
            filename = str(entry.get("filename", "")).replace("\\", "/")
            if not any(fragment in filename for fragment in fragments):
                continue
            lines = entry.get("summary", {}).get("lines", {})
            covered += int(lines.get("covered", 0))
            count += int(lines.get("count", 0))
            matched += 1
        if not matched or not count:
            failures.append(f"{group}: no instrumented source lines")
            continue
        percent = covered * 100.0 / count
        print(
            f"critical coverage {group}: {percent:.2f}% "
            f"({covered}/{count} lines across {matched} file(s))"
        )
        if percent < threshold:
            failures.append(f"{group}: {percent:.2f}% < {threshold:.0f}%")
    if failures:
        raise QualityError(
            "critical line coverage threshold failed: " + "; ".join(failures)
        )


def run_naming(paths: Sequence[str] = ()) -> None:
    command: list[object] = [
        sys.executable,
        ROOT / "scripts" / "check_naming.py",
    ]
    command.extend(paths)
    run(command)


def run_architecture() -> None:
    run([sys.executable, ROOT / "scripts" / "check_architecture.py"])


def run_generated_check() -> None:
    run(
        [
            sys.executable,
            ROOT / "scripts" / "generate_gpu_shaders.py",
            "--check",
        ]
    )


def find_archive(directory: Path, names: set[str]) -> Path:
    excluded = {"_deps", "test-consumer", "test-install"}
    candidates = [
        path
        for path in directory.rglob("*")
        if path.is_file()
        and path.name.lower() in names
        and not excluded.intersection(part.lower() for part in path.parts)
    ]
    if not candidates:
        expected = ", ".join(sorted(names))
        raise QualityError(
            f"built archive not found below {directory}; expected {expected}"
        )
    return min(candidates, key=lambda path: (len(path.parts), str(path)))


def archive_yona_symbols(archive: Path, llvm_nm: str) -> set[str]:
    result = run(
        [
            llvm_nm,
            "--defined-only",
            "--extern-only",
            "--format=posix",
            archive,
        ],
        capture=True,
    )
    symbols: set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if not fields or fields[0].endswith(":"):
            continue
        symbol = fields[0]
        if symbol.startswith("_Yona") or symbol.startswith("_yona"):
            symbol = symbol[1:]
        if symbol.lower().startswith("yona"):
            symbols.add(symbol)
    if not symbols:
        raise QualityError(f"no Yona-family symbols found in {archive}")
    return symbols


def run_symbol_contract(build_dir: Path | None) -> None:
    directory, _ = find_compile_database(build_dir)
    llvm_nm = resolve_tool("llvm-nm", version_22=True)
    archives = {
        "compiler": find_archive(
            directory, {"libyona_lib_static.a", "yona_lib_static.lib"}
        ),
        "runtime": find_archive(
            directory, {"libyona_runtime.a", "yona_runtime.lib"}
        ),
    }
    required = {
        "compiler": {
            "YonaTypedCoreAnalyze",
            "YonaTypedCoreDisposeModule",
        },
        "runtime": {
            "YonaRuntimeChannelCreate",
            "YonaRuntimeRelease",
            "YonaRuntimeRetain",
            "YonaRuntimeTaskCreate",
            "YonaStdGpuAvailable",
        },
    }
    output_directory = QUALITY_ROOT / "symbols"
    output_directory.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    canonical = re.compile(r"^Yona[A-Z][A-Za-z0-9]*$")
    retired_version = re.compile(r"(?:Abi|Api)Version")
    for family, archive in archives.items():
        symbols = archive_yona_symbols(archive, llvm_nm)
        (output_directory / f"{family}.txt").write_text(
            "\n".join(sorted(symbols)) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        for symbol in sorted(symbols):
            if not canonical.fullmatch(symbol):
                errors.append(f"{archive}: non-canonical export {symbol}")
            if retired_version.search(symbol):
                errors.append(f"{archive}: retired version export {symbol}")
        missing = required[family] - symbols
        for symbol in sorted(missing):
            errors.append(f"{archive}: missing canonical export {symbol}")
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        raise QualityError(
            f"symbol contract failed with {len(errors)} issue(s)"
        )
    print("canonical symbol contract passed")


def run_quality(args: argparse.Namespace) -> None:
    check_hygiene()
    run_naming()
    run_architecture()
    run_generated_check()
    run_symbol_contract(args.build_dir)
    run_formatters(check=True, selected=args.only)
    run_yona_style(args.build_dir, (), args.jobs)
    run_header_self_containment(args.build_dir, args.jobs)
    run_tidy(args.build_dir, args.jobs)
    run_analyzers(args.build_dir, args.jobs)
    preset = args.preset or default_preset()
    run_sanitizers(preset, args.jobs, args.build_dir)
    run_vulkan_validation(preset, args.jobs, args.build_dir)
    run_fuzz_check(args.build_dir, preset)
    run_coverage(preset, args.jobs, args.build_dir)


def main() -> int:
    args = parse_args()
    try:
        if args.command == "format":
            run_formatters(check=False, paths=args.paths, selected=args.only)
        elif args.command == "format-check":
            run_formatters(check=True, paths=args.paths, selected=args.only)
        elif args.command == "fuzz-check":
            run_fuzz_check(args.build_dir, args.preset or default_preset())
        elif args.command == "hygiene":
            check_hygiene(args.paths)
        elif args.command == "headers":
            run_header_self_containment(args.build_dir, args.jobs)
        elif args.command == "naming":
            run_naming(args.paths)
        elif args.command == "tidy":
            run_tidy(args.build_dir, args.jobs)
        elif args.command == "yona-style":
            run_yona_style(args.build_dir, args.paths, args.jobs)
        elif args.command == "analyze":
            run_analyzers(args.build_dir, args.jobs)
        elif args.command == "architecture":
            run_architecture()
        elif args.command == "generated":
            run_generated_check()
        elif args.command == "sanitize":
            run_sanitizers(
                args.preset or default_preset(), args.jobs, args.build_dir
            )
        elif args.command == "symbols":
            run_symbol_contract(args.build_dir)
        elif args.command == "vulkan":
            run_vulkan_validation(
                args.preset or default_preset(), args.jobs, args.build_dir
            )
        elif args.command == "coverage":
            run_coverage(
                args.preset or default_preset(), args.jobs, args.build_dir
            )
        elif args.command == "quality":
            run_quality(args)
    except (OSError, QualityError, subprocess.SubprocessError) as error:
        print(f"quality: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
