# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$VerificationDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Verify these Windows artifacts on Windows.' }
$PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
if (Test-Path -LiteralPath $VerificationDirectory) { throw 'Use a new verification directory.' }
New-Item -ItemType Directory -Path $VerificationDirectory | Out-Null
$VerificationDirectory = (Resolve-Path -LiteralPath $VerificationDirectory).Path
$archives = @(Get-ChildItem -LiteralPath $PackageDirectory -Filter 'deskbridge-*-portable.zip')
$installers = @(Get-ChildItem -LiteralPath $PackageDirectory -Filter 'deskbridge-*-setup.exe')
if ($archives.Count -ne 1 -or $installers.Count -ne 1) { throw 'Expected one portable ZIP and one installer.' }
$archive = $archives[0]
$installer = $installers[0]
$expected = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $PackageDirectory 'SHA256SUMS-windows.txt')) {
    if ($line -notmatch '^([0-9a-f]{64})  ([^/\\]+)$' -or $expected.ContainsKey($Matches[2])) {
        throw 'Invalid Windows checksum manifest.'
    }
    $expected[$Matches[2]] = $Matches[1]
}
if ($expected.Count -ne 2) { throw 'Expected exactly two package hashes.' }
foreach ($file in @($archive, $installer)) {
    if (-not $expected.ContainsKey($file.Name) -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant() -cne $expected[$file.Name]) {
        throw "Checksum mismatch: $($file.Name)"
    }
}

function Invoke-SmokeProcess([string]$Executable, [string[]]$Arguments, [string]$Label) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -PassThru `
        -RedirectStandardOutput (Join-Path $VerificationDirectory "$Label.stdout.txt") `
        -RedirectStandardError (Join-Path $VerificationDirectory "$Label.stderr.txt")
    if (-not $process.WaitForExit(120000)) {
        $process.Kill($true)
        throw "Timed out: $Label"
    }
    if ($process.ExitCode -ne 0) { throw "$Label failed: $($process.ExitCode)" }
}

$environmentNames = @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH',
    'QML_IMPORT_PATH', 'QT_QPA_PLATFORM', 'QTDIR', 'Qt6_DIR', 'Qt6_ROOT', 'QT_ROOT_DIR',
    'QT_HOST_PATH', 'CMAKE_PREFIX_PATH', 'OPENSSL_CONF', 'OPENSSL_MODULES')
$environmentBackup = @{}
try {
    foreach ($name in $environmentNames) {
        $environmentBackup[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $portable = Join-Path $VerificationDirectory 'portable'
    Expand-Archive -LiteralPath $archive.FullName -DestinationPath $portable
    $guis = @(Get-ChildItem -LiteralPath $portable -Recurse -Filter 'DeskBridge.exe')
    if ($guis.Count -ne 1) { throw 'Expected one packaged GUI.' }
    $payload = $guis[0].DirectoryName
    foreach ($relative in @('deskflow-core.exe', 'settings/DeskBridge.conf', 'Qt6Core.dll', 'Qt6Gui.dll',
            'Qt6Widgets.dll', 'Qt6Network.dll', 'Qt6Svg.dll', 'plugins/platforms/qwindows.dll',
            'vcruntime140.dll', 'msvcp140.dll', 'ThirdPartyLicenses/THIRD-PARTY-NOTICES.md',
            'ThirdPartyLicenses/Qt-6.11.2/qtbase/LICENSES/LGPL-3.0-only.txt',
            'ThirdPartyLicenses/Microsoft-Visual-C-Runtime-2015-2022/LICENSE.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $payload $relative))) { throw "Missing payload file: $relative" }
    }
    $env:PATH = "$payload;$env:SystemRoot/System32;$env:SystemRoot"
    Invoke-SmokeProcess $guis[0].FullName @('--version') 'portable-gui'
    Invoke-SmokeProcess (Join-Path $payload 'deskflow-core.exe') @('--version') 'portable-core'

    $installedDirectory = Join-Path $VerificationDirectory 'installed'
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
        "/DIR=`"$installedDirectory`"", "/LOG=`"$VerificationDirectory/installer.log`"")
    Invoke-SmokeProcess $installer.FullName $arguments 'install'
    $installedGui = Join-Path $installedDirectory 'DeskBridge.exe'
    $installedSettings = Join-Path $installedDirectory 'settings/DeskBridge.conf'
    if (-not (Test-Path -LiteralPath $installedGui) -or -not (Test-Path -LiteralPath $installedSettings)) {
        throw 'Installer payload missing.'
    }
    $env:PATH = "$installedDirectory;$env:SystemRoot/System32;$env:SystemRoot"
    Invoke-SmokeProcess $installedGui @('--version') 'installed-gui'
    Invoke-SmokeProcess (Join-Path $installedDirectory 'deskflow-core.exe') @('--version') 'installed-core'
    $sentinel = "[verification]`npreserve=" + [Guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText($installedSettings, $sentinel)
    Invoke-SmokeProcess $installer.FullName $arguments 'reinstall'
    if ([IO.File]::ReadAllText($installedSettings) -cne $sentinel) { throw 'Reinstall overwrote settings.' }
    Invoke-SmokeProcess (Join-Path $installedDirectory 'unins000.exe') @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') 'uninstall'
    for ($attempt = 0; $attempt -lt 40 -and (Test-Path -LiteralPath $installedGui); $attempt++) {
        Start-Sleep -Milliseconds 250
    }
    if ((Test-Path -LiteralPath $installedGui) -or [IO.File]::ReadAllText($installedSettings) -cne $sentinel) {
        throw 'Uninstall did not preserve settings while removing the app.'
    }
    Write-Output 'Downloaded package hashes and dependency files verified.'
    Write-Output 'Portable/installed GUI and core exited successfully with Qt/QML/OpenSSL developer variables cleared and only payload/Windows directories in PATH.'
    Write-Output 'Install/reinstall/uninstall succeeded; existing settings survived. Interactive input sharing remains a separate hardware check.'
} finally {
    foreach ($name in $environmentBackup.Keys) {
        [Environment]::SetEnvironmentVariable($name, $environmentBackup[$name], 'Process')
    }
}
