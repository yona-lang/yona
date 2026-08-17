#!/bin/sh
# Regenerate include/runtime/gpu_f32_scale_spv.inl from src/runtime/gpu_f32_scale.comp
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "$GLSLANG" ]; then
    if command -v glslangValidator >/dev/null 2>&1; then
        GLSLANG=$(command -v glslangValidator)
    elif command -v brew >/dev/null 2>&1; then
        GLSLANG="$(brew --prefix glslang)/bin/glslangValidator"
    fi
fi
if [ -z "$GLSLANG" ] || [ ! -x "$GLSLANG" ]; then
    echo "glslangValidator not found (set GLSLANG or install glslang)" >&2
    exit 1
fi
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
"$GLSLANG" -V -e main "$ROOT/src/runtime/gpu_f32_scale.comp" -o "$TMP"
python3 - "$TMP" "$ROOT/include/runtime/gpu_f32_scale_spv.inl" <<'PY'
import pathlib, sys

def main():
    spv_path = pathlib.Path(sys.argv[1])
    out_path = pathlib.Path(sys.argv[2])
    data = spv_path.read_bytes()
    lines = [
        "/* SPIR-V: f32 SSBO in-place *= scale (push: u32 n, f32 scale) — generated; do not edit by hand. */",
        "#ifndef YONA_RUNTIME_GPU_F32_SCALE_SPV_INL",
        "#define YONA_RUNTIME_GPU_F32_SCALE_SPV_INL",
        "",
        "static const unsigned char yona_gpu_f32_scale_spv_bytes[] = {",
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
    lines.append("#endif /* YONA_RUNTIME_GPU_F32_SCALE_SPV_INL */")
    lines.append("")
    out_path.write_text("\n".join(lines) + "\n")

if __name__ == "__main__":
    main()
PY
