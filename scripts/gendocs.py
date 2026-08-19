#!/usr/bin/env python3
"""
Generate API reference docs from ## doc comments in .yona source files.

Reads lib/Std/*.yona, extracts ## comments and public definitions,
writes compact markdown to docs/api/ (signature + prose, not the body).

Usage:
    python3 scripts/gendocs.py
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIB_DIR = ROOT / "lib" / "Std"
OUT_DIR = ROOT / "docs" / "api"

FN_START = re.compile(r"^([a-z][a-zA-Z0-9_]*)\b(.*)$")
EXTERN = re.compile(
    r"^extern(?:\s+(?:io|async|native))?\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*:\s*(.*?)(?:\s*=\s*\".*\")?\s*$"
)
TRAIT_METHOD = re.compile(r"^\s+([a-z][a-zA-Z0-9_]*)\s*:\s*(.+)$")
LEGACY_FN = re.compile(
    r"^### `([^`]+)`\n\n```(?:yona)?\n([\s\S]*?)\n```\n",
    re.MULTILINE,
)
LEGACY_TYPE = re.compile(
    r"^### `type ([A-Z][a-zA-Z0-9_]*)([^`]*)`\n",
    re.MULTILINE,
)


def is_internal(name: str) -> bool:
    return name.startswith("raw_") or name.startswith("yona_")


def compact_rhs(rhs: str) -> str | None:
    rhs = rhs.strip()
    if not rhs or len(rhs) > 120:
        return None
    if rhs.startswith("yona_") or rhs.startswith("raw_") or " raw_" in f" {rhs}":
        return None
    lowered = f" {rhs} "
    for kw in (" case ", " if ", " do ", " let ", " import ", " handle ", " perform "):
        if kw in lowered:
            return None
    return rhs


def parse_export_names(line: str) -> tuple[str, list[str]]:
    if line.startswith("export type "):
        return "type", [line[len("export type ") :].split()[0]]
    if line.startswith("export trait "):
        return "trait", [line[len("export trait ") :].split()[0]]
    if line.startswith("export "):
        names = [n.strip() for n in line[len("export ") :].split(",") if n.strip()]
        return "fn", names
    return "", []


def collect_block(lines: list[str], start: int, opener: str) -> tuple[str, int]:
    """Collect a `type` / `trait` / `instance` block. Returns (text, last_index)."""
    chunk = [lines[start]]
    i = start
    if opener == "type":
        depth = lines[start].count("{") - lines[start].count("}")
        while depth > 0:
            i += 1
            if i >= len(lines):
                break
            chunk.append(lines[i])
            depth += lines[i].count("{") - lines[i].count("}")
        return "\n".join(chunk).rstrip(), i

    # trait / instance: until a column-0 `end`
    i += 1
    while i < len(lines):
        chunk.append(lines[i])
        if lines[i].strip() == "end" and not lines[i].startswith(" "):
            break
        i += 1
    return "\n".join(chunk).rstrip(), i


def type_name(definition: str) -> str:
    m = re.match(r"^type\s+([A-Z][a-zA-Z0-9_]*)", definition)
    return m.group(1) if m else definition.split()[1]


def trait_name(definition: str) -> str:
    m = re.match(r"^trait\s+([A-Z][a-zA-Z0-9_]*)", definition)
    return m.group(1) if m else definition.split()[1]


def signature_of(fn: dict) -> str:
    if fn.get("type_sig"):
        return fn["type_sig"]
    lhs = fn.get("lhs") or fn["name"]
    rhs = fn.get("simple_rhs")
    if rhs is not None:
        return f"{lhs} = {rhs}"
    return lhs


def upsert_function(functions: list[dict], by_name: dict[str, dict], entry: dict) -> None:
    name = entry["name"]
    existing = by_name.get(name)
    if existing is None:
        functions.append(entry)
        by_name[name] = entry
        return
    if entry.get("type_sig") and not existing.get("type_sig"):
        existing["type_sig"] = entry["type_sig"]
    if entry.get("lhs") and not existing.get("lhs"):
        existing["lhs"] = entry["lhs"]
    if entry.get("simple_rhs") is not None and existing.get("simple_rhs") is None:
        existing["simple_rhs"] = entry["simple_rhs"]
    if entry.get("doc") and not existing.get("doc"):
        existing["doc"] = entry["doc"]


def parse_module(path: Path) -> dict:
    lines = path.read_text().splitlines()
    module = {
        "name": "",
        "module_doc": [],
        "types": [],
        "traits": [],
        "functions": [],
        "exported_fns": [],
        "exported_types": [],
        "exported_traits": [],
    }

    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("##"):
            module["module_doc"].append(line[2:].strip())
        elif line.startswith("module "):
            module["name"] = line.split("module ", 1)[1].strip()
            i += 1
            break
        else:
            break
        i += 1

    doc_buffer: list[str] = []
    functions: list[dict] = []
    by_name: dict[str, dict] = {}

    while i < len(lines):
        line = lines[i]

        if line.startswith("export "):
            kind, names = parse_export_names(line)
            bucket = {
                "type": "exported_types",
                "trait": "exported_traits",
                "fn": "exported_fns",
            }.get(kind)
            if bucket:
                seen = module[bucket]
                for n in names:
                    if n not in seen:
                        seen.append(n)
            i += 1
            continue

        if line.startswith("##"):
            body = line[2:].strip()
            if re.match(r"-{3,}", body):
                i += 1
                continue
            doc_buffer.append(body)
            i += 1
            continue

        if line.startswith("type "):
            definition, i = collect_block(lines, i, "type")
            module["types"].append(
                {
                    "name": type_name(definition),
                    "definition": definition,
                    "doc": doc_buffer[:],
                }
            )
            doc_buffer.clear()
            i += 1
            continue

        if line.startswith("trait "):
            definition, i = collect_block(lines, i, "trait")
            module["traits"].append(
                {
                    "name": trait_name(definition),
                    "definition": definition,
                    "doc": doc_buffer[:],
                }
            )
            for raw in definition.splitlines()[1:]:
                tm = TRAIT_METHOD.match(raw)
                if tm and tm.group(1) != "end":
                    upsert_function(
                        functions,
                        by_name,
                        {
                            "name": tm.group(1),
                            "type_sig": f"{tm.group(1)} : {tm.group(2).strip()}",
                            "lhs": None,
                            "simple_rhs": None,
                            "doc": [],
                        },
                    )
            doc_buffer.clear()
            i += 1
            continue

        if line.startswith("instance "):
            _, i = collect_block(lines, i, "instance")
            doc_buffer.clear()
            i += 1
            continue

        ext = EXTERN.match(line)
        if ext:
            name = ext.group(1)
            if not is_internal(name):
                type_sig = f"{name} : {ext.group(2).strip()}"
                upsert_function(
                    functions,
                    by_name,
                    {
                        "name": name,
                        "type_sig": type_sig,
                        "lhs": None,
                        "simple_rhs": None,
                        "doc": doc_buffer[:],
                    },
                )
            doc_buffer.clear()
            i += 1
            continue

        fn = FN_START.match(line)
        if fn and not line.startswith("export") and not line.startswith("module"):
            name = fn.group(1)
            rest = fn.group(2)
            if rest.lstrip().startswith(":"):
                upsert_function(
                    functions,
                    by_name,
                    {
                        "name": name,
                        "type_sig": line.strip(),
                        "lhs": None,
                        "simple_rhs": None,
                        "doc": doc_buffer[:],
                    },
                )
                doc_buffer.clear()
                i += 1
                continue

            lhs, sep, rhs = line.partition("=")
            if sep:
                lhs = lhs.strip()
                simple = compact_rhs(rhs)
                upsert_function(
                    functions,
                    by_name,
                    {
                        "name": name,
                        "type_sig": None,
                        "lhs": lhs,
                        "simple_rhs": simple,
                        "doc": doc_buffer[:],
                    },
                )
                doc_buffer.clear()
                i += 1
                continue

        if line.strip() and not line.startswith("##"):
            doc_buffer.clear()
        i += 1

    if module["exported_fns"]:
        exported = set(module["exported_fns"])
        for i, f in enumerate(functions):
            if f["name"] in exported or not f.get("doc"):
                continue
            for g in functions[i + 1 :]:
                if g["name"] in exported:
                    if not g.get("doc"):
                        g["doc"] = f["doc"]
                    break
        by = {f["name"]: f for f in functions}
        functions = [by[n] for n in module["exported_fns"] if n in by]
    else:
        functions = [f for f in functions if not is_internal(f["name"])]

    if module["exported_types"]:
        module["types"] = [t for t in module["types"] if t["name"] in module["exported_types"]]
    if module["exported_traits"]:
        module["traits"] = [t for t in module["traits"] if t["name"] in module["exported_traits"]]

    module["functions"] = functions
    return module


def render_doc(doc_lines: list[str]) -> str:
    result = []
    in_code = False
    for line in doc_lines:
        if line.startswith("```"):
            in_code = not in_code
            result.append(line)
        elif in_code:
            result.append(line)
        elif line == "":
            result.append("")
        else:
            result.append(line)
    return "\n".join(result)


def render_named_def(name: str, definition: str, doc: list[str]) -> list[str]:
    out = [f"### {name}", ""]
    if "\n" in definition:
        out.append(f"```yona\n{definition}\n```")
    else:
        out.append(f"`{definition}`")
    out.append("")
    if doc:
        out.append(render_doc(doc))
        out.append("")
    return out


def render_module(module: dict) -> str:
    out: list[str] = []
    name = module["name"].replace("\\", ".")
    out.append(f"# {name}")
    out.append("")

    if module["module_doc"]:
        out.append(render_doc(module["module_doc"]))
        out.append("")

    if module["types"]:
        out.append("## Types")
        out.append("")
        for t in module["types"]:
            out.extend(render_named_def(t["name"], t["definition"], t["doc"]))

    if module["traits"]:
        out.append("## Traits")
        out.append("")
        for t in module["traits"]:
            out.extend(render_named_def(t["name"], t["definition"], t["doc"]))

    if module["functions"]:
        out.append("## Functions")
        out.append("")
        for f in module["functions"]:
            out.append(f"### {f['name']}")
            out.append("")
            out.append(f"`{signature_of(f)}`")
            out.append("")
            if f["doc"]:
                out.append(render_doc(f["doc"]))
                out.append("")

    return "\n".join(out).rstrip() + "\n"


def compact_legacy(text: str) -> str:
    """Turn heading + implementation fence into a one-line signature."""

    def fn_sub(m: re.Match) -> str:
        name = m.group(1)
        first = m.group(2).strip().split("\n", 1)[0].rstrip()
        first = re.sub(r"\s*=\s*$", "", first)
        return f"### {name}\n\n`{first}`\n\n"

    text = LEGACY_FN.sub(fn_sub, text)
    text = LEGACY_TYPE.sub(r"### \1\n\n`type \1\2`\n", text)
    return re.sub(r"\n{3,}", "\n\n", text)


def first_sentence(module_doc: list[str]) -> str:
    desc = module_doc[0] if module_doc else ""
    if ". " in desc:
        desc = desc[: desc.index(". ") + 1]
    return desc


def parse_legacy_index_row(path: Path) -> dict | None:
    text = path.read_text()
    heading = re.match(r"^#\s+(.+)$", text, re.MULTILINE)
    if not heading:
        return None
    title = heading.group(1).strip()
    body = text[heading.end() :]
    paras = [p.strip() for p in re.split(r"\n\s*\n", body) if p.strip()]
    desc = ""
    for p in paras:
        if p.startswith("#") or p.startswith("|") or p.startswith("```") or p.startswith("**"):
            continue
        desc = p.split("\n", 1)[0]
        if ". " in desc:
            desc = desc[: desc.index(". ") + 1]
        break
    n_fn = len(re.findall(r"^### ", text, re.MULTILINE))
    # types/traits use ### too; count Functions-section headings only
    fn_section = re.search(r"^## Functions\n([\s\S]*?)(?=^## |\Z)", text, re.MULTILINE)
    if fn_section:
        n_fn = len(re.findall(r"^### ", fn_section.group(1), re.MULTILINE))
    type_section = re.search(r"^## Types\n([\s\S]*?)(?=^## |\Z)", text, re.MULTILINE)
    n_ty = len(re.findall(r"^### ", type_section.group(1), re.MULTILINE)) if type_section else 0
    return {
        "title": title,
        "file": path.name,
        "functions": n_fn,
        "types": n_ty,
        "desc": desc,
    }


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    modules = []
    written: set[str] = set()
    for yona_file in sorted(LIB_DIR.glob("*.yona")):
        module = parse_module(yona_file)
        if not module["name"]:
            continue
        out_name = yona_file.stem + ".md"
        (OUT_DIR / out_name).write_text(render_module(module))
        written.add(out_name)
        modules.append(module)
        extra = f", {len(module['traits'])} traits" if module["traits"] else ""
        print(
            f"  {module['name']}: {len(module['functions'])} functions, "
            f"{len(module['types'])} types{extra}"
        )

    leftover_rows = []
    for md in sorted(OUT_DIR.glob("*.md")):
        if md.name in written or md.name == "README.md":
            continue
        compacted = compact_legacy(md.read_text())
        if compacted != md.read_text():
            md.write_text(compacted)
        row = parse_legacy_index_row(md)
        if row:
            leftover_rows.append(row)
            print(f"  {row['title']}: {row['functions']} functions (legacy)")

    index = ["# Yona Standard Library API Reference", ""]
    n_fn = sum(len(m["functions"]) for m in modules) + sum(r["functions"] for r in leftover_rows)
    n_mod = len(modules) + len(leftover_rows)
    index.append(f"{n_fn} public functions across {n_mod} modules.")
    index.append("")
    index.append("| Module | Functions | Types | Description |")
    index.append("|--------|-----------|-------|-------------|")

    rows = []
    for m in modules:
        fname = m["name"].split("\\")[-1]
        rows.append(
            (
                m["name"],
                fname + ".md",
                len(m["functions"]),
                len(m["types"]) + len(m["traits"]),
                first_sentence(m["module_doc"]),
            )
        )
    for r in leftover_rows:
        name = r["title"].replace(".", "\\")
        if not name.startswith("Std"):
            name = r["title"]
        rows.append((name, r["file"], r["functions"], r["types"], r["desc"]))
    rows.sort(key=lambda r: r[0].lower())
    for name, fname, nf, nt, desc in rows:
        display = name.replace("\\", ".")
        index.append(f"| [{display}]({fname}) | {nf} | {nt} | {desc} |")
    index.append("")
    (OUT_DIR / "README.md").write_text("\n".join(index) + "\n")
    written.add("README.md")

    print(f"\nGenerated {len(modules)} module docs in {OUT_DIR}/")


if __name__ == "__main__":
    main()
