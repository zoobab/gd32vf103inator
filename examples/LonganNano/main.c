/*
 * Copyright (c) 2019-2020, Emil Renner Berthing
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
#include <stdio.h>

#include "gd32vf103/csr.h"

#include "lib/mtimer.h"
#include "lib/eclic.h"
#include "lib/rcu.h"
#include "lib/gpio.h"
#include "lib/stdio-usbacm.h"
#include "lib/stdio-uart0.h"

#include "LonganNano.h"
#include "display.h"
#include "sdcard.h"
#include "term.h"

#include "ff.h"

extern struct dp_font ter16n;
extern struct dp_font ter16b;
extern struct dp_font ter20n;
extern struct dp_font ter20b;
extern struct dp_font ter24n;
extern struct dp_font ter24b;

#define BLINK (CORECLOCK/4) /* 1 second */

void MTIMER_IRQHandler(void)
{
	uint64_t next;

	gpio_pin_toggle(LED_BLUE);

	next = mtimer_mtimecmp() + BLINK;
	MTIMER->mtimecmp_hi = next >> 32;
	MTIMER->mtimecmp_lo = next;
}

/* if the compiler can't generate functions suitable
 * for interrupt handlers, we can't implement this
 * function directly in C
 */
#ifdef __interrupt
void trap_entry(void)
{
	unsigned long mcause = csr_read(CSR_MCAUSE);

	if ((mcause & CSR_MCAUSE_EXCCODE_Msk) == 0xfffU)
		fprintf(uart0, "nmi!\n");

	fprintf(uart0, "trap: mcause = 0x%08lx\n", mcause);
	fprintf(uart0, "trap: mepc   = 0x%08lx\n", csr_read(CSR_MEPC));
	fprintf(uart0, "trap: mtval  = 0x%08lx\n", csr_read(CSR_MTVAL));

	while (1)
		/* forever */;
}
#endif

static void mtimer_enable(void)
{
	uint64_t next = mtimer_mtime() + BLINK;

	MTIMER->mtimecmp_hi = next >> 32;
	MTIMER->mtimecmp_lo = next;

	eclic_config(MTIMER_IRQn, ECLIC_ATTR_TRIG_LEVEL, 1);
	eclic_enable(MTIMER_IRQn);
}

#if FF_MULTI_PARTITION
PARTITION VolToPart[FF_VOLUMES] = {
	{ 0, 0 }, /* drive 0, autodetect */
};
#endif

uint32_t get_fattime(void)
{
	return (2020U - 1980U) << 25 | /* year */
	                    1U << 21 | /* month */
	                    1U << 16 | /* day */
	                   12U << 11 | /* hour */
	                    0U <<  5 | /* minute */
	                   0/2 <<  0;  /* seconds/2 */
}

static FRESULT
listdir(struct term *term, const char *path)
{
	FILINFO fi;
	DIR dir;
	FRESULT res;

	res = f_opendir(&dir, path);
	if (res != FR_OK)
		return res;

	while (1) {
		char *p;
		char c;

		res = f_readdir(&dir, &fi);
		if (res != FR_OK)
			break;
		if (fi.fname[0] == '\0')
			break;

		p = fi.fname;
		for (c = *p++; c != '\0'; c = *p++)
			term_putchar(term, c);
		term_putchar(term, '\n');
	};

	f_closedir(&dir);
	return res;
}

int main(void)
{
	struct term term;
	FATFS fs;

	/* initialize system clock */
	rcu_sysclk_init();

	/* initialize eclic */
	eclic_init();
	/* enable global interrupts */
	eclic_global_interrupt_enable();

	uart0_init(CORECLOCK, 115200, 2);
	usbacm_init(4);
	stdout = usbacm;

	mtimer_enable();

	RCU->APB2EN |= RCU_APB2EN_PAEN | RCU_APB2EN_PCEN;

	gpio_pin_set(LED_RED);
	gpio_pin_set(LED_GREEN);
	gpio_pin_set(LED_BLUE);
	gpio_pin_config(LED_RED,   GPIO_MODE_OD_2MHZ);
	gpio_pin_config(LED_GREEN, GPIO_MODE_OD_2MHZ);
	gpio_pin_config(LED_BLUE,  GPIO_MODE_OD_2MHZ);

	dp_init();
	dp_fill(0,0,160,80,0x000);
	dp_puts(&ter16n, 3*ter16n.width, 0*ter16n.height, 0xfff, 0x000, "Hello World!");
	dp_puts(&ter16n, 3*ter16n.width, 1*ter16n.height, 0xf00, 0x000, "Hello World!");
	dp_puts(&ter16n, 3*ter16n.width, 2*ter16n.height, 0x0f0, 0x000, "Hello World!");
	dp_puts(&ter16n, 3*ter16n.width, 3*ter16n.height, 0x00f, 0x000, "Hello World!");
	dp_puts(&ter16n, 3*ter16n.width, 4*ter16n.height, 0xf0f, 0x000, "Hello World!");
	dp_on();

	dp_line(0,0,160,80,0xf00);
	dp_line(160,0,0,80,0xf00);

	term_init(&term, 0xfff, 0x000);

	sd_init();
	if (f_mount(&fs, "", 1) == FR_OK)
		listdir(&term, "");

	while (1) {
		int c = usbacm_getchar();

		switch (c) {
		case '\r':
			term_putchar(&term, '\n');
			break;
		case '\t':
			term_putchar(&term, ' ');
			term_putchar(&term, ' ');
			break;
		case 0x7f:
			term_delete(&term);
			break;
		default:
			term_putchar(&term, c);
		}
	}
}
