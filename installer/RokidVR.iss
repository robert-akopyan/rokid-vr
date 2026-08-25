#ifndef MyAppVersion
  #define MyAppVersion "0.1.5"
#endif

#define MyAppName "RokidVR"
#define MyPublisher "RokidVR contributors"
#define MyURL "https://github.com/robert-akopyan/rokid-vr"

[Setup]
AppId={{A331E20B-783D-4E1D-AF18-12C86964C597}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyPublisher}
AppPublisherURL={#MyURL}
AppSupportURL={#MyURL}/issues
AppUpdatesURL={#MyURL}/releases
DefaultDirName={autopf}\RokidVR
DefaultGroupName=RokidVR
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\build\installer
OutputBaseFilename=RokidVR-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
UninstallDisplayName=RokidVR
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\package\RokidVRLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\package\rokidmax\*"; DestDir: "{app}\rokidmax"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\scripts\install-driver.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\scripts\uninstall-driver.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\config.example.ini"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\RokidVR"; Filename: "{app}\RokidVRLauncher.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\RokidVR"; Filename: "{app}\RokidVRLauncher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-driver.ps1"""; StatusMsg: "Registering the RokidVR SteamVR driver..."; Flags: runhidden waituntilterminated
Filename: "{app}\RokidVRLauncher.exe"; Description: "{cm:LaunchProgram,RokidVR}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\uninstall-driver.ps1"""; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterSteamVRDriver"

[Code]
function SteamVRInstalled: Boolean;
var
  SteamPath: String;
begin
  Result := RegQueryStringValue(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820', 'InstallLocation', SteamPath)
    and FileExists(AddBackslash(SteamPath) + 'bin\win64\vrpathreg.exe');
  if not Result then
    Result := RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', SteamPath)
    and FileExists(AddBackslash(SteamPath) + 'steamapps\common\SteamVR\bin\win64\vrpathreg.exe');
  if not Result then
    Result := FileExists(ExpandConstant('{pf32}\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not SteamVRInstalled then
    Result := 'SteamVR was not found. Install SteamVR through Steam, then run this installer again.';
end;
