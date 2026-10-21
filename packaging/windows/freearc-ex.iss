; FreeArc-ex Windows installer (Inno Setup 7.1.0).
;
; Pre-release build: only the console archiver is packaged for now; the GTK3 GUI
; is added when a Windows build of it exists (see docs/INSTALLER.md).
#define AppName "FreeArc-ex"
#define AppVersion "0.1.0-pre"
#define AppPublisher "FreeArc-ex contributors"
#define AppUrl "https://github.com/YadeWira/FreeArc-ex"

[Setup]
AppId={{8E1B3C52-6F0B-4C7E-9A2D-5B2F8C1D4A11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} (pre-release)
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}
DefaultDirName={autopf}\FreeArc-ex
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=stage\LICENSE
OutputDir=output
OutputBaseFilename=FreeArc-ex-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x86 x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\arc-ex.exe
InfoBeforeFile=stage\PRERELEASE.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add FreeArc-ex to the PATH"; GroupDescription: "Integration:"; Flags: unchecked

[Files]
Source: "stage\arc-ex.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\arc.groups"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "stage\PRERELEASE.txt"; DestDir: "{app}"; Flags: ignoreversion
; The GUI is optional: included only when the staging step produced it.
Source: "stage\freearc-ex.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\FreeArc-ex console"; Filename: "{cmd}"; Parameters: "/k ""{app}\arc-ex.exe"""; WorkingDir: "{app}"; Comment: "Open a console with arc-ex"
Name: "{group}\Uninstall FreeArc-ex"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath('{app}')

[Run]
Filename: "{cmd}"; Parameters: "/k ""{app}\arc-ex.exe"" --version"; \
    Description: "Open a console to check the installation"; Flags: postinstall nowait skipifsilent

[Code]
function NeedsAddPath(Param: string): Boolean;
var
  Path: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + Path + ';') = 0;
end;
