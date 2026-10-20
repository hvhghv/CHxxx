# ble_test - 纯 BLE 收发回传测试
# ---------------------------------------------------------------------------
# 目标：验证 BLE 广播 / 连接 / GATT 收发回传是否正常，
#       并修复 usb_dual 中「蓝牙无法连接」的问题。
#
# 功能：
#   - BLE 从机（Peripheral）广播，设备名 CH572-BLE
#   - GATT 透传服务 0xFFF0（RX 0xFFF1 写入 / TX 0xFFF2 通知）
#   - 收到数据后原样通知回去（echo 回环）
#   - **不依赖 USB**，纯 BLE 收发
#   - 配套 web_ble.html 可用浏览器直接连接测试
#
# 关键修复（相对 usb_dual）：
#   GAPRole_PeripheralInit()          GAP 角色初始化（缺失则 StartDevice 失败）
#   TMOS_ProcessEventRegister(handler) 事件处理（传 NULL 则事件被丢弃）
#   GAPROLE_ADVERT_ENABLED            广播使能（缺失则不广播）
#   GAPROLE_MIN/MAX_CONN_INTERVAL     连接参数（缺失则连接失败）
#   tmos_set_event(START_DEVICE_EVT)  延迟启动（协议栈就绪后再启动）
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

if(NOT COMMAND ch572_add_executable)
    get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" ABSOLUTE)
    include(${SDK_ROOT}/common/cmake/sdk.cmake)
endif()

# 纯 BLE：不启用 USB
ch572_add_executable(ble_test
    ble_test.c
    ble_profile.c
    BLE
)

# ---- BLE 协议栈内存配置（修复连接失败的关键）-------------------------------
# BLE 堆不能太小，否则 GAP 连接建立会失败：
#   3008 B（SDK 旧默认）: 广播可以，但连接会失败
#   4096 B             : 连接正常（CH59x 推荐）
#
# CH572 只有 12KB RAM，BLE 库（.highcode ~7.5KB + 堆 + 全局）已占 94%，
# 无法容纳 4096B 堆，故使用 3072B（库允许的最小值，ble_hal.c 硬性检查 >= 3KB）。
# 由于本项目不使用 USB，CH572 下 RAM 仍然充裕。
if(CHIP_SDK_RESOLVED STREQUAL "CH572")
    set(_ble_heap 3072)
    set(_ble_buff 2)
else()
    set(_ble_heap 4096)
    set(_ble_buff 5)
endif()

target_compile_definitions(ble_test PRIVATE
    BLE_MEMHEAP_SIZE=${_ble_heap}
    BLE_BUFF_NUM=${_ble_buff}
    BLE_SNV=FALSE
)
