; SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
; SPDX-License-Identifier: MIT
; Compile only from the inspected portable payload using build-personal.ps1.
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef OutputBaseFilename
  #error OutputBaseFilename is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif

[Setup]
AppId={{4E74D2B3-406B-4D7F-AE7E-DFB3B82DD02A}
AppName=DeskBridge
AppVersion={#AppVersion}
AppVerName=DeskBridge {#AppVersion}
AppPublisher=DeskBridge contributors
AppPublisherURL=https://github.com/shuaige619/DeskBridge
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\DeskBridge
DefaultGroupName=DeskBridge
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile=..\..\src\apps\res\deskflow.ico
UninstallDisplayIcon={app}\DeskBridge.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
LicenseFile=..\..\LICENSE

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "settings\DeskBridge.conf"; Flags: ignoreversion recursesubdirs createallsubdirs
; The same desktop-mode configuration location is used by GUI and core.
; A reinstall preserves settings/TLS, and uninstall leaves personal settings.
Source: "{#PayloadDir}\settings\DeskBridge.conf"; DestDir: "{app}\settings"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{autoprograms}\DeskBridge"; Filename: "{app}\DeskBridge.exe"
Name: "{autodesktop}\DeskBridge"; Filename: "{app}\DeskBridge.exe"; Tasks: desktopicon
