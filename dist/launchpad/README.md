# Launchpad PPA

**PPA:** [`ppa:kovariadam/yona`](https://launchpad.net/~kovariadam/+archive/ubuntu/yona)  
**Packages page:** https://launchpad.net/~kovariadam/+archive/ubuntu/yona/+packages  

**Signing key fingerprint:** `A527 AE5A 9746 F3D9 54CA  8F4C 9C7E 01C1 5210 C325`  
**Key ID:** `9C7E01C15210C325`

## GitHub Actions secrets (on `yona-lang/yonac-llvm`)

| Secret | Value |
|--------|--------|
| `LAUNCHPAD_PPA` | `ppa:kovariadam/yona` |
| `LAUNCHPAD_USER` | `kovariadam` |
| `LAUNCHPAD_GPG_FINGERPRINT` | `A527AE5A9746F3D954CA8F4C9C7E01C15210C325` |
| `LAUNCHPAD_GPG_KEY_ID` | `9C7E01C15210C325` |
| `LAUNCHPAD_GPG_PRIVATE_KEY` | ASCII-armored secret key (see below) |
| `LAUNCHPAD_GPG_PASSPHRASE` | Key passphrase (empty string if none) |

### Export and set the private key

```bash
FPR=A527AE5A9746F3D954CA8F4C9C7E01C15210C325
REPO=yona-lang/yonac-llvm

# 1) Metadata secrets (no private material)
gh secret set LAUNCHPAD_PPA --repo "$REPO" --body "ppa:kovariadam/yona"
gh secret set LAUNCHPAD_USER --repo "$REPO" --body "kovariadam"
gh secret set LAUNCHPAD_GPG_FINGERPRINT --repo "$REPO" --body "$FPR"
gh secret set LAUNCHPAD_GPG_KEY_ID --repo "$REPO" --body "9C7E01C15210C325"

# 2) Private key (prompts for passphrase if the key is protected)
gpg --armor --export-secret-keys "$FPR" | \
  gh secret set LAUNCHPAD_GPG_PRIVATE_KEY --repo "$REPO"

# 3) Passphrase (skip or use empty if the key has no passphrase)
printf '' | gh secret set LAUNCHPAD_GPG_PASSPHRASE --repo "$REPO"
```

Confirm the **public** key is on Launchpad:  
https://launchpad.net/~kovariadam/+editpgpkeys  

And published to the Ubuntu keyserver if you have not already:

```bash
gpg --send-keys --keyserver keyserver.ubuntu.com A527AE5A9746F3D954CA8F4C9C7E01C15210C325
```

## Do you need Launchpad API credentials?

**Usually no** for uploading packages. Uploads use `dput` + GPG; FTP login is anonymous and authenticity is the signature.

**Only if** CI must manage Launchpad via API (create PPAs, query builds, etc.):

1. Install a helper, e.g. `ppa-dev-tools` or [lpcli](https://github.com/canonical/lpcli)
2. Run `ppa credentials create` or `lpcli login` (browser OAuth)
3. Store the resulting access token / secret as optional secrets later

See: https://documentation.ubuntu.com/launchpad/user/how-to/launchpad-api/launchpad-web-signing/

## LLVM on Launchpad builders

Ubuntu Noble’s `llvm-dev` is LLVM 18. Yona’s CMake accepts 16+ and prefers 22. If `find_package(LLVM)` or C++23/LLVM API usage fails on the builder, the `launchpad` workflow job is allowed to fail (`continue-on-error`). Prefer Copr, AUR `yona-bin`, or [../debian/build-deb-from-release.sh](../debian/build-deb-from-release.sh) until a newer Ubuntu series is the PPA default.

## Install from this PPA (once packages are published)

```bash
sudo add-apt-repository ppa:kovariadam/yona
sudo apt update
sudo apt install yona
```
