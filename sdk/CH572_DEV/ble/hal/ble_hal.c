/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_hal.c
 * Description        : CH572 Lite SDK - BLE HAL 层
 *
 * 本文件等价于官方 HAL/MCU.c + HAL/RTC.c + HAL/SLEEP.c 的核心功能，
 * 但基于 ch32fun + ble_shim.c 实现。
 *
 * 提供的功能：
 *   - CH57x_BLEInit()   BLE 库初始化（填充 bleConfig_t 并调用 BLE_LibInit）
 *   - HAL_TimeInit()    RTC/TMOS 时基初始化
 *   - HAL_SleepInit()   低功耗唤醒初始化
 *   - HAL_ProcessEvent() HAL 任务事件处理
 *
 * 依赖：
 *   - ble_shim.c 提供的 StdPeriphDriver 等价函数
 *   - 官方 libCH572BLE_PERI.a
 *******************************************************************************/

#include "ch32fun.h"
#include "ble_config.h"
#include "ble_types.h"
#include <stdio.h>
#include <string.h>

/* 注意：不要在此包含 ch5xx_flash.h。
 * 该头文件的函数是非 static 的，若在多个 .c 中同时包含会导致重复符号。
 * Flash 访问统一由 ble_shim.c 提供（它包含 ch5xx_flash.h）。 */

/* ===========================================================================
 * 本文件对外提供的接口
 * =========================================================================== */
void CH57x_BLEInit(void);
void HAL_Init(void);
void HAL_TimeInit(void);
void HAL_SleepInit(void);
tmosEvents HAL_ProcessEvent(tmosTaskID task_id, tmosEvents events);
uint32_t CH57x_LowPower(uint32_t time);

/* ===========================================================================
 * 全局变量
 * =========================================================================== */
tmosTaskID halTaskID;
uint32_t   g_LLE_IRQLibHandlerLocation;
volatile uint32_t RTCTigFlag;

/* BLE 内存堆（默认定义，应用可通过弱符号覆盖） */
__attribute__((weak))
uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

/* ===========================================================================
 * Flash 回调（供 BLE 库的 SNV 使用）
 * =========================================================================== */
#if (defined(BLE_SNV)) && (BLE_SNV == TRUE)

uint32_t Lib_Read_Flash(uint32_t addr, uint32_t num, uint32_t *pBuf)
{
	FLASH_ROM_READ(addr, pBuf, num * 4);
	return 0;
}

uint32_t Lib_Write_Flash(uint32_t addr, uint32_t num, uint32_t *pBuf)
{
	FLASH_ROM_ERASE(addr, num * 4);
	FLASH_ROM_WRITE(addr, pBuf, num * 4);
	return 0;
}

#endif

/* ===========================================================================
 * RTC 中断处理
 * =========================================================================== */
__INTERRUPT
__HIGH_CODE
void RTC_IRQHandler(void)
{
	R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);
	RTCTigFlag = 1;
}

/* ===========================================================================
 * RTC 时基回调（供 BLE 库读取系统时钟）
 * =========================================================================== */
__HIGH_CODE
static uint32_t SYS_GetClockValue(void)
{
	uint32_t volatile rtc_count;
	do {
		rtc_count = R32_RTC_CNT_32K;
	} while (rtc_count != R32_RTC_CNT_32K);
	return rtc_count;
}

__HIGH_CODE
static void SYS_SetPendingIRQ(void)
{
	PFIC_SetPendingIRQ(RTC_IRQn);
}

/* ===========================================================================
 * BLE 时钟配置
 * =========================================================================== */
bleClockConfig_t BLE_ClockConfig(uint32_t lsifreq)
{
	bleClockConfig_t conf;
	conf.ClockAccuracy   = 2500;
	conf.ClockFrequency  = lsifreq;
	conf.ClockMaxCount   = RTC_MAX_COUNT;
	conf.getClockValue   = SYS_GetClockValue;
	conf.SetPendingIRQ   = SYS_SetPendingIRQ;
	return conf;
}

/* ===========================================================================
 * RTC / TMOS 时基初始化
 * =========================================================================== */
void HAL_TimeInit(void)
{
	bleClockConfig_t conf;
	uint32_t lsiFrq;

	SYS_SAFE_ACCESS(
		R8_LSI_CONFIG |= RB_CLK_LSI_PON;
	);

	lsiFrq = RTC_InitClock(Count_1024);
	RTC_InitTime(2021, 1, 28, 0, 0, 0);

	conf = BLE_ClockConfig(lsiFrq);
	TMOS_TimerInit(&conf);

	SYS_SAFE_ACCESS(
		R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;
	);

	PFIC_EnableIRQ(RTC_IRQn);
}

/* ===========================================================================
 * 低功耗初始化
 * =========================================================================== */
void HAL_SleepInit(void)
{
#if (defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
	SYS_SAFE_ACCESS(
		R8_SLP_WAKE_CTRL |= RB_SLP_RTC_WAKE;
	);
	SYS_SAFE_ACCESS(
		R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;
	);
	PFIC_EnableIRQ(RTC_IRQn);
#endif
}

/* ===========================================================================
 * BLE 库初始化
 * ===========================================================================
 * 等价于官方 CH57x_BLEInit()，但使用我们自己的 HAL 实现。
 */
void CH57x_BLEInit(void)
{
	uint8_t i;
	bleConfig_t cfg;

	/* 校验头文件与库版本一致 */
	if (tmos_memcmp(VER_LIB, VER_FILE, strlen(VER_FILE)) == FALSE) {
		printf("BLE head file / lib version mismatch!\n");
		while (1);
	}

	__SysTick_Config(SysTick_LOAD_RELOAD_Msk);

	sys_safe_access_enable();
	R32_MISC_CTRL = (R32_MISC_CTRL & (~(0x3f << 24))) | (0xe << 24);
	sys_safe_access_disable();

	g_LLE_IRQLibHandlerLocation = (uint32_t)LLE_IRQLibHandler;
	/* 官方用 BLEL_IRQn（=21），ch32fun 用 LLE_IRQn（=21），值相同 */
	PFIC_SetPriority(LLE_IRQn, 0xF0);

	tmos_memset(&cfg, 0, sizeof(bleConfig_t));

	cfg.MEMAddr       = (uint32_t)MEM_BUF;
	cfg.MEMLen        = (uint32_t)BLE_MEMHEAP_SIZE;
	cfg.BufMaxLen     = (uint32_t)BLE_BUFF_MAX_LEN;
	cfg.BufNumber     = (uint32_t)BLE_BUFF_NUM;
	cfg.TxNumEvent    = (uint32_t)BLE_TX_NUM_EVENT;
	cfg.TxPower       = (uint32_t)BLE_TX_POWER;
	cfg.WindowWidening = 120;

#if (defined(BLE_SNV)) && (BLE_SNV == TRUE)
	if ((BLE_SNV_ADDR + BLE_SNV_BLOCK * BLE_SNV_NUM) > 0x40000) {
		printf("BLE SNV config error\n");
		while (1);
	}
	cfg.SNVAddr       = (uint32_t)BLE_SNV_ADDR;
	cfg.SNVBlock      = (uint32_t)BLE_SNV_BLOCK;
	cfg.SNVNum        = (uint32_t)BLE_SNV_NUM;
	cfg.readFlashCB   = Lib_Read_Flash;
	cfg.writeFlashCB  = Lib_Write_Flash;
#endif

	cfg.srandCB = SYS_GetSysTickCnt;

#if (defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
	cfg.idleCB = CH57x_LowPower;
#endif

#if (defined(BLE_MAC)) && (BLE_MAC == TRUE)
	for (i = 0; i < 6; i++) {
		cfg.MacAddr[i] = MacAddr[5 - i];
	}
#else
	{
		uint8_t mac[6];
		GetMACAddress(mac);
		for (i = 0; i < 6; i++) {
			cfg.MacAddr[i] = mac[i];
		}
	}
#endif

	/* 注意：官方要求 >= 3*1024，但 CH572 只有 12K RAM，
	 * 在「BLE + USB CDC」极限配置下放宽到 3008 字节。
	 * 若出现 BLE 连接不稳定，请调大 BLE_MEMHEAP_SIZE。 */
	if (!cfg.MEMAddr || cfg.MEMLen < 3008) {
		printf("BLE memory too small\n");
		while (1);
	}

	i = BLE_LibInit(&cfg);
	if (i) {
		printf("BLE lib init error: %x\n", i);
		while (1);
	}
}

/* ===========================================================================
 * HAL 任务事件处理
 * =========================================================================== */
tmosEvents HAL_ProcessEvent(tmosTaskID task_id, tmosEvents events)
{
	uint8_t *msgPtr;

	if (events & SYS_EVENT_MSG) {
		msgPtr = tmos_msg_receive(task_id);
		if (msgPtr) {
			tmos_msg_deallocate(msgPtr);
		}
		return events ^ SYS_EVENT_MSG;
	}
	return 0;
}

/* ===========================================================================
 * HAL 初始化
 * =========================================================================== */
void HAL_Init(void)
{
	halTaskID = TMOS_ProcessEventRegister(HAL_ProcessEvent);
	HAL_TimeInit();
	HAL_SleepInit();
}

/* ===========================================================================
 * 低功耗回调（HAL_SLEEP 开启时使用）
 * =========================================================================== */
uint32_t CH57x_LowPower(uint32_t time)
{
#if (defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
	volatile uint32_t i;
	uint32_t time_tign, time_sleep, time_curr;
	uint32_t irq_status;
	uint16_t LSIWakeup_MaxTime = WAKE_UP_RTC_MAX_TIME;

	if (time <= LSIWakeup_MaxTime) {
		time_tign = time + (RTC_MAX_COUNT - LSIWakeup_MaxTime);
	} else {
		time_tign = time - LSIWakeup_MaxTime;
	}

	SYS_DisableAllIrq(&irq_status);
	time_curr = RTC_GetCycleLSI();

	if (time_tign < time_curr) {
		time_sleep = time_tign + (RTC_MAX_COUNT - time_curr);
	} else {
		time_sleep = time_tign - time_curr;
	}

	if ((time_sleep < SLEEP_RTC_MIN_TIME) ||
	    (time_sleep > SLEEP_RTC_MAX_TIME)) {
		SYS_RecoverIrq(irq_status);
		return 2;
	}

	RTC_SetCycleLSI(time_tign);
	SYS_RecoverIrq(irq_status);

	if (!RTCTigFlag) {
		/* CH572 无 RB_PWR_RAM12K（那是 CH571/3 的命名），
		 * 使用 RB_PWR_RAM2K | RB_PWR_EXTEND 保留必要 RAM 和 BLE/USB 域 */
		LowPower_Sleep(RB_PWR_RAM2K | RB_PWR_EXTEND | RB_XT_PRE_EN);
		HSECFG_Current(HSE_RCur_100);
		return 0;
	}
#endif
	return 3;
}
