#ifndef TI68K_CPUSTATE_H
#define TI68K_CPUSTATE_H

#include <stdint.h>

#define CPU_STATE_WORDS 46   /* 16 + 7 + 23 */

int cpu_state_save(uint32_t *out);        /* returns CPU_STATE_WORDS */
void cpu_state_load(const uint32_t *in);

#endif
