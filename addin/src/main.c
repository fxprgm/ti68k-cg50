/* TIbench: Musashi + bench/ti68k.c (a TI-89 / TI-92 Plus HW2) as a gint add-in, to measure what
 * one 68000 instruction costs in SH-4 instructions. Measurement build only: it embeds the ROM
 * (addin/prepare.py), so the .g3a is never shared or committed.
 *
 * Unpacks the ROM into DRAM, boots AMS and runs bench/scenario.c (the same key scenario as the
 * desktop bench), drawing the calculator's LCD at 2x with counters beside it. A stats block at a
 * fixed DRAM address is read by the casio-cg50 probe test (emu_go/ti68k_probe_test.go).
 * EXIT or MENU quits.
 */
#include <gint/display.h>
#include <gint/keyboard.h>
#include <string.h>

#include "m68k.h"
#include "prof.h"
#include "scenario.h"
#include "ti68k.h"

/* DRAM that neither the OS nor gint uses while an add-in runs (see ce_cg50/addin/src/cg50_mem.h:
   physical 0x0C1E0000-0x0C7FFFFF) */
#define STATS_ADDR 0x8C1E0000u
#define FLASH_ADDR 0x8C200000u   /* 2 MB */
#define RAM_ADDR   0x8C400000u   /* 256 KB */
/* 0x8C440000: Musashi's opcode handler table (256 KB), 0x8C480000: its cycle tables (320 KB),
   both moved out of .bss by prepare.py; end 0x8C4D0000 */

extern const uint8_t ti_rom_pages[];

/* Big-endian words at physical 0x0C1E0000. The probe reads them; it may write `draw_ms`. */
typedef struct {
	uint32_t magic;      /* 'TI68' once valid */
	uint32_t phase;      /* PH_* */
	uint32_t emu_ms;     /* emulated milliseconds */
	uint32_t instr;      /* 68000 instructions (low 32 bits) */
	uint32_t busy_kc;    /* CPU clocks not in low power, / 1000 */
	uint32_t step;       /* scenario step + 1 while it runs; 100 = done */
	uint32_t frames;     /* screens drawn */
	uint32_t core_us;    /* SH-4 time inside ti_run (TMU) */
	uint32_t ui_us;      /* SH-4 time drawing / polling */
	uint32_t draw_ms;    /* draw at most every draw_ms of SH-4 time (0 = only at step ends) */
	uint32_t load_us;    /* ROM unpack + reset */
	uint32_t model;      /* 89 or 92 */
	uint32_t clock_hz;   /* emulated CPU clock */
} stats_t;

enum { PH_START = 0, PH_LOAD = 1, PH_CORE = 2, PH_UI = 3, PH_QUIT = 4, PH_ERROR = 0xE0 };

#define ST ((volatile stats_t *)STATS_ADDR)

static int quit;
static uint32_t last_draw;

/* ---- screen -------------------------------------------------------------------------------- */

/* the calculator's LCD at 2x (89: 320x200; 92+: 240x128 at 1x would fit, 2x does not: 1x here) */
static void draw_lcd(void)
{
	const uint8_t *lcd = ti_lcd();
	int w = ti_lcd_w(), h = ti_lcd_h();
	int s = ti.model == TI_MODEL_89 ? 2 : 1;
	int x0 = 0, y0 = (DHEIGHT - h * s) / 2;
	const uint16_t on = 0x2104, off = 0xC618;
	for (int y = 0; y < h; y++) {
		const uint8_t *row = lcd + y * 30;
		uint16_t *d = gint_vram + (y0 + y * s) * DWIDTH + x0;
		for (int bx = 0; bx < w / 8; bx++) {
			uint8_t b = row[bx];
			for (int i = 0; i < 8; i++) {
				uint16_t c = (b & 0x80) ? on : off;
				b <<= 1;
				if (s == 2) { d[0] = c; d[1] = c; d += 2; }
				else *d++ = c;
			}
		}
		if (s == 2)
			memcpy(gint_vram + (y0 + y * 2 + 1) * DWIDTH + x0, gint_vram + (y0 + y * 2) * DWIDTH + x0, (size_t)w * 2 * 2);
	}
}

static void line(int *y, const char *label, uint32_t v100)
{
	int x = DWIDTH - 74;
	dtext(x, *y, C_BLACK, label);
	*y += 12;
	dprint(x, *y, C_BLUE, "%u.%02u", (unsigned)(v100 / 100), (unsigned)(v100 % 100));
	*y += 16;
}

/* The panel's numbers, frozen when the scenario ends (step 100): afterwards the machine keeps
   idling and the live counters keep growing, which would hide the result. */
static uint32_t fin_emu_ms, fin_instr, fin_core_us;
static int fin_done;

static void draw(void)
{
	dclear(C_WHITE);
	draw_lcd();
	if (ST->step == 100 && !fin_done) {
		fin_emu_ms = ST->emu_ms;
		fin_instr = ST->instr;
		fin_core_us = ST->core_us;
		fin_done = 1;
	}
	uint32_t emu_ms = fin_done ? fin_emu_ms : ST->emu_ms;
	uint32_t instr = fin_done ? fin_instr : ST->instr;
	uint32_t core_ms = (fin_done ? fin_core_us : ST->core_us) / 1000;
	int y = 2;
	dtext(DWIDTH - 74, y, C_RED, "TIbench");
	y += 16;
	line(&y, "emu s", emu_ms / 10);
	line(&y, "M instr", instr / 10000);
	line(&y, "SH-4 s", core_ms / 10);
	line(&y, "% speed", core_ms ? (uint32_t)((uint64_t)emu_ms * 10000 / core_ms) : 0);
	if (fin_done)
		dtext(DWIDTH - 74, y, C_RED, "DONE");
	else
		dprint(DWIDTH - 74, y, C_BLACK, "step %u", (unsigned)ST->step);
	dtext(DWIDTH - 74, DHEIGHT - 12, C_BLACK, "EXIT:quit");
	dupdate();
	ST->frames++;
}

static void ui(void)
{
	uint32_t t = prof_now();
	ST->phase = PH_UI;
	key_event_t ev;
	while ((ev = pollevent()).type != KEYEV_NONE)
		if (ev.type == KEYEV_DOWN && (ev.key == KEY_EXIT || ev.key == KEY_MENU))
			quit = 1;
	uint32_t dms = ST->draw_ms;
	if (dms && prof_us(last_draw, t) >= dms * 1000) {
		draw();
		last_draw = prof_now();
	}
	ST->ui_us += prof_us(t, prof_now());
}

/* ---- scenario hooks ------------------------------------------------------------------------ */

static void update_stats(void)
{
	ST->emu_ms = (uint32_t)(ti.cycles / (ti.clock_hz / 1000));
	ST->instr = (uint32_t)ti.instr;
	ST->busy_kc = (uint32_t)(ti.busy_cycles / 1000);
}

/* run in 10 ms slices, polling keys and drawing in between */
void scen_run_ms(uint32_t ms)
{
	while (ms && !quit) {
		uint32_t n = ms < 10 ? ms : 10;
		ST->phase = PH_CORE;
		uint32_t t = prof_now();
		ti_run(n * (ti.clock_hz / 1000));
		ST->core_us += prof_us(t, prof_now());
		update_stats();
		ms -= n;
		ui();
	}
}

void scen_phase(int step, const char *name, const char *shot)
{
	(void)name; (void)shot;
	ST->step = (uint32_t)step + 2;   /* the next step is running */
	uint32_t t = prof_now();
	ST->phase = PH_UI;
	draw();
	last_draw = prof_now();
	ST->ui_us += prof_us(t, last_draw);
}

/* ---- main ---------------------------------------------------------------------------------- */

static int unpack_rom(uint8_t *flash)
{
	const uint8_t *p = ti_rom_pages;
	if (memcmp(p, "TIRM", 4))
		return 0;
	uint32_t n = (uint32_t)(p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7]);
	const uint8_t *idx = p + 8;
	const uint8_t *data = idx + ((n * 2 + 3) & ~3u);
	memset(flash, 0xff, TI_FLASH_SIZE);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t page = (uint32_t)(idx[2 * i] << 8 | idx[2 * i + 1]);
		memcpy(flash + page * 4096, data + i * 4096, 4096);
	}
	return 1;
}

int main(void)
{
	memset((void *)ST, 0, sizeof *ST);
	ST->draw_ms = 500;
	ST->magic = 0x54493638; /* TI68 */
	prof_init();
	dclear(C_WHITE);
	dtext(4, 4, C_BLACK, "TIbench: unpacking the ROM...");
	dupdate();

	ST->phase = PH_LOAD;
	uint32_t t0 = prof_now();
	uint8_t *flash = (uint8_t *)FLASH_ADDR;
	if (!unpack_rom(flash) || ti_init(flash, TI_FLASH_SIZE, (uint8_t *)RAM_ADDR, flash)) {
		ST->phase = PH_ERROR + 1;
		dtext(4, 20, C_RED, "bad embedded ROM");
		dupdate();
		getkey();
		prof_quit();
		return 1;
	}
	ti_reset();
	ST->load_us = prof_us(t0, prof_now());
	ST->model = (uint32_t)ti.model;
	ST->clock_hz = ti.clock_hz;
	ST->step = 1;
	last_draw = prof_now();

	scenario_run();
	ST->step = 100;
	draw(); /* freezes the result at once */
	while (!quit)
		scen_run_ms(10);
	ST->phase = PH_QUIT;
	prof_quit();
	return 0;
}
