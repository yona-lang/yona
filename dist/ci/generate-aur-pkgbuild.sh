#!/usr/bin/env bash
# Write an updated AUR PKGBUILD for yona-bin (GitHub linux-x86_64 tarball).
set -euo pipefail

VERSION="${1:?version required (no v prefix)}"
OUT="${2:-PKGBUILD}"

cat >"$OUT" <<EOF
# Maintainer: Adam Kovari <adam@kovari.eu>
pkgname=yona-bin
pkgver=${VERSION}
pkgrel=1
pkgdesc="Yona programming language compiler targeting LLVM"
arch=('x86_64')
url="https://github.com/yona-lang/yona"
license=('GPL-3.0-only')
depends=('llvm-libs' 'clang' 'lld' 'pcre2')
provides=('yona')
conflicts=('yona')
options=('!strip')
source=("\$pkgname-\$pkgver-linux-x86_64.tar.gz::https://github.com/yona-lang/yona/releases/download/v\$pkgver/yona-\$pkgver-linux-x86_64.tar.gz")
sha256sums=('SKIP')

package() {
  cd "yona-\${pkgver}-linux-x86_64"
  install -Dm755 bin/yonac "\$pkgdir/usr/bin/yonac"
  install -Dm755 bin/yona "\$pkgdir/usr/bin/yona"
  install -Dm755 bin/yona-repl "\$pkgdir/usr/bin/yona-repl"
  install -Dm755 bin/yls "\$pkgdir/usr/bin/yls"
  install -d "\$pkgdir/usr/lib/yona"
  cp -a lib runtime "\$pkgdir/usr/lib/yona/"
  install -d "\$pkgdir/usr/include"
  cp -a include/yona "\$pkgdir/usr/include/"
}
EOF
