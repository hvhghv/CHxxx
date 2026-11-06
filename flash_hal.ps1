# ===========================================================================
# HAL 架构烧录脚本
# ===========================================================================
# 用法：
#   .\flash_hal.ps1 blink
#   .\flash_hal.ps1 blink -Build
# ===========================================================================
param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Target,

    [switch]$Build
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$wchisp = Join-Path $root "tools\wchisp\wchisp.exe"

if (-not (Test-Path $wchisp)) {
    Write-Host "错误：找不到 wchisp：$wchisp" -ForegroundColor Red
    exit 1
}

# ---- 编译 ----------------------------------------------------------------
if ($Build) {
    Write-Host "编译 $Target ..." -ForegroundColor Cyan
    cmake --build (Join-Path $root "build_hal") --target $Target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# ---- 找固件 --------------------------------------------------------------
# ★ 必须用 .hex，不能用 .bin：
#   .highcode 段的 VMA 是 0x20000000（RAM），LMA 是 0x04（Flash）。
#   objcopy -O binary 按 VMA 排列会跳过 .highcode，导致 Flash 0x04 处
#   没有向量表和 BLE 库代码，程序复位后立即跑飞。
#   .hex 格式保留 LMA，是唯一正确的烧录格式。
$hex = Join-Path $root "build_hal\main\$Target\$Target.hex"
$bin = Join-Path $root "build_hal\main\$Target\$Target.bin"

if (Test-Path $hex) {
    $fw = $hex
} elseif (Test-Path $bin) {
    Write-Host "警告：只有 .bin，缺少 .highcode 数据，可能无法运行" -ForegroundColor Yellow
    $fw = $bin
} else {
    Write-Host "错误：找不到固件（$hex 或 $bin）" -ForegroundColor Red
    Write-Host "请先编译：cmake --build build_hal --target $Target" -ForegroundColor Yellow
    exit 1
}

$size = (Get-Item $fw).Length
Write-Host "固件：$fw（$size 字节）" -ForegroundColor Green

# ---- 等待 ISP ------------------------------------------------------------
Write-Host "等待 ISP 设备（请按住 BOOT 键插 USB，或短接 PB22 到 GND）..." -ForegroundColor Yellow
Write-Host "  检测到设备后会自动烧录，无需其它操作。" -ForegroundColor DarkGray

$deadline = (Get-Date).AddSeconds(120)
$found = $false
while ((Get-Date) -lt $deadline) {
    $out = & $wchisp info 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) { $found = $true; break }
    Start-Sleep -Milliseconds 200
}

if (-not $found) {
    Write-Host "超时：未检测到 ISP 设备" -ForegroundColor Red
    exit 1
}

Write-Host "检测到 ISP 设备" -ForegroundColor Green

# ---- 烧录 ----------------------------------------------------------------
Write-Host "开始烧录..." -ForegroundColor Cyan
& $wchisp flash $fw
if ($LASTEXITCODE -ne 0) {
    Write-Host "烧录失败" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "烧录完成（已校验）" -ForegroundColor Green
