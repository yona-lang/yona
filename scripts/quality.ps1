$PythonCommand = Get-Command python3 -ErrorAction SilentlyContinue
if ($null -eq $PythonCommand) {
    $PythonCommand = Get-Command python -ErrorAction Stop
}

$QualityScript = Join-Path $PSScriptRoot 'quality.py'
& $PythonCommand.Source $QualityScript @args
exit $LASTEXITCODE
