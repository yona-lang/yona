#!/usr/bin/env pwsh

param (
    [Int16] $Port = 6666,
    [Int16] $Repetitions = 5
)

Start-Job -ScriptBlock {sh ../../yona -f ./server.yona $args[0] $args[1]} -Name YonaServer -ArgumentList $Port,$Repetitions

Start-Sleep -Seconds 1

1..$Repetitions | ForEach-Object { Start-Job -ScriptBlock {sh ../../yona -f ./client.yona $args[0] } -Name “YonaClient$_” -ArgumentList $Port }

Get-Job -Name YonaClient* | Wait-Job | Receive-Job
Get-Job -Name YonaServer | Wait-Job | Receive-Job

$ServerState = $(Get-Job YonaServer).State
$ClientState = $(Get-Job -Name YonaClient*).State | Select-Object -Unique

if ($ServerState -ne 'Completed') {
    throw "Server has not completed successfuly: $ServerState"
}

if ($ClientState -ne 'Completed') {
    throw "Clients has not completed successfuly: $ClientState"
}
