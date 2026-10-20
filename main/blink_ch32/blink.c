/********************************** (C) COPYRIGHT *******************************
 * File Name          : blink.c
 * Description        : CH32 系列通用 LED 闪烁示例
 *
 * 适用芯片：CH32X035 / CH32L103 / CH32V203
 * （CH32V003 请使用 main/blink_v003/）
 *
 * 只依赖 ch32fun 核心（GPIO + 延时），无 BLE / USB 依赖。
 * 引脚通过 -DBLINK_PIN 指定（默认 PA0）。
 *******************************************************************************/

#include "ch32fun.h"

/* 默认 LED 引脚：PA0（多数 WCH 评估板可用） */
#ifndef BLINK_PIN
#define BLINK_PIN   PA0
#endif

int main(void)
{
	SystemInit();

	funGpioInitAll();   /* 使能 GPIO 时钟 */

	funPinMode(BLINK_PIN, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);

	while (1) {
		funDigitalWrite(BLINK_PIN, FUN_HIGH);
		Delay_Ms(250);
		funDigitalWrite(BLINK_PIN, FUN_LOW);
		Delay_Ms(250);
	}
}
