#!/usr/bin/env bash
# Install Mozilla SOPS for CI / deploy workflows.
#
# Problem: `curl -o /usr/local/bin/sops` without sudo → "Permission denied".
# Fix: use passwordless sudo when available (GitHub-hosted ubuntu-*), else
# install to ~/.local/bin and add to GITHUB_PATH for subsequent steps.
#
# Usage in a workflow step:
#   env:
#     SOPS_VERSION: v3.9.1
#   run: bash scripts/ci-install-sops.sh
#
set -euo pipefail

SOPS_VERSION="${SOPS_VERSION:-v3.9.1}"
arch="$(uname -m)"
os="$(uname -s)"

if [[ "$os" == Darwin ]] || [[ "${RUNNER_OS:-}" == "macOS" ]]; then
  case "$arch" in
    x86_64 | amd64) asset="sops-${SOPS_VERSION}.darwin.amd64" ;;
    arm64) asset="sops-${SOPS_VERSION}.darwin.arm64" ;;
    *)
      echo "Unsupported macOS uname -m: $arch" >&2
      exit 1
      ;;
  esac
else
  case "$arch" in
    x86_64 | amd64) asset="sops-${SOPS_VERSION}.linux.amd64" ;;
    aarch64 | arm64) asset="sops-${SOPS_VERSION}.linux.arm64" ;;
    *)
      echo "Unsupported Linux uname -m: $arch" >&2
      exit 1
      ;;
  esac
fi

url="https://github.com/getsops/sops/releases/download/${SOPS_VERSION}/${asset}"

install_sudo() {
  sudo curl -fsSL -o /usr/local/bin/sops "$url"
  sudo chmod 755 /usr/local/bin/sops
}

install_user() {
  local dest="${HOME}/.local/bin"
  mkdir -p "$dest"
  curl -fsSL -o "${dest}/sops" "$url"
  chmod 755 "${dest}/sops"
  if [[ -n "${GITHUB_PATH:-}" ]]; then
    echo "$dest" >>"$GITHUB_PATH"
  fi
  export PATH="${dest}:${PATH}"
}

if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
  install_sudo
else
  install_user
fi

sops --version
