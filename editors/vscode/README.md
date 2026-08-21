# Yona

Yona language support for VS Code and Cursor: syntax highlighting and the
`yls` language server.

The extension does **not** bundle `yls`. Install the Yona toolchain so `yls`
is on `PATH`, or set `YONA_HOME` / `yona.languageServer.path`.

## Install from VSIX

After a `v*` GitHub Release, the Visual Studio Marketplace listing
(`yona-lang.yona`) and the Open VSX listing (namespace `yona-lang`) are
published by CI.

For a local or unreleased build:

```bash
cd editors/vscode
npm ci
npm test
npm run vsix
```

In VS Code or Cursor: **Extensions: Install from VSIX…** and pick
`yona-<version>.vsix` (for example `yona-0.1.4.vsix`).

`npm run package` is the same as `npm run vsix` (`vsce package`).

## Marketplace and Open VSX

Pushing a version tag (`v*`, for example `v0.1.4`) runs the Release
workflow. `vscode-vsix` builds the `.vsix`; `publish-marketplace` then
runs `vsce publish --packagePath` with environment `VSCE_PAT` (never
echoed, never committed). `publish-openvsx` runs
`ovsx publish --packagePath` with `OVSX_PAT`. If either secret is
unset that job is skipped and the workflow stays green. Namespace
`yona-lang` is claimed.

Still required of a maintainer:

1. Marketplace publisher `yona-lang` and repository secret `VSCE_PAT`
   (Azure DevOps PAT with Marketplace manage scope).
2. Open VSX access token as repository secret `OVSX_PAT` (already set;
   rotate at [open-vsx.org/user-settings/tokens](https://open-vsx.org/user-settings/tokens)
   if needed).
3. Cut a GitHub Release by pushing a `v*` tag whose version matches
   `editors/vscode/package.json`. The same workflow creates the GitHub
   Release and publishes the extension. Do not publish from every push.

Manual publish (tokens in the environment only):

```bash
npx vsce publish --packagePath yona-<version>.vsix
npx ovsx publish --packagePath yona-<version>.vsix
```

Equivalent npm scripts:

```bash
npm run publish:marketplace
npm run publish:ovsx
```

## Settings

| Setting | Meaning |
|---------|---------|
| `yona.languageServer.path` | Absolute path to `yls`. Empty: search `PATH`, then `$YONA_HOME/bin/yls`, then the directory that contains `yonac`. |
| `yona.trace.server` | `off`, `messages`, or `verbose` LSP tracing. |

See the [Editor and language server](https://yona-lang.org/guides/editor/)
guide for features and recovery behavior.
