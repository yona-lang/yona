#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "run-focused-tests.py"


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


with tempfile.TemporaryDirectory() as directory:
    fake = pathlib.Path(directory) / "fake-doctest.py"
    fake.write_text(
        """#!/usr/bin/env python3
import sys
args = sys.argv[1:]
filters = ' '.join(args)
if '--count' in args:
    print(0 if '-tc=zero' in args or '-ts=zero' in args else 1)
    raise SystemExit(0)
if 'failing' in filters:
    raise SystemExit(7)
raise SystemExit(0)
""",
        encoding="utf-8",
    )
    fake.chmod(0o755)

    def run(*arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(RUNNER), str(fake), *arguments],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    check(run("-tc=one").returncode == 0, "one matching test must run")
    zero = run("-tc=zero")
    check(zero.returncode != 0 and "zero" in zero.stdout, "zero match must identify its filter")
    mixed = run("-tc=one,zero")
    check(mixed.returncode != 0 and "zero" in mixed.stdout, "every comma item must be counted independently")
    check(run("-tc=one", "-tce=zero").returncode == 0, "exclusions need no positive match")
    check(run("-ts=suiteOne,suiteTwo", "-tc=one").returncode == 0, "suite lists must be split")
    check(run("-tc=failing").returncode == 7, "test failure status must propagate")
    subcase = run("-sc=child")
    check(subcase.returncode != 0 and "subcase" in subcase.stdout.lower(), "subcase filters must be rejected")

print("focused test runner contract passed")
