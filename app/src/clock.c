/* Time: a free-running TMU (Pphi/4) measures real time; a 1 kHz timer only wakes the CPU from
 * sleep(). gint picks an ETMU for the 1 kHz one (32768 Hz based, right in the casio-cg50
 * emulator too, which doesn't model the PLL that TMU periods are computed from). */
#include "clock.h"

#include <gint/clock.h>
#include <gint/mpu/tmu.h>
#include <gint/timer.h>

static int tmu = -1, waker = -1;
static uint32_t pphi;
static uint32_t last;
static uint64_t ticks;

static int cont(void) { return TIMER_CONTINUE; }

void clock_init(void)
{
	for (int i = 0; i < 3 && tmu < 0; i++)
		tmu = timer_configure(i | TIMER_Pphi_4, 0xffffffff, GINT_CALL(cont));
	if (tmu >= 0)
		timer_start(tmu);
	pphi = clock_freq()->Pphi_f;
	if (pphi < 4000000 || pphi > 200000000)   /* the emulator: assume the stock 29.49 MHz */
		pphi = 29491200;
	waker = timer_configure(TIMER_ANY, 1000, GINT_CALL(cont));
	if (waker >= 0)
		timer_start(waker);
	last = clock_now();
}

void clock_quit(void)
{
	if (waker >= 0)
		timer_stop(waker);
	if (tmu >= 0)
		timer_stop(tmu);
	waker = tmu = -1;
}

uint32_t clock_now(void)
{
	return tmu >= 0 ? SH7305_TMU.TMU[tmu].TCNT : 0;
}

uint32_t clock_us(uint32_t start, uint32_t end)
{
	return (uint32_t)((uint64_t)(start - end) * 4000000 / pphi);   /* the TMU counts down */
}

uint32_t clock_ms(void)
{
	uint32_t now = clock_now();
	ticks += (uint32_t)(last - now);
	last = now;
	return (uint32_t)(ticks * 4000 / pphi);
}

void clock_resync(void)
{
	last = clock_now();
}

uint32_t clock_cg_seconds(void)
{
	rtc_time_t t;
	rtc_get_time(&t);
	if (t.year < 2000 || t.year > 2099 || t.month > 11 || t.month_day < 1)
		return 0;
	static const uint16_t before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	uint32_t y = t.year - 2000;
	uint32_t days = y * 365 + (y + 3) / 4 + before[t.month] + t.month_day - 1;
	if (t.month >= 2 && y % 4 == 0)
		days++;
	return ((days * 24 + t.hours) * 60 + t.minutes) * 60 + t.seconds;
}
