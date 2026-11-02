# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Discover and select the exact enrolled ESP32-C6 USB Serial/JTAG device.
# Contributors: None
#requires -Version 7.2

Set-StrictMode -Version Latest

function Get-csiGarlandUsbIdentity {
    param([Parameter(Mandatory = $true)][string]$ParentInstanceId)

    $match = [regex]::Match(
        $ParentInstanceId,
        '^USB\\VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})\\(.+)$'
    )
    if (-not $match.Success) {
        return $null
    }
    [pscustomobject]@{
        Vid       = $match.Groups[1].Value.ToUpperInvariant()
        Pid       = $match.Groups[2].Value.ToUpperInvariant()
        UsbSerial = $match.Groups[3].Value.ToUpperInvariant()
    }
}

# The USB serial of this chip is its MAC address, so only a hash is ever stored.
function Get-csiGarlandIdentityHash {
    param([Parameter(Mandatory = $true)][string]$UsbSerial)

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($UsbSerial.Trim().ToUpperInvariant())
    return [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes))
}

function Get-csiGarlandSerialCandidates {
    $entities = @(Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%(COM%'" `
            -ErrorAction SilentlyContinue)
    foreach ($entity in $entities) {
        $named = [regex]::Match([string]$entity.Name, '\((COM\d+)\)')
        if (-not $named.Success -or $entity.PNPDeviceID -notlike 'USB\*') {
            continue
        }
        try {
            $parent = (Get-PnpDeviceProperty `
                    -InstanceId $entity.PNPDeviceID `
                    -KeyName 'DEVPKEY_Device_Parent' `
                    -WhatIf:$false `
                    -ErrorAction Stop).Data
        } catch {
            continue
        }
        $identity = Get-csiGarlandUsbIdentity -ParentInstanceId $parent
        if ($null -eq $identity) {
            continue
        }
        [pscustomobject]@{
            Port         = $named.Groups[1].Value.ToUpperInvariant()
            Vid          = $identity.Vid
            Pid          = $identity.Pid
            IdentityHash = Get-csiGarlandIdentityHash -UsbSerial $identity.UsbSerial
        }
    }
}

function Select-csiGarlandFlashTarget {
    param(
        # Empty is an answer: no port at all is what an unplugged board looks like.
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Candidates,
        [Parameter(Mandatory = $true)][psobject]$DeviceProfile,
        [string]$RequestedPort = ''
    )

    $requested = $RequestedPort.Trim().ToUpperInvariant()
    $found = @($Candidates | Where-Object {
            $_.Vid -ieq $DeviceProfile.vid -and
            $_.Pid -ieq $DeviceProfile.pid -and
            $_.IdentityHash -ieq $DeviceProfile.identityHash -and
            (-not $requested -or $_.Port -ieq $requested)
        })
    if ($found.Count -eq 0) {
        $portHint = if ($requested) { " on $requested" } else { '' }
        # The C6 keeps this port while its application runs, so a missing port
        # means an unplugged board, or an image that stopped USB by crashing.
        throw ("The enrolled csiGarland device was not found$portHint. No port was touched. " +
            'Check the USB cable. If the board is plugged in, hold BOOT, tap RESET, release BOOT and run again.')
    }
    if ($found.Count -ne 1) {
        throw "The enrolled identity matched $($found.Count) ports. Refusing an ambiguous flash."
    }
    return $found[0]
}

# Whether a partition table holds this entry exactly: right offset alone says nothing about how
# far a write there reaches. An entry is magic AA 50, type, subtype, offset, size, label of 16.
function Test-csiGarlandPartitionEntry {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Table,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][byte]$Type,
        [Parameter(Mandatory = $true)][byte]$SubType,
        [Parameter(Mandatory = $true)][uint32]$Offset,
        [Parameter(Mandatory = $true)][uint32]$Size
    )

    for ($at = 0; $at + 32 -le $Table.Length -and $Table[$at] -eq 0xAA -and $Table[$at + 1] -eq 0x50; $at += 32) {
        if ([System.Text.Encoding]::ASCII.GetString($Table, $at + 12, 16).TrimEnd([char]0) -ceq $Label) {
            return $Table[$at + 2] -eq $Type -and $Table[$at + 3] -eq $SubType -and
                [System.BitConverter]::ToUInt32($Table, $at + 4) -eq $Offset -and
                [System.BitConverter]::ToUInt32($Table, $at + 8) -eq $Size
        }
    }
    return $false
}

Export-ModuleMember -Function `
    Test-csiGarlandPartitionEntry, `
    Get-csiGarlandUsbIdentity, `
    Get-csiGarlandIdentityHash, `
    Get-csiGarlandSerialCandidates, `
    Select-csiGarlandFlashTarget
