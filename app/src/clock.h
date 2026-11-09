#ifndef TI68K_CLOCK_H
#define TI68K_CLOCK_H

#include <stdint.h>
#include <gint/rtc.h>

void clock_init(void);
void clock_quit(void);
uint32_t clock_now(void);                          /* TMU timestamp (counts down) */
uint32_t clock_us(uint32_t start, uint32_t end);   /* microseconds between two (< ~9 min) */
/* real ms, monotonic (call at least every 9 minutes; stands still in the OS world) */
uint32_t clock_ms(void);
/* forget the time spent away (the main menu, power off): the next clock_ms() goes on from here */
void clock_resync(void);
/* the CG50's clock in seconds since 2000-01-01, 0 if not set */
uint32_t clock_cg_seconds(void);

#endif
