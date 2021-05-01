param (
    [int]$repetitions
)

Measure-Command { 1..$repetitions | ForEach-Object { start-job -ScriptBlock { .\echo-client.exe } -Name “JobTime$_” } ; get-job -Name JobTime* | Wait-Job | Receive-Job }
