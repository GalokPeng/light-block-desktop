#ifndef AppVersion
  #error Pass /DAppVersion=X.Y.Z
#endif
#ifndef AppNumericVersion
  #error Pass /DAppNumericVersion=X.Y.Z.0
#endif
#ifndef AppDir
  #error Pass /DAppDir=absolute-path-to-portable-build
#endif
#ifndef OutputDir
  #error Pass /DOutputDir=absolute-release-output-path
#endif
[Setup]
#ifdef TestAppId
AppId={#TestAppId}
#else
AppId={{3CD16B31-077E-4CE9-BB01-578642D94876}
#endif
AppName={cm:AppTitle}
AppVersion={#AppVersion}
VersionInfoVersion={#AppNumericVersion}
VersionInfoTextVersion={#AppVersion}
AppVerName={cm:AppTitle} {#AppVersion}
AppPublisher=Light Block Desktop
DefaultDirName={localappdata}\Programs\Light Block Desktop
DefaultGroupName={cm:AppTitle}
UninstallDisplayIcon={app}\轻块桌面.exe
UninstallDisplayName={cm:AppTitle}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Qingkuai-v{#AppVersion}-Setup-x64
SetupIconFile=..\assets\app.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableWelcomePage=no
CloseApplications=yes
RestartApplications=no
AllowNoIcons=yes
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=no
UsePreviousLanguage=no
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Default.isl,ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "compiler:Default.isl,ChineseTraditional.isl"
[CustomMessages]
english.AppTitle=Light Block Desktop
english.CreateDesktopIcon=Create a desktop shortcut
english.LaunchApp=Launch Light Block Desktop
chinesesimplified.AppTitle=轻块桌面
chinesesimplified.CreateDesktopIcon=创建桌面快捷方式
chinesesimplified.LaunchApp=启动轻块桌面
chinesetraditional.AppTitle=輕塊桌面
chinesetraditional.CreateDesktopIcon=建立桌面捷徑
chinesetraditional.LaunchApp=啟動輕塊桌面
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked
[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Registry]
#ifndef TestAppId
Root: HKCU; Subkey: "Software\Classes\DesktopBackground\Shell\LightBlockDesktop.note"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\DesktopBackground\Shell\LightBlockDesktop.sound"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "StillWallpaper"; Flags: dontcreatekey uninsdeletevalue
#endif
[Icons]
Name: "{group}\{cm:AppTitle}"; Filename: "{app}\轻块桌面.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{cm:AppTitle}"; Filename: "{app}\轻块桌面.exe"; WorkingDir: "{app}"; Tasks: desktopicon
[Run]
Filename: "{app}\轻块桌面.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent
