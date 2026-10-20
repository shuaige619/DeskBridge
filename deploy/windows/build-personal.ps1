# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$InnoSetupCompiler,
    [switch]$PortableOnly,
    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Run this script in an x64 MSVC developer PowerShell on Windows.' }
$repoDir = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoDir 'build/personal-windows-x64' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoDir 'dist/windows' }
if ([string]::IsNullOrWhiteSpace($QtRoot) -or -not (Test-Path "$QtRoot/bin/windeployqt.exe")) {
    throw 'Pass -QtRoot pointing to a Windows x64 MSVC Qt installation (with LinguistTools, Svg, and translations).'
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -or -not (Test-Path "$VcpkgRoot/scripts/buildsystems/vcpkg.cmake")) {
    throw 'Pass -VcpkgRoot pointing to an existing bootstrapped vcpkg checkout.'
}
foreach ($tool in @('cl.exe', 'cmake', 'ninja', 'cpack', 'python')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing tool: $tool. Use an x64 MSVC developer shell." }
}
if ($env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'This build script targets Windows x64. Select the x64 MSVC developer environment.'
}

if (-not $PortableOnly) {
    if (-not $InnoSetupCompiler) {
        $innoCommand = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
        if ($innoCommand) { $InnoSetupCompiler = $innoCommand.Source }
        else { $InnoSetupCompiler = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe" }
    }
    if (-not (Test-Path -LiteralPath $InnoSetupCompiler)) {
        throw 'Install Inno Setup 6 and pass -InnoSetupCompiler, or use -PortableOnly.'
    }
    $InnoSetupCompiler = (Resolve-Path -LiteralPath $InnoSetupCompiler).Path
}

function Invoke-Checked([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed with exit code $LASTEXITCODE" }
}

function Invoke-IsolatedTests([string]$Directory) {
    $testRegex = '^(ShortcutRulesTests|ShortcutMapperTests|ShiftTapGestureTests|LoginStartupTests|ShortcutApplyControllerTests|KeyMapTests|KeyStateTests|IKeyStateTests|SettingsTests|ComputerShortcutTests|ServerProxyTests|CoreProcessStopTests|PersonalWindowTests|ShortcutPageTests|ChordRecorderTests|PersonalDialogTests|AdvancedPageTests|PreferenceChangesTests|PreferencesApplyControllerTests|ScreenLayoutPersistenceTests|DeviceIllustrationTests|ScreenLayoutTests|ServerConfigTests|ServerTests|ClientProxyTests|InputTimingMetricsTests|IpcServerAccessTests)$'
    $environmentBackup = @{}
    foreach ($name in @('QT_QPA_PLATFORM', 'XDG_CONFIG_HOME', 'XDG_STATE_HOME')) {
        $environmentBackup[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $profile = Join-Path $Directory ('test-profile-' + [Guid]::NewGuid().ToString('N'))
    $markers = @()
    $createdDirectories = @()
    try {
        New-Item -ItemType Directory -Force "$profile/config", "$profile/state" | Out-Null
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:XDG_CONFIG_HOME = "$profile/config"
        $env:XDG_STATE_HOME = "$profile/state"
        $manifestText = & ctest --test-dir "$Directory/src/unittests" --show-only=json-v1 -R $testRegex
        if ($LASTEXITCODE -ne 0) { throw 'Could not inspect selected test commands.' }
        $manifest = ($manifestText -join "`n") | ConvertFrom-Json
        $tests = @($manifest.tests)
        if ($tests.Count -eq 0) { throw 'No safe regression tests were found.' }
        $binaryDirectories = @($tests | ForEach-Object {
            $binary = [IO.Path]::GetFullPath([string]$_.command[0])
            if (-not $binary.StartsWith($Directory.TrimEnd([char[]]@([char]92, [char]47)) + [IO.Path]::DirectorySeparatorChar,
                                       [StringComparison]::OrdinalIgnoreCase) -or -not $binary.EndsWith('.exe')) {
                throw "Unexpected test binary outside the build directory: $binary"
            }
            [IO.Path]::GetDirectoryName($binary)
        } | Select-Object -Unique)
        # Windows Settings intentionally uses portable/user paths instead of
        # XDG_CONFIG_HOME. A disposable portable marker also isolates the first
        # singleton initialization, before tests select their own temp profiles.
        foreach ($binaryDirectory in $binaryDirectories) {
            $settingsDirectory = Join-Path $binaryDirectory 'settings'
            $marker = Join-Path $settingsDirectory 'DeskBridge.conf'
            if (Test-Path -LiteralPath $marker) { throw "Test portable marker already exists; use a fresh build directory: $marker" }
            if (-not (Test-Path -LiteralPath $settingsDirectory)) {
                New-Item -ItemType Directory $settingsDirectory | Out-Null
                $createdDirectories += $settingsDirectory
            }
            [IO.File]::WriteAllText($marker, '')
            $markers += $marker
        }
        Invoke-Checked 'ctest' @('--test-dir', "$Directory/src/unittests", '--output-on-failure',
            '--no-tests=error', '-R', $testRegex)
    } finally {
        foreach ($name in $environmentBackup.Keys) {
            [Environment]::SetEnvironmentVariable($name, $environmentBackup[$name], 'Process')
        }
        foreach ($marker in $markers) {
            if (Test-Path -LiteralPath $marker) { Remove-Item -LiteralPath $marker -Force }
        }
        foreach ($createdDirectory in $createdDirectories) {
            if ((Test-Path -LiteralPath $createdDirectory) -and
                @(Get-ChildItem -LiteralPath $createdDirectory -Force).Count -eq 0) {
                Remove-Item -LiteralPath $createdDirectory
            }
        }
    }
}

$originalPath = $env:PATH
try {
    $QtRoot = (Resolve-Path $QtRoot).Path
    $VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
    $env:PATH = "$QtRoot/bin;$env:PATH"
    # lupdate also loads Qt6Qml: detect an incomplete SDK before vcpkg builds.
    Invoke-Checked (Join-Path $QtRoot 'bin/lupdate.exe') @('-version')
    New-Item -ItemType Directory -Force $BuildDirectory, $OutputDirectory | Out-Null
    $BuildDirectory = (Resolve-Path $BuildDirectory).Path
    $OutputDirectory = (Resolve-Path $OutputDirectory).Path
    $noticesDirectory = Join-Path $BuildDirectory 'ThirdPartyLicenses'
    if (Get-ChildItem $OutputDirectory -File | Where-Object { $_.Name -match '^deskbridge-.*(?:-portable\.zip|-setup\.exe)$' }) {
        throw 'Output directory already contains a DeskBridge package; choose a new -OutputDirectory.'
    }
    Invoke-Checked 'cmake' @(
        '-S', $repoDir, '-B', $BuildDirectory, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DDESKBRIDGE_PERSONAL_BUILD=ON',
        '-DBUILD_INSTALLER=ON', '-DBUILD_TESTS=ON', '-DSKIP_BUILD_TESTS=ON', '-DVCPKG_QT=OFF',
        "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake",
        '-DVCPKG_TARGET_TRIPLET=x64-windows-release',
        "-DQt6_DIR=$QtRoot/lib/cmake/Qt6", "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DDESKBRIDGE_THIRD_PARTY_LICENSE_DIR:PATH=$($noticesDirectory.Replace([char]92, [char]47))",
        "-DDEPLOYQT:FILEPATH=$QtRoot/bin/windeployqt.exe"
    )
    Invoke-Checked 'cmake' @('--build', $BuildDirectory, '--config', 'Release', '--parallel', '8', '--', '-k', '0')
    if (-not $SkipTests) {
        # Mock/state/widget tests only; never native input/clipboard suites.
        Invoke-IsolatedTests $BuildDirectory
    }
    Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'prepare-notices.py'),
        '--directory', $noticesDirectory,
        '--openssl-prefix', (Join-Path $BuildDirectory 'vcpkg_installed/x64-windows-release'))
    # CPack stages the independent desktop payload; Inno wraps that verified ZIP below.
    Invoke-Checked 'cpack' @('--config', "$BuildDirectory/CPackConfig.cmake", '-G', 'ZIP', '-B', $OutputDirectory)
    $archives = @(Get-ChildItem $OutputDirectory -Filter 'deskbridge-*-portable.zip')
    if ($archives.Count -ne 1) { throw 'Expected exactly one DeskBridge portable ZIP.' }
    $archive = $archives[0]
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive.FullName)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace([char]92, [char]47) })
        foreach ($required in @('/DeskBridge.exe', '/deskflow-core.exe', '/settings/DeskBridge.conf',
                                 '/Qt6Core.dll', '/Qt6Svg.dll', '/plugins/platforms/qwindows.dll',
                                 '/vcruntime140.dll', '/msvcp140.dll',
                                 '/ThirdPartyLicenses/THIRD-PARTY-NOTICES.md',
                                 '/ThirdPartyLicenses/Qt-6.11.2/qtbase/LICENSES/LGPL-3.0-only.txt',
                                 '/ThirdPartyLicenses/Microsoft-Visual-C-Runtime-2015-2022/LICENSE.txt')) {
            if (-not ($entries | Where-Object { "/$_".EndsWith($required, [StringComparison]::OrdinalIgnoreCase) })) {
                throw "Portable archive is missing $required"
            }
        }
        if (-not ($entries -match '(^|/)libssl[^/]*\.dll$') -or -not ($entries -match '(^|/)libcrypto[^/]*\.dll$')) {
            throw 'Portable archive is missing OpenSSL runtime DLLs.'
        }
        if (-not ($entries -match '(^|/)ThirdPartyLicenses/OpenSSL-[0-9.]+/LICENSE\.txt$')) {
            throw 'Portable archive is missing the installed OpenSSL license.'
        }
        if ($entries -match '(^|/)deskflow-daemon\.exe$|\.msi$') { throw 'Portable archive unexpectedly includes a service/installer.' }
    } finally { $zip.Dispose() }

    $smokeDirectory = Join-Path $BuildDirectory ('portable-smoke-' + [Guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $archive.FullName -DestinationPath $smokeDirectory
    $gui = @(Get-ChildItem $smokeDirectory -Recurse -Filter 'DeskBridge.exe')[0]
    $exeDir = $gui.DirectoryName
    $installer = $null
    if (-not $PortableOnly) {
        $packageConfig = Get-Content -LiteralPath "$BuildDirectory/CPackConfig.cmake" -Raw
        $versionMatch = [regex]::Match($packageConfig, '(?m)^set\(CPACK_PACKAGE_VERSION "([0-9]+(?:\.[0-9]+){1,3})"\)')
        if (-not $versionMatch.Success) { throw 'Could not read the numeric CPack version for the Windows installer.' }
        $version = $versionMatch.Groups[1].Value
        $installerBase = $archive.BaseName -replace '-portable$', '-setup'
        Invoke-Checked $InnoSetupCompiler @(
            "/DPayloadDir=$exeDir", "/DOutputDir=$OutputDirectory", "/DOutputBaseFilename=$installerBase",
            "/DAppVersion=$version", (Join-Path $PSScriptRoot 'personal-setup.iss')
        )
        $installer = Get-Item -LiteralPath (Join-Path $OutputDirectory "$installerBase.exe")
    }
    # Hide developer Qt/vcpkg paths. These --version calls exit before input
    # hooks, sharing connections, or the main window are started.
    $env:PATH = "$exeDir;$env:SystemRoot/System32;$env:SystemRoot"
    $guiProcess = Start-Process -FilePath $gui.FullName -ArgumentList '--version' -Wait -PassThru
    if ($guiProcess.ExitCode -ne 0) { throw 'Packaged GUI --version smoke test failed.' }
    Invoke-Checked (Join-Path $exeDir 'deskflow-core.exe') @('--version')
    if ($installer) {
        $installedDirectory = Join-Path $BuildDirectory ('installer-smoke-' + [Guid]::NewGuid().ToString('N'))
        $installArguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
            "/DIR=`"$installedDirectory`"", "/LOG=`"$BuildDirectory/installer-smoke.log`"")
        $install = Start-Process -FilePath $installer.FullName -ArgumentList $installArguments -Wait -PassThru
        if ($install.ExitCode -ne 0) { throw "Installer smoke failed: $($install.ExitCode)" }
        $installedGui = Join-Path $installedDirectory 'DeskBridge.exe'
        $installedSettings = Join-Path $installedDirectory 'settings/DeskBridge.conf'
        if (-not (Test-Path -LiteralPath $installedGui) -or -not (Test-Path -LiteralPath $installedSettings)) {
            throw 'Installer did not deploy GUI/settings into the selected user directory.'
        }
        $env:PATH = "$installedDirectory;$env:SystemRoot/System32;$env:SystemRoot"
        $installed = Start-Process -FilePath $installedGui -ArgumentList '--version' -Wait -PassThru
        if ($installed.ExitCode -ne 0) { throw 'Installed GUI --version smoke failed.' }
        Invoke-Checked (Join-Path $installedDirectory 'deskflow-core.exe') @('--version')
        $sentinel = "[installer-smoke]`npreserve=" + [Guid]::NewGuid().ToString('N')
        [IO.File]::WriteAllText($installedSettings, $sentinel)
        $upgrade = Start-Process -FilePath $installer.FullName -ArgumentList $installArguments -Wait -PassThru
        if ($upgrade.ExitCode -ne 0 -or [IO.File]::ReadAllText($installedSettings) -cne $sentinel) {
            throw 'Reinstall failed to preserve existing user settings.'
        }
        $uninstall = Start-Process -FilePath (Join-Path $installedDirectory 'unins000.exe') -ArgumentList @(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'
        ) -Wait -PassThru
        if ($uninstall.ExitCode -ne 0) { throw 'Silent uninstall smoke failed.' }
        for ($attempt = 0; $attempt -lt 40 -and (Test-Path -LiteralPath $installedGui); $attempt++) {
            Start-Sleep -Milliseconds 250
        }
        if ((Test-Path -LiteralPath $installedGui) -or [IO.File]::ReadAllText($installedSettings) -cne $sentinel) {
            throw 'Uninstall did not remove the application while preserving user settings.'
        }
        Write-Output "Installer artifact: $($installer.FullName)"
    }
    $packages = @($archive)
    if ($installer) { $packages += $installer }
    $checksumLines = @($packages | ForEach-Object {
        "$((Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLowerInvariant())  $($_.Name)"
    })
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS-windows.txt'), $checksumLines)
    Write-Output "Portable artifact: $($archive.FullName)"
    Write-Output "SHA256: $((Get-FileHash -Algorithm SHA256 $archive.FullName).Hash)"
    Write-Output 'Package/dependency and version smoke checks passed. Installer mode also verified install/reinstall/uninstall. Interactive Windows 11 acceptance remains separate.'
} finally {
    $env:PATH = $originalPath
}
