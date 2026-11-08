/* The key scenario shared by the desktop bench (tibench.c) and the fx-CG50 add-in: boot, idle,
 * typing, three computations (factor, an integral, factor(2^64+1)), a graph and a redraw. */
#ifndef SCENARIO_H
#define SCENARIO_H

#include <stdint.h>

/* provided by the front end */
void scen_run_ms(uint32_t ms);                        /* run `ms` emulated milliseconds */
void scen_phase(int step, const char *name, const char *shot); /* a step ended */

extern int scen_step;                                 /* the step running now (0-based) */
int scenario_steps(void);
void scenario_run(void);                              /* all steps */
int scen_key_lookup(const char *name);                /* TI_KEY() of a key name, -1 if none */

#endif
