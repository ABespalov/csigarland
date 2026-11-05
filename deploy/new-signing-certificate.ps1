# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Make the root certificate and the signing certificate for firmware updates.
# Contributors: None
#
# Parameters
#
#   -Country <XX>
#       Two-letter country for the certificates, none unless given.
#
#   -Organization <name>, -Person <name>, -Email <address>
#       Who the certificates name. They go into every update file, so they become public as
#       soon as one is published.
#
#   -RenewSigning
#       Keep the root and issue a new signing key and certificate. Firmware already on a
#       board accepts it without an update, as it trusts the root.
#
#   -Force
#       Replace existing certificates with a new root and signing certificate. The old
#       root key is gone after this, and a board whose firmware trusts the old root takes
#       the next firmware over USB only.
#
# A first run makes both. It asks for a passphrase that protects the root key, and nothing
# recovers either: without the root key and passphrase, boards can only be updated over USB.
# Everything goes to cert/ at the repository root. Keep the root key and the passphrase
# apart, off this machine. Builds need only:
#   cert/ota-root-ca.local.crt, cert/ota-signing.local.key, cert/ota-signing.local.crt
#
# Git ignores that folder but for its README. The signing certificate, with its name and email, is inside every
# update file, so it is public as soon as a file is published.
#
# Examples:
#   pwsh -File deploy\new-signing-certificate.ps1
#   pwsh -File deploy\new-signing-certificate.ps1 -RenewSigning
#   pwsh -File deploy\new-signing-certificate.ps1 -Force
#requires -Version 7.2

[CmdletBinding()]
param(
    [ValidatePattern('^([A-Z]{2})?$')][string]$Country = '',
    [string]$Organization = '',
    [string]$Person = '',
    [string]$Email = '',
    [switch]$RenewSigning,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Tool = Join-Path $PSScriptRoot '..\firmware\app\scripts\ota_sign_tool.py'
$CertFolder = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\cert'))
$RootKey = Join-Path $CertFolder 'ota-root-ca.local.key'
$SigningKey = Join-Path $CertFolder 'ota-signing.local.key'

Import-Module (Join-Path $PSScriptRoot 'SigningPython.psm1') -Force

function Invoke-Tool {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    & $Python $Tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ota_sign_tool.py failed with exit code $LASTEXITCODE."
    }
}

if ($RenewSigning -and $Force) {
    throw '-RenewSigning keeps the root and -Force replaces it. Choose one.'
}
if ($RenewSigning) {
    if (-not (Test-Path -LiteralPath $RootKey)) {
        throw "No root key at $RootKey. Bring it back from where you keep it."
    }
} elseif (-not $Force -and ((Test-Path -LiteralPath $RootKey) -or (Test-Path -LiteralPath $SigningKey))) {
    throw 'Certificates exist already, and a new root would lock out every board that trusts this one. -RenewSigning replaces the signing key only, -Force replaces everything.'
}

$Python = Get-csiGarlandSigningPython
Write-Host 'Nothing recovers the root key or its passphrase. Keep both, apart from each other.'
$secret = Read-Host -AsSecureString -Prompt 'Passphrase for the root key'
$plain = [System.Net.NetworkCredential]::new('', $secret).Password
if ($plain.Length -lt 12) {
    throw 'The passphrase needs at least 12 characters.'
}
if (-not $RenewSigning) {
    $again = [System.Net.NetworkCredential]::new('', (Read-Host -AsSecureString -Prompt 'Once more')).Password
    if ($plain -cne $again) {
        throw 'The passphrases differ.'
    }
}

$env:CSIGARLAND_ROOT_PASSPHRASE = $plain
if ($Organization) { $env:CSIGARLAND_CERT_ORGANIZATION = $Organization }
if ($Person) { $env:CSIGARLAND_CERT_PERSON = $Person }
if ($Email) { $env:CSIGARLAND_CERT_EMAIL = $Email }
try {
    if (-not $RenewSigning) {
        Invoke-Tool -Arguments @('new-root', $CertFolder, $Country)
    }
    Invoke-Tool -Arguments @('new-signing', $CertFolder, $Country)
} finally {
    Remove-Item Env:CSIGARLAND_ROOT_PASSPHRASE, Env:CSIGARLAND_CERT_ORGANIZATION, Env:CSIGARLAND_CERT_PERSON, Env:CSIGARLAND_CERT_EMAIL -ErrorAction SilentlyContinue
}

# Every key here for this user alone: the tool writes its own so, and a root key kept from before by
# -RenewSigning is narrowed as well. Windows keeps the folder's ACL, the user's own.
if (-not $IsWindows) {
    foreach ($key in Get-ChildItem -LiteralPath $CertFolder -Filter '*.key' -File) {
        & chmod 600 $key.FullName
    }
}
Write-Host "Done. Keep $RootKey and its passphrase apart, off this machine."
if (-not $RenewSigning) {
    Write-Host 'Firmware built now trusts this root; a board on older firmware takes it over USB once.'
}
