# ble_usb —— ★ 核心验收：BLE + USB CDC 同时工作
hal_add_executable(ble_usb
    SOURCES ble_usb.c
    BLE
    USB
    UART
)
