#define MyAppName "Kaleido Launcher"
#define MyAppVersion "1.0.9"

[Setup]
; Same identity as the already-shipped 1.0.0 (which omitted AppId, so Inno
; keyed the uninstall entry on AppName). Keep this string stable — changing
; it would create a second Apps & Features row instead of upgrading.
AppId={#MyAppName}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName}
AppPublisher=KaleidoVR
AppPublisherURL=https://kalivr.com
AppCopyright=Copyright (C) 2026 KaleidoVR
; Apps & Features should show this name and let Windows list AppVersion beside it.
UninstallDisplayName={#MyAppName}
VersionInfoProductName={#MyAppName}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={commonpf64}\obs-studio
DefaultGroupName={#MyAppName}
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
; A real .ico is what Settings → Apps uses. {uninstallexe} and the DLL often show a blank tile.
UninstallDisplayIcon={app}\obs-plugins\64bit\kaleido-launcher.ico
OutputDir=output
OutputBaseFilename=KaleidoLauncher_v{#MyAppVersion}_Installer
Compression=lzma
SolidCompression=yes

[Files]
; The plugin ships no locale resources. The icon is only for the Apps & Features tile.
Source: "build_x64\Release\kaleido-launcher.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "logo\kaleido-launcher.ico"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
; Debug symbols, so crash reports from users are actionable
Source: "build_x64\Release\kaleido-launcher.pdb"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
; Optional shortcut or uninstaller reference
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Code]
function SplitUninstallCommand(const UninstallString: string; var Path, Args: string): Boolean;
var
  S: string;
  P: Integer;
begin
  Result := False;
  Path := '';
  Args := '';
  S := Trim(UninstallString);
  if S = '' then
    Exit;

  if S[1] = '"' then
  begin
    Delete(S, 1, 1);
    P := Pos('"', S);
    if P = 0 then
      Exit;
    Path := Copy(S, 1, P - 1);
    Args := Trim(Copy(S, P + 1, MaxInt));
  end
  else
  begin
    P := Pos(' ', S);
    if P = 0 then
      Path := S
    else
    begin
      Path := Copy(S, 1, P - 1);
      Args := Trim(Copy(S, P + 1, MaxInt));
    end;
  end;

  Result := Path <> '';
end;

procedure SilentUninstall(const UninstallString: string);
var
  Path, Args: string;
  ResultCode: Integer;
begin
  if not SplitUninstallCommand(UninstallString, Path, Args) then
    Exit;
  if Pos('/S', Args) = 0 then
  begin
    if Args = '' then
      Args := '/S'
    else
      Args := Args + ' /S';
  end;
  if FileExists(Path) then
    Exec(Path, Args, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function IsCPackNsisDuplicate(const SubKeyName, DisplayName: string): Boolean;
var
  LowerName, LowerDisplay: string;
begin
  Result := False;
  { This installer's own uninstall key. Never run it while we are installing. }
  if CompareText(SubKeyName, '{#MyAppName}_is1') = 0 then
    Exit;

  LowerDisplay := LowerCase(DisplayName);
  LowerName := LowerCase(SubKeyName);

  if LowerDisplay = 'kaleido launcher for obs' then
  begin
    Result := True;
    Exit;
  end;

  { CPack NSIS registers under the cmake package name. }
  if Pos('kaleido-launcher', LowerName) = 1 then
    Result := True;
end;

procedure RemoveDuplicateLauncherEntries(RootKey: Integer);
var
  Names: TArrayOfString;
  I: Integer;
  Key, DisplayName, UninstallString: string;
begin
  if not RegGetSubkeyNames(RootKey, 'Software\Microsoft\Windows\CurrentVersion\Uninstall', Names) then
    Exit;

  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + Names[I];
    DisplayName := '';
    RegQueryStringValue(RootKey, Key, 'DisplayName', DisplayName);
    if not IsCPackNsisDuplicate(Names[I], DisplayName) then
      Continue;
    if RegQueryStringValue(RootKey, Key, 'UninstallString', UninstallString) then
      SilentUninstall(UninstallString);
    if RegKeyExists(RootKey, Key) then
      RegDeleteKeyIncludingSubkeys(RootKey, Key);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    RemoveDuplicateLauncherEntries(HKLM64);
    RemoveDuplicateLauncherEntries(HKLM32);
  end;
end;
