# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$OutputDirectory = 'build/windows-identity-probe'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = [System.IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
New-Item -ItemType Directory -Force $output | Out-Null
$QtRoot = (Resolve-Path $QtRoot).Path
$env:PATH = "$QtRoot/bin;$env:PATH"
$arguments = @(
    '/nologo', '/std:c++20', '/EHsc', '/MD', '/O2', '/utf-8', '/Zc:__cplusplus',
    '/DQT_NO_DEBUG', '/DQT_CORE_LIB',
    "/I$PSScriptRoot/stubs", "/I$repo/src/lib", "/I$QtRoot/include", "/I$QtRoot/include/QtCore",
    "$PSScriptRoot/main.cpp", "$repo/src/lib/gui/personal/ConnectionIdentity.cpp",
    '/Fe:windows-identity-probe.exe', '/link', "$QtRoot/lib/Qt6Core.lib"
)
Push-Location $output
try {
    & cl @arguments 2>&1 | Tee-Object -FilePath 'compile.log'
    if ($LASTEXITCODE -ne 0) { throw "Native probe compilation failed: $LASTEXITCODE" }
    & './windows-identity-probe.exe' 'result.json' '6.11.2' 2>&1 | Tee-Object -FilePath 'probe.log'
    if ($LASTEXITCODE -ne 0) { throw "Native QSettings identity probe failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
