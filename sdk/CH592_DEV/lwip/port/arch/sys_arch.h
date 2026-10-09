/********************************** (C) COPYRIGHT *******************************
 * File Name          : sys_arch.h
 * Description        : LWIP sys 层（FreeRTOS 适配）头文件
 *******************************************************************************/

#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 信号量 = FreeRTOS 队列 */
typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;

/* 邮箱 = FreeRTOS 队列 */
typedef QueueHandle_t sys_mbox_t;

/* 线程 = FreeRTOS 任务句柄 */
typedef TaskHandle_t sys_thread_t;

/* 临界区保护类型（sys_arch_protect 返回值） */
typedef uint32_t sys_prot_t;

#define SYS_MBOX_NULL  NULL
#define SYS_SEM_NULL   NULL
#define SYS_MUTEX_NULL NULL

/* 线程优先级映射 */
#define LWIP_TASK_PRIO   ( tskIDLE_PRIORITY + 2 )
#define TCPIP_THREAD_PRIO LWIP_TASK_PRIO

#ifdef __cplusplus
}
#endif

#endif /* LWIP_ARCH_SYS_ARCH_H */
