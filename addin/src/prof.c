#include "prof.h"
#include <gint/clock.h>
#include <gint/mpu/tmu.h>
#include <gint/timer.h>

static int timer = -1;
static uint32_t pphi;

static int keep_running(void) { return TIMER_CONTINUE; }

void prof_init(void)
{
	for (int i = 0; i < 3 && timer < 0; i++)
		timer = timer_configure(i | TIMER_Pphi_4, 0xffffffff, GINT_CALL(keep_running));
	if (timer < 0)
		return;
	timer_start(timer);
	/* the casio-cg50 emulator doesn't model PLLCR: an implausible Pphi means the stock 29.49 MHz */
	pphi = clock_freq()->Pphi_f;
	if (pphi < 4000000 || pphi > 200000000)
		pphi = 29491200;
}

void prof_quit(void)
{
	if (timer >= 0)
		timer_stop(timer);
	timer = -1;
}

uint32_t prof_now(void) { return timer >= 0 ? SH7305_TMU.TMU[timer].TCNT : 0; }

uint32_t prof_us(uint32_t start, uint32_t end)
{
	return (uint32_t)((uint64_t)(start - end) * 4000000 / pphi);
}
