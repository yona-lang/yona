#!/usr/bin/env python3
"""Validate the source package that is published as the Yona Zed extension."""

from __future__ import annotations

import re
import sys
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXTENSION = ROOT / "editors" / "zed"
GRAMMAR = ROOT / "editors" / "tree-sitter-yona"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"zed extension check: {message}")


def main() -> None:
    manifest = tomllib.loads((EXTENSION / "extension.toml").read_text())
    config = tomllib.loads((EXTENSION / "languages" / "yona" / "config.toml").read_text())
    grammar = manifest["grammars"]["yona"]

    require(manifest["id"] == "yona", "extension id must be 'yona'")
    require(config["name"] == "Yona", "language name must be Yona")
    require({"yona", "yonai"}.issubset(config["path_suffixes"]), "missing Yona suffix")
    require(re.fullmatch(r"[0-9a-f]{40}", grammar["rev"]) is not None,
            "grammar revision must be a 40-character commit SHA")
    require(grammar["rev"] != "0" * 40, "grammar revision is still a placeholder")

    for query in ("highlights.scm", "brackets.scm", "outline.scm", "indents.scm"):
        require((EXTENSION / "languages" / "yona" / query).is_file(), f"missing {query}")

    source = (EXTENSION / "src" / "lib.rs").read_text()
    configured = source.index("settings::LspSettings")
    path = source.index('worktree.which("yls")')
    require(configured < path, "configured binary must take precedence over PATH")
    require('"--stdio"' in source, "yls must be launched with --stdio")
    require((GRAMMAR / "src" / "parser.c").is_file(), "generated parser is missing")


if __name__ == "__main__":
    main()
