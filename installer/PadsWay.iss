; PadsWay - Inno Setup installer script
; Build with: ISCC.exe installer\PadsWay.iss   (run from the repo root)
; Requires Inno Setup 6+  (https://jrsoftware.org/isdl.php)
;
; Design note: PadsWay stores its configuration (controllers.json, virtualpad.json,
; profiles, macros, pad_layouts.json, logs) under %LOCALAPPDATA%\PadsWay, NOT next to the
; executable, so it keeps working even when installed into a UAC-protected location such
; as Program Files. The factory copies shipped in data\ are seeded into that per-user
; folder on first run. (A portable.txt next to the exe would force the old next-to-exe
; behaviour, but the installer never ships one.) The installer still lets the user pick the
; install mode (all users / just me) and the target folder freely; default privilege level
; is "lowest" (no admin, per-user), overridable to all-users via the mode dialog.

#define AppName     "PadsWay"
; Version can be overridden from the command line: ISCC /DAppVersion=0.19 ...
; (package.ps1 passes it so the staging folder and installer stay in sync.)
#ifndef AppVersion
  #define AppVersion "0.18"
#endif
#define AppPublisher "pooka-v1"
#define AppURL      "https://github.com/pooka-v1/PadsWay"
#define AppExe      "PadsWay.exe"
; Staging folder produced by the packaging step (mirrors the portable zip contents):
#define StageDir    "..\dist\PadsWay-v" + AppVersion + "-win64"
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
; Use the exe's own embedded icon (from PadsWay.rc) rather than the loose .ico,
; so the uninstall entry shows the icon consistently across install modes.
UninstallDisplayIcon={app}\{#AppExe}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[CustomMessages]
; %n = newline, %1 = the LOCALAPPDATA\PadsWay path (filled in at runtime).
english.AskRemoveUserData=Do you also want to delete your PadsWay settings, profiles and macros?%n%n%1%n%nChoose No to keep them for a future reinstall.
spanish.AskRemoveUserData=¿Quieres borrar también tus ajustes, perfiles y macros de PadsWay?%n%n%1%n%nElige No para conservarlos para una futura reinstalación.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole portable payload: exe, factory data\, images\, docs and LICENSE. These are
; read-only / seed files; the user's real config lives in %LOCALAPPDATA%\PadsWay (seeded on
; first run), so a reinstall/upgrade that refreshes data\ never touches the user's settings.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
; App icon, used by the Start Menu / Desktop shortcuts below.
Source: "{#IconFile}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Shortcuts take their icon from the exe's embedded resource (more reliable than a
; separate .ico across install modes and the Windows icon cache).
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppExe}"
Name: "{group}\Quick Start Guide"; Filename: "{app}\QUICK_START.md"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The user's data normally lives in %LOCALAPPDATA%\PadsWay (offered for deletion by the
; [Code] prompt below). These entries only matter for the rare "installed + portable.txt"
; case, where runtime files end up next to the exe; harmless no-ops otherwise.
Type: filesandordirs; Name: "{app}\data\profiles"
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\data\controllers.json"
Type: files; Name: "{app}\data\virtualpad.json"
Type: files; Name: "{app}\data\macros.json"

[Code]
// After the app is removed, the per-user data in %LOCALAPPDATA%\PadsWay is kept by
// default (so a reinstall finds the user's controllers, profiles and macros intact).
// We offer a one-click full wipe via the standard Yes/No prompt.
procedure CurUninstallStepChanged(CurStep: TUninstallStep);
var
  UserData: String;
begin
  if CurStep = usPostUninstall then
  begin
    UserData := ExpandConstant('{localappdata}\PadsWay');
    if DirExists(UserData) then
    begin
      if MsgBox(FmtMessage(CustomMessage('AskRemoveUserData'), [UserData]),
                mbConfirmation, MB_YESNO) = IDYES then
        DelTree(UserData, True, True, True);
    end;
  end;
end;
