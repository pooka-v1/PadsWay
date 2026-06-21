; PadsWay - Inno Setup installer script
; Build with: ISCC.exe installer\PadsWay.iss   (run from the repo root)
; Requires Inno Setup 6+  (https://jrsoftware.org/isdl.php)
;
; Design note: PadsWay writes its configuration (controllers.json, virtualpad.json,
; profiles, macros) into data\ next to the executable. The installer lets the user pick
; the install mode (all users / just me) and the target folder freely. Default privilege
; level is "lowest" (no admin, per-user) but the user can override to all-users via the
; mode dialog. Caveat: if installed into the real, UAC-protected Program Files, the app
; may fail to save its config there; any normal writable folder (e.g. C:\Programas\PadsWay)
; works fine.

#define AppName     "PadsWay"
#define AppVersion  "0.18"
#define AppPublisher "pooka-v1"
#define AppURL      "https://github.com/pooka-v1/PadsWay"
#define AppExe      "PadsWay.exe"
; Staging folder produced by the packaging step (mirrors the portable zip contents):
#define StageDir    "..\dist\PadsWay-v0.18-win64"
#define IconFile    "..\PadsWay\images\icon\padsWayIcon.ico"

[Setup]
; A unique, fixed AppId keeps upgrades/uninstall consistent across versions.
AppId={{A7F3C2E1-9B4D-4E6A-8F12-3C5D7E9A1B2C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Default to per-user (no admin), but let the user choose all-users via the mode dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
LicenseFile={#StageDir}\LICENSE
InfoBeforeFile=DRIVERS_NOTICE.txt
OutputDir=..\dist
OutputBaseFilename=PadsWay-v{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\padsWayIcon.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole portable payload: exe, data\, images\, docs and LICENSE.
; controllers.json is excluded here and handled separately below, so a reinstall/upgrade
; never overwrites a configuration the user already created.
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "data\controllers.json"; Flags: recursesubdirs createallsubdirs ignoreversion
; Empty controllers list: only dropped on a clean install (prevents the "missing controllers"
; error on first run). On reinstall/upgrade the existing file is kept intact.
Source: "{#StageDir}\data\controllers.json"; DestDir: "{app}\data"; Flags: onlyifdoesntexist
; App icon, used by the Start Menu / Desktop shortcuts below.
Source: "{#IconFile}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\padsWayIcon.ico"
Name: "{group}\Quick Start Guide"; Filename: "{app}\QUICK_START.md"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\padsWayIcon.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Remove user-generated config created at runtime so uninstall leaves no leftovers.
Type: filesandordirs; Name: "{app}\data\profiles"
Type: filesandordirs; Name: "{app}\data\bots"
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\data\controllers.json"
Type: files; Name: "{app}\data\virtualpad.json"
Type: files; Name: "{app}\data\macros.json"
