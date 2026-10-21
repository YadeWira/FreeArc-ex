; FreeArc-ex Windows installer (Inno Setup 7.1.0).
;
; Pre-release build: the console archiver is packaged; the GTK3 GUI is added
; automatically when a Windows build of it is present in stage\ (see
; docs/INSTALLER.md). The wizard keeps the classic Inno Setup look the original
; FreeArc setup used, with the FreeArc banner and icon.
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
LicenseFile=stage\LICENSE
InfoBeforeFile=stage\PRERELEASE.txt
OutputDir=output
OutputBaseFilename=FreeArc-ex-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
; Classic wizard, like the original FreeArc 0.67 setup.
WizardStyle=classic
DisableWelcomePage=no
WizardImageFile=branding\banner.bmp
WizardSmallImageFile=branding\small.bmp
SetupIconFile=branding\freearc.ico
UninstallDisplayIcon={app}\arc-ex.exe
ArchitecturesAllowed=x86 x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "console"; Description: "FreeArc-ex console (arc-ex.exe)"; Types: full compact custom; Flags: fixed
Name: "docs"; Description: "Documentation and license"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: "addtopath"; Description: "Add FreeArc-ex to the PATH"; GroupDescription: "Integration:"; Flags: unchecked

[Files]
Source: "stage\arc-ex.exe"; DestDir: "{app}"; Components: console; Flags: ignoreversion
Source: "stage\arc.groups"; DestDir: "{app}"; Components: console; Flags: ignoreversion
Source: "stage\README.md"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "stage\LICENSE"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
Source: "stage\PRERELEASE.txt"; DestDir: "{app}"; Components: docs; Flags: ignoreversion
; The GUI is optional: included only when the staging step produced it.
Source: "stage\freearc-ex.exe"; DestDir: "{app}"; Components: console; \
    Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\FreeArc-ex console"; Filename: "{cmd}"; \
    Parameters: "/k ""{app}\arc-ex.exe"""; WorkingDir: "{app}"; Comment: "Open a console with arc-ex"
Name: "{group}\Uninstall FreeArc-ex"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FreeArc-ex console"; Filename: "{cmd}"; \
    Parameters: "/k ""{app}\arc-ex.exe"""; Tasks: desktopicon

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
