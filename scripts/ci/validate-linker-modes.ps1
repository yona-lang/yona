param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"
$yonac = Join-Path $BuildDir "yonac.exe"
if (-not (Test-Path $yonac)) { throw "missing $yonac" }
if (-not (Test-Path (Join-Path $BuildDir "runtime/compiled_runtime.o"))) { throw "missing runtime/compiled_runtime.o" }
if (-not (Test-Path (Join-Path $BuildDir "runtime/yona_runtime.lib"))) { throw "missing runtime/yona_runtime.lib" }

if (-not $env:YONAC_CC) { $env:YONAC_CC = "clang" }

$active = ((Get-Content (Join-Path $BuildDir "CMakeCache.txt")) |
    Where-Object { $_ -like "YONA_INPROCESS_LLD_AVAILABLE:BOOL=*" } |
    Select-Object -First 1).Split("=", 2)[1]

$smoke = Join-Path $PSScriptRoot "smoke.yona"
& $yonac --sysroot $BuildDir --linker-mode system $smoke -o (Join-Path $BuildDir "ci_link_system.exe")
$outSystem = & (Join-Path $BuildDir "ci_link_system.exe")
if ("$outSystem".Trim() -ne "3") { throw "system linker-mode smoke test failed: $outSystem" }

if ($active -match '^(ON|TRUE|YES|1)$') { $env:YONAC_REQUIRE_INPROCESS_LLD = "1" } else { Remove-Item Env:YONAC_REQUIRE_INPROCESS_LLD -ErrorAction SilentlyContinue }
& $yonac --sysroot $BuildDir --linker-mode inprocess $smoke -o (Join-Path $BuildDir "ci_link_inprocess.exe")
$outInprocess = & (Join-Path $BuildDir "ci_link_inprocess.exe")
if ("$outInprocess".Trim() -ne "3") { throw "inprocess linker-mode smoke test failed: $outInprocess" }
