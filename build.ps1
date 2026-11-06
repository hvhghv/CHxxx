# ===========================================================================
# HAL 架构构建脚本
# ===========================================================================
# 用法：
#   .\build.ps1                          编译当前配置的全部目标
#   .\build.ps1 -Chip ch59x              指定芯片并编译
#   .\build.ps1 -Chip ch32x035           切到 CH32X035
#   .\build.ps1 -Target blink            只编译一个目标
#   .\build.ps1 -Clean                   清理后重新配置
#   .\build.ps1 -Flash blink             编译并烧录
#   .\build.ps1 -List                    列出可用目标
#
# 支持的芯片：
#   ch59x      CH591 / CH592（BLE + USB）
#   ch572      CH572（BLE，12KB RAM）
#   ch585      CH585 / CH584（BLE 5.4 + USBHS）
#   ch32x035   CH32X035（USB FS + PDUSB，无 BLE）
#   ch32l103   CH32L103（USB FS + PDUSB，无 BLE）
#   ch32v20x   CH32V203（双 USB，无 BLE）
# ===========================================================================
param(
    [string]$Chip = "ch59x",
    [string]$Target = "",
    [switch]$Clean,
    [switch]$Flash,
    [switch]$List
)

$ErrorActionPreference = "Stop"
$root      = $PSScriptRoot
$buildDir  = Join-Path $root "build_hal"
$toolchain = Join-Path $root "sdk\cmake\toolchain-riscv.cmake"

$validChips = @("ch59x", "ch572", "ch585", "ch32x035", "ch32l103", "ch32v20x")

function Show-Usage {
    Write-Host ""
    Write-Host "HAL 架构构建脚本" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "用法："
    Write-Host "  .\build.ps1                         编译全部目标"
    Write-Host "  .\build.ps1 -Chip ch59x             指定芯片"
    Write-Host "  .\build.ps1 -Target blink           只编译一个目标"
    Write-Host "  .\build.ps1 -Clean                  清理重配"
    Write-Host "  .\build.ps1 -Flash blink            编译并烧录"
    Write-Host "  .\build.ps1 -List                   列出可用目标"
    Write-Host ""
    Write-Host "支持的芯片："
    foreach ($c in $validChips) { Write-Host "  $c" }
    Write-Host ""
}

if (-not (Test-Path $toolchain)) {
    Write-Host "错误：找不到工具链文件 $toolchain" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# 列出目标
# ---------------------------------------------------------------------------
function Get-Targets {
    if (-not (Test-Path $buildDir)) { return @() }
    Get-ChildItem (Join-Path $buildDir "main") -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Name }
}

if ($List) {
    $t = Get-Targets
    if ($t.Count -eq 0) {
        Write-Host "尚未配置（先运行 .\build.ps1）" -ForegroundColor Yellow
    } else {
        Write-Host "可用目标：" -ForegroundColor Cyan
        foreach ($n in $t) { Write-Host "  $n" }
    }
    exit 0
}

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------
$needConfig = $Clean -or (-not (Test-Path (Join-Path $buildDir "build.ninja")))

# 芯片变化时也要重配
$cacheFile = Join-Path $buildDir "CMakeCache.txt"
if (-not $needConfig -and (Test-Path $cacheFile)) {
    # ★ 缓存里的类型可能是 UNINITIALIZED / STRING，用宽松匹配
    $cachedChip = Select-String -Path $cacheFile -Pattern '^CHIP:[A-Z]+=(.*)$' |
                  Select-Object -First 1
    if ($cachedChip) {
        $cur = $cachedChip.Matches[0].Groups[1].Value.Trim()
        if ($cur -ne $Chip) {
            Write-Host "芯片从 $cur 切换到 $Chip，需要重新配置" -ForegroundColor Yellow
            $needConfig = $true
        }
    } else {
        $needConfig = $true
    }
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "清理 $buildDir ..." -ForegroundColor Yellow
    Remove-Item $buildDir -Recurse -Force
}

if ($needConfig) {
    Write-Host "配置（芯片 $Chip）..." -ForegroundColor Cyan
    # ★ 必须用双引号包住整个 -D 参数，否则 PowerShell 会把 $Chip 拆开
    cmake -B $buildDir -G Ninja `
          "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
          "-DHAL_BUILD=ON" `
          "-DCHIP=$Chip"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# ---------------------------------------------------------------------------
# 编译
# ---------------------------------------------------------------------------
if ($Target) {
    Write-Host "编译 $Target ..." -ForegroundColor Cyan
    cmake --build $buildDir --target $Target
} else {
    Write-Host "编译全部目标 ..." -ForegroundColor Cyan
    cmake --build $buildDir
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# ---------------------------------------------------------------------------
# 烧录
# ---------------------------------------------------------------------------
if ($Flash) {
    if (-not $Target) {
        Write-Host "错误：-Flash 需要同时指定 -Target" -ForegroundColor Red
        exit 1
    }
    & (Join-Path $root "flash_hal.ps1") $Target
    exit $LASTEXITCODE
}

# ---------------------------------------------------------------------------
# 汇总
# ---------------------------------------------------------------------------
$targets = Get-Targets
$ok = @()
foreach ($n in $targets) {
    $h = Join-Path $buildDir "main\$n\$n.hex"
    if (Test-Path $h) {
        $ok += [PSCustomObject]@{
            Target = $n
            Size   = (Get-Item $h).Length
        }
    }
}

if ($ok.Count -gt 0) {
    Write-Host ""
    Write-Host "编译产物（芯片 $Chip）：" -ForegroundColor Green
    $ok | ForEach-Object {
        Write-Host ("  OK  {0,-20} {1} bytes" -f $_.Target, $_.Size)
    }
}
