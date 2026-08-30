# Packaging

Install instructions for end users are in the root [README.md](../README.md). This tree holds packaging metadata and maintainer notes.

## Ubuntu / Debian

- **PPA:** [`ppa:kovariadam/yona`](https://launchpad.net/~kovariadam/+archive/ubuntu/yona) — see [launchpad/README.md](launchpad/README.md)
- Debian source metadata: [../packaging/debian/](../packaging/debian/)
- [debian/build-deb-from-release.sh](debian/build-deb-from-release.sh) — build a `.deb` from a GitHub Release tarball (no compiler needed)
- Signing key fingerprint: `A527 AE5A 9746 F3D9 54CA  8F4C 9C7E 01C1 5210 C325`

Launchpad builders compile from source against Ubuntu’s `llvm-dev` (Noble: LLVM 18; Yona requires 16+). They have **no network**, so the package uses distro `libcli11-dev` / `libxml2-dev` with `-DYONA_FETCH_DEPS=OFF`. The Launchpad job is `continue-on-error`. Use the binary `.deb` script or GitHub tarball if the PPA build fails.

## Fedora Copr

- Project: [kovariadam/yona](https://copr.fedorainfracloud.org/coprs/kovariadam/yona/)
- Spec: [../packaging/yona.spec](../packaging/yona.spec)

## Arch (AUR)

- Generated at release time by [ci/generate-aur-pkgbuild.sh](ci/generate-aur-pkgbuild.sh) → published as `yona-bin`

## Homebrew

- Tap: [`akovari/homebrew-tap`](https://github.com/akovari/homebrew-tap) (`brew install akovari/tap/yona`)
- Generator: [ci/generate-homebrew-formula.sh](ci/generate-homebrew-formula.sh) — **source** formula (Homebrew `llvm` / `lld` / `pcre2` / `cli11`), not GitHub CI tarballs
- Options: `--with-vulkan`, `--HEAD`; macOS (Intel + Apple Silicon) and Linuxbrew
- Published by the `homebrew` job on each `v*` tag (`HOMEBREW_TAP_SSH_KEY`)

## Releases

Maintainer release checklist and **one-time account setup**: [RELEASING.md](RELEASING.md)
