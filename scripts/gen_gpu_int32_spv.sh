#!/bin/sh
# Regenerate src/runtime/gpu/*_int32_spv.inc from shaders/*_int32.comp
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

emit_inc() {
    src="$1"
    dest="$2"
    symbol="$3"
    tmp=$(mktemp)
    "$GLSLANG" -V -e main "$src" -o "$tmp"
    python3 - "$tmp" "$dest" "$symbol" <<'PY'
import pathlib, sys
spv = pathlib.Path(sys.argv[1]).read_bytes()
out = pathlib.Path(sys.argv[2])
sym = sys.argv[3]
words = []
for i in range(0, len(spv), 4):
    w = int.from_bytes(spv[i:i+4], "little")
    words.append(f"0x{w:08x}u")
lines = [
    "#include <stdint.h>",
    "",
    f"static const uint32_t {sym}[] = {{",
]
row = []
for w in words:
    row.append(w)
    if len(row) == 8:
        lines.append("  " + ", ".join(row) + ",")
        row = []
if row:
    lines.append("  " + ", ".join(row) + ",")
lines.append("};")
lines.append(
    f"static const uint32_t {sym}WordCount = (uint32_t)(sizeof({sym})/sizeof({sym}[0]));"
)
lines.append("")
out.write_text("\n".join(lines))
PY
    rm -f "$tmp"
}

emit_inc "$ROOT/src/runtime/gpu/shaders/map_add_int32.comp" \
    "$ROOT/src/runtime/gpu/map_add_int32_spv.inc" kYonaGpuMapAddInt32Spv
emit_inc "$ROOT/src/runtime/gpu/shaders/map_mul_int32.comp" \
    "$ROOT/src/runtime/gpu/map_mul_int32_spv.inc" kYonaGpuMapMulInt32Spv
emit_inc "$ROOT/src/runtime/gpu/shaders/reduce_block_int32.comp" \
    "$ROOT/src/runtime/gpu/reduce_block_int32_spv.inc" kYonaGpuReduceBlockInt32Spv
emit_inc "$ROOT/src/runtime/gpu/shaders/filter_mark_int32.comp" \
    "$ROOT/src/runtime/gpu/filter_mark_int32_spv.inc" kYonaGpuFilterMarkInt32Spv
emit_inc "$ROOT/src/runtime/gpu/shaders/filter_scatter_int32.comp" \
    "$ROOT/src/runtime/gpu/filter_scatter_int32_spv.inc" kYonaGpuFilterScatterInt32Spv
emit_inc "$ROOT/src/runtime/gpu/shaders/filter_flags_to_int32.comp" \
    "$ROOT/src/runtime/gpu/filter_flags_to_int32_spv.inc" kYonaGpuFilterFlagsToInt32Spv
emit_inc "$ROOT/src/runtime/gpu/shaders/filter_prefix_inclusive_step_int32.comp" \
    "$ROOT/src/runtime/gpu/filter_prefix_inclusive_step_int32_spv.inc" \
    kYonaGpuFilterPrefixInclusiveStepInt32Spv
emit_inc "$ROOT/src/runtime/gpu/shaders/filter_inclusive_to_exclusive_int32.comp" \
    "$ROOT/src/runtime/gpu/filter_inclusive_to_exclusive_int32_spv.inc" \
    kYonaGpuFilterInclusiveToExclusiveInt32Spv
echo "wrote int32 SPIR-V embeds"
