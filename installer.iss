[Setup]
; Same identity as the already-shipped 1.0.0 (which omitted AppId, so Inno
; keyed the uninstall entry on AppName). Keep this string stable — changing
; it would create a second Apps & Features row instead of upgrading.
AppId=Kaleido Launcher
AppName=Kaleido Launcher
AppVersion=1.0.0
AppPublisher=KaleidoVR
AppPublisherURL=https://kalivr.com
AppCopyright=Copyright (C) 2026 KaleidoVR
DefaultDirName={commonpf64}\obs-studio
DefaultGroupName=Kaleido Launcher
PrivilegesRequired=admin
; Without this the installer runs in 32-bit mode and {commonpf64} would be unavailable,
; landing the plugin in Program Files (x86) where 64-bit OBS never looks for it.
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UsePreviousAppDir=yes
; OBS is already installed at DefaultDirName. The default "Folder Exists"
; prompt is noise — installing into that folder is the entire point.
DirExistsWarning=no
CloseApplications=yes
SetupIconFile=logo\kaleido-launcher.ico
UninstallDisplayIcon={uninstallexe}
OutputDir=output
OutputBaseFilename=KaleidoLauncher_v1.0.0_Installer
Compression=lzma
SolidCompression=yes

[Files]
; The plugin ships no resources, so only obs-plugins\64bit is populated.
Source: "build_x64\Release\kaleido-launcher.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
; Debug symbols, so crash reports from users are actionable
Source: "build_x64\Release\kaleido-launcher.pdb"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
; Optional shortcut or uninstaller reference
Name: "{group}\Uninstall Kaleido Launcher"; Filename: "{uninstallexe}"
