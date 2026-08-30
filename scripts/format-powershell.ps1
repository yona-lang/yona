param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('check', 'write')]
    [string] $Mode,

    [Parameter(Mandatory = $true, Position = 1, ValueFromRemainingArguments = $true)]
    [string[]] $Paths
)

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$Settings = Join-Path $RepositoryRoot 'ps-script-analyzer-settings.psd1'
$Failed = $false

foreach ($Path in $Paths) {
    $ResolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $Original = [System.IO.File]::ReadAllText($ResolvedPath)
    $Formatted = Invoke-Formatter -ScriptDefinition $Original -Settings $Settings

    if ($Mode -eq 'check') {
        if ($Original -cne $Formatted) {
            Write-Error "$Path is not formatted"
            $Failed = $true
        }
        continue
    }

    $Utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($ResolvedPath, $Formatted, $Utf8WithoutBom)
}

if ($Failed) {
    exit 1
}
