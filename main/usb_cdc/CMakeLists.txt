# usb_cdc —— USB CDC 虚拟串口
#
# 使用 HAL 自研 USB 状态机（port/ch59x/hal_usb_ch59x.c）。
#
# 历史：曾用官方 app_usb.c + hal_usb_official_wrap.c 验证硬件通路，
# 已确认 COM 口可枚举（CH340 VID/PID）。现在切回 HAL 实现，
# 状态机已按官方 USB_DevTransProcess 结构重写：
#   1) SETUP 用 RB_UIS_SETUP_ACT 判断，不用 token 值
#   2) 清 RB_UIF_TRANSFER 放在分支内部，不在读 INT_ST 之前
#   3) BUS_RST 后端点 OUT 方向置 ACK（不是 NAK）

hal_add_executable(usb_cdc
    SOURCES
        usb_cdc.c
    USB
)
