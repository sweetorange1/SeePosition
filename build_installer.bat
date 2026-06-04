@echo off
chcp 65001 >nul
REM ============================================================
REM SeePosition 安装包打包脚本
REM 功能：使用已编译好的 EXE 生成 Inno Setup 安装包
REM 使用：双击运行即可（需先编译好 Release 版本）
REM ============================================================

setlocal EnableDelayedExpansion

REM ========== 配置区域 ==========
REM 项目路径（脚本所在目录）
set "PROJECT_DIR=%~dp0"
set "DIST_DIR=%PROJECT_DIR%dist"
set "EXE_SOURCE=%PROJECT_DIR%cmake-build-release\SeePosition_artefacts\Release\SeePosition.exe"

REM ========== 查找 Inno Setup 编译器 ==========
set "ISCC="

echo 正在搜索 Inno Setup 编译器...

REM 检查常见的安装路径
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" (
    set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    goto :found_iscc
)
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" (
    set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
    goto :found_iscc
)
if exist "%ProgramFiles(x86)%\Inno Setup 5\ISCC.exe" (
    set "ISCC=%ProgramFiles(x86)%\Inno Setup 5\ISCC.exe"
    goto :found_iscc
)
REM 用户级安装路径（非管理员安装）
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" (
    set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
    goto :found_iscc
)

REM 从注册表查找 Inno Setup 安装路径
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" /v InstallLocation 2^>nul') do (
    if exist "%%b\ISCC.exe" (
        set "ISCC=%%b\ISCC.exe"
        goto :found_iscc
    )
)

REM 从 PATH 环境变量查找
for %%i in (ISCC.exe) do (
    if not "%%~$PATH:i"=="" (
        set "ISCC=%%~$PATH:i"
        goto :found_iscc
    )
)

REM 找不到，提示用户
cls
echo ============================================================
echo            需要安装 Inno Setup 才能打包
echo ============================================================
echo.
echo 未找到 Inno Setup 编译器（ISCC.exe）
echo.
echo 请按以下步骤操作：
echo.
echo   1. 访问官网下载 Inno Setup 6（免费）
echo      下载地址：https://jrsoftware.org/isdl.php
echo.
echo   2. 下载 "Inno Setup 6" 并安装（默认选项即可）
echo.
echo   3. 安装完成后，重新运行此脚本
echo.
echo ============================================================
echo.
pause
exit /b 1

:found_iscc
echo [✓] 找到 Inno Setup: %ISCC%
echo.

REM ========== 开始打包 ==========
echo ============================================================
echo   SeePosition 安装包打包脚本
echo ============================================================
echo.

REM --- 检查 EXE 是否存在 ---
echo [1/3] 检查编译好的 EXE...
if not exist "%EXE_SOURCE%" (
    cls
    echo ============================================================
    echo            未找到编译好的 EXE 文件
    echo ============================================================
    echo.
    echo 未找到: %EXE_SOURCE%
    echo.
    echo 请先编译项目：
    echo   1. 使用 Visual Studio 打开项目
    echo   2. 编译 Release x64 版本
    echo   3. 或运行: cmake --build cmake-build-release --config Release
    echo.
    echo ============================================================
    echo.
    pause
    exit /b 1
)
echo [成功] 找到 EXE: %EXE_SOURCE%
echo.

REM --- 创建 dist 目录 ---
echo [2/3] 创建输出目录...
if not exist "%DIST_DIR%" (
    mkdir "%DIST_DIR%"
)
echo [成功] 输出目录: %DIST_DIR%
echo.

REM --- 打包安装包 ---
echo [3/3] 正在生成安装包...
"%ISCC%" "%PROJECT_DIR%SeePosition_installer.iss"
if errorlevel 1 (
    echo [错误] 安装包生成失败！
    pause
    exit /b 1
)
echo [成功] 安装包生成完成
echo.

REM --- 完成 ---
echo ============================================================
echo  打包完成！
echo.
echo  安装包位置:
dir "%DIST_DIR%\*.exe" 2>nul
echo.
echo ============================================================
echo.
pause
endlocal
