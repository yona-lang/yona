#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
engine=${YONA_CONTAINER_ENGINE:-podman}
image=${YONA_ARM64_QEMU_IMAGE:-localhost/yona-arm64-qemu:fedora-42}
containerfile="$root_dir/containers/arm64/Containerfile"

command -v "$engine" >/dev/null || {
  echo "error: install Podman or set YONA_CONTAINER_ENGINE" >&2
  exit 1
}

if ! "$engine" image exists "$image"; then
  "$engine" build --arch arm64 --tag "$image" --file "$containerfile" "$root_dir"
fi

"$engine" run --rm --arch arm64 \
  --volume "$root_dir:/workspace:Z" \
  --workdir /workspace \
  "$image" \
  bash -ceu '
    cmake --preset arm64-debug-linux
    cmake --build --preset build-debug-linux-arm64 --target tests
    exec ./out/build/arm64-debug-linux/tests "$@"
  ' -- "$@"
