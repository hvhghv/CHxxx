/********************************** (C) COPYRIGHT *******************************
 * File Name          : blink_v003.c
 * Description        : CH32V003 示例 - 4 路 LED 闪烁
 *
 * 目标芯片：CH32V003（RV32EC，2KB RAM，16KB Flash）
 * 引脚：PD0 / PD4 / PD6 / PC0（CH32V003 开发板常见 LED 引脚）
 *
 * 该示例只依赖 ch32fun 核心（GPIO + 延时），无 BLE / USB 依赖，
 * 因此可在 CH32V003 上编译运行。
 *******************************************************************************/

#include "ch32fun.h"

#define PIN_1     PD0
#define PIN_K     PD4
#define PIN_BOB   PD6
#define PIN_KEVIN PC0

int main(void)
{
	SystemInit();

	funGpioInitAll();   /* 使能 GPIO */

	funPinMode(PIN_1,     GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
	funPinMode(PIN_K,     GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
	funPinMode(PIN_BOB,   GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
	funPinMode(PIN_KEVIN, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);

	while (1) {
		funDigitalWrite(PIN_1,     FUN_HIGH);
		funDigitalWrite(PIN_K,     FUN_HIGH);
		funDigitalWrite(PIN_BOB,   FUN_HIGH);
		funDigitalWrite(PIN_KEVIN, FUN_HIGH);
		Delay_Ms(250);

		funDigitalWrite(PIN_1,     FUN_LOW);
		funDigitalWrite(PIN_K,     FUN_LOW);
		funDigitalWrite(PIN_BOB,   FUN_LOW);
		funDigitalWrite(PIN_KEVIN, FUN_LOW);
		Delay_Ms(250);
	}
}
