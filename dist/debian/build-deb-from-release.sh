#!/usr/bin/env bash
# Build a simple binary .deb from a GitHub Release tarball (no LLVM toolchain needed).
# Usage:
#   ./dist/debian/build-deb-from-release.sh [version] [arch]
# Example:
#   ./dist/debian/build-deb-from-release.sh 0.1.2 amd64
set -euo pipefail

VERSION="${1:-0.1.2}"
ARCH="${2:-amd64}"

case "$ARCH" in
  amd64) TARBALL_ARCH=x86_64 ;;
  *)
    echo "unsupported arch: $ARCH (use amd64; Linux release tarballs are x86_64)" >&2
    exit 1
    ;;
esac

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

TARBALL="yona-${VERSION}-linux-${TARBALL_ARCH}.tar.gz"
TARBALL_URL="https://github.com/yona-lang/yonac-llvm/releases/download/v${VERSION}/${TARBALL}"
PKGDIR="$WORKDIR/yona_${VERSION}-1_${ARCH}"

mkdir -p "$PKGDIR/DEBIAN" "$PKGDIR/usr/bin" "$PKGDIR/usr/lib/yona" "$PKGDIR/usr/share/doc/yona"

echo "Downloading $TARBALL_URL"
curl -fsSL "$TARBALL_URL" -o "$WORKDIR/$TARBALL"
tar -xzf "$WORKDIR/$TARBALL" -C "$WORKDIR"
SRC="$WORKDIR/yona-${VERSION}-linux-${TARBALL_ARCH}"

install -m 0755 "$SRC/bin/yonac" "$PKGDIR/usr/bin/yonac"
install -m 0755 "$SRC/bin/yona" "$PKGDIR/usr/bin/yona"
cp -a "$SRC/lib" "$SRC/runtime" "$SRC/src" "$SRC/include" "$PKGDIR/usr/lib/yona/"
install -m 0644 "$SRC/README.md" "$PKGDIR/usr/share/doc/yona/README.md"
if [ -f "$SRC/CHANGELOG.md" ]; then
  install -m 0644 "$SRC/CHANGELOG.md" "$PKGDIR/usr/share/doc/yona/changelog"
  gzip -9n "$PKGDIR/usr/share/doc/yona/changelog"
fi
if [ -f "$SRC/LICENSE.txt" ]; then
  install -m 0644 "$SRC/LICENSE.txt" "$PKGDIR/usr/share/doc/yona/copyright"
fi

cat >"$PKGDIR/DEBIAN/control" <<EOF
Package: yona
Version: ${VERSION}-1
Section: devel
Priority: optional
Architecture: ${ARCH}
Maintainer: Adam Kovari <adam@kovari.eu>
Depends: libc6, llvm (>= 16), clang, lld, libpcre2-8-0
Homepage: https://github.com/yona-lang/yonac-llvm
Description: Yona programming language compiler targeting LLVM
 Compiled functional language with ADTs, pattern matching, and a stdlib.
EOF

OUT="$ROOT/dist/debian/yona_${VERSION}-1_${ARCH}.deb"
mkdir -p "$ROOT/dist/debian"
dpkg-deb --build --root-owner-group "$PKGDIR" "$OUT"
echo "Built $OUT"
echo "Install with: sudo apt install ./$(basename "$OUT")"
