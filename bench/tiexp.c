/* tiexp: a scriptable desktop session with bench/ti68k.c, for finding out how AMS behaves
 * (power off, RAM flags) before writing the add-in code for it.
 *
 *   build/tiexp <rom> <outdir> "cmd; cmd; ..."
 *
 * Commands: run MS | press KEY (held 100 ms, then until idle) | down KEY | up KEY |
 * shot NAME (PNG of the LCD) | dump NAME (raw LCD NAME.lcd + PNG) | snap (copy RAM) | diff (RAM bytes changed since snap) |
 * ports (I/O state, interrupt counts, low power) | busy MS (run MS, print the busy %) |
 * peek A,B,... (RAM bytes, hex addresses).
 * KEY: a name of bench/scenario.c's tables, e.g. 2nd, diamond, alpha, on, f1, x. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "scenario.h"
#include "ti68k.h"

void ti_write_png_gray(const char *path, const uint8_t *pix, int w, int h, int s);

static const char *outdir = ".";
static uint8_t ram_snap[TI_RAM_SIZE];

void scen_run_ms(uint32_t ms) { ti_run(ms * (ti.clock_hz / 1000)); }
void scen_phase(int step, const char *name, const char *shot) { (void)step; (void)name; (void)shot; }

static void shot(const char *name)
{
	int w = ti_lcd_w(), h = ti_lcd_h();
	const uint8_t *lcd = ti_lcd();
	static uint8_t pix[240 * 128];
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			pix[y * w + x] = (lcd[y * 30 + x / 8] >> (7 - x % 8)) & 1 ? 0x20 : 0xc8;
	char path[512];
	snprintf(path, sizeof path, "%s/%s.png", outdir, name);
	ti_write_png_gray(path, pix, w, h, 2);
	printf("shot %s\n", path);
}

static void wait_idle(void)
{
	int quiet = 0;
	for (int i = 0; i < 30000 && quiet < 6; i++) {
		uint64_t c0 = ti.cycles, b0 = ti.busy_cycles;
		scen_run_ms(10);
		quiet = (ti.busy_cycles - b0) * 5 < ti.cycles - c0 ? quiet + 1 : 0;
	}
}

static int key(const char *name)
{
	int k = scen_key_lookup(name);
	if (k < 0) {
		fprintf(stderr, "no key %s\n", name);
		exit(1);
	}
	return k;
}

static void ports(void)
{
	printf("io  :");
	for (int i = 0; i < 32; i++) printf(" %02x", ti.io[i]);
	printf("\nio2 :");
	for (int i = 0; i < 32; i++) printf(" %02x", ti.io2[i]);
	printf("\nlowpower %d pending %02x lcd_on %d sr %04x pc %06x  ints", ti.lowpower, ti.pending, ti.lcd_on,
	       m68k_get_reg(NULL, M68K_REG_SR), m68k_get_reg(NULL, M68K_REG_PC));
	for (int i = 1; i < 8; i++) printf(" L%d:%u", i, ti.ints[i]);
	printf("  lowpower entries %u\n", ti.lowpower_entries);
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s <rom> <outdir> \"cmd; ...\"\n", argv[0]);
		return 1;
	}
	outdir = argv[2];
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	static uint8_t rom[TI_FLASH_SIZE + 1], ram[TI_RAM_SIZE], flash[TI_FLASH_SIZE];
	size_t n = fread(rom, 1, sizeof rom, f);
	fclose(f);
	if (ti_init(rom, (uint32_t)n, ram, flash)) { fprintf(stderr, "bad ROM\n"); return 1; }
	ti_reset();

	char *script = strdup(argv[3]);
	for (char *cmd = strtok(script, ";"); cmd; cmd = strtok(NULL, ";")) {
		char op[32] = "", arg[64] = "";
		if (sscanf(cmd, " %31s %63s", op, arg) < 1) continue;
		if (!strcmp(op, "run")) scen_run_ms((uint32_t)atoi(arg));
		else if (!strcmp(op, "press")) {
			int k = key(arg);
			ti_key(k, 1); scen_run_ms(100); ti_key(k, 0); wait_idle();
		}
		else if (!strcmp(op, "down")) ti_key(key(arg), 1);
		else if (!strcmp(op, "up")) ti_key(key(arg), 0);
		else if (!strcmp(op, "shot")) shot(arg);
		else if (!strcmp(op, "dump")) {     /* the raw LCD (30 x 128 bytes) */
			char path[512];
			snprintf(path, sizeof path, "%s/%s.lcd", outdir, arg);
			FILE *o = fopen(path, "wb");
			if (o) { fwrite(ti_lcd(), 1, 30 * 128, o); fclose(o); }
			shot(arg);
		}
		else if (!strcmp(op, "snap")) memcpy(ram_snap, ti.ram, TI_RAM_SIZE);
		else if (!strcmp(op, "diff")) {
			int shown = 0;
			for (uint32_t a = 0; a < TI_RAM_SIZE; a++)
				if (ram_snap[a] != ti.ram[a] && !(a >= 0x4c00 && a < 0x5c00) && !(a >= 0x4400 && a < 0x4c00)) {
					if (shown++ < 80) printf("  %05x: %02x -> %02x\n", a, ram_snap[a], ti.ram[a]);
				}
			printf("diff: %d bytes outside the LCD\n", shown);
		}
		else if (!strcmp(op, "ports")) ports();
		else if (!strcmp(op, "peek")) { /* peek ADDR,ADDR,... (hex) */
			for (char *q = arg; *q; ) {
				uint32_t a = (uint32_t)strtoul(q, &q, 16);
				printf(" [%05x]=%02x", a, ti.ram[a & (TI_RAM_SIZE - 1)]);
				if (*q == ',') q++;
				else break;
			}
			printf("\n");
		}
		else if (!strcmp(op, "busy")) {
			uint64_t c0 = ti.cycles, b0 = ti.busy_cycles, i0 = ti.instr;
			scen_run_ms((uint32_t)atoi(arg));
			printf("busy %.1f %% (%llu instr)\n", 100.0 * (ti.busy_cycles - b0) / (ti.cycles - c0),
			       (unsigned long long)(ti.instr - i0));
		}
		else { fprintf(stderr, "unknown command %s\n", op); return 1; }
	}
	return 0;
}
