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

1. Sign in at https://copr.fedorainfracloud.org/ (FAS account).
2. Create project **`yona`** under user **`kovariadam`**: https://copr.fedorainfracloud.org/coprs/new/
3. Enable **network during the build** (CMake/LLVM fetch is not required, but matching winetop avoids surprises).
4. Enable the chroots you care about (Fedora current + EPEL if you want RHEL).
5. Confirm the project URL is https://copr.fedorainfracloud.org/coprs/kovariadam/yona/
6. API token: https://copr.fedorainfracloud.org/api/ — you already have `login` / `token` / `username` for winetop.

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
