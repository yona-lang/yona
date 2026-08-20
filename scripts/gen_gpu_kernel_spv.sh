#!/bin/sh
# Regenerate uint32 SPIR-V embeds under src/runtime/gpu/*_spv.inc
# from GLSL compute shaders. Used for new Track G kernels (square, filter LT).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "$GLSLANG" ]; then
    if command -v glslangValidator >/dev/null 2>&1; then
        GLSLANG=$(command -v glslangValidator)
    elif command -v brew >/dev/null 2>&1; then
        prefix=$(brew --prefix glslang 2>/dev/null || true)
        if [ -n "$prefix" ] && [ -x "$prefix/bin/glslangValidator" ]; then
            GLSLANG="$prefix/bin/glslangValidator"
        fi
    fi
fi
if [ -z "$GLSLANG" ] || [ ! -x "$GLSLANG" ]; then
    echo "glslangValidator not found (set GLSLANG or install glslang)" >&2
    exit 1
fi

emit_inc() {
    src="$1"
    dest="$2"
    symbol="$3"
    tmp=$(mktemp)
    "$GLSLANG" -V -e main "$src" -o "$tmp"
    python3 - "$tmp" "$dest" "$symbol" <<'PY'
import pathlib, struct, sys
spv = pathlib.Path(sys.argv[1]).read_bytes()
out = pathlib.Path(sys.argv[2])
symbol = sys.argv[3]
if len(spv) % 4 != 0:
    raise SystemExit(f"{sys.argv[1]}: SPIR-V length not a multiple of 4")
words = list(struct.unpack("<" + "I" * (len(spv) // 4), spv))
lines = [
    "#include <stdint.h>",
    "",
    f"static const uint32_t {symbol}[] = {{",
]
row = []
for w in words:
    row.append(f"0x{w:08x}u")
    if len(row) == 4:
        lines.append("  " + ",  ".join(row) + ",")
        row = []
if row:
    lines.append("  " + ",  ".join(row) + ",")
lines.append("};")
lines.append(
    f"static const uint32_t {symbol}WordCount = "
    f"(uint32_t)(sizeof({symbol})/sizeof({symbol}[0]));"
)
lines.append("")
out.write_text("\n".join(lines) + "\n")
PY
    rm -f "$tmp"
}

SH="$ROOT/src/runtime/gpu/shaders"
INC="$ROOT/src/runtime/gpu"

emit_inc "$SH/map_square_int64.comp" "$INC/map_square_int64_spv.inc" kYonaGpuMapSquareInt64Spv
emit_inc "$SH/map_square_int32.comp" "$INC/map_square_int32_spv.inc" kYonaGpuMapSquareInt32Spv
emit_inc "$SH/filter_mark_lt_int64.comp" "$INC/filter_mark_lt_int64_spv.inc" kYonaGpuFilterMarkLtInt64Spv
emit_inc "$SH/filter_mark_lt_int32.comp" "$INC/filter_mark_lt_int32_spv.inc" kYonaGpuFilterMarkLtInt32Spv
echo "wrote map_square + filter_mark_lt SPIR-V embeds"
