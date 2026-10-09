/********************************** (C) COPYRIGHT *******************************
 * File Name          : freertos_demo.c
 * Description        : CH592/CH591 FreeRTOS 示例
 *
 * 演示：
 *   - 多任务创建（task1 / task2 周期打印）
 *   - 互斥量保护 printf（App_Printf）
 *   - 任务延时 vTaskDelay
 *
 * 目标芯片：CH592 / CH591（RV32IMC，26KB RAM）
 *******************************************************************************/

#include "ch32fun.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdarg.h>
#include <stdio.h>

/* 任务参数 */
#define TASK1_TASK_PRIO     5
#define TASK1_STK_SIZE      256
#define TASK2_TASK_PRIO     5
#define TASK2_STK_SIZE      256

static TaskHandle_t Task1Task_Handler;
static TaskHandle_t Task2Task_Handler;
static SemaphoreHandle_t printMutex;

/*********************************************************************
 * App_Printf - 任务安全的 printf（用互斥量保护）
 *********************************************************************/
__HIGH_CODE
void App_Printf(const char *fmt, ...)
{
	char buf_str[128];
	va_list v_args;

	va_start(v_args, fmt);
	(void)mini_vsnprintf((char *)&buf_str[0],
	                     (unsigned int)sizeof(buf_str),
	                     (char const *)fmt,
	                     v_args);
	va_end(v_args);

	xSemaphoreTake(printMutex, portMAX_DELAY);
	printf("%s", buf_str);
	xSemaphoreGive(printMutex);
}

/*********************************************************************
 * task1 - 每 250ms 打印
 *********************************************************************/
__HIGH_CODE
void task1_task(void *pvParameters)
{
	(void)pvParameters;
	while (1) {
		App_Printf("task1 entry 1\n");
		vTaskDelay(configTICK_RATE_HZ / 4);
		App_Printf("task1 entry 2\n");
		vTaskDelay(configTICK_RATE_HZ / 4);
	}
}

/*********************************************************************
 * task2 - 每 500ms 打印
 *********************************************************************/
__HIGH_CODE
void task2_task(void *pvParameters)
{
	(void)pvParameters;
	while (1) {
		App_Printf("task2 entry 1\n");
		vTaskDelay(configTICK_RATE_HZ / 2);
		App_Printf("task2 entry 2\n");
		vTaskDelay(configTICK_RATE_HZ / 2);
	}
}

/*********************************************************************
 * main
 *********************************************************************/
int main(void)
{
	SystemInit();

	printf("FreeRTOS %s start.\n", tskKERNEL_VERSION_NUMBER);

	printMutex = xSemaphoreCreateMutex();
	if (printMutex == NULL) {
		printf("printMutex error\n");
		while (1);
	}

	xTaskCreate((TaskFunction_t)task2_task,
	            (const char *)"task2",
	            (uint16_t)TASK2_STK_SIZE,
	            (void *)NULL,
	            (UBaseType_t)TASK2_TASK_PRIO,
	            (TaskHandle_t *)&Task2Task_Handler);

	xTaskCreate((TaskFunction_t)task1_task,
	            (const char *)"task1",
	            (uint16_t)TASK1_STK_SIZE,
	            (void *)NULL,
	            (UBaseType_t)TASK1_TASK_PRIO,
	            (TaskHandle_t *)&Task1Task_Handler);

	vTaskStartScheduler();

	/* 不应运行到这里 */
	while (1) {
		printf("shouldn't run at here!!\n");
	}
}
