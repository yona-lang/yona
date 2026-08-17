# Releasing Yona

Push a version tag to trigger [.github/workflows/release.yml](../.github/workflows/release.yml):

```bash
# VERSION file and dist/copr/yona.spec Version: must match the tag
git tag v0.1.1
git push origin v0.1.1
```

The tag must already exist on the commit you want; CI uses `GITHUB_REF_NAME` from the tag push.

## What CI publishes

| Channel | Job | Secrets used |
|---------|-----|----------------|
| GitHub Release (Linux/macOS tarballs, Windows zip/MSI) | `release` | `GITHUB_TOKEN` (automatic) |
| AUR `yona-bin` | `aur` | `AUR_SSH_PRIVATE_KEY` |
| Fedora Copr `$COPR_USERNAME/yona` | `copr` | `COPR_LOGIN`, `COPR_TOKEN`, `COPR_USERNAME` |
| Launchpad `ppa:kovariadam/yona` | `launchpad` | `LAUNCHPAD_*` (`continue-on-error`) |

First real Copr / AUR / PPA publish happens on the **next `v*` tag**, after the one-time setup below. Pushing this documentation commit alone does not publish packages.

## One-time setup (you must do this)

These accounts and secrets are **not** created by CI. Do them once on your machine, then copy the same credentials you already use for [akovari/winetop](https://github.com/akovari/winetop) onto `yona-lang/yonac-llvm`.

### 1. Fedora Copr project

Sign in with your FAS account: https://copr.fedorainfracloud.org/

API token (same as winetop): https://copr.fedorainfracloud.org/api/ — values go in `~/.config/copr` and the GitHub secrets below.

#### Recommended chroots (v1)

Match [kovariadam/winetop](https://copr.fedorainfracloud.org/coprs/kovariadam/winetop/) **but start x86_64-only**. `dist/copr/yona.spec` runs `cmake --preset x64-release-linux`, which is the x86_64 Linux preset. aarch64 will mis-build until the spec picks `arm64-release-linux` on that arch.

| Enable now | Chroot | LLVM on that Fedora (approx.) |
|------------|--------|-------------------------------|
| yes | `fedora-43-x86_64` | 21 |
| yes | `fedora-44-x86_64` | 22 |
| yes | `fedora-45-x86_64` | 22 |
| yes | `fedora-rawhide-x86_64` | 22 |
| later | `fedora-43-aarch64`, `fedora-44-aarch64`, `fedora-45-aarch64`, `fedora-rawhide-aarch64` | same as above, after spec is arch-aware |
| no | `fedora-42-*` | Copr no longer lists `fedora-42-x86_64` (EOL) |
| no | EPEL 8/9/10, RHEL, CentOS Stream | older `llvm-devel`; C++23 + current LLVM APIs will likely fail |
| no | `i386`, `ppc64le`, `s390x`, `riscv64` | no CI, huge LLVM compile, riscv is QEMU-emulated |

#### Create via CLI (preferred)

```bash
# uses ~/.config/copr (same token as winetop)
copr-cli whoami   # expect: kovariadam

copr-cli create yona \
  --chroot fedora-43-x86_64 \
  --chroot fedora-44-x86_64 \
  --chroot fedora-45-x86_64 \
  --chroot fedora-rawhide-x86_64 \
  --enable-net on \
  --follow-fedora-branching on \
  --appstream off \
  --unlisted-on-hp off \
  --description "Yona programming language compiler targeting LLVM" \
  --instructions "sudo dnf copr enable kovariadam/yona && sudo dnf install yona"
```

`--enable-net on` matches the release workflow (`copr-cli buildscm … --enable-net on`). The spec does not download crates, but GitHub `Source0` / SCM clone is happier with network on.

`--follow-fedora-branching on` auto-adds the next Fedora x86_64 chroot when Rawhide branches (same as winetop growing F45).

Confirm: https://copr.fedorainfracloud.org/coprs/kovariadam/yona/

#### Same settings in the web UI

1. https://copr.fedorainfracloud.org/coprs/new/
2. **Name:** `yona` (not `yonac`, not `yonac-llvm`)
3. **Chroots:** tick only the four `fedora-{43,44,45,rawhide}-x86_64` boxes
4. **Follow Fedora branching:** on
5. **Enable networking during the build:** on
6. **Generate AppStream metadata:** off
7. **Unlisted on homepage:** off
8. Description / instructions as in the CLI command
9. Create. Do **not** add a package by hand; the GitHub `copr` job submits `buildscm` on the next `v*` tag.

#### After aarch64 is supported in the spec

```bash
copr-cli modify yona \
  --chroot fedora-43-x86_64 \
  --chroot fedora-44-x86_64 \
  --chroot fedora-45-x86_64 \
  --chroot fedora-rawhide-x86_64 \
  --chroot fedora-43-aarch64 \
  --chroot fedora-44-aarch64 \
  --chroot fedora-45-aarch64 \
  --chroot fedora-rawhide-aarch64
```

`modify --chroot` **replaces** the chroot set; list every chroot you want to keep.

#### Optional smoke build (before the first tag)

```bash
copr-cli buildscm kovariadam/yona \
  --clone-url https://github.com/yona-lang/yonac-llvm.git \
  --commit master \
  --spec dist/copr/yona.spec \
  --enable-net on
```

This compiles Yona + LLVM in Copr (~tens of minutes per chroot). Skip if you would rather wait for the release tag.

### 2. AUR (`yona-bin`)

1. AUR account with an SSH public key registered: https://aur.archlinux.org/account/
2. Store the matching **OpenSSH private key** as `AUR_SSH_PRIVATE_KEY` on this repo (see commands below).
   - Must begin with `-----BEGIN OPENSSH PRIVATE KEY-----`
   - Real newlines (not the two-character sequence `\n`)
3. First successful `aur` job **creates** `yona-bin`. You do not create the AUR package by hand.

### 3. Launchpad PPA

1. Launchpad account `kovariadam`: https://launchpad.net/~kovariadam
2. Create PPA **`yona`**: https://launchpad.net/~kovariadam/+activate-ppa  
   Target URL: https://launchpad.net/~kovariadam/+archive/ubuntu/yona
3. Confirm the **public** GPG key is on Launchpad: https://launchpad.net/~kovariadam/+editpgpkeys  
   Fingerprint: `A527 AE5A 9746 F3D9 54CA  8F4C 9C7E 01C1 5210 C325`  
   Key ID: `9C7E01C15210C325`
4. Ubuntu keyserver (if not already published):

   ```bash
   gpg --send-keys --keyserver keyserver.ubuntu.com A527AE5A9746F3D954CA8F4C9C7E01C15210C325
   ```

5. Noble builders use distro `llvm-dev` (~18). If the source package fails to configure, the job is allowed to fail; users can still install from Copr, AUR, or the GitHub tarball / `dist/debian/build-deb-from-release.sh`.

Full secret table: [launchpad/README.md](launchpad/README.md).

### 4. GitHub Actions secrets on `yona-lang/yonac-llvm`

You need **admin** on `yona-lang/yonac-llvm`. Reuse the winetop values:

```bash
REPO=yona-lang/yonac-llvm

# Copr (same API token as winetop)
gh secret set COPR_LOGIN --repo "$REPO"      # paste login from ~/.config/copr
gh secret set COPR_TOKEN --repo "$REPO"      # paste token
gh secret set COPR_USERNAME --repo "$REPO" --body "kovariadam"

# AUR (same key as winetop; OpenSSH private key, real newlines)
gh secret set AUR_SSH_PRIVATE_KEY --repo "$REPO" < ~/.ssh/id_ed25519   # or your AUR key path

# Launchpad (same GPG material as winetop)
FPR=A527AE5A9746F3D954CA8F4C9C7E01C15210C325
gh secret set LAUNCHPAD_PPA --repo "$REPO" --body "ppa:kovariadam/yona"
gh secret set LAUNCHPAD_USER --repo "$REPO" --body "kovariadam"
gh secret set LAUNCHPAD_GPG_FINGERPRINT --repo "$REPO" --body "$FPR"
gh secret set LAUNCHPAD_GPG_KEY_ID --repo "$REPO" --body "9C7E01C15210C325"
gpg --armor --export-secret-keys "$FPR" | gh secret set LAUNCHPAD_GPG_PRIVATE_KEY --repo "$REPO"
# empty if the key has no passphrase:
printf '' | gh secret set LAUNCHPAD_GPG_PASSPHRASE --repo "$REPO"
```

To copy from winetop without re-typing Copr fields (if `gh` can read the other repo’s secrets — it cannot; you must paste from `~/.config/copr` or the Copr API page).

Confirm secrets exist:

```bash
gh secret list --repo yona-lang/yonac-llvm
```

Expected names: `AUR_SSH_PRIVATE_KEY`, `COPR_LOGIN`, `COPR_TOKEN`, `COPR_USERNAME`, `LAUNCHPAD_PPA`, `LAUNCHPAD_USER`, `LAUNCHPAD_GPG_FINGERPRINT`, `LAUNCHPAD_GPG_KEY_ID`, `LAUNCHPAD_GPG_PRIVATE_KEY`, `LAUNCHPAD_GPG_PASSPHRASE`.

## Version bump checklist (each release)

1. `VERSION` file
2. `dist/copr/yona.spec` and `packaging/yona.spec` `Version:`
3. `packaging/debian/changelog` (Launchpad CI overwrites this on upload)
4. `CHANGELOG.md`
5. Tag `vX.Y.Z` and push

## Install after release

End-user matrix: [../README.md](../README.md#quick-start).

```bash
sudo dnf copr enable kovariadam/yona && sudo dnf install yona
yay -S yona-bin
sudo add-apt-repository ppa:kovariadam/yona && sudo apt install yona
```
