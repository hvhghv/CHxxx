# CH5xx / CH32V003 项目模板（多芯片）

基于 **CH5xx / CH32V003 Lite SDK** 的 CH572 / CH592 / CH591 / CH32V003 开发项目模板。

纯 GCC 工具链（内置）、CMake 构建、支持 BLE / USB，无需 MounRiver Studio。

---

## 目录结构

```
<项目根>/
├── CMakeLists.txt          # 根构建脚本
├── sdk_config.cmake        # 芯片选择（读 default_target.txt 的 chip）
├── default_target.txt      # 默认芯片 + 默认固件（改这里即可）
├── build.ps1               # 快捷脚本（PowerShell）
├── build.bat               # 快捷脚本（批处理）
├── Makefile                # 快捷脚本（make）
├── .gitignore
├── .github/workflows/      # GitHub Actions（build / release / pages）
├── scripts/
│   └── gen_matrix.sh       # 扫描 build_targets.txt 生成 CI 矩阵
├── web/                    # Web UI 前端（GitHub Pages 部署）
│   ├── index.html          # 导航页
│   └── gateway/            # gateway 前端
├── main/                   # 应用代码（多芯片共用）
│   ├── CMakeLists.txt
│   ├── usb_dual/           # 双 USB 串口设备（CH5xx 示例）
│   ├── freertos_demo/      # FreeRTOS 多任务（CH592/CH591 示例）
│   ├── gateway/            # 多功能网关（CH592/CH591）
│   └── blink_v003/         # LED 闪烁（CH32V003 示例）
├── sdk/                    # SDK 集合
│   ├── common/             # 共享：工具链、ch32fun、USB 栈、构建逻辑
│   ├── CH572_DEV/          # CH572 专属（BLE 库）
│   ├── CH592_DEV/          # CH592 / CH591 专属（BLE 库 + FreeRTOS）
│   └── CH32V003_DEV/       # CH32V003 专属（无 BLE/USB）
├── tool/
│   └── wchisp/             # USB 烧录工具（wchisp.exe）
└── tmp/                    # 参考源码（ch32fun / CH572EVT 副本，已 gitignore）
```

> 每个 `main/<项目>/build_targets.txt` 声明该项目支持/要构建的芯片，
> GitHub Actions 据此生成构建矩阵。

---

## 环境要求

| 工具 | 说明 |
|---|---|
| **CMake** ≥ 3.20 | 构建系统 |
| **Ninja** | 构建后端（也可用 make） |
| **RISC-V GCC** | **已内置**在 `sdk/common/prebuilt/`（xPack 15.2.0） |

---

## 快速开始

### 方式一：快捷脚本（推荐）

项目根提供了三种快捷入口，任选其一：

**PowerShell（Windows 原生，无需额外工具）**
```powershell
.\build.ps1              # 编译
.\build.ps1 flash        # 烧录（USB）
.\build.ps1 probe        # 探测 USB ISP 设备
.\build.ps1 help         # 查看全部命令
```

**批处理**
```cmd
build              :: 编译
build flash        :: 烧录（USB）
build probe        :: 探测 USB ISP 设备
```

**Makefile（跨平台，需 make）**
```bash
make              # 编译
make flash        # 烧录（USB）
make probe        # 探测 USB ISP 设备
make help         # 查看全部命令
```

三者功能完全一致，都支持：

| 命令 | 作用 |
|---|---|
| （无参数） | 编译 |
| `flash` | 烧录（USB，需 PA1 按键进 ISP） |
| `probe` | 探测 USB ISP 设备 |
| `verify` | 校验 Flash |
| `erase` | 擦除 Flash |
| `size` | 显示固件大小 |
| `config` | 重新配置 CMake |
| `clean` | 清理构建目录 |
| `rebuild` | 清理后重新编译 |
| `help` | 帮助 |

> 烧录通过 USB Bootloader（wchisp），无需编程器。

### 修改默认芯片与目标

项目根目录的 `default_target.txt` 同时配置**目标芯片**和**默认固件**：

```
chip   = CH572        # CH572 / CH592 / CH591 / CH32V003
target = usb_dual     # 对应 main/<固件名>/
```

改完直接重新运行 `.\build`（或 `cmake -B build`）即可，**无需清理 build 目录**。

兼容旧格式：若某行没有 `=`，则视为固件名（target）。

**方式一：编辑 `default_target.txt`（推荐）**

如上所示，同时设置 `chip` 与 `target`。

**方式二：命令行临时覆盖**

```powershell
.\build config -DCHIP_SDK=CH592              # 切换芯片
.\build config -DCH572_TARGET=myapp          # 切换默认固件
```
```bash
cmake -B build -DCHIP_SDK=CH592 -DCH572_TARGET=myapp
make config CHIP=CH592                       # Makefile
```

> 这是一次性覆盖，下次不带 `-D` 重新配置会回到 `default_target.txt` 的值。

**方式三：环境变量**

```powershell
$env:CHIP_SDK = "CH592"
$env:CH572_DEFAULT_TARGET = "myapp"
.\build config
```

**方式四：指定单个固件（不改默认）**

```powershell
.\build.ps1 flash usb_dual      # PowerShell
```
```bash
make flash T=usb_dual           # Makefile
```

> 若默认目标名不存在，配置时会给出警告并列出所有可用固件。

> **首次运行 PowerShell 脚本**若提示被禁止，执行一次：
> `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`

### 方式二：原生 CMake 命令

```bash
# 配置
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=CH572_DEV/cmake/riscv.cmake

# 编译
cmake --build build

# 烧录
cmake --build build --target flash
```

产物：

```
build/main/usb_dual/usb_dual.bin
build/main/usb_dual/usb_dual.hex
```

### Windows 注意事项

若 Ninja 不在 PATH 中，需显式指定：

```powershell
cmake -B build -G Ninja `
  -DCMAKE_MAKE_PROGRAM="C:/software/ninja-win/ninja.exe" `
  -DCMAKE_TOOLCHAIN_FILE="C:/绝对路径/CH572_DEV/cmake/riscv.cmake"
```

> 快捷脚本（`build.ps1` / `build.bat`）默认用 `ninja`，
> 若不在 PATH 可用 `-Ninja` 参数（ps1）或修改脚本内的 `NINJA` 变量。

---

## 烧录

项目自带 **wchisp**（`tool/wchisp/`），通过 USB Bootloader 烧录，**无需编程器**。

### 进入 Bootloader

1. 按住 **PA1 按键**
2. 插 USB（或复位）
3. 松开按键 → 芯片进入 USB ISP 模式

### 烧录

```powershell
.\build flash          # 烧录
.\build probe          # 探测 USB ISP 设备
.\build verify         # 校验 Flash
.\build erase          # 擦除 Flash
```
```cmd
build flash
```
```bash
make flash
```

### 全部烧录相关命令

| 命令 | 说明 |
|---|---|
| `flash` | 烧录固件 |
| `probe` | 探测 USB ISP 设备 |
| `verify` | 校验 Flash |
| `erase` | 擦除 Flash |
| `size` | 显示固件大小 |

### 直接调用工具

```bash
tool\wchisp\wchisp.exe flash build\main\usb_dual\usb_dual.bin
tool\wchisp\wchisp.exe probe          # 探测设备
tool\wchisp\wchisp.exe info           # 芯片信息
```

> Linux 下 wchisp 需 udev 规则（VID `4348`/`1a86`，PID `55e0`）。

### wchisp 常用参数

| 参数 | 说明 |
|---|---|
| `probe` | 探测 USB ISP 设备 |
| `info` | 读芯片信息 |
| `flash <file>` | 烧录固件（.bin/.hex/.elf） |
| `verify <file>` | 校验固件 |
| `erase` | 全片擦除 |
| `config info` | 读配置区 |
| `-d <n>` | 多设备时选择索引 |

---

## 示例：三 USB 串口设备

`main/usb_dual/` 把 CH572 枚举为**三个独立的 CDC-ACM 虚拟串口**：

| 串口 | 接口 | 功能 |
|---|---|---|
| **A「终端」** | Interface 0/1 | 系统信息、蓝牙控制命令 |
| **B「蓝牙桥」** | Interface 2/3 | USB ↔ BLE 双向透传 |
| **C「调试口」** | Interface 4/5 | **printf 输出 / scanf 输入** |

### 端点分配

| 端点 | 用途 | 方向 |
|---|---|---|
| EP1 IN | CDC-A 通知 | IN |
| EP2 OUT | CDC-A 数据接收 | OUT |
| EP3 IN | CDC-A 数据发送 | IN |
| EP4 OUT | CDC-B 数据接收 | OUT |
| EP5 IN | CDC-B 数据发送 | IN |
| EP6 OUT | CDC-C 数据接收 | OUT |
| EP7 IN | CDC-C 数据发送 | IN |

> **为什么 B/C 没有通知端点？**
> CH572 只有 EP1~EP7 共 7 个端点，3 个 CDC 标准配置需 9 个（每个 3 个）。
> CDC 的通知端点（Interrupt IN）是**可选**的，省略后 Windows 仍能正常识别。
> 另外 CH572 的 EP7 双向 DMA 与 RX 共用寄存器，**不能同时收发**，
> 因此所有数据端点均为单向。

### 串口 C：printf / scanf

串口 C 是专用调试口，已重定向标准输入输出：

```c
printf("count = %d\r\n", n);   /* 输出到串口 C */

int a, b;
scanf("%d %d", &a, &b);        /* 从串口 C 读取 */

int ch = getchar();             /* 阻塞读一个字符 */
```

实现方式（`_write` / `putchar` / `_read` / `getchar`）：

```c
int _write(int fd, const char *buf, int len);  /* printf 底层 */
int putchar(int c);                            /* printf("%c") */
int _read(int fd, char *buf, int len);         /* scanf 批量读 */
int getchar(void);                             /* 阻塞读一字节 */
```

> 注意：`getchar()` 会阻塞并调用 `TMOS_SystemProcess()` 保持 BLE 运行。
> 若不想阻塞，可先检查可用字节数再调用 `_read`。
>
> ⚠️ ch32fun 使用自实现的 mini_printf（`-nostdlib`），**不支持 `%f` 浮点**。
> 支持 `%d` `%u` `%x` `%X` `%s` `%c` `%l` 等。

### 串口 A 命令

| 命令 | 说明 |
|---|---|
| `info` | 显示系统信息（芯片 ID、时钟、**ROM/RAM 使用率**、BLE 状态） |
| `adv` | 启动 BLE 广播 |
| `stop` | 停止 BLE（断开连接） |
| `status` | 显示 BLE 连接状态 |
| `dbg` | printf 演示（输出到串口 C） |
| `reboot` | 软件复位重启固件 |
| `isp` | 显示进入 USB ISP 模式的步骤 |
| `web` | 显示 Web Bluetooth 页面使用步骤 |
| `help` | 帮助 |

`info` 输出示例：

```
=== CH572 System Info ===
Chip ID     : 0xXX
Core clock  : 60000000 Hz
ROM (Flash) : 21936 / 245760 B  (8%)
RAM         : 9440 / 12288 B  (76%)
  .data     : 48 B
  .bss      : 5448 B
  .highcode : 3944 B
  stack free: 2848 B
BLE stack   : idle
BLE heap    : 3584 bytes
USB         : dual CDC (terminal + BLE bridge)
```

> ROM/RAM 使用率由链接脚本导出的符号（`_etext`/`_edata`/`_ebss` 等）
> 在运行时计算，与链接器 `--print-memory-usage` 的结果一致。

### 串口 B 用法

- **PC → BLE**：往串口 B 写数据，会通过 BLE 通知发出
- **BLE → PC**：BLE 写入的数据会显示在串口 B

### Web Bluetooth 透传（`web_ble.html`）

`main/usb_dual/web_ble.html` 是一个纯前端页面，用浏览器 **Web Bluetooth API**
实现「浏览器 ↔ BLE ↔ USB 串口 B」透传，无需安装任何软件。

**GATT 服务定义**：

| 项目 | UUID | 属性 | 方向 |
|---|---|---|---|
| Service | `0xFFF0` | — | — |
| RX 特征 | `0xFFF1` | Write / WriteWithoutResponse | 浏览器 → USB |
| TX 特征 | `0xFFF2` | Notify | USB → 浏览器 |

**使用步骤**：

1. 在串口 A 输入 `adv` 启动 BLE 广播
2. 用 **Chrome / Edge（桌面版）** 打开 `web_ble.html`
   > ⚠️ Web Bluetooth 要求 **https://** 或 **http://localhost**
   > 直接双击打开 `file://` 不行，需起个本地服务：
   > ```bash
   > cd main/usb_dual
   > python -m http.server 8000
   > # 然后浏览器打开 http://localhost:8000/web_ble.html
   > ```
3. 点击「连接蓝牙」，选择 `CH572-BLE`
4. 输入数据回车发送 → 经 BLE → USB 串口 B
5. 串口 B 收到的数据会实时显示在右侧

**页面功能**：

| 功能 | 说明 |
|---|---|
| HEX / 文本模式 | 切换发送与显示的编码方式 |
| 本地回显 | 发送的内容也显示在左侧 |
| 自动滚动 | 新数据自动滚到底部 |
| 分包发送 | 自动按 20 字节 MTU 分块 |

**串口 A 命令 `web`** 可随时查看上述步骤。

### 资源占用

| 资源 | 占用 |
|---|---|
| FLASH | 25,380 B (10.33%) |
| RAM | 10,964 B (89.23%) |

> ⚠️ CH572 只有 12K RAM，此示例已用 89.23%，增加功能需留意。

---

## 添加新固件

1. 新建目录 `main/myapp/`
2. 放入三个文件：

**`main/myapp/CMakeLists.txt`**：
```cmake
cmake_minimum_required(VERSION 3.20)

if(NOT COMMAND ch572_add_executable)
    get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../CH572_DEV" ABSOLUTE)
    include(${SDK_ROOT}/cmake/sdk.cmake)
endif()

ch572_add_executable(myapp myapp.c)      # BLE 加 BLE，USB 加 USB
```

**`main/myapp/funconfig.h`**：
```c
#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H
#define FUNCONF_USE_HSI           0
#define FUNCONF_USE_HSE           1
#define CLK_SOURCE_CH5XX          CLK_SOURCE_PLL_60MHz
#define FUNCONF_SYSTEM_CORE_CLOCK 60 * 1000 * 1000
#define FUNCONF_USE_DEBUGPRINTF   1
#endif
```

**`main/myapp/myapp.c`**：
```c
#include "ch32fun.h"
#include <stdio.h>

int main(void)
{
    SystemInit();
    funPinMode(PA9, GPIO_CFGLR_OUT_10Mhz_PP);
    while (1) {
        funDigitalWrite(PA9, FUN_LOW);
        Delay_Ms(500);
        funDigitalWrite(PA9, FUN_HIGH);
        Delay_Ms(500);
    }
}
```

3. 在 `main/CMakeLists.txt` 加一行：
```cmake
add_subdirectory(myapp)
```

### ch572_add_executable 参数

```cmake
ch572_add_executable(<目标名> <源文件...>
    [BLE]                     启用官方 BLE 库
    [USB]                     启用 USB 设备支持（fsusb）
    [MARCH <string>]          覆盖架构扩展
    [LINKER_SCRIPT <path>]    自定义链接脚本
    [EXTRA_SOURCES ...]       额外源文件
    [EXTRA_INCLUDES ...]      额外头文件路径
    [EXTRA_LIBS ...]          额外链接库
)
```

---

## 关于 tmp/

`tmp/` 存放 `ch32fun` 和 `CH572EVT` 的参考副本，用于查阅实现细节。

**已加入 `.gitignore`，不纳入版本控制。**

---

## 持续集成（GitHub Actions）

| Workflow | 触发 | 作用 |
|---|---|---|
| `build.yml` | push / PR / 手动 | 编译所有默认目标（矩阵），上传 artifact |
| `release.yml` | tag `v*` | 编译 + 发布 GitHub Release（固件 `.bin`/`.hex`/`.elf`） |
| `pages.yml` | `web/` 变更 / 手动 | 部署 Web UI 到 GitHub Pages |

**构建矩阵**来自各 `main/<项目>/build_targets.txt`：

```
# gateway/build_targets.txt
chip = CH592
chip = CH591
```

当前矩阵（8 个组合）：

| 项目 | 芯片 |
|---|---|
| `usb_dual` | CH572 / CH592 / CH591 |
| `freertos_demo` | CH592 / CH591 |
| `gateway` | CH592 / CH591 |
| `blink_v003` | CH32V003 |

**产物命名**：`<项目>-<芯片>.bin`（如 `gateway-ch592.bin`）。

**工具链**：CI 使用 `actions/cache` 缓存 xPack RISC-V 工具链（413MB），
首次下载后复用。

**手动触发**：在 GitHub Actions 页面选对应 workflow → Run workflow。

**发布版本**：
```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## Web UI（GitHub Pages）

前端托管在 GitHub Pages，设备只需返回一个含 iframe 的小页面。

- 前端源：`web/` 目录
- 部署后地址：`https://<user>.github.io/<repo>/`
- 设备配置：`webui https://<user>.github.io/<repo>/gateway/`
- 前端支持 `?host=http://192.168.7.1` 参数或页面输入框指定设备地址

---

## 作为新项目模板使用

1. 复制整个项目目录
2. 删除不需要的 `main/` 子目录
3. 删除 `build/` 和 `tmp/`（如有）
4. 修改根 `CMakeLists.txt` 的 `project()` 名称
5. 开始开发

---

## 详细文档

SDK 的完整文档见 `CH572_DEV/README.md`。
