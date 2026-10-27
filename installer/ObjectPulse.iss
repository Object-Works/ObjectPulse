; Inno Setup script for ObjectPulse.
; Built by .github/workflows/build.yml:
;   iscc /DAppVersion=0.1.0 /DBuildDir=..\build\Release installer\ObjectPulse.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\Release"
#endif

#define AppName "ObjectPulse"
#define AppExe "ObjectPulse.exe"
#define AppPublisher "Object Works"
#define AppUrl "https://github.com/Object-Works/ObjectPulse"

[Setup]
AppId={{CCFE10A7-85D8-4AE9-9D57-2E7E561D6F33}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install by default; the user can still choose an all-users install.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile=..\src\pulse.ico
UninstallDisplayIcon={app}\{#AppExe}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
japanese.AutostartTask=Windows へのサインイン時に ObjectPulse を起動する
english.AutostartTask=Start ObjectPulse when I sign in to Windows
japanese.LaunchApp=ObjectPulse を起動する
english.LaunchApp=Launch ObjectPulse
japanese.RestoreTaskbarShortcut=Windows のタスクバーを元に戻す
english.RestoreTaskbarShortcut=Restore the Windows taskbar

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "{cm:AutostartTask}"

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autoprograms}\{cm:RestoreTaskbarShortcut}"; Filename: "{app}\{#AppExe}"; Parameters: "restore-taskbar"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ObjectPulse"; \
  ValueData: """{app}\{#AppExe}"""; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Quits the running notch and brings the Windows taskbar back, even after a crash.
Filename: "{app}\{#AppExe}"; Parameters: "restore-taskbar"; Flags: runhidden waituntilterminated; RunOnceId: "RestoreTaskbar"

[Code]
// Quit a running instance first so the taskbar is restored and the exe can be replaced.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Exe: String;
  Code: Integer;
begin
  Result := '';
  Exe := ExpandConstant('{app}\{#AppExe}');
  if FileExists(Exe) then
  begin
    Exec(Exe, 'quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
    Sleep(1000);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'ObjectPulse');
end;
