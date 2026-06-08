; Shunt installer (Inno Setup)

#ifndef GameVersion
  #define GameVersion "0.0.0"
#endif

#define MyAppName "Shunt"
#define MyAppCompany "SocaLabs"
#define MyAppPublisher "SocaLabs"
#define MyAppCopyright "2026 SocaLabs"
#define MyAppURL "https://socalabs.com/"
#define MyAppExeName "Shunt.exe"
#define MyAppVersion GameVersion

[Setup]
AppID={{A9276816-4DF3-4F02-B62F-76803226C143}
AppName={#MyAppCompany} {#MyAppName}
AppVerName={#MyAppCompany} {#MyAppName} {#MyAppVersion}
AppVersion={#MyAppVersion}
AppCopyright={#MyAppCopyright}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppCompany}\{#MyAppName}
DefaultGroupName={#MyAppCompany}\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=.\bin
OutputBaseFilename={#MyAppName}
Compression=lzma/ultra
SolidCompression=true
ShowLanguageDialog=auto
InternalCompressLevel=ultra
MinVersion=0,6.1.7600
DisableWelcomePage=no
DisableReadyPage=no
DisableReadyMemo=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright={#MyAppCopyright}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoProductTextVersion={#MyAppVersion}
UninstallDisplayName={#MyAppCompany} {#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=admin

[Languages]
Name: english; MessagesFile: compiler:Default.isl

[Files]
Source: "bin\app\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}";        Filename: "{app}\{#MyAppExeName}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
