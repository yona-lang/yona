#!/bin/sh
# Regenerate include/runtime/gpu_f64_mul2_spv.inl from src/runtime/gpu_f64_mul2.comp
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
glslangValidator -V -e main "$ROOT/src/runtime/gpu_f64_mul2.comp" -o "$TMP"
python3 - "$TMP" "$ROOT/include/runtime/gpu_f64_mul2_spv.inl" <<'PY'
import pathlib, sys

def main():
    spv_path = pathlib.Path(sys.argv[1])
    out_path = pathlib.Path(sys.argv[2])
    data = spv_path.read_bytes()
    lines = [
        "/* SPIR-V: f64 SSBO mul2 — generated; do not edit by hand. */",
        "#ifndef YONA_RUNTIME_GPU_F64_MUL2_SPV_INL",
        "#define YONA_RUNTIME_GPU_F64_MUL2_SPV_INL",
        "",
        "static const unsigned char yona_gpu_f64_mul2_spv_bytes[] = {",
    ]
    row = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02x}")
        if len(row) == 16:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* YONA_RUNTIME_GPU_F64_MUL2_SPV_INL */")
    lines.append("")
    out_path.write_text("\n".join(lines) + "\n")

if __name__ == "__main__":
    main()
PY
