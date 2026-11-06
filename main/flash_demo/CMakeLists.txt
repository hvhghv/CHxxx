# flash_demo —— Flash / ROM 读写验证
#
# 验证 hal_flash 层：
#   1. 读芯片 UID / MAC
#   2. Data Flash（CH5xx）或 Code Flash（CH32）读写擦
#   3. 掉电保持（复位后读回）
#
# CH32 没有独立 Data Flash，写的是程序区，
# 所以本示例在 CH32 上默认只做只读部分，
# 需要写时用 -DFLASH_DEMO_WRITE=ON 并在 CMake 里开 allow_code_write。
hal_add_executable(flash_demo
    SOURCES flash_demo.c
    UART
)
