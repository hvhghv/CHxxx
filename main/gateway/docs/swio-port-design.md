# CH32V003 SWIO 主机移植设计方案（CH592/CH591）

> 目标：让 CH592/CH591 网关作为 **SWIO 主机**，直接烧录/调试 CH32V003 目标芯片。
> 本文档只做设计，不含最终代码。

---

## 1. 背景与现状

### 1.1 当前实现的问题

`main/gateway/swio.c`（~380 行）实现的是**错误的简化协议**：

| 项 | 当前实现 | 权威协议（cnlohr `bitbang_rvswdio.h`） |
|---|---|---|
| 位序 | LSB-first ❌ | **MSB-first** ✅ |
| 命令格式 | 命令字节 + 32 位地址 + 数据 ❌ | 起始位 + **7 位寄存器号** + 起始位 + 32 位数据 ✅ |
| 访问对象 | 32 位内存地址 ❌ | **7 位 D-code 调试寄存器** ✅ |
| 接收机制 | 拉低→释放→采样 ❌ | 拉低→**预充电**→释放→采样 ✅ |
| Flash 访问 | 直接内存映射 ❌ | **通过调试模块让目标 CPU 执行 RISC-V 指令** ✅ |

**结论：`swio.c` 必须整体重写。**

### 1.2 权威参考

| 来源 | 许可 | 用途 |
|---|---|---|
| cnlohr `bitbang_rvswdio.h` | MIT/NewBSD（可商用） | 完整协议参考 |
| Ardulink `swio.c`（AVR 16MHz） | MIT | 位时序注释参考 |
| perigoso/sigrok-rvswd | - | 逻辑分析仪解码器 |

---

## 2. SWIO 协议本质

SWIO（WCH 称 SDI）是 **单线半双工的 RISC-V 调试模块（D-code）访问协议**。

**它不是内存总线**——它访问的是 7 位**调试寄存器**（0x00~0x7f）。
要读写目标内存/Flash，必须：

1. 把 RISC-V 指令（如 `lb x8,0(x8); c.ebreak`）写入 `DMPROGBUF0..7`
2. 用 `DMCOMMAND` 让目标 CPU **执行**这些指令
3. 从 `BDMDATA0/1` 读结果

### 2.1 位时序（T = 1/8MHz = 125ns，AVR 参考）

```
send_1:  拉低 2T → 释放高阻          （短）
send_0:  拉低 8T → 释放高阻          （长）
recv:    拉低2T → 预充电(输出高) → 释放 → 等4T → 采样(第6T) → 等线恢复
```

### 2.2 命令格式（MSB-first）

```
写: 起始位(1) + 寄存器号7位(MSB-first) + 起始位(1) + 数据32位(MSB-first) + 8μs
读: 起始位(1) + 寄存器号7位(MSB-first) + 起始位(0) + 读32位(MSB-first) + 8μs
```

### 2.3 D-code 调试寄存器表

| 寄存器 | 地址 | 说明 |
|---|---|---|
| BDMDATA0 | 0x04 | 数据 0（读结果/写参数） |
| BDMDATA1 | 0x05 | 数据 1 |
| DMCONTROL | 0x10 | 调试控制 |
| DMSTATUS | 0x11 | 调试状态 |
| DMHARTINFO | 0x12 | Hart 信息 |
| DMABSTRACTCS | 0x16 | 抽象命令状态 |
| DMCOMMAND | 0x17 | 抽象命令（执行/搬运） |
| DMABSTRACTAUTO | 0x18 | 自动执行 |
| DMPROGBUF0..7 | 0x20..0x27 | 程序缓冲（RISC-V 指令） |
| DMCPBR | 0x7C | 能力寄存器 |
| DMCFGR | 0x7D | 配置寄存器 |
| DMSHDWCFGR | 0x7E | 影子配置寄存器 |

---

## 3. CH592/CH591 主机硬件能力评估

### 3.1 能力对比

| 能力 | AVR 16MHz（参考） | CH592 @60MHz | 评估 |
|---|---|---|---|
| GPIO 翻转 | ~1 周期 | ~1-2 周期（SET/CLR 单周期写） | ✅ |
| 位时隙 T | 125ns | 可任意设定 | ✅ 更灵活 |
| NOP 精度 | 62.5ns | 16.7ns | ✅ 更精细 |
| 关中断忙等 | 支持 | 支持 | ✅ |
| **周期级计时** | 无 | **64 位 SysTick** | ✅✅ 优于 AVR |
| 单线开漏 | DDR 切换 | **无 OD 模式，需切方向** | ⚠️ 见 3.3 |

**结论：CH592 硬件能力充足，甚至优于 AVR。**

### 3.2 关键资源：64 位 SysTick

CH592 的 `SysTick->CNT` 是 **64 位**（见 `ch5xxhw.h`）：

```c
#define funSysTick32() (SysTick->CNTL)
#define funSysTickHigh() (SysTick->CNTH)
// 或直接 SysTick->CNT (uint64_t)
```

- CTLR bit2 `STCLK`：0 = HCLK，1 = STCLK
- 60MHz 下每个 tick = 16.7ns → **远优于 AVR 的 NOP 计数**

**用途**：做周期级精确延时，替代 NOP 计数，时序更稳定可移植。

### 3.3 ⚠️ 关键约束：CH5xx 无开漏模式

`funPinMode()` 在 CH5xx 上只支持：

```c
GPIO_ModeIN_Floating   // 高阻输入
GPIO_ModeIN_PU         // 上拉输入
GPIO_ModeIN_PD         // 下拉输入
GPIO_ModeOut_PP_5mA    // 推挽输出 5mA
GPIO_ModeOut_PP_20mA   // 推挽输出 20mA
```

**没有 `GPIO_ModeOut_OD`（开漏）！**

→ 必须用**切方向模型**模拟开漏：

| 动作 | 实现 |
|---|---|
| 拉低（输出 0） | `DIR=1`（输出）+ `OUT=0` |
| 释放（高阻，靠外部上拉拉高） | `DIR=0`（输入）+ `PU=1` |
| 预充电（输出高） | `DIR=1`（输出）+ `OUT=1`（短暂） |
| 读线 | `R32_PA_PIN & bit` |

**注意**：切方向涉及写 `R32_PA_DIR`，比 AVR 的 DDR 慢，需纳入时序标定。

### 3.4 快速 GPIO 访问宏（CH5xx）

```c
#define GPIO_SetBits(pin)     (*(&R32_PA_SET + OFFSET_FOR_GPIOB(pin)) = (1<<(pin & ~PB)))
#define GPIO_ResetBits(pin)   (*(&R32_PA_CLR + OFFSET_FOR_GPIOB(pin)) = (1<<(pin & ~PB)))
#define GPIO_ReadPortPin(pin) (*(&R32_PA_PIN + OFFSET_FOR_GPIOB(pin)) &  (1<<(pin & ~PB)))
```

- `SET`/`CLR` 是**单周期写**，比读-改-写快
- GPIOB 偏移 0x20（`OFFSET_FOR_GPIOB`）
- 直接操作寄存器（绕过 `funDigitalWrite`）以获得最小延时

### 3.5 ⚠️ 引脚冲突风险

CH592 自身用 SWIO 引脚做调试/烧录。作为 SWIO 主机时：
- 必须确认所选引脚**不是** CH592 自己的调试口
- 必须确认 pinmux 可将该引脚配为普通 GPIO
- 需外部 **1kΩ 串联电阻**（防止总线冲突过流，参考 Ardulink）

---

## 4. 移植架构设计

### 4.1 文件结构

```
main/gateway/
├── swio.c          # 重写：位层 + D-code 协议 + Flash 编程
├── swio.h          # 接口（保持现有函数签名兼容）
└── swio_port.h     # 新增：主机平台抽象（GPIO/延时）
```

### 4.2 分层设计

```
┌─────────────────────────────────────┐
│  应用层：swio_flash_program()        │  HTTP 上传 → 烧录 → 校验
├─────────────────────────────────────┤
│  Flash 层：Unlock/Erase/Write/Verify │  通过 DM 执行 RISC-V 指令
├─────────────────────────────────────┤
│  协议层：MCFWriteReg32/MCFReadReg32  │  7 位 D-code 寄存器访问
├─────────────────────────────────────┤
│  位层：Send1/Send0/ReadBit           │  MSB-first + 预充电
├─────────────────────────────────────┤
│  平台层：swio_port（GPIO + 延时）    │  CH592 专有
└─────────────────────────────────────┘
```

### 4.3 平台抽象层（swio_port.h）

```c
/* 引脚配置（pinmux 编号） */
typedef struct {
    uint32_t chfun;      /* ch32fun 引脚宏（PA0..PB15） */
    uint32_t pinmask;    /* 位掩码 */
    volatile uint32_t *dir;
    volatile uint32_t *out;    /* R32_PA_OUT */
    volatile uint32_t *set;    /* R32_PA_SET */
    volatile uint32_t *clr;    /* R32_PA_CLR */
    volatile uint32_t *pu;
    volatile uint32_t *pin;    /* R32_PA_PIN */
} swio_port_t;

/* 平台原语（内联，最小延时） */
static inline void port_drive_low(swio_port_t *p);      /* DIR=1, OUT=0 */
static inline void port_release(swio_port_t *p);        /* DIR=0, PU=1 */
static inline void port_drive_high(swio_port_t *p);     /* DIR=1, OUT=1（预充电） */
static inline int  port_read(swio_port_t *p);           /* PIN & bit */

/* 周期级延时（基于 64 位 SysTick） */
static inline void port_delay_cycles(uint32_t cycles);
```

**延时实现**：

```c
static inline void port_delay_cycles(uint32_t cycles) {
    uint64_t start = SysTick->CNT;
    while ((SysTick->CNT - start) < cycles) { /* busy wait */ }
}
```

### 4.4 位层（照搬 bitbang_rvswdio.h 逻辑）

```c
/* t1coeff = 一个"标称位时隙"的周期数（需标定） */
static void send_1(swio_port_t *p, int t1coeff) {
    port_drive_low(p);
    port_delay_cycles(t1coeff);       /* 短拉低 */
    port_release(p);
    port_delay_cycles(t1coeff);
}

static void send_0(swio_port_t *p, int t1coeff) {
    port_drive_low(p);
    port_delay_cycles(t1coeff * 4);   /* 长拉低 */
    port_release(p);
    port_delay_cycles(t1coeff);
}

/* 返回 0/1，超时返回 2 */
static int read_bit(swio_port_t *p, int t1coeff) {
    int ret;
    port_drive_low(p);
    port_delay_cycles(t1coeff);
    port_release(p);
    port_drive_high(p);               /* 预充电（总线冲突检测） */
    port_delay_cycles(t1coeff);       /* 等 2× */
    ret = port_read(p);
    /* 等待线恢复高（若被目标拉低） */
    for (int t = 0; t < MAX_IN_TIMEOUT; t++)
        if (port_read(p)) return ret;
    return 2;  /* timeout */
}
```

### 4.5 协议层（MCFRead/WriteReg32）

```c
static void MCFWriteReg32(swio_port_t *p, int t1, uint8_t cmd, uint32_t val) {
    disable_irq();
    send_1(p, t1);                       /* 起始位 */
    for (uint32_t m = 1<<6; m; m >>= 1)  /* 7 位命令，MSB-first */
        (cmd & m) ? send_1(p, t1) : send_0(p, t1);
    send_1(p, t1);                       /* 起始位 */
    for (uint32_t m = 1<<31; m; m >>= 1) /* 32 位数据，MSB-first */
        (val & m) ? send_1(p, t1) : send_0(p, t1);
    enable_irq();
    delay_us(8);
}

static int MCFReadReg32(swio_port_t *p, int t1, uint8_t cmd, uint32_t *val) {
    uint32_t r = 0;
    disable_irq();
    send_1(p, t1);                       /* 起始位 */
    for (uint32_t m = 1<<6; m; m >>= 1)  /* 7 位命令 */
        (cmd & m) ? send_1(p, t1) : send_0(p, t1);
    send_0(p, t1);                       /* 起始位=0 → 读 */
    for (int i = 0; i < 32; i++) {       /* 读 32 位 */
        r <<= 1;
        int b = read_bit(p, t1);
        if (b == 1) r |= 1;
        if (b == 2) { enable_irq(); return -21; }  /* 超时 */
    }
    *val = r;
    enable_irq();
    delay_us(8);
    return 0;
}
```

### 4.6 初始化与握手

```c
int swio_init(swio_port_t *p) {
    /* 1. 设置引脚为输入上拉 */
    port_release(p);
    /* 2. 尝试 SWIO 握手 */
    MCFWriteReg32(p, t1, DMSHDWCFGR, 0x5aa50000 | (1<<10));
    MCFWriteReg32(p, t1, DMCFGR,     0x5aa50000 | (1<<10));
    MCFWriteReg32(p, t1, DMSHDWCFGR, 0x5aa50000 | (1<<10));  /* try twice */
    MCFWriteReg32(p, t1, DMCFGR,     0x5aa50000 | (1<<10));
    MCFWriteReg32(p, t1, DMCONTROL,  0x00000001);
    MCFWriteReg32(p, t1, DMCONTROL,  0x00000001);
    /* 3. 读回验证 */
    uint32_t v;
    if (MCFReadReg32(p, t1, DMCFGR, &v) == 0 && (v & 0xffff0000) == 0x5aa50000)
        return SWIO_OK;   /* 找到 RVSWIO 接口 */
    return SWIO_ERR_HANDSHAKE;
}
```

### 4.7 芯片识别（RISC-V 指令注入）

```c
int swio_read_chip_id(swio_port_t *p, uint32_t *chip_id) {
    /* 读 0x7f 寄存器 */
    MCFReadReg32(p, t1, 0x7f, &sevenf_id);
    if (sevenf_id == 0) {
        /* 写 RISC-V 指令：lb x8, 0(x8); c.ebreak */
        MCFWriteReg32(p, t1, DMPROGBUF0, 0x00040403);  /* lb x8, 0(x8) */
        MCFWriteReg32(p, t1, DMPROGBUF1, 0x00100073);  /* c.ebreak */
        MCFWriteReg32(p, t1, BDMDATA0, 0x40001041);    /* CH32V003 ID 地址 */
        MCFWriteReg32(p, t1, DMCOMMAND, 0x00271008);   /* copy to x8 + execute */
        wait_for_done(p, t1);
        MCFWriteReg32(p, t1, DMCOMMAND, 0x00221008);   /* copy from x8 */
        MCFReadReg32(p, t1, BDMDATA0, chip_id);
        *chip_id &= 0xff;
    }
    /* 校验：CH32V003 应为 0x0030 系列 */
    return SWIO_OK;
}
```

### 4.8 Flash 编程（通过 DM 执行指令）

CH32V003 Flash 寄存器（**通过目标 CPU 的 load/store 访问**）：

| 寄存器 | 地址 |
|---|---|
| FLASH->KEYR | 0x40022004 |
| FLASH->OBKEYR | 0x40022008 |
| FLASH->STATR | 0x4002200C |
| FLASH->CTLR | 0x40022010 |
| FLASH->ADDR | 0x40022014 |
| FLASH->MODEKEYR | 0x40022024 |

**解锁**：写 KEYR = 0x45670123, 0xCDEF89AB（OBKEYR/MODEKEYR 同样）

**擦除**（页 64 字节）：
```
CTLR = CR_PAGE_ER(0x00020000)
ADDR = 页地址
CTLR = CR_STRT_Set(0x40) | CR_PAGE_ER
等 STATR.BSY 清零
CTLR = 0
```

**编程**（V003 需每字 bufload）：
```
CTLR = CR_PAGE_PG(0x00010000) | CR_BUF_RST(0x00080000)
循环写 CTLR = CR_PAGE_PG | CR_BUF_LOAD(0x00040000) + 数据
CTLR = CR_PAGE_PG | CR_STRT_Set
```

> ⚠️ 关键：所有 Flash 访问都通过 **`WriteWord`/`ReadWord`**（即 DM 执行 RISC-V 指令）完成，
> 不是直接内存映射。这是与当前错误实现的最大区别。

---

## 5. 时序标定方法

### 5.1 标定目标

`t1coeff` = 一个"标称位时隙"对应的 **CPU 周期数**。

AVR 版：T=125ns，t1coeff 对应约 2 个 NOP（16MHz）。
CH592 @60MHz：需实测。

### 5.2 标定步骤

1. 用 `SysTick->CNT` 测量一次 `port_drive_low + port_release` 的**实际周期开销**（含 GPIO 方向切换）
2. 目标位时隙：参考 AVR 的 T=125ns → CH592 约 **7.5 周期**
   - `send_1` 拉低 ≈ 15 周期，`send_0` 拉低 ≈ 60 周期
3. 用逻辑分析仪（或 CH32V003 目标）验证
4. 微调 `t1coeff` 直到握手稳定

### 5.3 建议的初始值

```c
/* 60MHz 下的初始估计（需实测调整） */
#define SWIO_T1_COEFF   8      /* 标称位时隙 ≈ 133ns */
#define SWIO_MAX_TIMEOUT 1000  /* 读超时周期数 */
```

---

## 6. 分阶段实施计划

### 阶段 A：平台层 + 位层（验证硬件）

- 实现 `swio_port.h`（GPIO 抽象 + SysTick 延时）
- 实现 `send_1/send_0/read_bit`
- **验证**：用逻辑分析仪观察波形是否符合预期

### 阶段 B：协议层 + 握手（验证协议）

- 实现 `MCFWriteReg32/MCFReadReg32`
- 实现 `swio_init` 握手（DMCFGR = 0x5aa5xxxx）
- **验证**：能否握手成功（读到 0x5aa50000）

### 阶段 C：芯片识别（验证 RISC-V 注入）

- 实现 `swio_read_chip_id`
- **验证**：能否读到 CH32V003 的 chip ID

### 阶段 D：Flash 编程（完整功能）

- 实现 Unlock/Erase/Write/Verify
- 接入现有 HTTP 上传流程
- **验证**：烧录一个 blink 固件并运行

### 阶段 E：可选功能

- 终端 printf 轮询（`PollTerminal`）
- GDB 支持（暂不做）

---

## 7. 风险与缓解

| 风险 | 等级 | 缓解 |
|---|---|---|
| 时序标定不准 | 高 | 用 SysTick 精确延时 + 逻辑分析仪验证 |
| 无 OD 模式导致波形畸变 | 中 | 切方向模型 + 外部 1kΩ 电阻 |
| 引脚冲突（CH592 调试口） | 中 | 选非调试引脚 + pinmux 验证 |
| 无真实目标无法验证 | 高 | 分阶段：先握手，再 ID，最后 Flash |
| RISC-V 指令注入复杂 | 高 | 严格照搬 bitbang_rvswdio.h |
| 中断干扰时序 | 中 | 关键段 `__disable_irq()` |

---

## 8. 与现有代码的接口

保持 `swio.h` 现有函数签名，内部重写：

```c
/* 现有接口（保留） */
void swio_set_pin(int pin);
int  swio_handshake(uint32_t *chip_id);
int  swio_flash_program(uint32_t addr, const uint8_t *data, uint32_t len);
int  swio_progress(void);

/* 内部新增 */
static int  swio_dm_init(void);
static int  swio_dm_write_reg(uint8_t reg, uint32_t val);
static int  swio_dm_read_reg(uint8_t reg, uint32_t *val);
static int  swio_dm_read_word(uint32_t addr, uint32_t *val);
static int  swio_dm_write_word(uint32_t addr, uint32_t val);
```

---

## 9. 总结

| 维度 | 结论 |
|---|---|
| **硬件可行性** | ✅ CH592 @60MHz + 64 位 SysTick，能力充足 |
| **协议适用性** | ✅ 协议是目标侧定义，与主机无关 |
| **移植难度** | ⚠️ 中等偏高（~800-1000 行，需重写） |
| **最大风险** | ⚠️ 时序标定 + 引脚冲突 + 无硬件验证 |
| **建议** | 分阶段实施，先握手验证硬件，再逐步完整 |

**核心变更**：从"简化内存访问协议"改为"完整 D-code 调试模块协议"。
