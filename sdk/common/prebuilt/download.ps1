# ---------------------------------------------------------------------------
# 下载 xPack RISC-V 工具链（Windows / PowerShell）
# ---------------------------------------------------------------------------
# 下载 xpack-riscv-none-elf-gcc-15.2.0-1-win32-x64.zip 并解压到
# sdk/common/prebuilt/xpack-riscv-none-elf-gcc-15.2.0-1/
#
# 用法：
#   .\download.ps1              下载并解压（已存在则跳过）
#   .\download.ps1 -Force       强制重新下载
#
# 若提示脚本被禁止运行，执行：
#   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
# ---------------------------------------------------------------------------

param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# 工具链版本与下载信息
$Version  = "15.2.0-1"
$PkgName  = "xpack-riscv-none-elf-gcc-$Version"
$ZipName  = "$PkgName-win32-x64.zip"
$Url      = "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v$Version/$ZipName"
$Sha256   = "85ef714dacd273b1dadf4af4892774520ac01915bfa6da816a56e7e41591e09e"

$Prebuilt = $PSScriptRoot
$Dest     = Join-Path $Prebuilt $PkgName
$ZipPath  = Join-Path $Prebuilt $ZipName

Write-Host "=== xPack RISC-V 工具链下载 ===" -ForegroundColor Cyan
Write-Host "版本：$Version"
Write-Host "目标：$Dest"

# 已存在则跳过
if ((Test-Path $Dest) -and (-not $Force)) {
    Write-Host "已存在，跳过下载。使用 -Force 强制重新下载。" -ForegroundColor Yellow
    exit 0
}

# 下载
if ((-not (Test-Path $ZipPath)) -or $Force) {
    Write-Host "下载中：$Url" -ForegroundColor Cyan
    Write-Host "（约 444 MB，请耐心等待）"
    # 优先用 curl（Windows 10+ 内置），否则用 Invoke-WebRequest
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        & curl.exe -L --fail --output $ZipPath $Url
        if ($LASTEXITCODE -ne 0) { throw "curl 下载失败（退出码 $LASTEXITCODE）" }
    } else {
        $ProgressPreference = 'SilentlyContinue'   # 加速 Invoke-WebRequest
        Invoke-WebRequest -Uri $Url -OutFile $ZipPath
    }
} else {
    Write-Host "压缩包已存在，跳过下载。" -ForegroundColor Yellow
}

# 校验 SHA256
Write-Host "校验 SHA256..." -ForegroundColor Cyan
$actual = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLower()
if ($actual -ne $Sha256) {
    Remove-Item $ZipPath -Force -ErrorAction SilentlyContinue
    throw "SHA256 校验失败！`n  期望：$Sha256`n  实际：$actual`n（已删除损坏文件，请重试）"
}
Write-Host "SHA256 校验通过。" -ForegroundColor Green

# 解压
Write-Host "解压中..." -ForegroundColor Cyan
if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
Expand-Archive -Path $ZipPath -DestinationPath $Prebuilt -Force

# 清理压缩包
Remove-Item $ZipPath -Force -ErrorAction SilentlyContinue

# 验证
$gcc = Join-Path $Dest "bin/riscv-none-elf-gcc.exe"
if (Test-Path $gcc) {
    Write-Host "完成！工具链已就绪：" -ForegroundColor Green
    Write-Host "  $Dest"
    & $gcc --version | Select-Object -First 1
} else {
    throw "解压后未找到 $gcc，请检查压缩包结构。"
}
