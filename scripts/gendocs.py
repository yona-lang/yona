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
DOCUMENTED_FN = re.compile(
    r"^### `([^`]+)`\n\n```(?:yona)?\n([\s\S]*?)\n```\n",
    re.MULTILINE,
)
DOCUMENTED_TYPE = re.compile(
    r"^### `type ([A-Z][a-zA-Z0-9_]*)([^`]*)`\n",
    re.MULTILINE,
)
YONAI_FN = re.compile(r"^(?:FN|IO|AFN|NAT)\s+(\S+)\s+(\d+)\s*(.*)$")

SIMPLE_RHS = re.compile(
    r"^(?:0|[1-9][0-9]*|[0-9]+\.[0-9]+|true|false|\(\)|\"[^\"]*\")$"
)

STRING_PARAM_NAMES = {
    "s",
    "str",
    "string",
    "msg",
    "message",
    "path",
    "host",
    "url",
    "name",
    "body",
    "text",
    "contents",
    "raw",
    "sql",
}
INT_PARAM_NAMES = {
    "n",
    "i",
    "k",
    "fd",
    "port",
    "status",
    "len",
    "count",
    "cap",
    "capacity",
    "offset",
    "size",
    "lo",
    "hi",
    "exp",
    "base",
    "code",
}
SEQ_PARAM_NAMES = {"seq", "xs", "ys", "zs", "list", "files", "urls"}
FN_PARAM_NAMES = {"fn", "f", "g", "pred", "handler"}
OPT_PARAM_NAMES = {"opt", "maybe", "option"}
ITER_FNS = {"split", "lines", "chars", "readLines", "readChunks"}
FILTER_FNS = {"filter", "any", "all", "takeWhile", "dropWhile", "find"}
FOLD_FNS = {"fold", "foldl", "foldr", "scanl", "scan"}
KNOWN_POLY = {
    "Option": 1,
    "Result": 2,
    "Iterator": 1,
    "Linear": 1,
    "Sender": 1,
    "Receiver": 1,
    "Stream": 1,
}
CONCRETE_CTYPES = {
    "INT": "Int",
    "FLOAT": "Float",
    "BOOL": "Bool",
    "STRING": "String",
    "SYMBOL": "Symbol",
    "UNIT": "()",
    "BYTE_ARRAY": "ByteArray",
    "INT_ARRAY": "IntArray",
    "FLOAT_ARRAY": "FloatArray",
}


def is_internal(name: str) -> bool:
    return name.startswith("raw_") or name.startswith("yona_")


def compact_rhs(rhs: str) -> str | None:
    rhs = rhs.strip()
    return rhs if SIMPLE_RHS.fullmatch(rhs) else None


def parse_yonai(path: Path) -> dict[str, dict]:
    """Map exported function names to canonical parameter/return descriptors."""
    if not path.exists():
        return {}
    out: dict[str, dict] = {}
    for line in path.read_text().splitlines():
        m = YONAI_FN.match(line)
        if not m:
            continue
        symbol, arity_s, rest = m.group(1), m.group(2), m.group(3).strip()
        name = symbol.split("__")[-1]
        arity = int(arity_s)
        if "->" not in rest:
            continue
        left, right = rest.split("->", 1)
        params = left.split()
        if arity == 0:
            params = []
        elif len(params) > arity:
            params = params[:arity]
        rtoks = right.split()
        ret: list[str] = []
        i = 0
        while i < len(rtoks):
            tok = rtoks[i]
            if tok in ("borrow", "effects", "effectscheme", "hof"):
                break
            ret.append(tok)
            i += 1
        out[name] = {"params": params, "ret": ret}
    return out


def type_arities_from_defs(types: list[dict]) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    for t in types:
        m = re.match(
            r"^type\s+([A-Z][a-zA-Z0-9_]*)((?:\s+[a-z])*)", t["definition"]
        )
        if m:
            out[m.group(1)] = m.group(2).split()
    return out


def pretty_arrow(
    params: list[str],
    ret: list[str],
    type_arities: dict[str, list[str]] | None,
    *,
    fn_name: str = "",
    param_names: list[str] | None = None,
) -> str:
    """Render CType tokens as a Yona arrow signature: `String -> Int -> Response`."""
    type_arities = type_arities or {}
    param_names = param_names or []
    n = 0

    def fresh() -> str:
        nonlocal n
        ch = chr(ord("a") + min(n, 25))
        n += 1
        return ch

    fn_dom: str | None = None
    fn_rng: str | None = None
    seq_from_dom_used = False
    set_elem: str | None = None
    dict_k: str | None = None
    dict_v: str | None = None
    dict_int_n = 0
    primary = next(iter(type_arities), None) if len(type_arities) == 1 else None
    payload: str | None = None

    def poly_arity(name: str) -> int:
        if name in type_arities:
            return len(type_arities[name])
        return KNOWN_POLY.get(name, 0)

    def option_payload() -> str:
        nonlocal payload
        if payload is None:
            payload = fresh()
        return payload

    def named_adt(name: str, prefer: str | None) -> str:
        arity = poly_arity(name)
        if arity <= 0:
            return name
        if prefer and arity == 1:
            return f"{name} {prefer}"
        return name + " " + " ".join(fresh() for _ in range(arity))

    descriptor_variables: dict[str, str] = {}

    def descriptor_parts(text: str) -> list[str]:
        parts: list[str] = []
        depth = 0
        start = 0
        for index, char in enumerate(text):
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            elif char == "," and depth == 0:
                parts.append(text[start:index])
                start = index + 1
        parts.append(text[start:])
        return [part for part in parts if part]

    def descriptor(text: str) -> str:
        if text in CONCRETE_CTYPES:
            return CONCRETE_CTYPES[text]
        if "(" not in text or not text.endswith(")"):
            return "".join(part.title() for part in text.split("_"))
        name, body = text.split("(", 1)
        arguments = descriptor_parts(body[:-1])
        rendered = [descriptor(argument) for argument in arguments]
        if name == "VAR":
            variable = arguments[0] if arguments else "value"
            if variable not in descriptor_variables:
                descriptor_variables[variable] = fresh()
            return descriptor_variables[variable]
        if name == "FUNCTION":
            return "(" + " -> ".join(rendered) + ")"
        if name == "TUPLE":
            return "(" + ", ".join(rendered) + ")"
        if name == "Seq":
            return "[" + (rendered[0] if rendered else fresh()) + "]"
        if name == "Set":
            return "Set " + (rendered[0] if rendered else fresh())
        if name == "Dict":
            return "Dict " + " ".join(rendered or [fresh(), fresh()])
        if name == "LINEAR":
            return "Linear " + (rendered[0] if rendered else fresh())
        if name == "ADT":
            return " ".join(rendered)
        return name + (" " + " ".join(rendered) if rendered else "")

    if fn_name == "channel" and ret and ret[0] == "TUPLE":
        return "Int -> (Linear (Sender a), Linear (Receiver a))"

    if fn_name in FOLD_FNS and params[:3] == ["FUNCTION", "INT", "SEQ"]:
        a, b = fresh(), fresh()
        if fn_name == "foldr":
            return f"({a} -> {b} -> {b}) -> {b} -> [{a}] -> {b}"
        if fn_name in ("scanl", "scan"):
            return f"({b} -> {a} -> {b}) -> {b} -> [{a}] -> [{b}]"
        return f"({b} -> {a} -> {b}) -> {b} -> [{a}] -> {b}"

    if fn_name == "forEach" and params == ["FUNCTION", "DICT"]:
        k, v, r = fresh(), fresh(), fresh()
        return f"({k} -> {v} -> {r}) -> Dict {k} {v} -> ()"
    if fn_name == "forEach" and params == ["FUNCTION", "SET"]:
        a, r = fresh(), fresh()
        return f"({a} -> {r}) -> Set {a} -> ()"

    def one(tok: str, *, is_ret: bool = False, pname: str = "") -> str:
        nonlocal \
            fn_dom, \
            fn_rng, \
            seq_from_dom_used, \
            set_elem, \
            dict_k, \
            dict_v, \
            dict_int_n
        if "(" in tok and tok.endswith(")"):
            return descriptor(tok)
        if tok == "FUNCTION":
            if fn_name in FILTER_FNS:
                fn_dom = fresh()
                fn_rng = fn_dom
                return f"({fn_dom} -> Bool)"
            fn_dom, fn_rng = fresh(), fresh()
            return f"({fn_dom} -> {fn_rng})"
        if tok == "SET":
            if set_elem is None:
                set_elem = fn_dom or fresh()
            return f"Set {set_elem}"
        if tok == "DICT":
            if dict_k is None:
                dict_k, dict_v = fresh(), fresh()
            return f"Dict {dict_k} {dict_v}"
        if tok == "SEQ":
            if set_elem and (is_ret or not fn_dom):
                return f"[{set_elem}]"
            if dict_k and is_ret:
                return f"[{dict_k}]"
            if is_ret:
                v = fn_rng or fresh()
            elif fn_dom and not seq_from_dom_used:
                v = fn_dom
                seq_from_dom_used = True
            else:
                v = fresh()
            return f"[{v}]"
        if tok == "TUPLE":
            return f"({fresh()}, {fresh()})"
        if tok == "LINEAR":
            return f"Linear {fresh()}"
        if tok == "PROMISE":
            if fn_name in ("readLine", "readLineFrom"):
                return "Option String"
            return "()"
        if tok == "INT":
            if set_elem and not is_ret:
                return set_elem
            if dict_k and not is_ret:
                slot = dict_k if dict_int_n == 0 else dict_v
                dict_int_n += 1
                return slot
            if dict_k and is_ret and fn_name == "get":
                return dict_v
            if (
                fn_name in ("send", "close", "isClosed", "length", "capacity")
                and "Sender" in type_arities
                and not is_ret
            ):
                if pname in ("v", "x", "val", "value"):
                    return option_payload()
                return named_adt("Sender", option_payload())
            if (
                fn_name in ("recv", "tryRecv")
                and "Receiver" in type_arities
                and not is_ret
            ):
                return named_adt("Receiver", option_payload())
            if fn_name == "openFile" and is_ret:
                return "FileHandle"
            if pname in STRING_PARAM_NAMES:
                return "String"
            if pname in INT_PARAM_NAMES:
                return "Int"
            if pname in SEQ_PARAM_NAMES:
                return f"[{fresh()}]"
            if pname in FN_PARAM_NAMES:
                d, r = fresh(), fresh()
                return f"({d} -> {r})"
            if pname in OPT_PARAM_NAMES:
                return named_adt("Option", option_payload())
            if primary == "Option":
                return option_payload()
            if primary and poly_arity(primary) > 0:
                return fresh()
            return "Int"
        if tok == "ADT":
            if fn_name == "openFile" and not is_ret:
                return "FileMode"
            if is_ret and fn_name in ITER_FNS:
                return f"Iterator {fresh()}"
            name = primary
            if not name and pname in OPT_PARAM_NAMES:
                name = "Option"
            if name == "Option":
                prefer = (fn_rng if is_ret else fn_dom) or option_payload()
                return named_adt("Option", prefer)
            if name:
                prefer = fn_rng if is_ret else fn_dom
                return named_adt(name, prefer)
            return fresh()
        if tok in CONCRETE_CTYPES:
            return CONCRETE_CTYPES[tok]
        return "".join(p.title() for p in tok.split("_"))

    parts = [
        one(t, pname=param_names[i] if i < len(param_names) else "")
        for i, t in enumerate(params)
    ]
    if not ret:
        ret_s = fresh()
    elif ret[0] == "TUPLE" and len(ret) > 1:
        ret_s = "(" + ", ".join(one(t, is_ret=True) for t in ret[1:]) + ")"
    else:
        ret_s = one(ret[0], is_ret=True)
    if not parts:
        return ret_s
    return " -> ".join(parts + [ret_s])


def parse_export_names(line: str) -> tuple[str, list[str]]:
    if line.startswith("export type "):
        return "type", [line[len("export type ") :].split()[0]]
    if line.startswith("export trait "):
        return "trait", [line[len("export trait ") :].split()[0]]
    if line.startswith("export "):
        names = [
            n.strip() for n in line[len("export ") :].split(",") if n.strip()
        ]
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


def signature_of(
    fn: dict,
    yonai: dict[str, dict] | None = None,
    type_arities: dict[str, list[str]] | None = None,
) -> str:
    """Always `name : T1 -> T2 -> R`, never a parameter list."""
    name = fn["name"]
    nparams = 0
    pnames: list[str] = []
    if fn.get("lhs"):
        bits = fn["lhs"].split()
        pnames = bits[1:]
        nparams = len(pnames)

    def with_const(sig: str) -> str:
        if fn.get("simple_rhs") is not None and nparams == 0 and "=" not in sig:
            return f"{sig} = {fn['simple_rhs']}"
        return sig

    if fn.get("type_sig"):
        sig = fn["type_sig"]
        if ":" not in sig or not sig.startswith(name):
            sig = f"{name} : {sig}"
        return with_const(sig)

    y = (yonai or {}).get(name)
    if y:
        body = pretty_arrow(
            y["params"],
            y["ret"],
            type_arities,
            fn_name=name,
            param_names=pnames,
        )
        return with_const(f"{name} : {body}")

    letters = "abcdefghijklmnopqrstuvwxyz"
    if nparams == 0:
        body = "a"
    else:
        args = [letters[i] for i in range(nparams)]
        body = " -> ".join(args) + f" -> {letters[nparams]}"
    return with_const(f"{name} : {body}")


def upsert_function(
    functions: list[dict], by_name: dict[str, dict], entry: dict
) -> None:
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
    if (
        entry.get("simple_rhs") is not None
        and existing.get("simple_rhs") is None
    ):
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
        "yonai": parse_yonai(path.with_suffix(".yonai")),
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
        if (
            fn
            and not line.startswith("export")
            and not line.startswith("module")
        ):
            name = fn.group(1)
            rest = fn.group(2)
            if rest.lstrip().startswith(":"):
                # Yona permits long arrow signatures to continue on following
                # indented lines. A trailing arrow is unambiguously incomplete;
                # join continuations before recording the public contract.
                signature_lines = [line.strip()]
                while signature_lines[-1].endswith("->") and i + 1 < len(lines):
                    i += 1
                    signature_lines.append(lines[i].strip())
                upsert_function(
                    functions,
                    by_name,
                    {
                        "name": name,
                        "type_sig": " ".join(signature_lines),
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
        module["types"] = [
            t for t in module["types"] if t["name"] in module["exported_types"]
        ]
    if module["exported_traits"]:
        module["traits"] = [
            t
            for t in module["traits"]
            if t["name"] in module["exported_traits"]
        ]

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
    arities = type_arities_from_defs(module["types"])

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
            sig = signature_of(f, module.get("yonai"), arities)
            out.append(f"### `{sig}`")
            out.append("")
            if f["doc"]:
                out.append(render_doc(f["doc"]))
                out.append("")

    return "\n".join(out).rstrip() + "\n"


def compact_existing_doc(text: str) -> str:
    """Turn heading + implementation fence into a one-line signature."""

    def fn_sub(m: re.Match) -> str:
        name = m.group(1)
        first = m.group(2).strip().split("\n", 1)[0].rstrip()
        first = re.sub(r"\s*=\s*$", "", first)
        return f"### {name}\n\n`{first}`\n\n"

    text = DOCUMENTED_FN.sub(fn_sub, text)
    text = DOCUMENTED_TYPE.sub(r"### \1\n\n`type \1\2`\n", text)
    return re.sub(r"\n{3,}", "\n\n", text)


HEADING_SIG = re.compile(
    r"^### ([a-z][a-zA-Z0-9_]*)\n\n`([^`]+)`\n", re.MULTILINE
)
HEADING_TYPED = re.compile(
    r"^### `([a-z][a-zA-Z0-9_]*) : [^`]+`\n", re.MULTILINE
)


def apply_yonai_sigs(
    text: str,
    yonai: dict[str, dict],
    type_arities: dict[str, list[str]] | None = None,
) -> str:
    """Replace parameter-list signatures with `name : T1 -> T2 -> R` from .yonai."""

    def pretty_of(name: str) -> str | None:
        if name not in yonai:
            return None
        y = yonai[name]
        return pretty_arrow(
            y["params"],
            y["ret"],
            type_arities,
            fn_name=name,
        )

    def repl(m: re.Match) -> str:
        name, old = m.group(1), m.group(2)
        pretty = pretty_of(name)
        if pretty:
            return f"### `{name} : {pretty}`\n"
        if ":" in old:
            # Already a type signature on the following line — lift it into the heading.
            sig = old if old.startswith(name) else f"{name} : {old}"
            return f"### `{sig}`\n"
        bits = old.split()
        nparams = max(0, len(bits) - 1)
        letters = "abcdefghijklmnopqrstuvwxyz"
        if nparams == 0:
            body = "a"
        else:
            args = [letters[i] for i in range(nparams)]
            body = " -> ".join(args) + f" -> {letters[nparams]}"
        return f"### `{name} : {body}`\n"

    def repl_typed(m: re.Match) -> str:
        name = m.group(1)
        pretty = pretty_of(name)
        if not pretty:
            return m.group(0)
        return f"### `{name} : {pretty}`\n"

    return HEADING_TYPED.sub(repl_typed, HEADING_SIG.sub(repl, text))


def first_sentence(module_doc: list[str]) -> str:
    desc = module_doc[0] if module_doc else ""
    if ". " in desc:
        desc = desc[: desc.index(". ") + 1]
    return desc


def parse_existing_index_row(path: Path) -> dict | None:
    text = path.read_text()
    heading = re.match(r"^#\s+(.+)$", text, re.MULTILINE)
    if not heading:
        return None
    title = heading.group(1).strip()
    body = text[heading.end() :]
    paras = [p.strip() for p in re.split(r"\n\s*\n", body) if p.strip()]
    desc = ""
    for p in paras:
        if (
            p.startswith("#")
            or p.startswith("|")
            or p.startswith("```")
            or p.startswith("**")
        ):
            continue
        desc = p.split("\n", 1)[0]
        if ". " in desc:
            desc = desc[: desc.index(". ") + 1]
        break
    n_fn = len(re.findall(r"^### ", text, re.MULTILINE))
    # types/traits use ### too; count Functions-section headings only
    fn_section = re.search(
        r"^## Functions\n([\s\S]*?)(?=^## |\Z)", text, re.MULTILINE
    )
    if fn_section:
        n_fn = len(re.findall(r"^### ", fn_section.group(1), re.MULTILINE))
    type_section = re.search(
        r"^## Types\n([\s\S]*?)(?=^## |\Z)", text, re.MULTILINE
    )
    n_ty = (
        len(re.findall(r"^### ", type_section.group(1), re.MULTILINE))
        if type_section
        else 0
    )
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
        compacted = compact_existing_doc(md.read_text())
        yonai = parse_yonai(LIB_DIR / (md.stem + ".yonai"))
        compacted = apply_yonai_sigs(compacted, yonai)
        if compacted != md.read_text():
            md.write_text(compacted)
        row = parse_existing_index_row(md)
        if row:
            leftover_rows.append(row)
            print(f"  {row['title']}: {row['functions']} functions")

    index = ["# Yona Standard Library API Reference", ""]
    n_fn = sum(len(m["functions"]) for m in modules) + sum(
        r["functions"] for r in leftover_rows
    )
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
