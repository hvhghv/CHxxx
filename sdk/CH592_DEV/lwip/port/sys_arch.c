/********************************** (C) COPYRIGHT *******************************
 * File Name          : sys_arch.c
 * Description        : LWIP sys 层实现（FreeRTOS 适配）
 *
 * 提供 LWIP 所需的信号量 / 互斥量 / 邮箱 / 线程 / 时间 接口。
 *******************************************************************************/

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "arch/sys_arch.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "ch32fun.h"

/* ---------------------------------------------------------------------------
 * 时间
 * ------------------------------------------------------------------------- */
u32_t sys_now(void)
{
	/* FreeRTOS tick → 毫秒 */
	return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ---------------------------------------------------------------------------
 * 随机数（LCG，-nostdlib 下无 libc rand）
 * ------------------------------------------------------------------------- */
u32_t lwip_rand_impl(void)
{
	static u32_t seed = 0x12345678;
	seed = seed * 1103515245u + 12345u;
	return (seed >> 16) & 0x7FFF;
}

/* ---------------------------------------------------------------------------
 * 信号量
 * ------------------------------------------------------------------------- */
err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
	*sem = xSemaphoreCreateCounting(0xFFFF, count);
	if (*sem == NULL) {
		return ERR_MEM;
	}
	return ERR_OK;
}

void sys_sem_free(sys_sem_t *sem)
{
	if (*sem != NULL) {
		vSemaphoreDelete(*sem);
		*sem = NULL;
	}
}

void sys_sem_signal(sys_sem_t *sem)
{
	xSemaphoreGive(*sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
	TickType_t ticks;
	TimeOut_t tmo;
	TickType_t start = xTaskGetTickCount();

	if (timeout == 0) {
		/* 无限等待 */
		while (xSemaphoreTake(*sem, portMAX_DELAY) != pdTRUE);
		return 0;
	}

	ticks = timeout / portTICK_PERIOD_MS;
	if (ticks == 0) ticks = 1;
	vTaskSetTimeOutState(&tmo);

	if (xSemaphoreTake(*sem, ticks) == pdTRUE) {
		return (u32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
	}
	return SYS_ARCH_TIMEOUT;
}

/* ---------------------------------------------------------------------------
 * 互斥量
 * ------------------------------------------------------------------------- */
err_t sys_mutex_new(sys_mutex_t *mutex)
{
	*mutex = xSemaphoreCreateMutex();
	if (*mutex == NULL) {
		return ERR_MEM;
	}
	return ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex)
{
	if (*mutex != NULL) {
		vSemaphoreDelete(*mutex);
		*mutex = NULL;
	}
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
	xSemaphoreTake(*mutex, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
	xSemaphoreGive(*mutex);
}

/* ---------------------------------------------------------------------------
 * 邮箱
 * ------------------------------------------------------------------------- */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
	*mbox = xQueueCreate((UBaseType_t)size, sizeof(void *));
	if (*mbox == NULL) {
		return ERR_MEM;
	}
	return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
	if (*mbox != NULL) {
		vQueueDelete(*mbox);
		*mbox = NULL;
	}
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
	while (xQueueSendToBack(*mbox, &msg, portMAX_DELAY) != pdTRUE);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	if (xQueueSendToBack(*mbox, &msg, 0) == pdTRUE) {
		return ERR_OK;
	}
	return ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (xQueueSendToBackFromISR(*mbox, &msg, &xHigherPriorityTaskWoken) == pdTRUE) {
		return ERR_OK;
	}
	return ERR_MEM;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
	TickType_t ticks;
	TickType_t start = xTaskGetTickCount();
	void *dummy;

	if (msg == NULL) msg = &dummy;

	if (timeout == 0) {
		while (xQueueReceive(*mbox, msg, portMAX_DELAY) != pdTRUE);
		return 0;
	}

	ticks = timeout / portTICK_PERIOD_MS;
	if (ticks == 0) ticks = 1;

	if (xQueueReceive(*mbox, msg, ticks) == pdTRUE) {
		return (u32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
	}
	*msg = NULL;
	return SYS_ARCH_TIMEOUT;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
	void *dummy;
	if (msg == NULL) msg = &dummy;
	if (xQueueReceive(*mbox, msg, 0) == pdTRUE) {
		return 0;
	}
	return SYS_MBOX_EMPTY;
}

/* ---------------------------------------------------------------------------
 * 线程
 * ------------------------------------------------------------------------- */
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread,
                            void *arg, int stacksize, int prio)
{
	TaskHandle_t handle = NULL;
	xTaskCreate((TaskFunction_t)thread, name,
	            (configSTACK_DEPTH_TYPE)stacksize, arg,
	            (UBaseType_t)prio, &handle);
	return handle;
}

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
void sys_init(void)
{
	/* FreeRTOS 已由 main 初始化，无需额外操作 */
}

/* ---------------------------------------------------------------------------
 * 临界区（保护 core locking）
 * ------------------------------------------------------------------------- */
sys_prot_t sys_arch_protect(void)
{
	taskENTER_CRITICAL();
	return 0;
}

void sys_arch_unprotect(sys_prot_t pval)
{
	(void)pval;
	taskEXIT_CRITICAL();
}
