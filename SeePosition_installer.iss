; SeePosition Installer Script
; Inno Setup 6.x 安装包脚本
; 功能：
;   - 可选创建桌面快捷方式
;   - 可选加入开始菜单
;   - 安装完成后可选启动软件
;   - 自动复制 settings 目录下的预设文件到 AppData\Roaming\SeePosition

#define MyAppName      "SeePosition"
#define MyAppVersion   "0.1.0"
#define MyAppPublisher "iisaacbeats.cn"
#define MyAppURL       "https://iisaacbeats.cn"
#define MyAppExeName   "SeePosition.exe"

[Setup]
; 基本信息
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=dist
OutputBaseFilename={#MyAppName}_Setup_{#MyAppVersion}_x64
SetupIconFile=icon\icon.ico

; 压缩与架构
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; 权限与日志
PrivilegesRequired=admin
SetupLogging=yes
UsePreviousAppDir=no

; 卸载相关
UninstallDisplayIcon={app}\{#MyAppExeName}
CreateUninstallRegKey=yes

; 升级安装时自动关闭正在运行的程序
CloseApplications=force
CloseApplicationsFilter={#MyAppExeName}
RestartApplications=no

[Languages]
; 简体中文设为默认语言（列表第一个为默认）
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
; 自定义多语言消息
english.CreateStartMenu=Create a Start Menu folder
chinesesimplified.CreateStartMenu=创建开始菜单文件夹
english.LaunchProgram=Launch SeePosition
chinesesimplified.LaunchProgram=启动 SeePosition

[Tasks]
; 桌面快捷方式（用户可选）
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; 开始菜单文件夹（用户可选）
Name: "startmenu"; Description: "{cm:CreateStartMenu}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; 主程序 EXE
Source: "cmake-build-release\SeePosition_artefacts\Release\SeePosition.exe"; \
    DestDir: "{app}"; \
    Flags: ignoreversion

; 图标资源（如果有）
Source: "icon\*"; \
    DestDir: "{app}\icon"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Components: main

; 预设文件：安装时复制到 AppData\Roaming\SeePosition
; 注意：{userappdata} 对应 C:\Users\用户名\AppData\Roaming
Source: "settings\*"; \
    DestDir: "{userappdata}\SeePosition"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Components: main

[Components]
Name: "main"; Description: "SeePosition Main Program"; Types: full compact custom; Flags: fixed

[Icons]
; 开始菜单快捷方式（仅当用户勾选 startmenu 任务时创建）
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startmenu
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"; Tasks: startmenu

; 桌面快捷方式（仅当用户勾选 desktopicon 任务时创建）
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; 安装完成后可选启动软件
Filename: "{app}\{#MyAppExeName}"; \
    Description: "{cm:LaunchProgram}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时清理 AppData\Roaming 目录下的配置文件（可选，注释掉则保留用户配置）
; Type: filesandordirs; Name: "{userappdata}\SeePosition"

[Code]
// 安装完成后确保 AppData\Roaming\SeePosition 目录存在
// JUCE 的 userApplicationDataDirectory 在 Windows 上对应 AppData\Roaming
procedure CurStepChanged(CurStep: TSetupStep);
var
  AppDataDir: string;
begin
  if CurStep = ssPostInstall then
  begin
    // 确保预设文件目标目录存在
    AppDataDir := GetEnv('APPDATA') + '\SeePosition';
    if not DirExists(AppDataDir) then
      CreateDir(AppDataDir);
  end;
end;

// 卸载前确认
function InitializeUninstall(): Boolean;
begin
  Result := True;
end;

// 卸载完成后可选清理 AppData
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  AppDataDir: string;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    AppDataDir := GetEnv('APPDATA') + '\SeePosition';
    // 可选：询问用户是否删除配置
    // 这里默认保留用户配置
  end;
end;
