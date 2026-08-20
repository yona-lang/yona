param(
    [string]$BuildDir = "out/build/x64-release",
    [string]$Version = "0.1.0",
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
$stageDir = Join-Path $absOutDir "stage"
$wxsPath = Join-Path $PSScriptRoot "YonaInstaller.wxs"

Require-Path $absBuildDir "Build directory"
Require-Path (Join-Path $absBuildDir "yonac.exe") "yonac.exe"
Require-Path (Join-Path $absBuildDir "yona.exe") "yona.exe"
Require-Path (Join-Path $absBuildDir "yona-repl.exe") "yona-repl.exe"
Require-Path (Join-Path $absBuildDir "yls.exe") "yls.exe"
Require-Path (Join-Path $absBuildDir "runtime") "runtime directory"
Require-Path $wxsPath "WiX source"

New-Item -ItemType Directory -Path $absOutDir -Force | Out-Null
if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir
}

New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stageDir "bin") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stageDir "lib\Std") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stageDir "runtime") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stageDir "src\runtime\platform") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stageDir "include\yona\runtime") -Force | Out-Null

Copy-Item (Join-Path $absBuildDir "yonac.exe") (Join-Path $stageDir "bin\yonac.exe")
Copy-Item (Join-Path $absBuildDir "yona.exe") (Join-Path $stageDir "bin\yona.exe")
Copy-Item (Join-Path $absBuildDir "yona-repl.exe") (Join-Path $stageDir "bin\yona-repl.exe")
Copy-Item (Join-Path $absBuildDir "yls.exe") (Join-Path $stageDir "bin\yls.exe")
Copy-Item (Join-Path $repoRoot "lib\Std\*") (Join-Path $stageDir "lib\Std\") -Recurse -Force
Copy-Item (Join-Path $absBuildDir "runtime\*") (Join-Path $stageDir "runtime\") -Recurse -Force
Copy-Item (Join-Path $repoRoot "src\compiled_runtime.c") (Join-Path $stageDir "src\compiled_runtime.c")
Copy-Item (Join-Path $repoRoot "src\runtime\*") (Join-Path $stageDir "src\runtime\") -Recurse -Force
Copy-Item (Join-Path $repoRoot "src\runtime\platform\*") (Join-Path $stageDir "src\runtime\platform\") -Recurse -Force
Copy-Item (Join-Path $repoRoot "include\yona\runtime\*") (Join-Path $stageDir "include\yona\runtime\") -Recurse -Force
Copy-Item (Join-Path $repoRoot "LICENSE.txt") (Join-Path $stageDir "LICENSE.txt")
Copy-Item (Join-Path $repoRoot "README.md") (Join-Path $stageDir "README.md")
Copy-Item (Join-Path $repoRoot "INSTALL.md") (Join-Path $stageDir "INSTALL.md")
Copy-Item (Join-Path $repoRoot "CHANGELOG.md") (Join-Path $stageDir "CHANGELOG.md")

$msiPath = Join-Path $absOutDir "yona-$Version-windows-x64.msi"

& wix build `
  -d Version=$Version `
  -d StagingDir=$stageDir `
  -arch x64 `
  -out $msiPath `
  $wxsPath
if ($LASTEXITCODE -ne 0) {
    throw "wix build failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path $msiPath)) {
    throw "wix build reported success but MSI is missing: $msiPath"
}

Write-Host "MSI built: $msiPath"
