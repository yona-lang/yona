#!/usr/bin/env pwsh

param (
    [int]$repetitions
)

Measure-Command { 1..$repetitions | ForEach-Object { start-job -ScriptBlock { .\echo-client } -Name “YonaClient$_” } ; get-job -Name YonaClient* | Wait-Job | Receive-Job }
