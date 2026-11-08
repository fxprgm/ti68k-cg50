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

/* ---- PNG (stored deflate blocks) ----------------------------------------------------------- */

static uint32_t crc_table[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t k = i;
			for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
			crc_table[i] = k;
		}
	c = ~c;
	while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return ~c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
	uint8_t h[8];
	put32(h, n);
	memcpy(h + 4, type, 4);
	fwrite(h, 1, 8, f);
	if (n) fwrite(data, 1, n, f);
	uint32_t c = crc32_update(0, (const uint8_t *)type, 4);
	c = crc32_update(c, data, n);
	put32(h, c);
	fwrite(h, 1, 4, f);
}

/* gray 8-bit image, scaled by `s` */
static void write_png(const char *path, const uint8_t *pix, int w, int h, int s)
{
	int W = w * s, H = h * s;
	uint32_t raw_n = (uint32_t)(H * (W + 1));
	uint8_t *raw = malloc(raw_n);
	for (int y = 0; y < H; y++) {
		raw[y * (W + 1)] = 0;
		for (int x = 0; x < W; x++) raw[y * (W + 1) + 1 + x] = pix[(y / s) * w + x / s];
	}
	uint32_t nblk = (raw_n + 65534) / 65535;
	uint8_t *z = malloc(raw_n + nblk * 5 + 6);
	uint32_t zn = 0, a = 1, b = 0;
	z[zn++] = 0x78; z[zn++] = 0x01;
	for (uint32_t off = 0; off < raw_n; off += 65535) {
		uint32_t n = raw_n - off < 65535 ? raw_n - off : 65535;
		z[zn++] = off + n == raw_n;
		z[zn++] = n & 0xff; z[zn++] = n >> 8; z[zn++] = ~n & 0xff; z[zn++] = (~n >> 8) & 0xff;
		memcpy(z + zn, raw + off, n);
		zn += n;
	}
	for (uint32_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
	put32(z + zn, b << 16 | a);
	zn += 4;
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); exit(1); }
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	uint8_t ihdr[13];
	put32(ihdr, (uint32_t)W); put32(ihdr + 4, (uint32_t)H);
	ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	png_chunk(f, "IHDR", ihdr, 13);
	png_chunk(f, "IDAT", z, zn);
	png_chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw);
	free(z);
}

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
	write_png(path, pix, w, h, 2);
	free(pix);
}

/* ---- keys ---------------------------------------------------------------------------------- */

typedef struct { const char *name; int key; } keyname_t;

/* TI-89: rows of port 0x60001B (bit 7..0), TIGCC / TiEmu key matrix */
static const keyname_t keys89[] = {
	{"alpha", TI_KEY(0, 7)}, {"diamond", TI_KEY(0, 6)}, {"shift", TI_KEY(0, 5)}, {"2nd", TI_KEY(0, 4)},
	{"right", TI_KEY(0, 3)}, {"down", TI_KEY(0, 2)}, {"left", TI_KEY(0, 1)}, {"up", TI_KEY(0, 0)},
	{"f5", TI_KEY(1, 7)}, {"clear", TI_KEY(1, 6)}, {"^", TI_KEY(1, 5)}, {"/", TI_KEY(1, 4)},
	{"*", TI_KEY(1, 3)}, {"-", TI_KEY(1, 2)}, {"+", TI_KEY(1, 1)}, {"enter", TI_KEY(1, 0)},
	{"f4", TI_KEY(2, 7)}, {"bksp", TI_KEY(2, 6)}, {"t", TI_KEY(2, 5)}, {",", TI_KEY(2, 4)},
	{"9", TI_KEY(2, 3)}, {"6", TI_KEY(2, 2)}, {"3", TI_KEY(2, 1)}, {"neg", TI_KEY(2, 0)},
	{"f3", TI_KEY(3, 7)}, {"catalog", TI_KEY(3, 6)}, {"z", TI_KEY(3, 5)}, {")", TI_KEY(3, 4)},
	{"8", TI_KEY(3, 3)}, {"5", TI_KEY(3, 2)}, {"2", TI_KEY(3, 1)}, {".", TI_KEY(3, 0)},
	{"f2", TI_KEY(4, 7)}, {"mode", TI_KEY(4, 6)}, {"y", TI_KEY(4, 5)}, {"(", TI_KEY(4, 4)},
	{"7", TI_KEY(4, 3)}, {"4", TI_KEY(4, 2)}, {"1", TI_KEY(4, 1)}, {"0", TI_KEY(4, 0)},
	{"f1", TI_KEY(5, 7)}, {"home", TI_KEY(5, 6)}, {"x", TI_KEY(5, 5)}, {"=", TI_KEY(5, 4)},
	{"|", TI_KEY(5, 3)}, {"ee", TI_KEY(5, 2)}, {"sto", TI_KEY(5, 1)}, {"apps", TI_KEY(5, 0)},
	{"esc", TI_KEY(6, 0)}, {"on", TI_KEY_ON}, {NULL, 0}
};

/* TI-92 Plus */
static const keyname_t keys92[] = {
	{"down", TI_KEY(0, 7)}, {"right", TI_KEY(0, 6)}, {"up", TI_KEY(0, 5)}, {"left", TI_KEY(0, 4)},
	{"hand", TI_KEY(0, 3)}, {"shift", TI_KEY(0, 2)}, {"diamond", TI_KEY(0, 1)}, {"2nd", TI_KEY(0, 0)},
	{"3", TI_KEY(1, 7)}, {"2", TI_KEY(1, 6)}, {"1", TI_KEY(1, 5)}, {"f8", TI_KEY(1, 4)},
	{"w", TI_KEY(1, 3)}, {"s", TI_KEY(1, 2)}, {"z", TI_KEY(1, 1)},
	{"6", TI_KEY(2, 7)}, {"5", TI_KEY(2, 6)}, {"4", TI_KEY(2, 5)}, {"f3", TI_KEY(2, 4)},
	{"e", TI_KEY(2, 3)}, {"d", TI_KEY(2, 2)}, {"x", TI_KEY(2, 1)},
	{"9", TI_KEY(3, 7)}, {"8", TI_KEY(3, 6)}, {"7", TI_KEY(3, 5)}, {"f7", TI_KEY(3, 4)},
	{"r", TI_KEY(3, 3)}, {"f", TI_KEY(3, 2)}, {"c", TI_KEY(3, 1)}, {"sto", TI_KEY(3, 0)},
	{",", TI_KEY(4, 7)}, {")", TI_KEY(4, 6)}, {"(", TI_KEY(4, 5)}, {"f2", TI_KEY(4, 4)},
	{"t", TI_KEY(4, 3)}, {"g", TI_KEY(4, 2)}, {"v", TI_KEY(4, 1)}, {"space", TI_KEY(4, 0)},
	{"tan", TI_KEY(5, 7)}, {"cos", TI_KEY(5, 6)}, {"sin", TI_KEY(5, 5)}, {"f6", TI_KEY(5, 4)},
	{"y", TI_KEY(5, 3)}, {"h", TI_KEY(5, 2)}, {"b", TI_KEY(5, 1)}, {"/", TI_KEY(5, 0)},
	{"p", TI_KEY(6, 7)}, {"enter2", TI_KEY(6, 6)}, {"ln", TI_KEY(6, 5)}, {"f1", TI_KEY(6, 4)},
	{"u", TI_KEY(6, 3)}, {"j", TI_KEY(6, 2)}, {"n", TI_KEY(6, 1)}, {"^", TI_KEY(6, 0)},
	{"*", TI_KEY(7, 7)}, {"apps", TI_KEY(7, 6)}, {"clear", TI_KEY(7, 5)}, {"f5", TI_KEY(7, 4)},
	{"i", TI_KEY(7, 3)}, {"k", TI_KEY(7, 2)}, {"m", TI_KEY(7, 1)}, {"=", TI_KEY(7, 0)},
	{"esc", TI_KEY(8, 6)}, {"mode", TI_KEY(8, 5)}, {"+", TI_KEY(8, 4)},
	{"o", TI_KEY(8, 3)}, {"l", TI_KEY(8, 2)}, {"theta", TI_KEY(8, 1)}, {"bksp", TI_KEY(8, 0)},
	{"neg", TI_KEY(9, 7)}, {".", TI_KEY(9, 6)}, {"0", TI_KEY(9, 5)}, {"f4", TI_KEY(9, 4)},
	{"q", TI_KEY(9, 3)}, {"a", TI_KEY(9, 2)}, {"enter", TI_KEY(9, 1)}, {"-", TI_KEY(9, 0)},
	{"on", TI_KEY_ON}, {NULL, 0}
};

static const keyname_t *keytab;

static int key_lookup(const char *name)
{
	for (const keyname_t *k = keytab; k->name; k++)
		if (!strcmp(k->name, name)) return k->key;
	fprintf(stderr, "unknown key '%s'\n", name);
	exit(1);
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
static double tot_ms, tot_busy, tot_host;
static uint64_t tot_instr;

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

static void run_ms(double ms)
{
	ti_run((uint32_t)(ms * ti.clock_hz / 1000.0));
}

/* run until the CPU has been mostly idle (busy < 20 %) for 60 ms; give up after max_ms */
static void wait_idle(double max_ms)
{
	double waited = 0;
	int quiet = 0;
	while (waited < max_ms) {
		uint64_t c0 = ti.cycles, b0 = ti.busy_cycles;
		run_ms(10);
		waited += 10;
		double busy = (double)(ti.busy_cycles - b0) / (double)(ti.cycles - c0);
		quiet = busy < 0.2 ? quiet + 1 : 0;
		if (quiet >= 6) return;
	}
	printf("  (still busy after %.0f ms)\n", max_ms);
}

static void press(const char *name)
{
	int k = key_lookup(name);
	ti_key(k, 1);
	run_ms(100);
	ti_key(k, 0);
	wait_idle(300000);
}

/* "a b c": press each key in turn */
static void keys(const char *seq)
{
	char buf[256];
	snprintf(buf, sizeof buf, "%s", seq);
	for (char *t = strtok(buf, " "); t; t = strtok(NULL, " "))
		press(t);
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
	int r = ti_init(rom, (uint32_t)n);
	if (r) { fprintf(stderr, "bad ROM (%d)\n", r); return 1; }
	keytab = ti.model == TI_MODEL_89 ? keys89 : keys92;
	printf("model %s HW%d, CPU %u Hz, %u cycles per timer tick, flash at 0x%06x\n",
	       ti.model == TI_MODEL_89 ? "TI-89" : "TI-92 Plus", ti.hw, ti.clock_hz, ti.cycles_per_tick, ti.flash_base);
	ti_hist_hook = hist_hook;
	ti_reset();
	printf("start: PC 0x%06x SSP 0x%06x\n", m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SP));
	print_header();
	total0 = phase0 = take();

	/* boot: one-second steps until AMS idles */
	for (int s = 0; s < 6; s++) {
		char nm[32], shot[32];
		snprintf(nm, sizeof nm, "boot %d-%d s", s, s + 1);
		snprintf(shot, sizeof shot, "boot_%ds", s + 1);
		run_ms(1000);
		phase_end(nm, shot);
	}
	run_ms(5000);
	phase_end("idle at the home screen (5 s)", "idle");

	int is89 = ti.model == TI_MODEL_89;
	keys("2 + 3 enter");
	phase_end("2+3 ENTER", "add");
	keys("1 2 3 4 5 6 7");
	phase_end("type 7 digits", "digits");
	keys("clear clear");
	phase_end("CLEAR CLEAR", NULL);
	keys("f2 2 x ^ 3 - 1 )");
	phase_end("type factor(x^3-1)", NULL);
	keys("enter");
	phase_end("ENTER factor(x^3-1)", "factor");
	keys("clear f3 2 1 / ( x ^ 3 + 1 ) , x )");
	phase_end("type integral 1/(x^3+1) dx", NULL);
	keys("enter");
	phase_end("ENTER integral 1/(x^3+1) dx", "integral");
	keys("clear f2 2 2 ^ 6 4 + 1 )");
	phase_end("type factor(2^64+1)", NULL);
	keys("enter");
	phase_end("ENTER factor(2^64+1)", "factor64");
	/* graph sin(x): Y= editor, y1 = sin(x), GRAPH */
	if (is89) keys("diamond f1"); else keys("diamond w");
	phase_end("open Y= editor", "yequ");
	if (is89) keys("enter 2nd y x ) enter"); else keys("enter sin x ) enter");
	phase_end("type y1=sin(x)", NULL);
	if (is89) keys("diamond f3"); else keys("diamond r");
	phase_end("GRAPH sin(x)", "graph");
	keys("f2 6");
	phase_end("ZoomStd (F2 6) redraw", "zoomstd");
	if (is89) keys("home"); else keys("diamond q");
	run_ms(5000);
	phase_end("HOME, idle 5 s", "end");

	snap_t s = take();
	double ms = ms_of(s.cycles - total0.cycles), bms = ms_of(s.busy - total0.busy);
	uint64_t in = s.instr - total0.instr;
	printf("%-34s %8.0f %8.0f %5.1f %10llu %8.3f %7.2f %6.1f %6u %8.1f\n", "all", ms, bms, 100 * bms / ms,
	       (unsigned long long)in, in / bms / 1000.0, (double)(s.busy - total0.busy) / in,
	       100.0 * (s.instr_ram - total0.instr_ram) / in, s.ints[1] - total0.ints[1], (s.host - total0.host) * 1000);
	(void)tot_ms; (void)tot_busy; (void)tot_host; (void)tot_instr;
	printf("interrupts taken: ");
	for (int i = 1; i < 8; i++) printf("L%d %u  ", i, ti.ints[i]);
	printf("\nlow-power entries %u, flash program %u erase %u, I/O reads %u writes %u\n",
	       ti.lowpower_entries, ti.flash_writes, ti.flash_erases, ti.io_reads, ti.io_writes);
	printf("host: %.1f ns per 68k instruction (Musashi + counters + histogram)\n",
	       (s.host - total0.host) * 1e9 / in);

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
