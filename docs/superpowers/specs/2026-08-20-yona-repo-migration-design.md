# Design: move LLVM Yona onto yona-lang/yona

Date: 2026-08-20

## Goal

Keep the 137 stars and the `https://github.com/yona-lang/yona` URL. Replace that
repo’s git history with `yona-lang/yonac-llvm`. Preserve the GraalVM/Truffle
tree (git, wiki, published releases, all issues) on `yona-lang/yona-graalvm`.
Archive `yonac-llvm` with a README pointer (do not delete).

## Why not rename

Stars follow the GitHub repository object, not the name. Renaming `yona` →
`yona-graalvm` would take the stars with the archive.

## Order (safety)

1. Create empty `yona-lang/yona-graalvm`.
2. Mirror-push all refs from `yona` (branches `master`, `sockets`, `stm`, tags).
3. Copy wiki `yona.wiki.git` → `yona-graalvm.wiki.git`.
4. Recreate published GitHub Releases (skip draft “Release refs/heads/master”).
5. Transfer every issue (open and closed) from `yona` → `yona-graalvm`.
6. Verify SHAs, tags, wiki HEAD, issue count. Then archive `yona-graalvm`.
7. `--mirror` force-push `yonac-llvm` onto `yona`.
8. Recreate LLVM releases (`v0.1.1`–`v0.1.3`; tag `v0.1.4` has no GH release).
9. Transfer LLVM issues to `yona`. Update description/topics.
10. Rewrite clone/docs/formula/workflow URLs to `yona-lang/yona`.
11. Replace `yonac-llvm` with a short README and archive it.

## Cannot move

- Actions **secret values** (must be re-created on `yona` by a human).
- Fork parentage (7 existing forks of `yona` stay; history will look unrelated).
- GitHub Actions run history.
- Discussions (0 on `yona`; nothing to copy).
- Merged PRs (cannot transfer; git history on the archive still has the commits).

## Secrets to re-add on yona-lang/yona

`AUR_SSH_PRIVATE_KEY`, `CLOUDFLARE_API_TOKEN`, `COPR_LOGIN`, `COPR_TOKEN`,
`COPR_USERNAME`, `HOMEBREW_TAP_REPO`, `HOMEBREW_TAP_SSH_KEY`,
`LAUNCHPAD_GPG_FINGERPRINT`, `LAUNCHPAD_GPG_KEY_ID`, `LAUNCHPAD_GPG_PASSPHRASE`,
`LAUNCHPAD_GPG_PRIVATE_KEY`, `LAUNCHPAD_PPA`, `LAUNCHPAD_USER`. Variable:
`CLOUDFLARE_ACCOUNT_ID`.

## Docs after swap

Replace `yona-lang/yonac-llvm` with `yona-lang/yona` in `INSTALL.md`, site
pages, `Formula/yona.rb`, release workflow, and issue links (new numbers after
transfer). Issue-transfer redirects keep working while `yonac-llvm` stays
archived, not deleted.
