# Packaging

Install instructions for end users are in the root [README.md](../README.md). This tree holds packaging metadata and maintainer notes.

## Ubuntu / Debian

- **PPA:** [`ppa:kovariadam/yona`](https://launchpad.net/~kovariadam/+archive/ubuntu/yona) — see [launchpad/README.md](launchpad/README.md)
- Debian source metadata: [../packaging/debian/](../packaging/debian/)
- [debian/build-deb-from-release.sh](debian/build-deb-from-release.sh) — build a `.deb` from a GitHub Release tarball (no compiler needed)
- Signing key fingerprint: `A527 AE5A 9746 F3D9 54CA  8F4C 9C7E 01C1 5210 C325`

Launchpad builders compile from source against Ubuntu’s `llvm-dev`. Noble ships LLVM 18; Yona prefers 16+ and recommends 22. The Launchpad job is `continue-on-error` until a builder image with a new enough LLVM is available. Use the binary `.deb` script or GitHub tarball if the PPA build fails.

## Fedora Copr

- Project: [kovariadam/yona](https://copr.fedorainfracloud.org/coprs/kovariadam/yona/)
- Spec: [copr/yona.spec](copr/yona.spec) (same content as [../packaging/yona.spec](../packaging/yona.spec))

## Arch (AUR)

- Generated at release time by [ci/generate-aur-pkgbuild.sh](ci/generate-aur-pkgbuild.sh) → published as `yona-bin`

## Homebrew

- Formula: [../Formula/yona.rb](../Formula/yona.rb) — source build; not auto-published to a tap yet

## Releases

Maintainer release checklist and **one-time account setup**: [RELEASING.md](RELEASING.md)
