# prebuilt/ — 预编译工具链

本目录存放 RISC-V 交叉编译工具链（**不纳入版本控制**，约 1.7GB）。

## 下载工具链

工具链为 **xPack GNU RISC-V Embedded GCC 15.2.0-1**，需先下载：

### Windows

```powershell
# PowerShell
.\download.ps1

# 或 CMD
download.bat
```

### Linux / macOS / CI

```bash
# 下载并解压到本目录（Linux x64）
VERSION=15.2.0-1
PKG=xpack-riscv-none-elf-gcc-$VERSION
curl -L -o $PKG-linux-x64.tar.gz \
  https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v$VERSION/$PKG-linux-x64.tar.gz
tar xzf $PKG-linux-x64.tar.gz -C .
rm $PKG-linux-x64.tar.gz
```

## 目录结构

下载解压后：

```
prebuilt/
├── download.ps1                          # Windows 下载脚本
├── download.bat                          # Windows CMD 包装
├── README.md
└── xpack-riscv-none-elf-gcc-15.2.0-1/    # 工具链（gitignore）
    ├── bin/riscv-none-elf-gcc[.exe]
    ├── lib/
    └── ...
```

CMake 工具链文件 `sdk/common/cmake/riscv.cmake` 会自动查找
`prebuilt/xpack-riscv-none-elf-gcc-*`（取版本号最大的）。

## 各平台下载地址

| 平台 | 文件 | 大小 |
|---|---|---|
| Windows x64 | `xpack-riscv-none-elf-gcc-15.2.0-1-win32-x64.zip` | 444 MB |
| Linux x64 | `xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz` | 413 MB |
| Linux arm64 | `xpack-riscv-none-elf-gcc-15.2.0-1-linux-arm64.tar.gz` | 406 MB |
| macOS arm64 | `xpack-riscv-none-elf-gcc-15.2.0-1-darwin-arm64.tar.gz` | 383 MB |

下载基础 URL：
`https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v15.2.0-1/`

## 校验

Windows 包 SHA256：
`85ef714dacd273b1dadf4af4892774520ac01915bfa6da816a56e7e41591e09e`
