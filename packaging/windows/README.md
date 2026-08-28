# Windows Installer (WiX v5)

This directory contains the MSI installer definition for Yona.

## Layout

- `YonaInstaller.wxs`: WiX v5 package definition (`<Files>` harvest of the staging tree).
- `build-msi.ps1`: stages files from a build tree and invokes `wix build`.

## Prerequisites

- WiX Toolset **v5** (`wix.exe` on `PATH`) — v4 rejects `<Files>` inside `ComponentGroup`
- A built Yona tree (default: `out/build/x64-release`; ARM64:
  `out/build/arm64-release`)

## Build MSI

```powershell
pwsh ./packaging/windows/build-msi.ps1 -BuildDir out/build/x64-release -Version 0.1.0

# Native Windows ARM64
pwsh ./packaging/windows/build-msi.ps1 -BuildDir out/build/arm64-release -Architecture arm64 -Version 0.1.0
```

The script creates:

- output directory: `out/installer/windows/<architecture>/`
- staging tree: `out/installer/windows/<architecture>/stage`
- msi: `out/installer/windows/<architecture>/yona-<version>-windows-<architecture>.msi`

`-Architecture` accepts only `x64` (the default) or `arm64`; it is used for
both the isolated staging directory and WiX package architecture.

## Notes

- The draft currently installs into `ProgramFiles64Folder\Yona`.
- `bin` is appended to system `PATH` via MSI environment table.
- The file payload is taken from the staged layout:
  - `bin/` (`yonac.exe`, `yona.exe`, `yona-repl.exe`, `yls.exe`)
  - `lib/Std/`
  - `runtime/` (including the pinned `yona_pcre2.lib` archive required by
    public `Std\Regex` programs)
  - `src/runtime/` (for advanced fallback workflows)
  - `include/yona/runtime/`
  - top-level docs/license files
