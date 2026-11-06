# HAL 重构进度

> 最后更新：阶段 0-4 编译全部通过，等待硬件验证

## 快速开始

```powershell
# 配置（只需一次）
cmake -B build_hal -G Ninja `
      "-DCMAKE_TOOLCHAIN_FILE=C:/Users/Administrator/Desktop/demo/CHxxx/sdk/cmake/toolchain-riscv.cmake" `
      -DHAL_BUILD=ON -DCHIP=ch59x

# 编译全部
cmake --build build_hal

# 编译单个
cmake --build build_hal --target ble_echo

# 烧录（会等待 ISP，按住 BOOT 插 USB）
.\flash_hal.ps1 ble_echo
```

## 已完成（编译通过）

| 阶段 | 内容 | 产物 | FLASH |
|------|------|------|-------|
| 0 | sdk/vendor/ch59x 官方库拷贝 | 61 个文件 | — |
| 1 | common + port + CMake + blink | `blink.bin` | 8136 B |
| 2 | 中断子系统（影子副本 + 汇编 trampoline） | `irq_test.bin` | 8260 B |
| 3 | UART + 可切换日志 | `uart_demo.bin` | 11680 B |
| 4 | BLE 多服务 / 多特征值 | `ble_echo.bin` | 155464 B |
| 5 | USB CDC（端点状态机 + 复合描述符） | `usb_cdc.bin` | 13108 B |
| **5+** | **★ BLE + USB CDC 共存** | **`ble_usb.bin`** | **156776 B** |

## 待硬件验证

### 1. blink（阶段 1）
- **验证方式**：PA13 每 500ms 翻转；EEPROM `0x0000` 每 2 秒写入心跳
- **读取**：`wchisp eeprom dump tmp\hb.bin`，期望 `0xB0000000 | 计数`

### 2. irq_test（阶段 2）★ 最关键
- **验证方式**：TMR0 每 10ms 中断一次，每秒落盘
- **读取**：`wchisp eeprom dump tmp\irq.bin`，偏移 `0x1000`
- **期望**：
  - `[0] = 0xC0DE0000 | 轮次`
  - `[1] = TMR0 计数`（应约等于 `轮次 × 100`）
  - `[2] = SysTick 计数`（应等于轮次）
  - `[3] = IENR[0] 影子`（bit16/20/21/28 应全为 1）
- **这是整个重构的基石**：如果 `[1]` 为 0，说明中断仍不工作

### 3. uart_demo（阶段 3）
- **验证方式**：UART1 (PA9=TXD, PA8=RXD) 115200 输出
- **期望**：串口助手看到 `=== CH59x HAL UART Demo ===`、时钟频率、每秒一行 tick
- **额外**：每 5 次演示日志后端切换（UART → RAM → UART）

### 4. ble_echo（阶段 4）★ 核心目标
- **验证方式**：手机 nRF Connect 扫描
- **期望**：
  - 扫到 `CH592-BLE`
  - 连接后看到 2 个服务：
    - `Echo` (0xFFF0)：RX(0xFFF1) 写 / TX(0xFFF2) 通知
    - `Sensor` (0x181A)：Temp(0x2A6E) 读+通知
  - 往 RX 写数据，TX 原样通知回来
  - UART1 同时输出日志

### 5. usb_cdc（阶段 5）
- **验证方式**：插 USB，看 Windows 设备管理器
- **期望**：
  - 出现新 COM 口（VID 0x1A86 / PID 0xFE07）
  - 打开串口能看到 `=== CH59x HAL USB CDC Demo ===`
  - 发送任意字符能回显
- **如果枚举失败**：检查设备管理器是否有「未知 USB 设备」，记录错误码

### 6. ble_usb（阶段 5+）★★ 最重要的验收
- **验证方式**：手机扫描 + 插 USB
- **期望**：
  - 手机能扫到 `CH592-BLE` 并连接
  - Windows 同时识别出 COM 口（VID 0x1A86 / PID 0xFE08）
  - **两者互不干扰**：BLE 能收发的同时 USB 也能收发
  - BLE 收到的数据会转发到 USB，USB 收到的会转发到 BLE
  - USB 日志显示 `[N] ble=1 usb=1`

> **这是旧架构完全做不到的场景**。旧架构里 `fsusb.c` 的
> `NVIC_EnableIRQ(USB_IRQn)` 会清掉 BB/LLE 中断使能位，
> 导致 BLE 协议栈收不到链路层事件、射频永不发包。
> 新架构的 `hal_irq_enable()` 用影子副本彻底解决了这个问题。

## 未完成

### 阶段 5 剩余：HID / MSC / Audio
- `sdk/hal/include/hal_usb.h` — 类驱动框架已定义
- `sdk/hal/src/hal_usb_cdc_common.c` — CDC 类驱动已实现
- `sdk/port/ch59x/hal_usb_ch59x.c` — **端点状态机已实现**（EP0 控制传输 + EP1~3 数据）
- **待实现**：
  - `sdk/hal/src/hal_usb_hid_common.c`（HID 报告描述符 + 中断端点）
  - `sdk/hal/src/hal_usb_msc_common.c`（BOT 协议 + SCSI 命令）
  - `sdk/hal/src/hal_usb_audio_common.c`（等时端点）

**CH592 端点限制**：只有 EP0~EP3（共 4 个）。
- CDC 占 3 个（notify + data in + data out）→ 只剩 1 个给其它类
- 因此 **CDC + HID 复合设备可行**（HID 只需 1 个 IN 端点）
- **CDC + MSC 不可行**（MSC 需 2 个端点）

### 阶段 6：低功耗
- `hal_sys_sleep()` 接口已定义（`sdk/hal/include/hal_sys.h`）
- `sdk/port/ch59x/hal_sys_ch59x.c` 已实现 sleep/standby/shutdown
- **待验证**：RTC 定时唤醒、GPIO 唤醒、BLE 唤醒

## 关键文件说明

| 路径 | 说明 |
|------|------|
| `sdk/hal/include/hal_core.h` | 通用层总入口（**不叫 hal.h**，避免与官方 HAL.h 冲突） |
| `sdk/hal/include/hal_*.h` | 各外设接口定义 |
| `sdk/hal/src/hal_log.c` | 日志核心（自带轻量格式化，不用 newlib） |
| `sdk/hal/src/hal_usb_cdc_common.c` | CDC 类驱动（芯片无关） |
| `sdk/port/ch59x/hal_irq_ch59x.c` | ★ 中断影子副本管理 |
| `sdk/port/ch59x/hal_irq_trampoline_ch59x.S` | ★ 中断入口汇编（完整保存寄存器） |
| `sdk/port/ch59x/hal_ble_ch59x.c` | BLE 实现（官方 HAL + 闭源库粘合） |
| `sdk/vendor/ch59x/` | 官方 EVT 原样拷贝（唯一改动：ISP592.h 加 include guard） |
| `sdk/cmake/chip_ch59x.cmake` | 芯片描述 |
| `sdk/cmake/hal.cmake` | 构建规则 |
| `docs/ARCHITECTURE.md` | 架构设计文档 |

## 已知坑（必读）

见 `docs/ARCHITECTURE.md` 附录 B，共 10 条。最关键的 3 条：

1. **`hal.h` 与官方 `HAL.h` 文件名冲突** → 必须叫 `hal_core.h`
2. **`EEPROM_ERASE` 的 Length 必须是 4096 整数倍** → 否则死循环
3. **newlib `vsnprintf` 拖进 43KB FLASH** → 用自带轻量格式化
