/* tibench: boot a TI-89 / TI-92 Plus flash image (AMS) in Musashi + bench/ti68k.c, run a key
 * scenario, and count the 68000 work: instructions per emulated second while AMS is busy and
 * while it idles. That rate is what a fx-CG50 port must reach for 100 % speed.
 *
 *   bench/build.sh && build/tibench <rom> [outdir]
 *
 * Per phase it prints: emulated ms, busy ms (CPU not in low-power/STOP), instructions, the
 * instruction rate while busy (M instr per busy second), cycles per instruction, interrupts.
 * Screenshots (PNG, LCD as the calculator shows it) go to outdir after every phase. At the end:
 * the most executed opcodes (with a disassembly) and the RAM/flash split.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include "scenario.h"
#include "ti68k.h"

extern void (*ti_hist_hook)(uint32_t pc);

/* ---- opcode histogram -------------------------------------------------------------------- */

static uint64_t hist[65536];
static uint64_t hist_group[16];

static void hist_hook(uint32_t pc)
{
	uint16_t op = (uint16_t)m68k_read_memory_16(pc);
	hist[op]++;
}

void ti_write_png_gray(const char *path, const uint8_t *pix, int w, int h, int s); /* png.c */

static const char *outdir = NULL;

static void screenshot(const char *name)
{
	if (!outdir) return;
	int w = ti_lcd_w(), h = ti_lcd_h();
	const uint8_t *lcd = ti_lcd();
	uint8_t *pix = malloc((size_t)(w * h));
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			pix[y * w + x] = (lcd[y * 30 + x / 8] >> (7 - x % 8)) & 1 ? 0x20 : 0xc8;
	char path[512];
	snprintf(path, sizeof path, "%s/%s.png", outdir, name);
	ti_write_png_gray(path, pix, w, h, 2);
	free(pix);
}

/* ---- measurement --------------------------------------------------------------------------- */

typedef struct {
	uint64_t cycles, busy, instr, instr_ram;
	uint32_t ints[8];
	double host;
} snap_t;

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static snap_t take(void)
{
	snap_t s = {ti.cycles, ti.busy_cycles, ti.instr, ti.instr_ram, {0}, now_s()};
	memcpy(s.ints, ti.ints, sizeof s.ints);
	return s;
}

static snap_t phase0, total0;

static double ms_of(uint64_t cycles) { return cycles * 1000.0 / ti.clock_hz; }

static void print_header(void)
{
	printf("%-34s %8s %8s %5s %10s %8s %7s %6s %6s %8s\n", "phase", "emu ms", "busy ms", "busy%",
	       "instr", "Mi/busy s", "cyc/ins", "RAM%", "int1", "host ms");
}

static void phase_end(const char *name, const char *shot)
{
	snap_t s = take();
	double ms = ms_of(s.cycles - phase0.cycles), bms = ms_of(s.busy - phase0.busy);
	uint64_t in = s.instr - phase0.instr;
	printf("%-34s %8.0f %8.0f %5.1f %10llu %8.3f %7.2f %6.1f %6u %8.1f\n", name, ms, bms,
	       ms > 0 ? 100.0 * bms / ms : 0, (unsigned long long)in, bms > 0 ? in / bms / 1000.0 : 0,
	       in ? (double)(s.busy - phase0.busy) / in : 0, in ? 100.0 * (s.instr_ram - phase0.instr_ram) / in : 0,
	       s.ints[1] - phase0.ints[1], (s.host - phase0.host) * 1000);
	fflush(stdout);
	if (shot) screenshot(shot);
	phase0 = s;
}

void scen_run_ms(uint32_t ms)
{
	ti_run(ms * (ti.clock_hz / 1000));
}

void scen_phase(int step, const char *name, const char *shot)
{
	(void)step;
	phase_end(name, shot);
}

/* ---- main ---------------------------------------------------------------------------------- */

static int cmp_desc(const void *a, const void *b)
{
	uint64_t x = hist[*(const uint32_t *)a], y = hist[*(const uint32_t *)b];
	return x < y ? 1 : x > y ? -1 : 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <rom> [outdir]\n", argv[0]);
		return 1;
	}
	if (argc > 2) outdir = argv[2];
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	static uint8_t rom[TI_FLASH_SIZE + 1];
	size_t n = fread(rom, 1, sizeof rom, f);
	fclose(f);
	static uint8_t ram[TI_RAM_SIZE], flash[TI_FLASH_SIZE];
	int r = ti_init(rom, (uint32_t)n, ram, flash);
	if (r) { fprintf(stderr, "bad ROM (%d)\n", r); return 1; }
	printf("model %s HW%d, CPU %u Hz, %u cycles per timer tick, flash at 0x%06x\n",
	       ti.model == TI_MODEL_89 ? "TI-89" : "TI-92 Plus", ti.hw, ti.clock_hz, ti.cycles_per_tick, ti.flash_base);
	if (!getenv("TIBENCH_NOHIST"))
		ti_hist_hook = hist_hook;
	ti_reset();
	printf("start: PC 0x%06x SSP 0x%06x\n", m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SP));
	print_header();
	total0 = phase0 = take();

	scenario_run();

	snap_t s = take();
	double ms = ms_of(s.cycles - total0.cycles), bms = ms_of(s.busy - total0.busy);
	uint64_t in = s.instr - total0.instr;
	printf("%-34s %8.0f %8.0f %5.1f %10llu %8.3f %7.2f %6.1f %6u %8.1f\n", "all", ms, bms, 100 * bms / ms,
	       (unsigned long long)in, in / bms / 1000.0, (double)(s.busy - total0.busy) / in,
	       100.0 * (s.instr_ram - total0.instr_ram) / in, s.ints[1] - total0.ints[1], (s.host - total0.host) * 1000);
	printf("interrupts taken: ");
	for (int i = 1; i < 8; i++) printf("L%d %u  ", i, ti.ints[i]);
	printf("\nlow-power entries %u, flash program %u erase %u, I/O reads %u writes %u\n",
	       ti.lowpower_entries, ti.flash_writes, ti.flash_erases, ti.io_reads, ti.io_writes);
	printf("flash blocks changed (64 KB, offset in the image):");
	for (int b = 0; b < 32; b++) if (ti.fl_dirty >> b & 1) printf(" 0x%06x", b << 16);
	printf("\n");
	/* changed byte ranges vs the image */
	for (uint32_t i = 0, k = 0; i < TI_FLASH_SIZE && k < 12; ) {
		if (ti.flash[i] == rom[i]) { i++; continue; }
		uint32_t j = i;
		while (j < TI_FLASH_SIZE && (ti.flash[j] != rom[j] || (j + 16 < TI_FLASH_SIZE && memcmp(ti.flash + j, rom + j, 16)))) j++;
		printf("  changed 0x%06x-0x%06x (%u bytes), now starts %02x %02x %02x %02x, was %02x %02x %02x %02x\n", i, j - 1, j - i,
		       ti.flash[i], ti.flash[i + 1], ti.flash[i + 2], ti.flash[i + 3], rom[i], rom[i + 1], rom[i + 2], rom[i + 3]);
		i = j;
		k++;
	}
	printf("host: %.1f ns per 68k instruction (Musashi + counters%s)\n",
	       (s.host - total0.host) * 1e9 / in, ti_hist_hook ? " + opcode histogram" : "");

	/* opcode statistics */
	static uint32_t idx[65536];
	uint64_t used = 0;
	for (uint32_t i = 0; i < 65536; i++) {
		idx[i] = i;
		hist_group[i >> 12] += hist[i];
		if (hist[i]) used++;
	}
	qsort(idx, 65536, sizeof idx[0], cmp_desc);
	printf("\n%llu distinct opcode words executed. By top nibble:", (unsigned long long)used);
	for (int g = 0; g < 16; g++) printf(" %X:%.1f%%", g, 100.0 * hist_group[g] / in);
	printf("\nTop 40 opcode words (%% of all instructions, cumulative):\n");
	double cum = 0;
	for (int i = 0; i < 40; i++) {
		uint32_t op = idx[i];
		uint8_t opd[10] = {(uint8_t)(op >> 8), (uint8_t)op, 0, 0, 0, 0, 0, 0, 0, 0};
		char dis[100];
		m68k_disassemble_raw(dis, 0, opd, opd, M68K_CPU_TYPE_68000);
		double p = 100.0 * hist[op] / in;
		cum += p;
		printf("  %04x %-28s %5.2f %6.2f\n", op, dis, p, cum);
	}
	int n90 = 0, n99 = 0;
	cum = 0;
	for (int i = 0; i < 65536 && cum < 99.0; i++) {
		cum += 100.0 * hist[idx[i]] / in;
		if (cum < 90.0) n90 = i + 2;
		n99 = i + 1;
	}
	printf("opcode words covering 90%%: %d, 99%%: %d\n", n90, n99);
	return 0;
}
