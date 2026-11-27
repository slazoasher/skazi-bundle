; DW-303 Windows installer (Inno Setup 6.3+), x64 only, per-machine.
;
;   VST3       -> {commoncf64}\VST3\DW-303.vst3            (whole bundle)
;   CLAP       -> {commoncf64}\CLAP\DJW Audio\              (DW-303.clap + WebView2Loader.dll)
;   Standalone -> {autopf}\DJW Audio\DW-303\                (DW-303.exe + WebView2Loader.dll)
;
; LICENSE.txt, THIRD-PARTY.txt and the uninstaller always go to the app folder.
; Build with installer\build-windows-installer.ps1, or by hand:
;
;   ISCC.exe /DAppVersion=0.1.0 /DStageDir=C:\src\dw-303\target\plugins installer\dw-303.iss
;
; Defines (all optional except AppVersion):
;   AppVersion         product version, e.g. 0.1.0 (from Cargo.toml [workspace.package])
;   AppVersionNumeric  four-part numeric file version, e.g. 0.1.0.0
;   StageDir           staged bundles (default ..\target\plugins, relative to this script)
;   OutputDir          where the setup .exe goes (default ..\target\installer)
;
; The Microsoft Edge WebView2 Runtime (needed by the panel editor) is detected,
; not bundled: when it is missing Setup shows a page that offers to open the
; download site after installation.

#ifndef AppVersion
  #error AppVersion is not defined; pass /DAppVersion=<version> to ISCC
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "0.0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\target\plugins"
#endif
#ifndef OutputDir
  #define OutputDir "..\target\installer"
#endif

#define AppName "DW-303"
#define Company "DJW Audio"
#define AppURL "https://djwaudio.co/#dw-303"
#define WebView2URL "https://developer.microsoft.com/microsoft-edge/webview2/"

[Setup]
; Never change AppId: it ties upgrades and the uninstaller entry together.
AppId={{85DABE48-B624-4FB8-90BE-CD200794D91D}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Company}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
AppCopyright=Copyright (C) {#Company}. GPL-3.0-or-later.
VersionInfoVersion={#AppVersionNumeric}
VersionInfoCompany={#Company}
VersionInfoDescription={#AppName} Setup
VersionInfoProductName={#AppName}
VersionInfoProductTextVersion={#AppVersion}
DefaultDirName={autopf}\{#Company}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
LicenseFile={#StageDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\DW-303.exe
SetupLogging=yes
; Code signing hook (unsigned for now). Define a sign tool in the Inno Setup IDE
; (Tools > Configure Sign Tools) or on the command line, for example
;   ISCC.exe "/Sdw303sign=signtool.exe sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a $f" ...
; then uncomment:
;SignTool=dw303sign
;SignedUninstaller=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: standalone; Flags: unchecked

[InstallDelete]
; Replace earlier copies wholesale so stale files never linger inside a bundle.
Type: filesandordirs; Name: "{commoncf64}\VST3\DW-303.vst3"; Components: vst3
Type: files; Name: "{commoncf64}\CLAP\DJW Audio\DW-303.clap"; Components: clap

[Files]
; Signing: add "signonce" to the Flags of the .exe, .dll, .clap and .vst3 entries
; once SignTool above is enabled.
Source: "{#StageDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD-PARTY.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\VST3\DW-303.vst3\*"; DestDir: "{commoncf64}\VST3\DW-303.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\CLAP\DW-303.clap"; DestDir: "{commoncf64}\CLAP\{#Company}"; Components: clap; Flags: ignoreversion
Source: "{#StageDir}\CLAP\WebView2Loader.dll"; DestDir: "{commoncf64}\CLAP\{#Company}"; Components: clap; Flags: ignoreversion
Source: "{#StageDir}\Standalone\DW-303.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#StageDir}\Standalone\WebView2Loader.dll"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\DW-303.exe"; WorkingDir: "{app}"; Components: standalone
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\DW-303.exe"; WorkingDir: "{app}"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{app}\DW-303.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\DW-303.vst3"
Type: dirifempty; Name: "{commoncf64}\CLAP\{#Company}"
Type: dirifempty; Name: "{app}"
Type: dirifempty; Name: "{autopf}\{#Company}"

[Code]
const
  WebView2ClientGuid = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';

var
  WebView2Missing: Boolean;
  WebView2Page: TInputOptionWizardPage;

function WebView2VersionAt(const RootKey: Integer; const SubKey: String): Boolean;
var
  Version: String;
begin
  Result := RegQueryStringValue(RootKey, SubKey, 'pv', Version)
    and (Version <> '') and (Version <> '0.0.0.0');
end;

function WebView2Installed: Boolean;
begin
  // Per-machine runtime (Evergreen, registered in the 32-bit view), then per-user.
  Result :=
    WebView2VersionAt(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\' + WebView2ClientGuid) or
    WebView2VersionAt(HKLM32, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + WebView2ClientGuid) or
    WebView2VersionAt(HKCU, 'Software\Microsoft\EdgeUpdate\Clients\' + WebView2ClientGuid);
end;

procedure InitializeWizard;
begin
  WebView2Missing := not WebView2Installed;
  if WebView2Missing then
  begin
    Log('Microsoft Edge WebView2 Runtime not found; the panel editor will not open until it is installed.');
    WebView2Page := CreateInputOptionPage(wpSelectComponents,
      'Microsoft Edge WebView2 Runtime required',
      'The DW-303 panel editor needs the WebView2 Runtime, which was not found on this computer.',
      'DW-303 will still install and play, but plug-in hosts will show generic controls and the ' +
      'standalone app cannot show its panel until the Microsoft Edge WebView2 Runtime (Evergreen) ' +
      'is installed. It is free from Microsoft and is already present on most Windows 10 and 11 systems.' + #13#10#13#10 +
      'Setup does not download or install it for you.',
      False, False);
    WebView2Page.Add('Open the WebView2 download page when Setup finishes');
    WebView2Page.Values[0] := True;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorCode: Integer;
begin
  // Nested ifs: WebView2Page exists only when the runtime is missing.
  if (CurStep = ssDone) and WebView2Missing and not WizardSilent then
  begin
    if WebView2Page.Values[0] then
    begin
      if not ShellExecAsOriginalUser('open', '{#WebView2URL}', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode) then
        MsgBox('Could not open your browser. Download the WebView2 Runtime from:' + #13#10 + '{#WebView2URL}',
          mbInformation, MB_OK);
    end;
  end;
end;
