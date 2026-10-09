/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_hal.c
 * Description        : CH592/CH591 Lite SDK - BLE HAL 层
 *
 * 本文件等价于官方 HAL/MCU.c + HAL/RTC.c + HAL/SLEEP.c 的核心功能，
 * 但基于 ch32fun + ble_shim.c 实现。
 *
 * 提供的功能：
 *   - CH59x_BLEInit()   BLE 库初始化（填充 bleConfig_t 并调用 BLE_LibInit）
 *   - HAL_TimeInit()    RTC/TMOS 时基初始化
 *   - HAL_SleepInit()   低功耗唤醒初始化
 *   - HAL_ProcessEvent() HAL 任务事件处理（含 RF 定时校准）
 *   - Lib_Calibration_LSI() 内部 32K 时钟校准
 *   - HAL_GetInterTempValue() 内部温度传感器读取
 *
 * 依赖：
 *   - ble_shim.c 提供的 StdPeriphDriver 等价函数
 *   - 官方 libCH59xBLE.a
 *******************************************************************************/

#include "ch32fun.h"
#include "ble_config.h"
#include "ble_types.h"
#include <stdio.h>
#include <string.h>

/* 注意：不要在此包含 ch5xx_flash.h / ISP592.h。
 * 它们的函数是非 static 的，若在多个 .c 中同时包含会导致重复符号。
 * Flash/EEPROM 访问统一由 ble_shim.c 提供。 */

/* ===========================================================================
 * 本文件对外提供的接口
 * =========================================================================== */
void CH59x_BLEInit(void);
void HAL_Init(void);
void HAL_TimeInit(void);
void HAL_SleepInit(void);
tmosEvents HAL_ProcessEvent(tmosTaskID task_id, tmosEvents events);
uint32_t CH59x_LowPower(uint32_t time);
void Lib_Calibration_LSI(void);
uint16_t HAL_GetInterTempValue(void);

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
 * Flash/EEPROM 回调（供 BLE 库的 SNV 使用）
 * ===========================================================================
 * CH59x 有独立 Data-Flash(EEPROM)，SNV 存储在 EEPROM 区。
 * 注意 CH592A 版本要求擦除长度必须是 EEPROM_BLOCK_SIZE 的整数倍。
 */
#if (defined(BLE_SNV)) && (BLE_SNV == TRUE)

uint32_t Lib_Read_Flash(uint32_t addr, uint32_t num, uint32_t *pBuf)
{
	EEPROM_READ(addr, pBuf, num * 4);
	return 0;
}

/* CH592A 特殊处理：EEPROM 擦除必须按块对齐 */
static void Lib_Write_Flash_592A(uint32_t addr, uint32_t num, uint32_t *pBuf)
{
	__attribute__((aligned(4))) uint32_t FLASH_BUF[(BLE_SNV_BLOCK * BLE_SNV_NUM) / 4];

	EEPROM_READ(addr & 0xFFFFF000, FLASH_BUF, BLE_SNV_BLOCK * BLE_SNV_NUM);
	tmos_memcpy(&FLASH_BUF[addr & 0xFFF], pBuf, num * 4);
	EEPROM_ERASE(addr & 0xFFFFF000,
	             ((BLE_SNV_BLOCK * BLE_SNV_NUM + EEPROM_BLOCK_SIZE - 1) / EEPROM_BLOCK_SIZE) * EEPROM_BLOCK_SIZE);
	EEPROM_WRITE(addr & 0xFFFFF000, FLASH_BUF, BLE_SNV_BLOCK * BLE_SNV_NUM);
}

uint32_t Lib_Write_Flash(uint32_t addr, uint32_t num, uint32_t *pBuf)
{
	if (((*(uint32_t *)ROM_CFG_VERISON) & 0xFF) == DEF_CHIP_ID_CH592A) {
		Lib_Write_Flash_592A(addr, num, pBuf);
	} else {
		EEPROM_ERASE(addr, num * 4);
		EEPROM_WRITE(addr, pBuf, num * 4);
	}
	return 0;
}

#endif /* BLE_SNV */

/* ===========================================================================
 * 内部 32K 时钟校准（供 BLE 库 rcCB 回调）
 * =========================================================================== */
void Lib_Calibration_LSI(void)
{
	Calibration_LSI(Level_64);
}

/* ===========================================================================
 * 内部温度传感器读取（供 BLE 库 tsCB 回调，用于 RF 温漂补偿）
 * ===========================================================================
 * CH59x 内置温度传感器，通过 ADC 通道读取。
 * 官方实现见 HAL/MCU.c 的 HAL_GetInterTempValue()。
 */
uint16_t HAL_GetInterTempValue(void)
{
	/* 使用官方 ADC 温度传感器读取（由 ble_shim.c 提供 ADC 初始化） */
	return ADC_ReadTempSensor();
}

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
/* BLE 初始化错误码（供应用查询，替代 printf 以省格式化引擎 ~750B）
 * 0=OK；非 0 表示致命错误（已进入 while(1)） */
volatile uint8_t g_ble_init_err = 0;

/* ===========================================================================
 * 等价于官方 CH59x_BLEInit()，但使用我们自己的 HAL 实现。
 */
void CH59x_BLEInit(void)
{
	uint8_t i;
	bleConfig_t cfg;

	/* 校验头文件与库版本一致 */
	if (tmos_memcmp(VER_LIB, VER_FILE, strlen(VER_FILE)) == FALSE) {
		g_ble_init_err = 1;   /* 版本不匹配 */
		while (1);
	}

	/* SysTick 配置（CH59x 官方要求禁用 SysTick 中断） */
	__SysTick_Config(SysTick_LOAD_RELOAD_Msk);
	PFIC_DisableIRQ(SysTick_IRQn);

	g_LLE_IRQLibHandlerLocation = (uint32_t)LLE_IRQLibHandler;
	PFIC_SetPriority(BLEL_IRQn, 0xF0);

	tmos_memset(&cfg, 0, sizeof(bleConfig_t));

	cfg.MEMAddr       = (uint32_t)MEM_BUF;
	cfg.MEMLen        = (uint32_t)BLE_MEMHEAP_SIZE;
	cfg.BufMaxLen     = (uint32_t)BLE_BUFF_MAX_LEN;
	cfg.BufNumber     = (uint32_t)BLE_BUFF_NUM;
	cfg.TxNumEvent    = (uint32_t)BLE_TX_NUM_EVENT;
	cfg.TxPower       = (uint32_t)BLE_TX_POWER;
	cfg.WindowWidening = 120;

#if (defined(BLE_SNV)) && (BLE_SNV == TRUE)
	if ((BLE_SNV_ADDR + BLE_SNV_BLOCK * BLE_SNV_NUM) > (0x78000 - FLASH_ROM_MAX_SIZE)) {
		g_ble_init_err = 2;   /* SNV 配置错误 */
		while (1);
	}
	cfg.SNVAddr       = (uint32_t)BLE_SNV_ADDR;
	cfg.SNVBlock      = (uint32_t)BLE_SNV_BLOCK;
	cfg.SNVNum        = (uint32_t)BLE_SNV_NUM;
	cfg.readFlashCB   = Lib_Read_Flash;
	cfg.writeFlashCB  = Lib_Write_Flash;
#endif

	/* CH59x 新增：多连接配置 */
	cfg.ConnectNumber = (PERIPHERAL_MAX_CONNECTION & 3) | (CENTRAL_MAX_CONNECTION << 2);

	cfg.srandCB = SYS_GetSysTickCnt;

#if (defined(TEM_SAMPLE)) && (TEM_SAMPLE == TRUE)
	cfg.tsCB = HAL_GetInterTempValue;   /* 温度校准 RF 内部 RC */
#if (CLK_OSC32K)
	cfg.rcCB = Lib_Calibration_LSI;     /* 内部 32K 校准 */
#endif
#endif

#if (defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
	cfg.idleCB = CH59x_LowPower;
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

	/* 官方要求 >= 4*1024，CH59x 有 26K RAM，用 6K 堆 */
	if (!cfg.MEMAddr || cfg.MEMLen < 4 * 1024) {
		g_ble_init_err = 3;   /* 内存堆太小 */
		while (1);
	}

	/* 高功率发射时的 RF 配置（官方 CH59x_BLEInit 有这段） */
	if (cfg.TxPower & (1 << 7)) {
		sys_safe_access_enable();
		*(volatile uint32_t *)(0x40001048) |= (6 << 6);
		*(volatile uint32_t *)(0x40001020) |= (2 << 19);
		sys_safe_access_disable();
	}

	i = BLE_LibInit(&cfg);
	if (i) {
		g_ble_init_err = i;   /* BLE 库初始化失败 */
		while (1);
	}
}

/* ===========================================================================
 * HAL 任务事件处理
 * ===========================================================================
 * 包含 CH59x 新增的 RF 定时校准事件（HAL_REG_INIT_EVENT）。
 */
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

#if (defined(BLE_CALIBRATION_ENABLE)) && (BLE_CALIBRATION_ENABLE == TRUE)
	if (events & HAL_REG_INIT_EVENT) {
		/* 定时校准 RF 与内部 32K（耗时 < 10ms） */
		BLE_RegInit();
#if (CLK_OSC32K)
		Lib_Calibration_LSI();
#else
		{
			uint8_t x32Kpw = (R8_XT32K_TUNE & 0xfc) | 0x01;
			sys_safe_access_enable();
			R8_XT32K_TUNE = x32Kpw;   /* LSE 驱动能力调到额定值 */
			sys_safe_access_disable();
		}
#endif
		tmos_start_task(halTaskID, HAL_REG_INIT_EVENT,
		                MS1_TO_SYSTEM_TIME(BLE_CALIBRATION_PERIOD));
		return events ^ HAL_REG_INIT_EVENT;
	}
#endif

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

#if (defined(BLE_CALIBRATION_ENABLE)) && (BLE_CALIBRATION_ENABLE == TRUE)
	/* 启动定时校准任务 */
	tmos_start_task(halTaskID, HAL_REG_INIT_EVENT,
	                MS1_TO_SYSTEM_TIME(BLE_CALIBRATION_PERIOD));
#endif
}

/* ===========================================================================
 * 低功耗回调（HAL_SLEEP 开启时使用）
 * =========================================================================== */
uint32_t CH59x_LowPower(uint32_t time)
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

	R32_RTC_TRIG = time_sleep;
	R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;
	R8_SLP_WAKE_CTRL |= RB_SLP_RTC_WAKE;

	SYS_RecoverIrq(irq_status);

	LowPower_Sleep(RB_PWR_RAM2K | RB_PWR_RAM16K | RB_PWR_EXT1 | RB_PWR_EXT2);

	/* 等待 RTC 唤醒 */
	for (i = 0; i < 800; i++);

	R8_RTC_MODE_CTRL &= ~RB_RTC_TRIG_EN;
	SYS_RecoverIrq(irq_status);

	return 0;
#else
	(void)time;
	return 0;
#endif
}
