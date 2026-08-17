#!/usr/bin/env bash
# Build a signed Debian source package and dput to Launchpad PPA.
# Expects GPG key already imported; env: LAUNCHPAD_PPA, LAUNCHPAD_GPG_KEY_ID
set -euo pipefail

VERSION="${1:?version without v}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

rm -rf debian
cp -a packaging/debian debian
# compat level comes from Build-Depends: debhelper-compat (= 13)
rm -f debian/compat

# Refresh changelog version for this upload
cat >debian/changelog <<EOF
yona (${VERSION}-1) noble; urgency=medium

  * Upstream release v${VERSION}.

 -- Adam Kovari <adam@kovari.eu>  $(date -Ru)
EOF

mkdir -p debian/source
echo '3.0 (native)' >debian/source/format

# Native package: create orig-less source
dpkg-buildpackage -S -us -uc -d --no-check-builddeps

CHANGES=$(ls -1 ../yona_${VERSION}-1_source.changes | head -1)
debsign -k "${LAUNCHPAD_GPG_KEY_ID}" "$CHANGES"
dput "${LAUNCHPAD_PPA}" "$CHANGES"
echo "Uploaded $CHANGES to ${LAUNCHPAD_PPA}"
