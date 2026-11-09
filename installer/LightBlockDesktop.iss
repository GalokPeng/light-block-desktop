#ifndef AppVersion
  #error Pass /DAppVersion=X.Y.Z
#endif
#ifndef AppDir
  #error Pass /DAppDir=absolute-path-to-portable-build
#endif
#ifndef OutputDir
  #error Pass /DOutputDir=absolute-release-output-path
#endif

[Setup]
AppId={{3CD16B31-077E-4CE9-BB01-578642D94876}
AppName=轻块桌面
AppVersion={#AppVersion}
AppPublisher=Light Block Desktop
DefaultDirName={localappdata}\Programs\Light Block Desktop
DefaultGroupName=轻块桌面
UninstallDisplayIcon={app}\轻块桌面.exe
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Qingkuai-v{#AppVersion}-Setup-x64
SetupIconFile=..\assets\still.ico
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
AllowNoIcons=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\轻块桌面"; Filename: "{app}\轻块桌面.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\轻块桌面"; Filename: "{app}\轻块桌面.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\轻块桌面.exe"; Description: "Launch 轻块桌面"; Flags: nowait postinstall skipifsilent
