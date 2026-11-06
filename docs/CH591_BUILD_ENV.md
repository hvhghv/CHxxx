# CH591 / CH592 编译环境说明

> 本文档记录 **CH591/CH592（CH59x 系列）用 GCC 15.2 编译官方 WCH 例程**时必须满足的全部配置要求。
> 每一条都经过反汇编逐指令比对验证，缺失任意一条都会导致固件「能烧录、不运行」或「能运行、USB/BLE 不工作」。
>
> 验证基准：`tmp/CH592EVT/EVT/EXAM/BLE/BLE_USB/obj/BLE_USB.hex`（WCH 官方 MounRiver 工具链编译，160224 字节，实测 USB CDC 正常枚举）。

---

## 1. 芯片事实

| 项目 | CH591 | CH592 |
|---|---|---|
| Code Flash | **192 KiB** | 448 KiB |
| Data EEPROM | 32 KiB | 32 KiB |
| RAM | 26 KiB | 26 KiB |
| 内核 | RV32IMAC | RV32IMAC |
| USB 端点 | EP0~EP3（共 4 个） | EP0~EP3（共 4 个） |

**烧录时 `wchisp info` 会打印芯片型号与 Flash 大小，务必以实际输出为准**：

```
Chip: CH591[0x9122] (Code Flash: 192KiB, Data EEPROM: 32KiB)
```

> ⚠️ 固件超过 192 KiB 时，`wchisp flash` 会报 `Verify failed, mismatch`。
> 这不是烧录问题，是固件超出 Flash 容量。

---

## 2. 工具链

### 2.1 当前使用

```
xPack GNU RISC-V Embedded GCC 15.2.0
路径：sdk/prebuilt/xpack-riscv-none-elf-gcc-15.2.0-1/bin/
```

### 2.2 官方工具链

官方 `.cproject` 声明的是 **MounRiver Studio 的 GNU MCU Eclipse RISC-V GCC**（toolchain id `512258282`）。

两者生成的代码有差异，**必须通过编译选项补偿**（见第 4 节）。已验证补偿后固件可正常运行。

### 2.3 工具链路径注意

CMake 在 `project()` 之前 `CMAKE_EXECUTABLE_SUFFIX` 为空，Windows 下必须显式补 `.exe`：

```cmake
if(CMAKE_HOST_WIN32)
    set(CMAKE_C_COMPILER   "${TOOLCHAIN_DIR}/riscv-none-elf-gcc.exe")
    set(CMAKE_ASM_COMPILER "${TOOLCHAIN_DIR}/riscv-none-elf-gcc.exe")
    # ...
endif()
```

---

## 3. 架构参数

### 3.1 编译

```
-march=rv32imac_zicsr_zifencei_zmmul
-mabi=ilp32
```

- `rv32imac`：基础整数 + 乘除 + 原子 + 压缩指令
- `zicsr` / `zifencei`：GCC 12+ 从 `i` 扩展中拆分出来，**必须显式声明**
- `zmmul`：乘法子集（CH59x 内核支持）

### 3.2 链接

```
-march=rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs
```

**链接时必须额外放宽 `zba/zbb/zbc/zbs`** —— 官方 `libCH59xBLE.a` 用 GCC 13+ 编译，内部含位操作扩展指令。
只在链接放宽、编译不放宽，可避免用户代码生成这些指令（CH59x 内核不完整支持）。

---

## 4. 编译选项（缺一不可）

### 4.1 完整列表

```cmake
target_compile_options(<target> PRIVATE
    -Os
    -fmessage-length=0
    -fsigned-char
    -fno-common
    -std=gnu99
    -ffunction-sections
    -fdata-sections
    -fno-optimize-sibling-calls
    -march=rv32imac_zicsr_zifencei_zmmul
    -mabi=ilp32
)
```

### 4.2 每一项的作用与缺失后果

| 选项 | 作用 | 缺失后果 |
|---|---|---|
| `-Os` | 尺寸优化 | 固件超出 192 KiB |
| `-fmessage-length=0` | 不折行诊断信息 | 仅影响日志，无功能影响 |
| **`-fsigned-char`** | `char` 为有符号 | RISC-V GCC 默认 `char` **无符号**，WCH 代码大量用 `char` 做数值，符号性不同会导致协议栈行为异常 |
| `-fno-common` | 禁止未初始化全局变量合并到 common 段 | 重复定义被静默合并，产生难以定位的冲突 |
| `-std=gnu99` | 官方使用 gnu99 | 默认 gnu17 对老代码有兼容风险 |
| `-ffunction-sections` | 每个函数独立段 | `--gc-sections` 无法回收未用函数，固件膨胀 |
| `-fdata-sections` | 每个数据独立段 | 同上 |
| **`-fno-optimize-sibling-calls`** | 禁止尾调用优化 | 见 4.3 |

### 4.3 `-fno-optimize-sibling-calls` 详解（关键）

官方 `app_usb.c` 的中断入口：

```c
__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void USB_IRQHandler(void) { USB_DevTransProcess(); }
```

**官方工具链生成（正确）：**
```asm
auipc ra, ...
jalr  ra, ...        ; 调用，ra 被设为返回地址
mret                 ; 从 MPIE 恢复 MIE
```

**GCC 15.2 默认生成（错误）：**
```asm
auipc t1, ...
jr    -1398(t1)      ; 尾调用，不保存 ra！
```

`USB_DevTransProcess` 结尾是 `ret`（跳到 `ra`）。由于 `jr` 没有设置 `ra`，`ret` 会跳到**垃圾地址**，系统崩溃。

加上 `-fno-optimize-sibling-calls` 后恢复为 `jalr` 调用形式。

---

## 5. 链接选项（缺一不可）

```cmake
target_link_options(<target> PRIVATE
    -march=rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs
    -mabi=ilp32
    -nostartfiles
    --specs=nano.specs
    --specs=nosys.specs
    -T${VENDOR_DIR}/Ld/Link.ld
    -Wl,--gc-sections
    -Wl,--no-warn-rwx-segments
)
```

| 选项 | 作用 | 缺失后果 |
|---|---|---|
| `-nostartfiles` | 不用标准启动文件 | 与自定义 `startup_CH592.S` 冲突 |
| **`--specs=nano.specs`** | 用 newlib-nano | 完整 newlib 的 printf 会拉入约 36 KiB，固件超 192 KiB |
| **`--specs=nosys.specs`** | 提供 syscall 空实现 | **USB 不枚举（见 5.1）** |
| `-Wl,--gc-sections` | 回收未用段 | 固件膨胀 |
| `-Wl,--no-warn-rwx-segments` | 抑制 RWX 段警告 | 仅警告 |

### 5.1 `--specs=nosys.specs` 详解（最隐蔽的坑）

官方 `.cproject` 明确勾选 **"Do not use syscalls (--specs=nosys.specs)"**。

**缺失时会发生什么：**

1. newlib 的 `printf` 依赖 `_write` syscall
2. 没有 `nosys.specs`，链接器从 `libc_nano.a` 拉入默认桩（`readr.o`/`writr.o`）
3. 这些桩内部调用未定义的 `_read`/`_write`，链接器进一步引入 `errno`、`_sbrk` 相关代码
4. **`_sbrk` 初始化异常 → 堆状态损坏**
5. `main()` 里的 `PRINT("%s\n", VER_LIB)` 执行后破坏内存
6. 后续 `app_usb_init()` 的 USB 状态机失效 → **设备插入但永不枚举**

**现象特征：**
- 程序启动标记全部写入（`main` 跑完了）
- USB 寄存器配置正确（`R8_USB_CTRL=0x29`、`UDEV_CTRL=0xA1`、`PIN_ANALOG_IE=0xC0`）
- D+ 已拉高（`UDEV_CTRL` 的 `DP_PIN` 位为 1）
- 但主机收不到设备描述符

**加上 `nosys.specs` 后立即正常枚举。**

---

## 6. 链接脚本要求

### 6.1 段布局（必须与官方一致）

```
.init       VMA 0x00000000  LMA 0x00000000   4 字节       (j handle_reset)
.highcode   VMA 0x20000000  LMA 0x00000004   ~8 KiB       (含 .vector + .vector_handler)
.text       VMA 0x00001FF0  LMA 0x00001FF0   ~150 KiB     (含 handle_reset)
.data       VMA 0x20001FEC  LMA 0x00026AEC
.bss        VMA 0x20002460  —                ~11 KiB
.stack      VMA 0x20006600  —                512 字节
_eusrstack = 0x20006800
```

### 6.2 `.vector` 必须放在 `.highcode` 内部（关键）

```ld
.highcodelalign :
{
    . = ALIGN(4);
    PROVIDE(_highcode_lma = .);      /* = 0x04 */
} >FLASH AT>FLASH

.highcode :
{
    . = ALIGN(4);
    PROVIDE(_highcode_vma_start = .);   /* = 0x20000000 */
    KEEP(*(SORT_NONE(.vector)))         /* ★ 向量表在这里 */
    KEEP(*(SORT_NONE(.vector_handler)))
    *(.highcode);
    *(.highcode.*);
    . = ALIGN(4);
    PROVIDE(_highcode_vma_end = .);
} >RAM AT>FLASH
```

**反汇编证据**（官方 `handle_reset`）：
```asm
li   a0, 4                        ; _highcode_lma = 0x04
auipc a1, 0x1fffe
addi a1, a1, 72                   ; _highcode_vma_start = 0x20000000
auipc a2, 0x20000
addi a2, a2, -32                  ; _highcode_vma_end = 0x20001FA0
bgeu a1, a2, skip                 ; 拷贝 .highcode
...
auipc t0, 0x1fffe
addi t0, t0, -50                  ; t0 = 0x20000000
ori  t0, t0, 3                    ; mtvec = 0x20000003  ★ 向量表在 RAM
csrw mtvec, t0
```

**错误做法：把 `.vector` 拆成独立 Flash 段（`>FLASH AT>FLASH`）。**

后果：`mtvec` 变成 `0x07`（指向 Flash），而 WCH 的 `mtvec` 模式 3 要求向量表在 **RAM**，中断向量全部错位。

### 6.3 `.highcode` 段内顺序

```
*(.vector)          先：向量表
*(.vector_handler)  次：weak 默认中断处理
*(.highcode)        后：真实代码（含 BLE 库中断处理）
*(.highcode.*)
```

---

## 7. 启动文件要求

`startup_CH592.S` 的 `handle_reset` 必须包含以下步骤（顺序不可变）：

```asm
la   gp, __global_pointer$
la   sp, _eusrstack

/* 1. 拷贝 .highcode  Flash -> RAM */
la   a0, _highcode_lma
la   a1, _highcode_vma_start
la   a2, _highcode_vma_end
... 逐字拷贝 ...

/* 2. 拷贝 .data  Flash -> RAM */
/* 3. 清零 .bss */

/* 4. WCH 私有 CSR 配置 */
li   t0, 0x1f
csrw 0xbc0, t0          ; 流水线控制位 & 静态预测控制位

li   t0, 0x3
csrw 0x804, t0          ; 嵌套中断 & 硬件压栈使能

li   t0, 0x88
csrw mstatus, t0        ; MIE=1, MPIE=1

/* 5. 向量表基址 + 模式 3 */
la   t0, _vector_base   ; = 0x20000000（RAM）
ori  t0, t0, 3
csrw mtvec, t0

/* 6. 跳到 main */
la   t0, main
csrw mepc, t0
mret
```

> ⚠️ `csrw 0xbc0` 和 `csrw 0x804` 是 WCH 私有 CSR，**漏掉会导致中断完全不响应**。

---

## 8. 中断处理要求

### 8.1 中断入口必须 `mret` 结尾

见 4.3。GCC 15.2 对 `__attribute__((interrupt("WCH-Interrupt-fast")))` 生成 `ret` 而非 `mret`：

```asm
; GCC 15.2 生成（错误）
addi sp, sp, -16
sw   ra, 12(sp)
auipc ra, ...
jalr ra, ...
lw   ra, 12(sp)
addi sp, sp, 16
ret                     ; ← 不恢复 mstatus，MIE 永久为 0

; 官方工具链生成（正确）
auipc ra, ...
jalr ra, ...
mret                    ; ← 从 MPIE 恢复 MIE
```

**`ret` 的后果：** 第一次中断返回后 `MIE=0`，全局中断永久关闭，后续 USB 中断全部丢失。

**补偿方案：** 用汇编实现中断入口（参考 `sdk/port/ch59x/usb_irq_mret.S`）。

### 8.2 `BB_IRQHandler` 必须指向 `LLE_IRQHandler`

官方 hex 里 IRQ20(BB) 和 IRQ21(LLE) 指向**同一地址**。`libCH59xBLE.a` 只提供 `LLE_IRQHandler`，BB 中断由它统一处理。

若 `BB_IRQHandler` 落到 startup 的 weak 默认处理（`j 1b` 自循环），射频中断一来就卡死。

```asm
; sdk/port/ch59x/ble_irq_alias.S
BB_IRQHandler:
    j LLE_IRQHandler
```

### 8.3 `PFIC->IENR` 是只写寄存器

```c
// 官方 core_riscv.h（赋值，会清掉同字其它位）
PFIC->IENR[n >> 5] = 1 << (n & 0x1F);
```

同一 32 位字覆盖 IRQ0~31。若先使能 BB(20)/LLE(21)，再使能 USB(22)，前两者会被清掉。

**HAL 层用影子副本解决**（`sdk/port/ch59x/hal_irq_ch59x.c`）。

---

## 9. 验证清单

编译后按以下顺序验证，任一项不符即为配置缺失：

### 9.1 体积

```
text + data ≤ 192 KiB（CH591）
```

参考值（官方 BLE_USB）：`text=158444, data=1400`

### 9.2 向量表

```powershell
python tmp/cmp_vec.py <官方hex> <我们的hex>
```

必须满足：

| 项 | 期望 |
|---|---|
| `0x14` magic | `0xF5F9BDA9` |
| `0x1C`~`0x40` | `0x00000000` |
| `0x54` LLE | 非 `0x20000000` 的真实地址 |
| `0x58` USB | 非 `0x20000000` 的真实地址 |

### 9.3 `mtvec`

```powershell
riscv-none-elf-objdump -d <elf> | Select-String "csrw.*mtvec"
```

必须是 `0x20000003`（RAM），不能是 `0x07`（Flash）。

### 9.4 中断入口

```powershell
riscv-none-elf-objdump -d <elf> | Select-String "<USB_IRQHandler>:" -Context 0,8
```

最后一条必须是 `mret`，不能是 `ret`。

### 9.5 烧录

```powershell
wchisp info    # 确认 Chip: CH591, Code Flash: 192KiB
wchisp flash <hex>
```

**必须烧 `.hex`，不能烧 `.bin`** —— `objcopy -O binary` 按 VMA 排序，会丢掉 `.highcode`（VMA 在 RAM）。

---

## 10. 常见故障对照表

| 现象 | 原因 | 修复 |
|---|---|---|
| `Verify failed, mismatch` | 固件超 192 KiB | 加 `--specs=nano.specs`，检查 `--gc-sections` |
| 连 `main()` 都进不去 | `.vector` 被塞进 `.highcode`（VMA 在 RAM），烧录后 0x04 起错位 | 见 6.2 |
| 中断全部不响应 | `mtvec` 指向 Flash | 见 6.2 |
| 中断只响应一次 | 入口用 `ret` 而非 `mret` | 见 8.1 |
| BLE 不广播 | `BB_IRQHandler` 落到死循环 | 见 8.2 |
| **USB 不枚举** | **缺 `--specs=nosys.specs`** | 见 5.1 |
| USB 不枚举（已加 nosys） | `USB_IRQHandler` 尾调用优化 | 见 4.3 |
| EEPROM 连续写 6~7 次后失效 | 写非 256 整数倍，内部读-改-写消耗状态 | 先 `EEPROM_ERASE(addr, 256)` 再写整页 |

---

## 11. 烧录工具

```
tools/wchisp/wchisp.exe
```

| 命令 | 用途 |
|---|---|
| `wchisp info` | 查看芯片型号、Flash 大小、option bytes |
| `wchisp flash <hex>` | 烧录 |
| `wchisp verify <hex>` | 校验 |
| `wchisp eeprom dump <file>` | 读 Data Flash |
| `wchisp config disable-debug` | 关闭调试接口（部分板子必须关闭才能烧录） |

**进入 ISP 模式：** 按住 BOOT 键插 USB，或短接 BOOT 引脚到 GND。
BOOT 引脚由 option byte 决定（`PB22` 或 `PB11`），`wchisp info` 的 `DOWNLOAD_CFG` 字段会显示。

---

## 12. 参考实现

| 文件 | 说明 |
|---|---|
| `sdk/cmake/toolchain-riscv.cmake` | 工具链定义 |
| `sdk/cmake/chip_ch59x.cmake` | 芯片参数 |
| `sdk/cmake/hal.cmake` | 编译/链接选项 |
| `sdk/vendor/ch59x/Ld/Link.ld` | 链接脚本 |
| `sdk/vendor/ch59x/Startup/startup_CH592.S` | 启动文件 |
| `sdk/port/ch59x/usb_irq_mret.S` | USB 中断入口（`mret`） |
| `sdk/port/ch59x/ble_irq_alias.S` | BB → LLE 别名 |
| `main/ble_usb_official/CMakeLists.txt` | 完整对齐官方的构建示例 |
| `tmp/cmp_vec.py` | 向量表对比工具 |
