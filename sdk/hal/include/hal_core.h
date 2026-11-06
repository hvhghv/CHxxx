/**
 * @file    hal.h
 * @brief   通用层总入口
 *
 * 应用层只需 #include "hal_core.h" 即可使用全部外设接口。
 * 芯片相关头文件（如 CH59x_gpio.h）不应出现在应用代码中。
 */
#ifndef HAL_H
#define HAL_H

#include "hal_types.h"
#include "hal_sys.h"
#include "hal_time.h"
#include "hal_irq.h"
#include "hal_gpio.h"
#include "hal_uart.h"
#include "hal_log.h"
#include "hal_ble.h"
#include "hal_usb.h"
#include "hal_usb_cdc.h"

/* 以下头文件在对应阶段实现后逐步启用 */
/* #include "hal_i2c.h"        */
/* #include "hal_spi.h"        */
/* #include "hal_pwm.h"        */
/* #include "hal_adc.h"        */
/* #include "hal_flash.h"      */
/* #include "hal_usb_hid.h"    */
/* #include "hal_usb_msc.h"    */
/* #include "hal_bt_classic.h" */

/**
 * 通用层初始化。由 port 层实现，在 main() 最开始调用一次。
 * 内部按顺序初始化：系统时钟 → 时基 → 中断 → GPIO
 */
hal_err_t hal_init(void);

#endif /* HAL_H */
