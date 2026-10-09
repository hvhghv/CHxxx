/********************************** (C) COPYRIGHT *******************************
 * File Name          : swio.c
 * Description        : CH32V003 SWIO 单线烧录协议实现（CH591/CH592 主机）
 *
 * SWIO（WCH 称 SDI）是单线半双工的 RISC-V 调试模块（D-code）访问协议。
 *
 * 协议要点（参考 cnlohr bitbang_rvswdio.h，MIT/NewBSD）：
 *   - 位编码：1 = 短拉低，0 = 长拉低（MSB-first）
 *   - 命令：起始位 + 7 位寄存器号 + 起始位 + 32 位数据（均 MSB-first）
 *   - 访问对象：7 位 D-code 调试寄存器（0x00~0x7f），非内存地址
 *   - 内存/Flash 访问：通过注入 RISC-V 指令让目标 CPU 执行
 *
 * ⚠️ 时序敏感，位操作全程关中断忙等。
 *******************************************************************************/

#include "ch32fun.h"
#include "swio.h"
#include "swio_port.h"
#include "pinmux.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * D-code 调试寄存器（7 位地址）
 * ------------------------------------------------------------------------- */
#define DM_BDMDATA0       0x04
#define DM_BDMDATA1       0x05
#define DM_DMCONTROL      0x10
#define DM_DMSTATUS       0x11
#define DM_DMHARTINFO     0x12
#define DM_DMABSTRACTCS   0x16
#define DM_DMCOMMAND      0x17
#define DM_DMABSTRACTAUTO 0x18
#define DM_DMPROGBUF0     0x20
#define DM_DMPROGBUF1     0x21
#define DM_DMPROGBUF2     0x22
#define DM_DMPROGBUF3     0x23
#define DM_DMPROGBUF4     0x24
#define DM_DMPROGBUF5     0x25
#define DM_DMPROGBUF6     0x26
#define DM_DMPROGBUF7     0x27
#define DM_DMCPBR         0x7C
#define DM_DMCFGR         0x7D
#define DM_DMSHDWCFGR     0x7E

/* DMCOMMAND 常用操作码 */
#define DMCMD_COPY_X8_EXEC   0x00271008u  /* 写 BDMDATA0 → x8 并执行 */
#define DMCMD_COPY_FROM_X8   0x00221008u  /* 从 x8 读回 BDMDATA0 */
#define DMCMD_COPY_TO_X9     0x00231009u  /* 写 BDMDATA0 → x9 */
#define DMCMD_COPY_TO_X10    0x0023100au
#define DMCMD_COPY_TO_X11    0x0023100bu
#define DMCMD_COPY_TO_X12    0x0023100cu
#define DMCMD_EXEC_ONLY      0x00240000u  /* 仅执行 */

/* CH32V003 Flash 寄存器（通过目标 CPU 的 load/store 访问） */
#define FLASH_KEYR       0x40022004u
#define FLASH_OBKEYR     0x40022008u
#define FLASH_STATR      0x4002200Cu
#define FLASH_CTLR       0x40022010u
#define FLASH_ADDR       0x40022014u
#define FLASH_OBR        0x4002201Cu
#define FLASH_MODEKEYR   0x40022024u

#define FLASH_KEY1       0x45670123u
#define FLASH_KEY2       0xCDEF89ABu
#define CTLR_PAGE_PG     0x00010000u
#define CTLR_PAGE_ER     0x00020000u
#define CTLR_BUF_LOAD    0x00040000u
#define CTLR_BUF_RST     0x00080000u
#define CTLR_STRT        0x00000040u
#define CTLR_LOCK        0x00000080u
#define STATR_BSY        0x00000001u

/* CH32V003 芯片 ID 地址（读取 1 字节） */
#define CH32V003_ID_ADDR 0x40001041u

/* ---------------------------------------------------------------------------
 * 时序参数（60MHz，需实测标定）
 * ---------------------------------------------------------------------------
 * T = 1/8MHz = 125ns（AVR 参考）
 * 60MHz 下 1 周期 = 16.7ns → 标称位时隙约 7.5 周期
 * ------------------------------------------------------------------------- */
#define SWIO_T1_COEFF    8      /* 标称位时隙（周期数） */
#define SWIO_MAX_TIMEOUT 2000   /* 读位超时（周期数） */

/* ---------------------------------------------------------------------------
 * 状态
 * ------------------------------------------------------------------------- */
static swio_port_t g_port;
static int g_progress = 0;
static int g_t1 = SWIO_T1_COEFF;

/* ---------------------------------------------------------------------------
 * 引脚编号 → 端口描述
 * ------------------------------------------------------------------------- */
static int pin_to_port(int pin, swio_port_t *p)
{
	uint32_t chfun;
	uint32_t bit;
	uint32_t off;

	if (pin < 0 || pin >= PIN_COUNT) return -1;

	if (pin < 16) {
		chfun = (uint32_t)pin;      /* PA0..PA15 */
		off = 0;
	} else {
		chfun = PB | (uint32_t)(pin - 16);  /* PB0..PB23 */
		off = 0x20;                 /* OFFSET_FOR_GPIOB */
	}
	bit = 1u << (chfun & 0xF);

	p->chfun  = chfun;
	p->bit    = bit;
	p->dir    = (volatile uint32_t *)((uintptr_t)&R32_PA_DIR    + off);
	p->out    = (volatile uint32_t *)((uintptr_t)&R32_PA_OUT    + off);
	p->set    = (volatile uint32_t *)((uintptr_t)&R32_PA_SET    + off);
	p->clr    = (volatile uint32_t *)((uintptr_t)&R32_PA_CLR    + off);
	p->pu     = (volatile uint32_t *)((uintptr_t)&R32_PA_PU     + off);
	p->pd_drv = (volatile uint32_t *)((uintptr_t)&R32_PA_PD_DRV + off);
	p->pin    = (volatile uint32_t *)((uintptr_t)&R32_PA_PIN    + off);
	p->valid  = 1;
	return 0;
}

/* ---------------------------------------------------------------------------
 * 位层（MSB-first + 预充电）
 * ------------------------------------------------------------------------- */

/* 发送位 1：短拉低 */
static void swio_send_1(swio_port_t *p)
{
	swio_port_drive_low(p);
	swio_port_delay_cycles(g_t1);
	swio_port_release(p);
	swio_port_delay_cycles(g_t1);
}

/* 发送位 0：长拉低 */
static void swio_send_0(swio_port_t *p)
{
	swio_port_drive_low(p);
	swio_port_delay_cycles(g_t1 * 4);
	swio_port_release(p);
	swio_port_delay_cycles(g_t1);
}

/* 接收位：拉低启动 → 预充电 → 释放 → 采样 → 等恢复
 * 返回 0/1，超时返回 2 */
static int swio_read_bit(swio_port_t *p)
{
	int ret;
	uint32_t timeout;

	swio_port_drive_low(p);
	swio_port_delay_cycles(g_t1);
	swio_port_release(p);
	swio_port_drive_high(p);           /* 预充电（总线冲突检测） */
	swio_port_delay_cycles(g_t1 * 2);
	ret = swio_port_read(p);

	/* 等待线恢复高（若被目标拉低） */
	for (timeout = 0; timeout < SWIO_MAX_TIMEOUT; timeout++) {
		if (swio_port_read(p)) {
			swio_port_delay_cycles(g_t1 / 2);
			return ret;
		}
	}
	return 2;   /* 超时 */
}

/* ---------------------------------------------------------------------------
 * 协议层：D-code 寄存器读写（MSB-first）
 * ------------------------------------------------------------------------- */

/* 写 32 位调试寄存器 */
static void swio_dm_write_reg(swio_port_t *p, uint8_t reg, uint32_t val)
{
	uint32_t mask;

	swio_port_irq_disable();

	swio_send_1(p);                        /* 起始位 */
	for (mask = 1u << 6; mask; mask >>= 1) /* 7 位寄存器号 */
		(reg & mask) ? swio_send_1(p) : swio_send_0(p);

	swio_send_1(p);                        /* 起始位 */
	for (mask = 1u << 31; mask; mask >>= 1) /* 32 位数据 */
		(val & mask) ? swio_send_1(p) : swio_send_0(p);

	swio_port_irq_enable();
	swio_port_delay_us(8);
}

/* 读 32 位调试寄存器，返回 0 成功 */
static int swio_dm_read_reg(swio_port_t *p, uint8_t reg, uint32_t *val)
{
	uint32_t mask, r = 0;
	int i;

	swio_port_irq_disable();

	swio_send_1(p);                        /* 起始位 */
	for (mask = 1u << 6; mask; mask >>= 1) /* 7 位寄存器号 */
		(reg & mask) ? swio_send_1(p) : swio_send_0(p);

	swio_send_0(p);                        /* 起始位=0 → 读 */

	for (i = 0; i < 32; i++) {             /* 读 32 位 */
		int b;
		r <<= 1;
		b = swio_read_bit(p);
		if (b == 1) r |= 1;
		if (b == 2) {                      /* 超时 */
			swio_port_irq_enable();
			return -21;
		}
	}
	*val = r;

	swio_port_irq_enable();
	swio_port_delay_us(8);
	return 0;
}

/* 等待抽象命令完成 */
static int swio_dm_wait_done(swio_port_t *p)
{
	uint32_t rrv;
	int timeout = 1000;
	int r;

	do {
		r = swio_dm_read_reg(p, DM_DMABSTRACTCS, &rrv);
		if (r) return r;
		if (timeout-- == 0) return -8;
	} while (rrv & (1u << 12));

	if ((rrv >> 8) & 7) {
		swio_dm_write_reg(p, DM_DMABSTRACTCS, 0x00000700u);
		return -33;
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * 初始化与握手
 * ------------------------------------------------------------------------- */
int swio_handshake(uint32_t *chip_id)
{
	uint32_t v;

	if (!g_port.valid) return SWIO_ERR_NO_PIN;

	/* DMCFGR/DMSHDWCFGR 写入 0x5aa50000 | (1<<10)，允许从机输出 */
	swio_dm_write_reg(&g_port, DM_DMSHDWCFGR, 0x5aa50000u | (1u << 10));
	swio_dm_write_reg(&g_port, DM_DMCFGR,     0x5aa50000u | (1u << 10));
	swio_dm_write_reg(&g_port, DM_DMSHDWCFGR, 0x5aa50000u | (1u << 10)); /* try twice */
	swio_dm_write_reg(&g_port, DM_DMCFGR,     0x5aa50000u | (1u << 10));

	swio_dm_write_reg(&g_port, DM_DMCONTROL, 0x00000001u);
	swio_dm_write_reg(&g_port, DM_DMCONTROL, 0x00000001u);

	/* 读回验证 */
	if (swio_dm_read_reg(&g_port, DM_DMCFGR, &v) == 0 &&
	    (v & 0xffff0000u) == 0x5aa50000u) {
		if (chip_id) *chip_id = v;
		return SWIO_OK;
	}
	return SWIO_ERR_HANDSHAKE;
}

/* ---------------------------------------------------------------------------
 * 芯片识别（通过注入 RISC-V 指令）
 * ------------------------------------------------------------------------- */
static int swio_read_chip_id(uint32_t *chip_id)
{
	uint32_t sevenf = 0;
	uint32_t id = 0;

	/* 先读 0x7f 寄存器 */
	if (swio_dm_read_reg(&g_port, 0x7f, &sevenf) != 0) return SWIO_ERR_HANDSHAKE;

	if (sevenf == 0) {
		/* 写 RISC-V 指令：lb x8, 0(x8); c.ebreak */
		swio_dm_write_reg(&g_port, DM_DMPROGBUF0, 0x00040403u);
		swio_dm_write_reg(&g_port, DM_DMPROGBUF1, 0x00100073u);
		/* 芯片 ID 地址 → x8 */
		swio_dm_write_reg(&g_port, DM_BDMDATA0, CH32V003_ID_ADDR);
		swio_dm_write_reg(&g_port, DM_DMCOMMAND, DMCMD_COPY_X8_EXEC);
		if (swio_dm_wait_done(&g_port) != 0) return SWIO_ERR_HANDSHAKE;
		/* 从 x8 读回 */
		swio_dm_write_reg(&g_port, DM_DMCOMMAND, DMCMD_COPY_FROM_X8);
		if (swio_dm_read_reg(&g_port, DM_BDMDATA0, &id) != 0) return SWIO_ERR_HANDSHAKE;
		id &= 0xff;
	}

	if (chip_id) *chip_id = id;
	return SWIO_OK;
}

/* 公开的芯片 ID 读取（供终端/HTTP 调用） */
int swio_read_chip_id_pub(uint32_t *chip_id)
{
	return swio_read_chip_id(chip_id);
}

/* ---------------------------------------------------------------------------
 * 目标内存/Flash 读写（通过 DM 执行 RISC-V 指令）
 * ------------------------------------------------------------------------- */

/* 读目标内存一个字 */
int swio_read_word(uint32_t addr, uint32_t *data)
{
	uint32_t v = 0;

	if (!g_port.valid) return SWIO_ERR_NO_PIN;

	/* 程序：lw x8, 0(x9); c.ebreak */
	swio_dm_write_reg(&g_port, DM_DMPROGBUF0, 0x0004a403u); /* lw x8, 0(x9) */
	swio_dm_write_reg(&g_port, DM_DMPROGBUF1, 0x00100073u); /* c.ebreak */
	swio_dm_write_reg(&g_port, DM_BDMDATA0, addr);
	swio_dm_write_reg(&g_port, DM_DMCOMMAND, DMCMD_COPY_TO_X9);
	swio_dm_write_reg(&g_port, DM_DMCOMMAND, DMCMD_EXEC_ONLY);
	swio_dm_write_reg(&g_port, DM_DMCOMMAND, DMCMD_COPY_FROM_X8);
	if (swio_dm_wait_done(&g_port) != 0) return SWIO_ERR_WRITE;
	if (swio_dm_read_reg(&g_port, DM_BDMDATA0, &v) != 0) return SWIO_ERR_WRITE;

	if (data) *data = v;
	return SWIO_OK;
}

/* 写目标内存一个字 */
int swio_write_word(uint32_t addr, uint32_t data)
{
	if (!g_port.valid) return SWIO_ERR_NO_PIN;

	/* 程序：sw x8, 0(x9); c.ebreak */
	swio_dm_write_reg(&g_port, DM_DMPROGBUF0, 0x0084a023u); /* sw x8, 0(x9) */
	swio_dm_write_reg(&g_port, DM_DMPROGBUF1, 0x00100073u); /* c.ebreak */
	swio_dm_write_reg(&g_port, DM_BDMDATA0, addr);
	swio_dm_write_reg(&g_port, DM_DMCOMMAND, DMCMD_COPY_TO_X9);
	swio_dm_write_reg(&g_port, DM_BDMDATA0, data);
	swio_dm_write_reg(&g_port, DM_DMCOMMAND, DMCMD_COPY_X8_EXEC);
	if (swio_dm_wait_done(&g_port) != 0) return SWIO_ERR_WRITE;
	return SWIO_OK;
}

/* ---------------------------------------------------------------------------
 * Flash 操作
 * ------------------------------------------------------------------------- */
int swio_flash_unlock(void)
{
	uint32_t ctlr = 0;

	if (!g_port.valid) return SWIO_ERR_NO_PIN;

	if (swio_read_word(FLASH_CTLR, &ctlr) != SWIO_OK) return SWIO_ERR_UNLOCK;
	if (ctlr & 0x8080u) {
		swio_write_word(FLASH_KEYR, FLASH_KEY1);
		swio_write_word(FLASH_KEYR, FLASH_KEY2);
		swio_write_word(FLASH_OBKEYR, FLASH_KEY1);
		swio_write_word(FLASH_OBKEYR, FLASH_KEY2);
		swio_write_word(FLASH_MODEKEYR, FLASH_KEY1);
		swio_write_word(FLASH_MODEKEYR, FLASH_KEY2);
		swio_read_word(FLASH_CTLR, &ctlr);
		if (ctlr & 0x8080u) return SWIO_ERR_UNLOCK;
	}
	return SWIO_OK;
}

static int swio_flash_wait(void)
{
	uint32_t st = 0;
	int timeout = 2000;

	do {
		if (swio_read_word(FLASH_STATR, &st) != SWIO_OK) return SWIO_ERR_WRITE;
	} while ((st & STATR_BSY) && timeout-- > 0);

	swio_write_word(FLASH_STATR, 0);   /* 清标志 */
	if (st & 0x10u) return SWIO_ERR_WRITE;  /* WRPRTERR */
	if (st & STATR_BSY) return SWIO_ERR_WRITE;
	return SWIO_OK;
}

/* CH32V003 Flash 页大小 = 64 字节 */
#define SWIO_FLASH_PAGE   64

int swio_flash_erase(uint32_t addr, uint32_t len)
{
	uint32_t end;

	if (!g_port.valid) return SWIO_ERR_NO_PIN;
	if (len == 0) return SWIO_OK;

	end = addr + len;
	for (uint32_t a = addr; a < end; a += SWIO_FLASH_PAGE) {
		if (swio_flash_wait() != SWIO_OK) return SWIO_ERR_ERASE;
		swio_write_word(FLASH_CTLR, CTLR_PAGE_ER);
		swio_write_word(FLASH_ADDR, a);
		swio_write_word(FLASH_CTLR, CTLR_STRT | CTLR_PAGE_ER);
		if (swio_flash_wait() != SWIO_OK) return SWIO_ERR_ERASE;
		swio_write_word(FLASH_CTLR, 0);
	}
	return SWIO_OK;
}

int swio_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
	if (!g_port.valid || !data) return SWIO_ERR_NO_PIN;

	for (uint32_t off = 0; off < len; off += SWIO_FLASH_PAGE) {
		/* 开始页编程 + 复位缓冲 */
		if (swio_flash_wait() != SWIO_OK) return SWIO_ERR_WRITE;
		swio_write_word(FLASH_CTLR, CTLR_PAGE_PG);
		swio_write_word(FLASH_CTLR, CTLR_PAGE_PG | CTLR_BUF_RST);

		/* 写 64 字节（16 个字），V003 需每字 bufload */
		for (int i = 0; i < 16; i++) {
			uint32_t w = 0xFFFFFFFFu;
			uint32_t base = off + (uint32_t)i * 4;
			for (int j = 0; j < 4; j++) {
				if (base + (uint32_t)j < len) {
					((uint8_t *)&w)[j] = data[base + (uint32_t)j];
				}
			}
			swio_write_word(FLASH_CTLR, CTLR_PAGE_PG | CTLR_BUF_LOAD);
			swio_write_word(FLASH_CTLR, w);   /* 写缓冲（CTLR 地址即数据口） */
		}

		/* 触发页编程 */
		swio_write_word(FLASH_ADDR, addr + off);
		swio_write_word(FLASH_CTLR, CTLR_PAGE_PG | CTLR_STRT);
		if (swio_flash_wait() != SWIO_OK) return SWIO_ERR_WRITE;

		g_progress = (int)((off + SWIO_FLASH_PAGE) * 100 / len);
	}
	return SWIO_OK;
}

int swio_flash_verify(uint32_t addr, const uint8_t *data, uint32_t len)
{
	if (!g_port.valid || !data) return SWIO_ERR_NO_PIN;

	for (uint32_t off = 0; off < len; off += 4) {
		uint32_t got = 0, exp = 0xFFFFFFFFu;
		if (swio_read_word(addr + off, &got) != SWIO_OK) return SWIO_ERR_VERIFY;
		for (int j = 0; j < 4; j++) {
			if (off + (uint32_t)j < len) ((uint8_t *)&exp)[j] = data[off + (uint32_t)j];
		}
		if (got != exp) return SWIO_ERR_VERIFY;
	}
	return SWIO_OK;
}

void swio_set_pin(int pin)
{
	if (pin_to_port(pin, &g_port) != 0) {
		g_port.valid = 0;
		return;
	}
	/* 空闲时释放（高阻上拉） */
	swio_port_release(&g_port);
}

void swio_reset_target(void)
{
	if (!g_port.valid) return;
	swio_port_drive_low(&g_port);
	Delay_Ms(20);
	swio_port_release(&g_port);
	Delay_Ms(10);
}

int swio_flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
	int r;
	uint32_t id = 0;

	g_progress = 0;

	r = swio_handshake(NULL);
	if (r != SWIO_OK) return r;

	r = swio_read_chip_id(&id);
	if (r != SWIO_OK) return r;

	r = swio_flash_unlock();
	if (r != SWIO_OK) return r;

	g_progress = 5;
	r = swio_flash_erase(addr, len);
	if (r != SWIO_OK) return r;

	g_progress = 20;
	r = swio_flash_write(addr, data, len);
	if (r != SWIO_OK) return r;

	g_progress = 90;
	r = swio_flash_verify(addr, data, len);
	if (r != SWIO_OK) return r;

	g_progress = 100;
	return SWIO_OK;
}

int swio_progress(void)
{
	return g_progress;
}

/* ---------------------------------------------------------------------------
 * 流式烧录（HTTP 上传用）
 * ---------------------------------------------------------------------------
 * 无需缓存整个固件：边收边按 64B 页写入目标 Flash。
 * 流程：begin（握手+解锁+擦除）→ data（反复）→ end（写尾页）
 * ------------------------------------------------------------------------- */
static struct {
	int      active;
	uint32_t base;      /* 起始地址 */
	uint32_t total;     /* 总长度 */
	uint32_t written;   /* 已接收字节数 */
	uint8_t  buf[SWIO_FLASH_PAGE];  /* 页缓冲 */
	uint32_t buf_len;   /* 缓冲中字节数 */
} g_stream;

/* 写入一个完整的 64B 页 */
static int swio_stream_flush_page(void)
{
	uint32_t addr = g_stream.base + (g_stream.written / SWIO_FLASH_PAGE) * SWIO_FLASH_PAGE;
	uint32_t i;
	int r;

	/* 补齐到 64B（用 0xFF） */
	for (i = g_stream.buf_len; i < SWIO_FLASH_PAGE; i++) {
		g_stream.buf[i] = 0xFF;
	}

	if (swio_flash_wait() != SWIO_OK) return SWIO_ERR_WRITE;
	swio_write_word(FLASH_CTLR, CTLR_PAGE_PG);
	swio_write_word(FLASH_CTLR, CTLR_PAGE_PG | CTLR_BUF_RST);

	for (i = 0; i < SWIO_FLASH_PAGE; i += 4) {
		uint32_t w;
		memcpy(&w, &g_stream.buf[i], 4);
		swio_write_word(FLASH_CTLR, CTLR_PAGE_PG | CTLR_BUF_LOAD);
		swio_write_word(FLASH_CTLR, w);
	}

	swio_write_word(FLASH_ADDR, addr);
	swio_write_word(FLASH_CTLR, CTLR_PAGE_PG | CTLR_STRT);
	r = swio_flash_wait();
	if (r != SWIO_OK) return SWIO_ERR_WRITE;

	g_stream.buf_len = 0;
	return SWIO_OK;
}

int swio_stream_begin(uint32_t addr, uint32_t total_len)
{
	int r;
	uint32_t id = 0;

	g_progress = 0;
	g_stream.active = 0;
	g_stream.base = addr;
	g_stream.total = total_len;
	g_stream.written = 0;
	g_stream.buf_len = 0;

	if (!g_port.valid) return SWIO_ERR_NO_PIN;
	if (total_len == 0) return SWIO_ERR_WRITE;

	r = swio_handshake(NULL);
	if (r != SWIO_OK) return r;

	r = swio_read_chip_id(&id);
	if (r != SWIO_OK) return r;

	r = swio_flash_unlock();
	if (r != SWIO_OK) return r;

	/* 擦除整个目标范围（按 64B 页对齐向上取整） */
	uint32_t erase_len = ((total_len + SWIO_FLASH_PAGE - 1) / SWIO_FLASH_PAGE) * SWIO_FLASH_PAGE;
	g_progress = 5;
	r = swio_flash_erase(addr, erase_len);
	if (r != SWIO_OK) return r;

	g_stream.active = 1;
	g_progress = 10;
	return SWIO_OK;
}

int swio_stream_data(const uint8_t *data, uint32_t len)
{
	if (!g_stream.active) return SWIO_ERR_NO_PIN;

	while (len > 0) {
		uint32_t space = SWIO_FLASH_PAGE - g_stream.buf_len;
		uint32_t n = (len < space) ? len : space;
		int r;

		memcpy(&g_stream.buf[g_stream.buf_len], data, n);
		g_stream.buf_len += n;
		g_stream.written += n;
		data += n;
		len -= n;

		if (g_stream.buf_len >= SWIO_FLASH_PAGE) {
			r = swio_stream_flush_page();
			if (r != SWIO_OK) return r;
		}

		/* 进度 10~95 */
		if (g_stream.total) {
			g_progress = 10 + (int)((g_stream.written * 85) / g_stream.total);
		}
	}
	return SWIO_OK;
}

int swio_stream_end(void)
{
	int r;

	if (!g_stream.active) return SWIO_ERR_NO_PIN;

	/* 写最后不足一页的数据 */
	if (g_stream.buf_len > 0) {
		r = swio_stream_flush_page();
		if (r != SWIO_OK) return r;
	}

	g_stream.active = 0;
	g_progress = 100;
	return SWIO_OK;
}

int swio_stream_active(void)
{
	return g_stream.active;
}
