/********************************** (C) COPYRIGHT *******************************
 * File Name          : framehdl.h
 * Description        : 帧命令处理器注册
 *******************************************************************************/

#ifndef _FRAMEHDL_H
#define _FRAMEHDL_H

#ifdef __cplusplus
extern "C" {
#endif

/* 注册所有帧命令处理器（GPIO/ADC/PWM/UART） */
void framehdl_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRAMEHDL_H */
