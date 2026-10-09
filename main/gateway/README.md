# CH591/CH592 多功能网关

裸机（无 FreeRTOS）多功能网关固件，集成 **USB（4×CDC + RNDIS）**、**LWIP 网络**、
**HTTP 服务器 + Web UI**、**BLE（动态 GATT）**、**DHCP 服务器**、**帧协议**、
**引脚复用**、**SWIO 烧录** 于一体。

目标芯片：**CH592**（448KB Flash，BLE 双模式）或 **CH591**（192KB Flash，BLE 仅从机）。

---

## 功能总览

| 功能 | 说明 |
|---|---|
| **USB 复合设备** | 4×CDC-ACM（终端 + 3×UART 转发）+ 1×RNDIS（虚拟网卡） |
| **LWIP 网络** | NO_SYS=1 raw API，静态 IP `192.168.7.1/24` |
| **DHCP 服务器** | RNDIS 主机自动获取 IP（可配置，**不派发网关**） |
| **HTTP 服务器** | REST API + 内嵌单页 Web UI（约 3.5KB） |
| **BLE** | GAP + 动态 GATT 服务注册（应用芯片通过帧命令注册 UUID） |
| **帧协议** | 8 字节头 + CRC16，UART0-3 / SPI 通道 |
| **引脚复用** | 40 引脚（PA0-15, PB0-23），9 种功能，可保存到 Flash |
| **SWIO 烧录** | 单线协议烧录 CH32V003 |
| **网络路由** | IP 包 → 串口/SPI 帧转发 |

---

## 目录结构

```
main/gateway/
├── gateway.c          主程序（USB 回调 + 主循环 + 终端命令）
├── usb_config.h       USB 描述符（4×CDC + RNDIS）
├── funconfig.h        芯片配置（60MHz PLL）
├── pinmux.c/h         引脚复用管理
├── config.c/h         Flash 配置存储（magic + CRC32）
├── frame.c/h          帧协议抽象层（可插拔 codec）
├── frame_wire.c       帧格式实现（8B 头 + CRC16）
├── framelink.c/h      帧链路（字节流同步 + 分发）
├── framehdl.c/h       帧命令处理器（GPIO/ADC/PWM/UART/NET）
├── netroute.c/h       网络路由（IP 包 ↔ 帧转发）
├── periph.c/h         外设驱动（PWM/ADC/UART/SPI）
├── httpd.c/h          HTTP 服务器 + REST API
├── webui.h            内嵌单页 Web UI（#include 生成的头文件）
├── webui.html         Web UI 源（纯 HTML，可浏览器预览）
├── tools/html2h.py    HTML → C 头文件转换脚本（构建时运行）
├── dhcps.c/h          极简 DHCP 服务器
├── swio.c/h           SWIO 烧录 CH32V003
├── bleapp.c/h         BLE 应用（GAP + 动态 GATT）
├── blemgr.c/h         BLE 帧命令管理器
├── ble_proto.h        BLE 帧命令协议定义
└── util.c             atoi（-nostdlib 无 libc）
```

---

## 构建

```bash
# CH592（BLE 双模式）
cmake -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM=ninja \
  -DCMAKE_TOOLCHAIN_FILE=sdk/common/cmake/riscv.cmake \
  -DCHIP_SDK=CH592
cmake --build build --target gateway

# CH591（自动降为 BLE 仅从机）
cmake -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM=ninja \
  -DCMAKE_TOOLCHAIN_FILE=sdk/common/cmake/riscv.cmake \
  -DCHIP_SDK=CH591
cmake --build build --target gateway
```

### 资源占用

| 芯片 | FLASH | RAM | BLE 模式 |
|---|---|---|---|
| **CH592** | 226920 B / 448 KB (49.46%) | 26016 B / 26 KB (97.72%) | dual（双模式） |
| **CH591** | 195576 B / 192 KB (99.48%) | 25472 B / 26 KB (95.67%) | perf（仅从机） |

### 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `CHIP_SDK` | CH572 | 目标芯片（CH591/CH592） |
| `BLE_ROLE` | dual | BLE 角色：`perf`（仅从机）/`cent`（仅主机）/`dual`（双模式） |
| `BLE_UUID128` | OFF | 支持 128bit UUID（开启后帧负载上限 128，关闭时 48，省 RAM） |
| `GW_CDC_COUNT` | 4 | CDC-ACM 数量：**3**（终端+2 UART，CH591）/ **4**（终端+3 UART，CH592） |

> CH591 会自动将 `BLE_ROLE` 从 `dual` 降为 `perf`、`GW_CDC_COUNT` 从 4 降为 3（资源限制）。

---

## 快速上手

1. **烧录**固件到 CH592/CH591
2. 用 USB 连接电脑，出现：
   - **CH592**：4 个串口（CDC-A 终端 / CDC-B/C/D = UART0/1/2 转发）
   - **CH591**：3 个串口（CDC-A 终端 / CDC-B/C = UART0/1 转发）
   - 1 个网卡（RNDIS）

> CDC ↔ UART 为**固定映射**（无动态绑定）：CDC-B→UART0、CDC-C→UART1、CDC-D→UART2（仅 CH592）。
3. 打开串口 A（115200），输入 `help` 查看命令
4. 浏览器访问 **`http://192.168.7.1/`** 使用 Web UI

---

## 终端命令（CDC-A）

| 命令 | 功能 |
|---|---|
| `help` | 显示帮助 |
| `info` | 系统信息（芯片/时钟） |
| `net` | 网络信息（IP/RNDIS 状态） |
| `pins` | 列出引脚复用 |
| `gpio N` | 读 GPIO N 电平 |
| `set N V` | 设置 GPIO N = V |
| `adc N` | 读 ADC 通道 N（0-13） |
| `save` / `load` | 保存/加载配置到 Flash |
| `default` | 恢复默认配置 |
| `frame U T` | 在 UART U 发送 type T 的测试帧 |
| `spi master [div]` / `spi slave` | 配置 SPI 主/从模式 |
| `uart N [mode forward\|frame\|baud B\|parity none\|odd\|even\|stop 1\|2]` | 查看/配置 UART N（**仅 CH592**，CH591 Flash 受限时省略） |
| `swio N` | 设置 SWIO 烧录引脚 |
| `swiohs` | SWIO 握手（读 DMCFGR） |
| `swiochip` | SWIO 读目标芯片 ID |
| `swioreset` | SWIO 复位目标芯片 |
| `swioflash <addr> <len>` | SWIO 流式烧录：执行后终端口进入二进制接收模式，随后发送 `len` 字节固件数据（addr 支持 `0x` 十六进制或十进制） |
| `ble [central\|peripheral\|scan\|list\|conn N\|disc\|send X]` | BLE 控制 |
| `blef reg` | 测试注册 BLE 服务 |
| `dhcp [ip mask]` | 查看/设置 DHCP 派发 IP 与掩码 |
| `webui [url]` | 查看/设置前端 URL |
| `echo X` | 回显 X |

---

## HTTP REST API

| 端点 | 功能 |
|---|---|
| `GET /` | Web UI 首页 |
| `GET /api/info` | 系统信息 |
| `GET /api/pins` | 引脚复用列表 |
| `GET /api/gpio?pin=N` | 读 GPIO |
| `GET/POST /api/gpio?pin=N&val=V` | 写 GPIO |
| `GET /api/adc?ch=N` | 读 ADC |
| `POST /api/save` / `load` | 保存/加载配置 |
| `GET /api/routes` | 路由表 |
| `POST /api/route/add?dest=X&mask=Y&ch=Z` | 添加路由 |
| `POST /api/route/del?dest=X&mask=Y` | 删除路由 |
| `GET /api/swio` | SWIO 烧录进度 |
| `POST /api/swio?pin=N` | 设置 SWIO 引脚 |
| `GET /api/swio/handshake` | SWIO 握手 |
| `POST /api/swio/flash?addr=X` | **HTTP 固件上传**（body=原始 .bin，Content-Length 指定长度，流式烧录） |
| `GET /api/ble` | BLE 状态 |
| `GET /api/dhcp` | DHCP 状态 |
| `POST /api/dhcp?ip=X&mask=Y` | 设置 DHCP 派发 IP 与掩码 |
| `GET /api/webui` | 查看前端 URL |
| `POST /api/webui?url=X` | 设置前端 URL（GitHub Pages 等） |

---

## 帧协议

用于芯片间通信（UART0-3 / SPI），也可承载 BLE 命令。

### 帧格式（8 字节头 + 负载）

| 偏移 | 字段 |
|---|---|
| 0-1 | magic（`0x55AA` 小端） |
| 2[0-4] | version（固定 `0x04`） |
| 2[5] | crc_range（1=校验负载，0=仅头部） |
| 2[6-7] | reserved（必须 0） |
| 3 | type |
| 4-5 | payload_length（uint16 小端） |
| 6-7 | crc16（多项式 `0x1021`，小端） |
| 8-(8+n) | payload |

### 帧类型

| type | 名称 | 说明 |
|---|---|---|
| `0x00` | PING | 心跳/探测 |
| `0x10` | GPIO_READ | 读 GPIO |
| `0x11` | GPIO_WRITE | 写 GPIO |
| `0x20` | ADC_READ | 读 ADC |
| `0x30-0x32` | PWM_SET/START/STOP | PWM 控制 |
| `0x40` | UART_CFG | 配置串口 |
| `0x50` | NET_FWD | 网络帧转发 |
| `0x60-0x63` | BLE_REG_SVC/UNREG/SEND/LINK_ACK | BLE 命令 |
| `0x70-0x74` | BLE_LINK_UP/DOWN/NOTIFY_EN/DIS/DATA_RX | BLE 事件 |

> 响应帧 type = 请求 type | `0x80`。

---

## BLE 帧命令框架

**架构**：CH592 作为 **BLE 物理层**，应用芯片通过帧命令注册 GATT 服务/特征。

```
应用芯片 ──帧命令──→ CH592 ──BLE──→ 手机
         ←─事件───          ←────
```

- 应用芯片注册服务 UUID + 特征 UUID（**UUID 独占**）
- 连接/数据事件通过帧命令回推
- 数据透传：手机 ↔ 应用芯片
- 逻辑连接号（`logical_id`）单调递增，永不复用
- 连接事件模式：`EAGER`（连接即通知）/ `LAZY`（首次写入才通知）

---

## 引脚复用

- **引脚编号**：`0-15` = PA0-PA15，`16-39` = PB0-PB23
- **功能类型**：NONE / GPIO_IN / GPIO_OUT / UART_TX / UART_RX / PWM / ADC / SPI / I2C / SWIO
- **持久化**：`save` 存到 Flash 末扇区（CH591 `0x2F000`，CH592 `0x6F000`）

---

## 硬件资源

| 资源 | CH591 | CH592 |
|---|---|---|
| RAM | 26 KB | 26 KB |
| Flash | 192 KB | 448 KB |
| UART | 4 | 4 |
| GPIO | 24 | 24 |
| ADC | 6 通道 | 14 通道 |
| PWM | 8 路（PWM4-11） | 8 路（PWM4-11） |
| BLE | 仅从机 | 从机 + 主机 |

---

## HTTP 固件上传（SWIO 烧录 CH32V003）

WebUI 的 **SWIO** 卡片提供固件上传：选择 `.bin` 文件 + 目标地址 → 点击 **Flash**。

**流程**（流式，无需缓存整个固件）：
1. `POST /api/swio/flash?addr=0x08000000`，body 为原始固件二进制
2. 服务器解析 `Content-Length` → 握手 + 读芯片 ID + 解锁 + **擦除目标全部页**
3. 边接收 TCP 数据边按 64 字节页 SWIO 写入
4. 收完 → 返回 `OK flashed`（失败返回 `ERR flash r=<code>`）

**命令行上传**（curl）：
```
curl -X POST --data-binary @app.bin "http://192.168.7.1/api/swio/flash?addr=0x08000000"
```

**开关**：强制启用（无 CMake 选项）

### 编辑 Web UI

Web UI 源文件为 `webui.html`（**纯 HTML**，可直接浏览器打开预览）。
构建时由 `tools/html2h.py` 自动转成 `webui_html.h`（C 字符串数组）并编译进固件。
修改 `webui.html` 后重新编译即可生效。

> 注意：`webui.html` 中 **不能用 JS 的 `//` 行注释**（转换脚本会去除换行，
> `//` 会吞掉后续代码）；如需注释请用 `/* */` 块注释。

---

## 外部前端（GitHub Pages）

前端可托管在 GitHub Pages 等外部地址，固件只返回一个含 iframe 的小页面，
这样前端可随意编写（不占固件 Flash）。

**配置前端 URL**：
```
webui https://user.github.io/ch59x-gw/          # 终端
POST /api/webui?url=https://user.github.io/ch59x-gw/   # API
```

- 配置后，访问 `http://192.168.7.1/` 会返回 `<iframe src="配置的URL">`
- 未配置时：CH592 用内嵌 UI（fallback）；CH591 显示提示
- **CORS**：所有 API 响应带 `Access-Control-Allow-Origin: *`，
  支持外部前端跨域调用；`OPTIONS` 预检返回 204

**开关**：`GW_EMBED_WEBUI`（CMake 选项）
- **CH592**：默认开启（保留内嵌 UI 作 fallback）
- **CH591**：默认关闭（省 ~2.5KB Flash，仅用外部前端）

---

## 注意事项

- **RAM 紧张**：两芯片 RAM 均 >94%，加功能需先精简
- **BLE 内存堆**：`BLE_MEMHEAP_SIZE` 必须 ≥ 4096（否则死机）
- **SWIO 协议**：完整 D-code 调试模块协议（MSB-first + 预充电 + RISC-V 指令注入），
  时序参数 `SWIO_T1_COEFF`（默认 8）需用真实 CH32V003 实测校准
- **HTTP 上传**：强制启用；CH591 Flash 紧张（99.6%），需注意余量
- **Web UI**：源文件 `webui.html`（纯 HTML），构建时转 C 头文件；禁用 `//` 行注释
- **帧负载上限**：`FRAMELINK_MAX_PAYLOAD`（默认 128，RAM 紧张可降到 48）
