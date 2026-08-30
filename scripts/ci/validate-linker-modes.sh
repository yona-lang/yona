#!/usr/bin/env bash
# Smoke-test yonac system and in-process linker modes against a built tree.
set -euo pipefail

BUILD_DIR="${1:?usage: validate-linker-modes.sh <build-dir>}"
YONAC="${BUILD_DIR}/yonac"
CC="${YONAC_CC:-${CC:-clang}}"

test -x "${YONAC}"
test -f "${BUILD_DIR}/runtime/libyona_runtime.a"

INPROC_ACTIVE="$(awk -F= '/^YONA_INPROCESS_LLD_AVAILABLE:BOOL=/{print $2}' "${BUILD_DIR}/CMakeCache.txt" | tr -d '\r')"

SMOKE="$(cd "$(dirname "$0")" && pwd)/smoke.yona"
YONAC_CC="${CC}" "${YONAC}" --sysroot "${BUILD_DIR}" --linker-mode system "${SMOKE}" -o "${BUILD_DIR}/ci_link_system"
[[ "$("${BUILD_DIR}/ci_link_system")" == "3" ]]

if [[ "${INPROC_ACTIVE}" =~ ^(ON|TRUE|YES|1)$ ]]; then
  YONAC_REQUIRE_INPROCESS_LLD=1 YONAC_CC="${CC}" "${YONAC}" --sysroot "${BUILD_DIR}" --linker-mode inprocess "${SMOKE}" -o "${BUILD_DIR}/ci_link_inprocess"
else
  YONAC_CC="${CC}" "${YONAC}" --sysroot "${BUILD_DIR}" --linker-mode inprocess "${SMOKE}" -o "${BUILD_DIR}/ci_link_inprocess"
fi
[[ "$("${BUILD_DIR}/ci_link_inprocess")" == "3" ]]
