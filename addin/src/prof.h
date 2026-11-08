/* Microsecond timing with a TMU left running at Pphi/4. */
#pragma once
#include <stdint.h>

void prof_init(void);
void prof_quit(void);
uint32_t prof_now(void);                         /* the TMU counter (counts down) */
uint32_t prof_us(uint32_t start, uint32_t end);  /* microseconds between two stamps (< ~9 min) */
