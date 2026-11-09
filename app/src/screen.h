#ifndef TI68K_SCREEN_H
#define TI68K_SCREEN_H

#include <stdint.h>

#define SCREEN89_Y 12
#define SCREEN92_Y 16

void screen_init(void);
void screen_invalidate(void);            /* redraw everything at the next update */
/* convert what changed into gint_vram; 1 and the CG50 rows touched, or 0 */
int screen_update(int *y0, int *y1);
/* where the TI screen goes on the CG50 */
void screen_area(int *x, int *y, int *w, int *h);
uint16_t screen_color(int quarters);     /* the LCD palette: 0 off .. 4 on */

#endif
