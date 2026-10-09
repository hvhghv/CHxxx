@echo off
REM ---------------------------------------------------------------------------
REM CH572/CH592 项目 - 快捷构建脚本（批处理）
REM ---------------------------------------------------------------------------
REM 用法：
REM   build            编译
REM   build flash      烧录（USB，wchisp）
REM   build probe      探测 USB ISP 设备
REM   build verify     校验 Flash
REM   build erase      擦除 Flash
REM   build size       显示固件大小
REM   build config     重新配置 CMake
REM   build clean      清理构建目录
REM   build rebuild    清理后重新编译
REM
REM 指定固件：build flash usb_dual
REM 指定芯片：build config -DCHIP_SDK=CH592
REM
REM 芯片与默认固件也可在 default_target.txt 中配置。
REM ---------------------------------------------------------------------------

setlocal

set "ROOT=%~dp0"
set "BUILD_DIR=build"
set "TOOLCHAIN=%ROOT%sdk\common\cmake\riscv.cmake"
set "NINJA=ninja"

set "ACTION=%~1"
set "TARGET=%~2"

if "%ACTION%"=="" set "ACTION=build"

REM 收集第 3 个及之后的参数，透传给 cmake（用于 -DCHIP_SDK=... 等）
set "EXTRA_ARGS="
shift
shift
:collect_args
if "%~1"=="" goto :args_done
set "EXTRA_ARGS=%EXTRA_ARGS% %1"
shift
goto :collect_args
:args_done

if /i "%ACTION%"=="build"     goto :build
if /i "%ACTION%"=="config"    goto :config
if /i "%ACTION%"=="flash"     goto :flash
if /i "%ACTION%"=="probe"     goto :probe
if /i "%ACTION%"=="verify"    goto :verify
if /i "%ACTION%"=="erase"     goto :erase
if /i "%ACTION%"=="size"      goto :size
if /i "%ACTION%"=="clean"     goto :clean
if /i "%ACTION%"=="rebuild"   goto :rebuild
if /i "%ACTION%"=="help"      goto :help
if /i "%ACTION%"=="-h"        goto :help
if /i "%ACTION%"=="--help"    goto :help

echo 未知命令: %ACTION%
goto :help

REM ---------------------------------------------------------------------------
:config
echo 配置 CMake...
cmake -B "%ROOT%%BUILD_DIR%" -G Ninja -DCMAKE_MAKE_PROGRAM=%NINJA% -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" %EXTRA_ARGS%
if errorlevel 1 exit /b %errorlevel%
goto :eof

:ensure_config
if not exist "%ROOT%%BUILD_DIR%\build.ninja" (
    call :config
    goto :eof
)
REM default_target.txt 比 build.ninja 新 → 重新配置（切换芯片/目标）
if exist "%ROOT%default_target.txt" (
    for %%A in ("%ROOT%default_target.txt") do set "DT_TIME=%%~tA"
    for %%B in ("%ROOT%%BUILD_DIR%\build.ninja") do set "NJ_TIME=%%~tB"
    if "%DT_TIME%" GTR "%NJ_TIME%" (
        echo 检测到 default_target.txt 已更新，重新配置...
        call :config
    )
)
goto :eof

:build
call :ensure_config
echo 编译...
cmake --build "%ROOT%%BUILD_DIR%"
goto :eof

:flash
call :ensure_config
if "%TARGET%"=="" (set "T=flash") else (set "T=%TARGET%-flash")
echo 烧录...
cmake --build "%ROOT%%BUILD_DIR%" --target %T%
goto :eof

:probe
call :ensure_config
cmake --build "%ROOT%%BUILD_DIR%" --target probe
goto :eof

:verify
call :ensure_config
if "%TARGET%"=="" (set "T=verify") else (set "T=%TARGET%-verify")
cmake --build "%ROOT%%BUILD_DIR%" --target %T%
goto :eof

:erase
call :ensure_config
cmake --build "%ROOT%%BUILD_DIR%" --target erase
goto :eof

:size
call :ensure_config
if "%TARGET%"=="" (set "T=size") else (set "T=%TARGET%-size")
cmake --build "%ROOT%%BUILD_DIR%" --target %T%
goto :eof

:clean
echo 清理 %BUILD_DIR% ...
if exist "%ROOT%%BUILD_DIR%" rmdir /S /Q "%ROOT%%BUILD_DIR%"
echo 已清理。
goto :eof

:rebuild
call :clean
call :build
goto :eof

:help
echo CH572/CH592 项目快捷命令：
echo   build            编译
echo   build flash      烧录（USB，wchisp）
echo   build probe      探测 USB ISP 设备
echo   build verify     校验 Flash
echo   build erase      擦除 Flash
echo   build size       显示固件大小
echo   build config     重新配置 CMake
echo   build clean      清理构建目录
echo   build rebuild    清理后重新编译
echo.
echo 指定固件：build flash usb_dual
echo 指定芯片：build config -DCHIP_SDK=CH592
echo.
echo 芯片与默认固件也可在 default_target.txt 中配置。
goto :eof
