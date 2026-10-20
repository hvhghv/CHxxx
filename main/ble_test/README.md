# ble_test — 纯 BLE 收发回传测试

验证 CH572 / CH59x 的 **BLE 广播 / 连接 / GATT 收发回传**，
并修复 `usb_dual` 中「蓝牙搜不到 / 连不上」的问题。

**本项目不使用 USB**，上电即广播，收到数据原样通知回去（echo 回环）。

---

## 1. 功能

| 项目 | 说明 |
|---|---|
| 角色 | BLE 从机（Peripheral） |
| 设备名 | `CH572-BLE` |
| 服务 | `0xFFF0` |
| RX 特征 | `0xFFF1`（Write / WriteWithoutResponse）→ 设备 |
| TX 特征 | `0xFFF2`（Notify）→ 手机/浏览器 |
| 行为 | **收到什么就原样通知回去**（回环测试） |
| 启动 | 上电自动广播，无需任何触发 |

---

## 2. 修复的 6 个「蓝牙无法连接」问题

对照 WCH 官方 `Peripheral` 例程（`tmp/CH572EVT/EVT/EXAM/BLE/Peripheral`），
`usb_dual` 缺失了以下步骤，**每一处都会导致连接失败**：

| # | 缺失项 | 后果 |
|---|---|---|
| 1 | `GAPRole_PeripheralInit()` | GAP 外设角色未初始化，`StartDevice` 直接失败 |
| 2 | `TMOS_ProcessEventRegister(NULL)` | 事件无人处理，`START_DEVICE_EVT` 被丢弃 |
| 3 | `GAPROLE_ADVERT_ENABLED = 1` | 广播使能未置位，广播不启动 |
| 4 | `GAPROLE_MIN/MAX_CONN_INTERVAL` | 连接参数未设置，连接建立失败 |
| 5 | `tmos_set_event(BLE_START_DEVICE_EVT)` | 未通过事件延迟启动，协议栈未就绪 |
| 6 | 广播包 `len` 写错（`0x0B` 应为 `0x0A`） | 广播包非法，**搜不到设备** |

### 正确初始化顺序

```c
BLE_LIB_INIT();                        // 1. 协议栈
HAL_Init();                            // 2. HAL
GAPRole_PeripheralInit();              // 3. ★ GAP 外设角色
ble_task_id = TMOS_ProcessEventRegister(ble_process_event);  // 4. ★ 事件处理

GGS_AddService(GATT_ALL_SERVICES);         // 5. GAP 服务
GATTServApp_AddService(GATT_ALL_SERVICES); // 6. GATT 服务
GGS_SetParameter(GGS_DEVICE_NAME_ATT, ...); // 7. 设备名

ble_profile_init();                    // 8. 自定义透传服务

GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, ...);      // 9. ★ 广播使能
GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, ...);   // 10. ★ 连接参数
GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, ...);

GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, 160);          // 11. 广播间隔
GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, 160);

tmos_set_event(ble_task_id, BLE_START_DEVICE_EVT);      // 12. ★ 延迟启动
```

事件处理里才真正启动：

```c
static uint16_t ble_process_event(uint8_t task_id, uint16_t events)
{
    if (events & SYS_EVENT_MSG) {
        uint8_t *pMsg = tmos_msg_receive(task_id);
        if (pMsg) tmos_msg_deallocate(pMsg);
        return (events ^ SYS_EVENT_MSG);
    }
    if (events & BLE_START_DEVICE_EVT) {
        GAPRole_PeripheralStartDevice(task_id, NULL, &ble_role_cb);  // ★
        return (events ^ BLE_START_DEVICE_EVT);
    }
    return 0;
}
```

---

## 3. 广播包格式

BLE 广播是严格 TLV（Type-Length-Value），**长度必须精确**：

```c
/* 广播包：Flags + UUID */
static uint8_t advData[] = {
    0x02, 0x01, 0x06,               /* len=2, type=0x01(Flags), value=0x06 */
    0x03, 0x03, 0xF0, 0xFF,         /* len=3, type=0x03(UUID16), 0xFFF0 */
};

/* 扫描响应包：完整设备名 */
static uint8_t scanRspData[] = {
    0x0A, 0x09, 'C','H','5','7','2','-','B','L','E',  /* len=10 = 1(type)+9(name) */
};
```

> ⚠️ 常见错误：`len` 写成 `0x0B`（多 1 字节）→ 广播包非法 → **搜不到设备**。
> 设备名放 **扫描响应包**，不是广播包。

---

## 4. 编译烧录

```powershell
# CH572
.\build config -DCHIP_SDK=CH572 -DCH572_TARGET=ble_test
.\build
.\build flash          # 按住 PA1 后重新插 USB 进入 ISP

# CH592 / CH591
.\build config -DCHIP_SDK=CH592 -DCH572_TARGET=ble_test
.\build
.\build flash
```

### 资源占用

| 芯片 | FLASH | RAM |
|---|---|---|
| CH572（12KB） | 76,872 B / 240 KB (31.3%) | **12,060 B / 12 KB (98.1%)** ⚠️ |
| CH592X（26KB） | 141,896 B / 448 KB (30.9%) | 13,416 B / 26 KB (50.4%) |

> **CH572 说明**：BLE 库固定占用 `.highcode` 7.5KB + 堆 3KB（库硬性要求 ≥3KB），
> 合计已占 87%。本项目已去掉 USB、精简所有缓冲，98.1% 是物理极限。
> 若需更多余量，建议用 CH592/CH591。

---

## 5. 测试步骤

### 5.1 用浏览器（Web Bluetooth）

Web Bluetooth 要求 **https:// 或 http://localhost**：

```powershell
cd main/ble_test
python -m http.server 8000
```

Chrome / Edge（桌面版）打开：
```
http://localhost:8000/web_ble.html
```

1. 点「连接蓝牙」
2. 选择 **`CH572-BLE`**
3. 在输入框输入文字回车 → **右侧会立刻收到同样的内容**（回环）

### 5.2 用手机 App

任意 BLE 调试 App（如 nRF Connect、LightBlue）：

1. 扫描 → 找到 `CH572-BLE` → 连接
2. 找到服务 `0xFFF0`
3. 订阅 `0xFFF2` 的通知（点通知图标）
4. 向 `0xFFF1` 写入数据
5. 立刻会在 `0xFFF2` 收到相同数据

---

## 6. 验证要点

| 现象 | 说明 |
|---|---|
| 能搜到 `CH572-BLE` | ✅ 广播正常 |
| 能连接成功 | ✅ GAP 参数正确 |
| 订阅通知后写入有回传 | ✅ GATT 服务正常 |
| 断开后能重新搜到 | ✅ 自动恢复广播 |

---

## 7. 文件说明

| 文件 | 说明 |
|---|---|
| `ble_test.c` | 主程序：BLE 初始化 + 回环逻辑 |
| `ble_profile.c/h` | GATT 透传服务（0xFFF0） |
| `funconfig.h` | 时钟配置 |
| `web_ble.html` | Web Bluetooth 测试页面 |
| `CMakeLists.txt` | 构建配置（含按芯片的 BLE 堆设置） |

---

## 8. 与 usb_dual 的关系

`usb_dual` 同时跑 BLE + 多路 USB CDC，在 CH572（12KB RAM）上
BLE 库已占 94%，无法容纳 USB 栈。

本项目**去掉 USB**，专门验证 BLE，可作为 `usb_dual` 的 BLE 部分参考实现。
