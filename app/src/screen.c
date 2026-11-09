/* The TI's LCD on the CG50 screen, converted only where it changed (a shadow copy of the 1-bit
 * frame), pushed only for the band of CG50 rows that changed (main.c rounds it to whole groups
 * of 4 rows, the rule of gint's partial DMA transfer on the real CG50).
 *
 * TI-89: 160x100 at x2 = 320x200, rows 12-211, columns 0-319 (the panel takes 320-395).
 * TI-92 Plus: 240x128 at x1.5 = 360x192, rows 16-207, columns 0-359: every 2 TI pixels become
 * 3 CG50 pixels, the middle one the average of its two neighbours (4 gray levels in all), so
 * text keeps even strokes instead of alternating 1- and 2-pixel columns. x1 would leave the
 * 6x8 font of AMS under 1 mm high; x2 (480x256) doesn't fit. */
#include "screen.h"
#include "mem.h"
#include "ti68k.h"

#include <gint/display.h>
#include <string.h>

#define SHADOW ((uint8_t *)MEM_SHADOW)

static int valid;
static uint16_t pal[5];      /* 0..4 quarters of "on" */

void screen_init(void)
{
	/* an LCD-like gray-green: off pixels light, on pixels dark */
	const int off_r = 24, off_g = 50, off_b = 22, on_r = 3, on_g = 6, on_b = 4;
	for (int q = 0; q <= 4; q++) {
		int r = off_r + (on_r - off_r) * q / 4, g = off_g + (on_g - off_g) * q / 4, b = off_b + (on_b - off_b) * q / 4;
		pal[q] = (uint16_t)(r << 11 | g << 5 | b);
	}
	valid = 0;
}

void screen_invalidate(void) { valid = 0; }

uint16_t screen_color(int q) { return pal[q]; }

void screen_area(int *x, int *y, int *w, int *h)
{
	if (ti.model == TI_MODEL_89)
		*x = 0, *y = SCREEN89_Y, *w = 320, *h = 200;
	else
		*x = 0, *y = SCREEN92_Y, *w = 360, *h = 192;
}

static void row89(const uint8_t *src, int y)
{
	uint32_t *d = (uint32_t *)(gint_vram + (SCREEN89_Y + 2 * y) * DWIDTH);
	uint32_t on = (uint32_t)pal[4] << 16 | pal[4], off = (uint32_t)pal[0] << 16 | pal[0];
	for (int bx = 0; bx < 20; bx++) {
		uint8_t b = src[bx];
		for (int i = 0; i < 8; i++) {
			*d++ = (b & 0x80) ? on : off;
			b <<= 1;
		}
	}
	memcpy(gint_vram + (SCREEN89_Y + 2 * y + 1) * DWIDTH, gint_vram + (SCREEN89_Y + 2 * y) * DWIDTH, 320 * 2);
}

static void pair92(const uint8_t *r0, const uint8_t *r1, int k)
{
	uint16_t *d0 = gint_vram + (SCREEN92_Y + 3 * k) * DWIDTH;
	uint16_t *d1 = d0 + DWIDTH, *d2 = d1 + DWIDTH;
	for (int bx = 0; bx < 30; bx++) {
		uint8_t u = r0[bx], v = r1[bx];
		for (int i = 0; i < 4; i++) {
			int a0 = u >> 7 & 1, b0 = u >> 6 & 1, a1 = v >> 7 & 1, b1 = v >> 6 & 1;
			u <<= 2, v <<= 2;
			*d0++ = pal[a0 * 4], *d0++ = pal[(a0 + b0) * 2], *d0++ = pal[b0 * 4];
			*d2++ = pal[a1 * 4], *d2++ = pal[(a1 + b1) * 2], *d2++ = pal[b1 * 4];
			*d1++ = pal[(a0 + a1) * 2], *d1++ = pal[a0 + b0 + a1 + b1], *d1++ = pal[(b0 + b1) * 2];
		}
	}
}

int screen_update(int *y0, int *y1)
{
	const uint8_t *lcd = ti_lcd();
	int lo = 1000, hi = -1;
	if (ti.model == TI_MODEL_89) {
		for (int y = 0; y < 100; y++) {
			const uint8_t *s = lcd + 30 * y;
			uint8_t *sh = SHADOW + 30 * y;
			if (valid && !memcmp(s, sh, 20))
				continue;
			memcpy(sh, s, 20);
			row89(s, y);
			if (lo > SCREEN89_Y + 2 * y) lo = SCREEN89_Y + 2 * y;
			hi = SCREEN89_Y + 2 * y + 1;
		}
	} else {
		for (int k = 0; k < 64; k++) {
			const uint8_t *s = lcd + 60 * k;
			uint8_t *sh = SHADOW + 60 * k;
			if (valid && !memcmp(s, sh, 60))
				continue;
			memcpy(sh, s, 60);
			pair92(s, s + 30, k);
			if (lo > SCREEN92_Y + 3 * k) lo = SCREEN92_Y + 3 * k;
			hi = SCREEN92_Y + 3 * k + 2;
		}
	}
	valid = 1;
	if (hi < 0)
		return 0;
	*y0 = lo, *y1 = hi;
	return 1;
}
