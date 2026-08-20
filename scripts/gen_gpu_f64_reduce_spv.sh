#!/bin/sh
# Regenerate include/runtime/gpu_f64_reduce_spv.inl and gpu_f32_reduce_spv.inl
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

emit_inl() {
    src="$1"
    dest="$2"
    guard="$3"
    symbol="$4"
    comment="$5"
    tmp=$(mktemp)
    "$GLSLANG" -V -e main "$src" -o "$tmp"
    python3 - "$tmp" "$dest" "$guard" "$symbol" "$comment" <<'PY'
import pathlib, sys
spv_path = pathlib.Path(sys.argv[1])
out_path = pathlib.Path(sys.argv[2])
guard = sys.argv[3]
symbol = sys.argv[4]
comment = sys.argv[5]
data = spv_path.read_bytes()
lines = [
    f"/* {comment} — generated; do not edit by hand. */",
    f"#ifndef {guard}",
    f"#define {guard}",
    "",
    f"static const unsigned char {symbol}[] = {{",
]
row = []
for b in data:
    row.append(f"0x{b:02x}")
    if len(row) == 16:
        lines.append("    " + ", ".join(row) + ",")
        row = []
if row:
    lines.append("    " + ", ".join(row) + ",")
lines.append("};")
lines.append("")
lines.append(f"#endif /* {guard} */")
lines.append("")
out_path.write_text("\n".join(lines) + "\n")
PY
    rm -f "$tmp"
}

emit_inl "$ROOT/src/runtime/gpu_f64_reduce.comp" \
    "$ROOT/include/runtime/gpu_f64_reduce_spv.inl" \
    YONA_RUNTIME_GPU_F64_REDUCE_SPV_INL \
    yona_gpu_f64_reduce_spv_bytes \
    "SPIR-V: f64 SSBO block reduce (push: u32 n)"
emit_inl "$ROOT/src/runtime/gpu_f32_reduce.comp" \
    "$ROOT/include/runtime/gpu_f32_reduce_spv.inl" \
    YONA_RUNTIME_GPU_F32_REDUCE_SPV_INL \
    yona_gpu_f32_reduce_spv_bytes \
    "SPIR-V: f32 SSBO block reduce (push: u32 n)"
echo "wrote float reduce SPIR-V embeds"
