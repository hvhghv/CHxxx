# ===========================================================================
# HAL 架构一键验证脚本
# ===========================================================================
# 用法：
#   .\verify_hal.ps1                      # 只编译，不烧录
#   .\verify_hal.ps1 -Flash blink         # 编译并烧录 blink
#   .\verify_hal.ps1 -Flash irq_test      # 烧录中断测试（最关键）
#   .\verify_hal.ps1 -Flash ble_usb       # 烧录 BLE+USB 共存（最重要验收）
#   .\verify_hal.ps1 -ReadEeprom          # 读取 EEPROM 诊断数据
#   .\verify_hal.ps1 -ListUsb             # 列出当前 USB 设备（查枚举）
# ===========================================================================
param(
    [string]$Flash = "",
    [switch]$Build,
    [switch]$ReadEeprom,
    [switch]$ListUsb,
    [string]$DumpFile = "tmp\hal_dump.bin"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$wchisp = Join-Path $root "tools\wchisp\wchisp.exe"
$buildDir = Join-Path $root "build_hal"

function Write-Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "  [OK] $msg" -ForegroundColor Green }
function Write-Err($msg)  { Write-Host "  [!!] $msg" -ForegroundColor Red }

# ---------------------------------------------------------------------------
# 列出 USB 设备（检查枚举是否成功）
# ---------------------------------------------------------------------------
if ($ListUsb) {
    Write-Step "当前 USB 设备（COM 口 + WCH 设备）"
    Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match "COM\d+|WCH|CH59|CH34|USB 串行" } |
        Select-Object Name, DeviceID, Status |
        Format-Table -AutoSize
    Write-Host ""
}

# ---------------------------------------------------------------------------
# 编译
# ---------------------------------------------------------------------------
if ($Build -or $Flash -or (-not $ReadEeprom)) {
    Write-Step "编译全部目标"
    cmake --build $buildDir 2>&1 | Select-String -Pattern "error|FLASH:" | ForEach-Object { $_.Line }
    if ($LASTEXITCODE -ne 0) {
        Write-Err "编译失败"
        exit 1
    }
    Write-Ok "编译完成"

    Write-Host ""
    Get-ChildItem (Join-Path $buildDir "main") -Recurse -Filter "*.bin" |
        Sort-Object Name |
        ForEach-Object { "  {0,-16} {1,8} 字节" -f $_.Name, $_.Length }
}

# ---------------------------------------------------------------------------
# 烧录
# ---------------------------------------------------------------------------
if ($Flash) {
    Write-Step "烧录 $Flash"
    & (Join-Path $root "flash_hal.ps1") $Flash
    if ($LASTEXITCODE -ne 0) { exit 1 }
    Write-Ok "烧录完成，程序已运行"
    Write-Host "`n请等待 5 秒后再读取 EEPROM 诊断数据" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 读取 EEPROM
# ---------------------------------------------------------------------------
if ($ReadEeprom) {
    Write-Step "读取 EEPROM 诊断数据"
    Write-Host "请按住 BOOT 键插 USB（或短接 PB22 到 GND）..." -ForegroundColor Yellow

    $dumpPath = Join-Path $root $DumpFile
    Remove-Item $dumpPath -ErrorAction SilentlyContinue

    $ok = $false
    for ($i = 0; $i -lt 400; $i++) {
        $r = & $wchisp eeprom dump $dumpPath 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0 -and (Test-Path $dumpPath)) { $ok = $true; break }
        Start-Sleep -Milliseconds 500
    }

    if (-not $ok) {
        Write-Err "超时：未检测到 ISP 设备"
        exit 1
    }
    Write-Ok "读取成功：$dumpPath"

    $b = [IO.File]::ReadAllBytes($dumpPath)

    function Show-Words($base, $count, $title) {
        Write-Host ""
        Write-Host ("  --- {0} (偏移 0x{1:X4}) ---" -f $title, $base)
        for ($i = 0; $i -lt $count; $i++) {
            $v = [BitConverter]::ToUInt32($b, $base + $i * 4)
            Write-Host ("    [{0,2}] = 0x{1:X8}" -f $i, $v)
        }
    }

    # blink 心跳（0x0000）
    Show-Words 0x0000 1 "blink 心跳"
    $hb = [BitConverter]::ToUInt32($b, 0)
    $tag = $hb -band 0xFFFF0000
    if ($tag -eq 0xB0000000) {
        Write-Ok "blink 在运行，心跳计数 = $($hb -band 0xFFFF)"
    } elseif ($tag -eq 0xDEAD0000) {
        Write-Err "blink 初始化失败，错误码 = $(-($hb -band 0xFFFF))"
    } else {
        Write-Host "  (未检测到 blink 标记)" -ForegroundColor DarkGray
    }

    # irq_test（0x1000）
    Show-Words 0x1000 4 "irq_test 中断测试"
    $irq0 = [BitConverter]::ToUInt32($b, 0x1000)
    $tmr0 = [BitConverter]::ToUInt32($b, 0x1004)
    $syst = [BitConverter]::ToUInt32($b, 0x1008)
    $ienr = [BitConverter]::ToUInt32($b, 0x100C)
    if (($irq0 -band 0xFFFF0000) -eq 0xC0DE0000) {
        $round = $irq0 -band 0xFFFF
        if ($tmr0 -gt 0) {
            Write-Ok "中断工作正常！轮次=$round TMR0=$tmr0 SysTick=$syst"
        } else {
            Write-Err "TMR0 中断未触发（轮次=$round），中断子系统有问题"
        }
        $expect = (1 -shl 16) -bor (1 -shl 20) -bor (1 -shl 21) -bor (1 -shl 28)
        if (($ienr -band $expect) -eq $expect) {
            Write-Ok "IENR 影子副本正确：bit16/20/21/28 已置位"
        } else {
            Write-Err ("IENR 影子副本异常：0x{0:X8}，期望含 0x{1:X8}" -f $ienr, $expect)
        }
    } else {
        Write-Host "  (未检测到 irq_test 标记)" -ForegroundColor DarkGray
    }

    Write-Host ""
}
