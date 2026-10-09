# ---------------------------------------------------------------------------
# CH572/CH592 项目 - 快捷构建脚本（PowerShell）
# ---------------------------------------------------------------------------
# 用法：
#   .\build.ps1              编译
#   .\build.ps1 flash        烧录（USB，wchisp）
#   .\build.ps1 probe        探测 USB ISP 设备
#   .\build.ps1 verify       校验 Flash
#   .\build.ps1 erase        擦除 Flash
#   .\build.ps1 size         显示固件大小
#   .\build.ps1 config       重新配置 CMake
#   .\build.ps1 clean        清理构建目录
#   .\build.ps1 rebuild      清理后重新编译
#
# 指定固件：.\build.ps1 flash usb_dual
# 指定芯片：.\build.ps1 config -DCHIP_SDK=CH592
# 修改默认目标：.\build.ps1 config -DCH572_TARGET=myapp
#
# 芯片与默认固件也可在 default_target.txt 中配置。
#
# 若提示脚本被禁止运行，执行：
#   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
# ---------------------------------------------------------------------------

param(
    [Parameter(Position = 0)]
    [string]$Action = "build",

    [Parameter(Position = 1)]
    [string]$Target = "",

    [string]$BuildDir = "build",
    [string]$Ninja = "ninja",

    # 透传给 cmake 的额外参数（仅 config 使用）
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs = @()
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Toolchain = "$Root/sdk/common/cmake/riscv.cmake"

# ---------------------------------------------------------------------------
# 工具检查
# ---------------------------------------------------------------------------
function Require-Tool($name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        Write-Host "错误：找不到 $name，请先安装并加入 PATH" -ForegroundColor Red
        exit 1
    }
}

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------
function Invoke-Config {
    Require-Tool cmake
    Write-Host "配置 CMake..." -ForegroundColor Cyan
    $cmakeArgs = @(
        "-B", "$Root/$BuildDir",
        "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=$Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain"
    )
    # config 动作下，位置参数若形如 -D... 则作为 cmake 选项透传
    if ($Target -and $Target.StartsWith("-D")) { $cmakeArgs += $Target }
    if ($ExtraArgs.Count -gt 0) { $cmakeArgs += $ExtraArgs }
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Ensure-Configured {
    $ninjaFile = "$Root/$BuildDir/build.ninja"
    $dtFile = "$Root/default_target.txt"
    $needConfig = $false
    if (-not (Test-Path $ninjaFile)) {
        $needConfig = $true
    } elseif ((Test-Path $dtFile) -and
              ((Get-Item $dtFile).LastWriteTime -gt (Get-Item $ninjaFile).LastWriteTime)) {
        # default_target.txt 比上次配置更新 → 需要重新配置（切换芯片/目标）
        Write-Host "检测到 default_target.txt 已更新，重新配置..." -ForegroundColor Yellow
        $needConfig = $true
    }
    if ($needConfig) {
        Invoke-Config
    }
}

# ---------------------------------------------------------------------------
# 动作分发
# ---------------------------------------------------------------------------
function Invoke-Build {
    Ensure-Configured
    Write-Host "编译..." -ForegroundColor Cyan
    & cmake --build "$Root/$BuildDir"
    exit $LASTEXITCODE
}

function Invoke-CMakeTarget($name) {
    Ensure-Configured
    Write-Host "执行目标：$name" -ForegroundColor Cyan
    & cmake --build "$Root/$BuildDir" --target $name
    exit $LASTEXITCODE
}

function Invoke-Clean {
    Write-Host "清理 $BuildDir ..." -ForegroundColor Yellow
    if (Test-Path "$Root/$BuildDir") {
        Remove-Item -Recurse -Force "$Root/$BuildDir"
    }
    Write-Host "已清理。" -ForegroundColor Green
}

function Write-HelpText {
    Write-Host "CH572/CH592 项目快捷命令：" -ForegroundColor Cyan
    Write-Host "  .\build.ps1              编译"
    Write-Host "  .\build.ps1 flash        烧录（USB，wchisp）"
    Write-Host "  .\build.ps1 probe        探测 USB ISP 设备"
    Write-Host "  .\build.ps1 verify       校验 Flash"
    Write-Host "  .\build.ps1 erase        擦除 Flash"
    Write-Host "  .\build.ps1 size         显示固件大小"
    Write-Host "  .\build.ps1 config       重新配置 CMake"
    Write-Host "  .\build.ps1 clean        清理构建目录"
    Write-Host "  .\build.ps1 rebuild      清理后重新编译"
    Write-Host ""
    Write-Host "指定固件：.\build.ps1 flash usb_dual"
    Write-Host "指定芯片：.\build.ps1 config -DCHIP_SDK=CH592"
    Write-Host "芯片与默认固件也可在 default_target.txt 中配置。"
}

# ---------------------------------------------------------------------------
# 主逻辑
# ---------------------------------------------------------------------------
switch ($Action.ToLower()) {
    "build"    { Invoke-Build }
    "config"   { Invoke-Config }
    "flash"    { Invoke-CMakeTarget $(if ($Target) { "$Target-flash" } else { "flash" }) }
    "probe"    { Invoke-CMakeTarget "probe" }
    "verify"   { Invoke-CMakeTarget $(if ($Target) { "$Target-verify" } else { "verify" }) }
    "erase"    { Invoke-CMakeTarget "erase" }
    "size"     { Invoke-CMakeTarget $(if ($Target) { "$Target-size" } else { "size" }) }
    "clean"    { Invoke-Clean }
    "rebuild"  { Invoke-Clean; Invoke-Build }
    "help"     { Write-HelpText }
    "-h"       { Write-HelpText }
    "--help"   { Write-HelpText }
    default {
        Write-Host "未知命令：$Action" -ForegroundColor Red
        Write-HelpText
        exit 1
    }
}
