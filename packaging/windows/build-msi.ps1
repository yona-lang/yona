param(
    [string]$BuildDir = "out/build/x64-release",
    [string]$Version = "0.1.0",
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",
    [string]$OutDir = "out/installer/windows"
)

$ErrorActionPreference = "Stop"

function Require-Path([string]$Path, [string]$What) {
    if (-not (Test-Path $Path)) {
        throw "$What not found: $Path"
    }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$absBuildDir = Join-Path $repoRoot $BuildDir
$absOutDir = Join-Path $repoRoot $OutDir
$architectureOutDir = Join-Path $absOutDir $Architecture
$stageDir = Join-Path $architectureOutDir "stage"
$wxsPath = Join-Path $PSScriptRoot "YonaInstaller.wxs"

Require-Path $absBuildDir "Build directory"
Require-Path (Join-Path $absBuildDir "yonac.exe") "yonac.exe"
Require-Path (Join-Path $absBuildDir "yona.exe") "yona.exe"
Require-Path (Join-Path $absBuildDir "yona-repl.exe") "yona-repl.exe"
Require-Path (Join-Path $absBuildDir "yls.exe") "yls.exe"
Require-Path $wxsPath "WiX source"

New-Item -ItemType Directory -Path $absOutDir -Force | Out-Null
New-Item -ItemType Directory -Path $architectureOutDir -Force | Out-Null
if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir
}

New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
& cmake --install $absBuildDir --prefix $stageDir
if ($LASTEXITCODE -ne 0) {
    throw "cmake --install failed with exit code $LASTEXITCODE"
}
Copy-Item (Join-Path $repoRoot "LICENSE.txt") (Join-Path $stageDir "LICENSE.txt")
Copy-Item (Join-Path $repoRoot "README.md") (Join-Path $stageDir "README.md")
Copy-Item (Join-Path $repoRoot "INSTALL.md") (Join-Path $stageDir "INSTALL.md")
Copy-Item (Join-Path $repoRoot "CHANGELOG.md") (Join-Path $stageDir "CHANGELOG.md")

$msiPath = Join-Path $architectureOutDir "yona-$Version-windows-$Architecture.msi"

& wix build `
  -d Version=$Version `
  -d StagingDir=$stageDir `
  -arch $Architecture `
  -out $msiPath `
  $wxsPath
if ($LASTEXITCODE -ne 0) {
    throw "wix build failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path $msiPath)) {
    throw "wix build reported success but MSI is missing: $msiPath"
}

Write-Host "MSI built: $msiPath"
