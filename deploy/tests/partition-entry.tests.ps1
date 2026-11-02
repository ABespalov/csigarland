# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Holds the check before writing effects alone to what a partition table has to say.
# Contributors: None
#
# Example: pwsh -File deploy\tests\partition-entry.tests.ps1
#requires -Version 7.2

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot '..\DeviceDetection.psm1') -Force

function New-Entry {
    param([string]$Label, [byte]$Type, [byte]$SubType, [uint32]$Offset, [uint32]$Size)

    $entry = [byte[]]::new(32)
    $entry[0] = 0xAA
    $entry[1] = 0x50
    $entry[2] = $Type
    $entry[3] = $SubType
    [System.BitConverter]::GetBytes($Offset).CopyTo($entry, 4)
    [System.BitConverter]::GetBytes($Size).CopyTo($entry, 8)
    [System.Text.Encoding]::ASCII.GetBytes($Label).CopyTo($entry, 12)
    return , $entry
}

$wanted = @{ Label = 'pack_a'; Type = 1; SubType = 0x40; Offset = 0x3E0000; Size = 0x8000 }
$cases = @(
    @{ Name = 'as laid out'; Table = (New-Entry 'pack_a' 1 0x40 0x3E0000 0x8000); Expected = $true },
    @{ Name = 'right place, too small, as with a neighbour inside the write'; Table = (New-Entry 'pack_a' 1 0x40 0x3E0000 0x4000); Expected = $false },
    @{ Name = 'right place, another subtype'; Table = (New-Entry 'pack_a' 1 0x81 0x3E0000 0x8000); Expected = $false },
    @{ Name = 'right place, an application'; Table = (New-Entry 'pack_a' 0 0x40 0x3E0000 0x8000); Expected = $false },
    @{ Name = 'another place'; Table = (New-Entry 'pack_a' 1 0x40 0x3E8000 0x8000); Expected = $false },
    @{ Name = 'another label'; Table = (New-Entry 'pack_b' 1 0x40 0x3E0000 0x8000); Expected = $false },
    @{ Name = 'second in the table'; Table = [byte[]]((New-Entry 'nvs' 1 2 0x9000 0x6000) + (New-Entry 'pack_a' 1 0x40 0x3E0000 0x8000)); Expected = $true },
    @{ Name = 'no table'; Table = [byte[]]::new(64); Expected = $false }
)
$failed = 0
foreach ($case in $cases) {
    $result = Test-csiGarlandPartitionEntry -Table $case.Table @wanted
    if ($result -ne $case.Expected) {
        Write-Host "FAILED: $($case.Name)"
        $failed++
    }
}
if ($failed -gt 0) {
    throw "$failed partition entry cases went wrong."
}
Write-Host "Partition entries are judged as they should be in $($cases.Count) cases."
