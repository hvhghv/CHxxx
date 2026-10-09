@echo off
REM ---------------------------------------------------------------------------
REM 下载 xPack RISC-V 工具链（Windows / CMD 包装）
REM ---------------------------------------------------------------------------
REM 调用同目录的 download.ps1。
REM 用法：
REM   download.bat            下载并解压（已存在则跳过）
REM   download.bat -Force     强制重新下载
REM ---------------------------------------------------------------------------

setlocal
set "SCRIPT=%~dp0download.ps1"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo 下载失败（退出码 %RC%）。
)

endlocal & exit /b %RC%
