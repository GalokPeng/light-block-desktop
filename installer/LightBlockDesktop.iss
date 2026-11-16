#ifndef AppVersion
  #error Pass /DAppVersion=X.Y.Z-beta.N
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
AppId={{C07C418A-08FE-4E9B-8136-348822397B6B}
#endif
AppName={cm:AppTitle}
AppVersion={#AppVersion}
VersionInfoVersion={#AppNumericVersion}
VersionInfoTextVersion={#AppVersion}
AppVerName={cm:AppTitle} {#AppVersion}
AppPublisher=Light Block Desktop
DefaultDirName={localappdata}\Programs\Light Block Desktop Preview
DefaultGroupName={cm:AppTitle}
UninstallDisplayIcon={app}\轻块桌面测试版.exe
UninstallDisplayName={cm:AppTitle}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Qingkuai-Preview-v{#AppVersion}-Setup-x64
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
english.AppTitle=Light Block Desktop Preview (Extensive optimization)
english.PreviewWarning=This preview is unstable. It installs separately and does not replace the stable version.
english.CreateDesktopIcon=Create a desktop shortcut
english.LaunchApp=Launch Light Block Desktop Preview
english.StableInstallBlocked=This is an unstable preview build. Choose a separate folder; the stable installation must remain unchanged.
chinesesimplified.AppTitle=轻块桌面测试版（极度优化）
chinesesimplified.PreviewWarning=测试版（极度优化）尚不稳定。独立安装，不替换正式版。
chinesesimplified.CreateDesktopIcon=创建桌面快捷方式
chinesesimplified.LaunchApp=启动轻块桌面测试版
chinesesimplified.StableInstallBlocked=这是尚不稳定的测试版。请选择单独的目录，不能覆盖正式版安装。
chinesetraditional.AppTitle=輕塊桌面測試版（極度優化）
chinesetraditional.PreviewWarning=測試版（極度優化）尚不穩定。獨立安裝，不替換正式版。
chinesetraditional.CreateDesktopIcon=建立桌面捷徑
chinesetraditional.LaunchApp=啟動輕塊桌面測試版
chinesetraditional.StableInstallBlocked=這是尚不穩定的測試版。請選擇獨立目錄，不能覆蓋正式版安裝。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
#ifndef TestAppId
Root: HKCU; Subkey: "Software\Classes\DesktopBackground\Shell\LightBlockDesktopPreview.note"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\DesktopBackground\Shell\LightBlockDesktopPreview.sound"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "LightBlockDesktopPreview"; Flags: dontcreatekey uninsdeletevalue
#endif

[Icons]
Name: "{group}\{cm:AppTitle}"; Filename: "{app}\轻块桌面测试版.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{cm:AppTitle}"; Filename: "{app}\轻块桌面测试版.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\轻块桌面测试版.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

[Code]
procedure InitializeWizard;
begin
  WizardForm.WelcomeLabel2.Caption := CustomMessage('PreviewWarning') + #13#10 + #13#10 + WizardForm.WelcomeLabel2.Caption;
end;

function SameDirectoryOrChild(A, B: String): Boolean;
var
  ParentDirectory: String;
  ChildDirectory: String;
begin
  ChildDirectory := AddBackslash(ExpandFileName(A));
  ParentDirectory := AddBackslash(ExpandFileName(B));
  Result := CompareText(Copy(ChildDirectory, 1, Length(ParentDirectory)), ParentDirectory) = 0;
end;

function WithinStablePortableDirectory(Directory: String): Boolean;
var
  Candidate: String;
  ParentDirectory: String;
begin
  Result := False;
  Candidate := RemoveBackslashUnlessRoot(ExpandFileName(Directory));
  while Candidate <> '' do
  begin
    if FileExists(AddBackslash(Candidate) + '轻块桌面.exe') then
    begin
      Result := True;
      Exit;
    end;
    ParentDirectory := ExtractFileDir(Candidate);
    if CompareText(ParentDirectory, Candidate) = 0 then
      Exit;
    Candidate := ParentDirectory;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  StableDirectory: String;
  TargetDirectory: String;
begin
  Result := '';
  TargetDirectory := ExpandConstant('{app}');
  if WithinStablePortableDirectory(TargetDirectory) or
    SameDirectoryOrChild(TargetDirectory, ExpandConstant('{localappdata}\Programs\Light Block Desktop')) then
    Result := CustomMessage('StableInstallBlocked');
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{3CD16B31-077E-4CE9-BB01-578642D94876}_is1',
    'InstallLocation', StableDirectory) and (StableDirectory <> '') and
    SameDirectoryOrChild(TargetDirectory, StableDirectory) then
    Result := CustomMessage('StableInstallBlocked');
end;
