/* State file: see state.h. */
#include "state.h"
#include "clock.h"
#include "cpustate.h"
#include "files.h"
#include "mem.h"
#include "ti68k.h"

#include <stddef.h>
#include <string.h>

#define SCRATCH ((uint8_t *)MEM_SCRATCH)
#define BLOCK 0x10000u
#define NBLOCKS (TI_FLASH_SIZE / BLOCK)

static uint32_t rom_fp;

static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 0x01000193u; }

void state_rom_loaded(void)
{
	const uint32_t *p = (const uint32_t *)ROM_MEM;
	uint32_t h = 0x811C9DC5u;
	for (uint32_t i = 0; i < TI_FLASH_SIZE / 4; i += 4)
		h = fnv(h, p[i] ^ (p[i + 1] << 1) ^ (p[i + 2] << 2) ^ (p[i + 3] << 3));
	rom_fp = h;
}

static uint32_t layout(void)
{
	uint32_t h = 0x811C9DC5u;
	h = fnv(h, sizeof(ti_t));
	h = fnv(h, offsetof(ti_t, fl_dirty));
	h = fnv(h, offsetof(ti_t, cycles));
	h = fnv(h, CPU_STATE_WORDS);
	h = fnv(h, TI_RAM_SIZE);
	h = fnv(h, STATE_VERSION);
	return h;
}

/* ---- encoder: RLE into IOBUF, flushed to the file 64 KB at a time ------------------------- */

static struct {
	int h, err;
	uint32_t outn, comp, raw, s1, s2;
	int nlit, rn;
	uint8_t rb;
	uint8_t lit[128];
} e;

static void flush(void)
{
	if (e.outn && !e.err && files_write(e.h, IOBUF, (int)e.outn) < 0)
		e.err = 1;
	e.outn = 0;
}

static inline void put(uint8_t b)
{
	IOBUF[e.outn++] = b;
	e.comp++;
	if (e.outn == MEM_IOSIZE)
		flush();
}

static void lits(void)
{
	if (!e.nlit)
		return;
	put((uint8_t)(e.nlit - 1));
	for (int i = 0; i < e.nlit; i++)
		put(e.lit[i]);
	e.nlit = 0;
}

static void run_out(void)
{
	lits();
	put((uint8_t)(0x80 + e.rn - 3));
	put(e.rb);
}

static void end_run(void)
{
	if (e.rn >= 3)
		run_out();
	else
		while (e.rn-- > 0) {
			e.lit[e.nlit++] = e.rb;
			if (e.nlit == 128)
				lits();
		}
	e.rn = 0;
}

static void enc(const uint8_t *p, uint32_t n)
{
	uint32_t s1 = e.s1, s2 = e.s2;
	for (uint32_t i = 0; i < n; i++) {
		uint8_t b = p[i];
		s1 += b;
		s2 += s1;
		if (e.rn && b == e.rb) {
			if (++e.rn == 130) {
				run_out();
				e.rn = 0;
			}
			continue;
		}
		end_run();
		e.rb = b;
		e.rn = 1;
	}
	e.s1 = s1, e.s2 = s2;
	e.raw += n;
}

static void enc32(uint32_t v)
{
	uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
	enc(b, 4);
}

static void base_name(char *out, const char *name, const char *ext)
{
	strcpy(out, name);
	char *dot = strrchr(out, '.');
	if (dot)
		*dot = 0;
	strcat(out, ext);
}

int state_save(const char *name, uint32_t emu_ms, state_info_t *info, const char **err)
{
	char tmp[FILES_NAME_MAX + 8];
	base_name(tmp, name, ".tmp");
	memset(&e, 0, sizeof e);
	e.h = files_create(tmp);
	if (e.h < 0) {
		*err = "cannot create the state file";
		return -1;
	}
	uint32_t cpu[CPU_STATE_WORDS];
	cpu_state_save(cpu);
	enc32(CPU_STATE_WORDS);
	enc((const uint8_t *)cpu, sizeof cpu);
	enc((const uint8_t *)&ti, sizeof ti);
	enc(ti.ram, TI_RAM_SIZE);
	uint32_t nb = 0;
	for (uint32_t b = 0; b < NBLOCKS && !e.err; b++) {
		const uint32_t *f = (const uint32_t *)(ti.flash + b * BLOCK), *r = (const uint32_t *)(ROM_MEM + b * BLOCK);
		if (!memcmp(f, r, BLOCK))
			continue;
		uint32_t *x = (uint32_t *)SCRATCH;
		for (uint32_t i = 0; i < BLOCK / 4; i++)
			x[i] = f[i] ^ r[i];
		enc32(b);
		enc(SCRATCH, BLOCK);
		nb++;
	}
	enc32(0xFFFFFFFFu);
	end_run();
	lits();
	uint32_t comp = e.comp;
	if (e.comp & 1)
		put(0);                          /* even writes */
	state_trailer_t t;
	memset(&t, 0, sizeof t);
	memcpy(t.magic, "TI68KSV", 8);
	t.version = STATE_VERSION;
	t.layout = layout();
	t.rom_fp = rom_fp;
	t.model = (uint32_t)ti.model;
	t.raw_size = e.raw;
	t.comp_size = comp;
	t.nblocks = nb;
	t.sum1 = e.s1, t.sum2 = e.s2;
	t.cg_time = clock_cg_seconds();
	t.emu_ms = emu_ms;
	if (e.outn + sizeof t > MEM_IOSIZE)
		flush();
	memcpy(IOBUF + e.outn, &t, sizeof t);
	e.outn += sizeof t;
	flush();
	int rc = files_close(e.h);
	if (e.err || rc < 0) {
		files_remove(tmp);
		*err = "writing the state file failed (storage memory full?)";
		return -2;
	}
	files_remove(name);
	if (files_rename(tmp, name) < 0) {
		*err = "cannot rename the state file";
		return -3;
	}
	if (info) {
		info->file_size = ((comp + 1) & ~1u) + sizeof t;
		info->raw_size = e.raw;
		info->nblocks = nb;
	}
	return 0;
}

/* ---- decoder ------------------------------------------------------------------------------- */

static struct {
	int h, err;
	uint32_t pos, left, inpos, inn, s1, s2, raw;
	int run, lit;
	uint8_t rb;
} d;

static int fill(void)
{
	uint32_t n = d.left < MEM_IOSIZE ? d.left : MEM_IOSIZE;
	if (!n || files_read(d.h, IOBUF, (int)((n + 1) & ~1u), (int)d.pos) < (int)n) {
		d.err = 1;
		return 0;
	}
	d.pos += n, d.left -= n, d.inpos = 0, d.inn = n;
	return 1;
}

static inline int get(void)
{
	if (d.inpos == d.inn && !fill())
		return 0;
	return IOBUF[d.inpos++];
}

/* n bytes into dst (NULL: check only) */
static void dec(uint8_t *dst, uint32_t n)
{
	uint32_t s1 = d.s1, s2 = d.s2;
	for (uint32_t i = 0; i < n && !d.err; i++) {
		uint8_t b;
		if (d.run) {
			b = d.rb;
			d.run--;
		} else if (d.lit) {
			b = (uint8_t)get();
			d.lit--;
		} else {
			int c = get();
			if (c >= 0x80) {
				d.rb = (uint8_t)get();
				d.run = c - 0x80 + 3;
				b = d.rb;
				d.run--;
			} else {
				d.lit = c + 1;
				b = (uint8_t)get();
				d.lit--;
			}
		}
		s1 += b;
		s2 += s1;
		if (dst)
			dst[i] = b;
	}
	d.s1 = s1, d.s2 = s2;
	d.raw += n;
}

static uint32_t dec32(void)
{
	uint8_t b[4];
	dec(b, 4);
	return (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
}

/* one pass over the stream: apply = 0 checks it only (sums, structure). 0 or -1 (*err) */
static int stream(const char *name, const state_trailer_t *t, int apply, const char **err)
{
	memset(&d, 0, sizeof d);
	d.h = files_open_read(name);
	if (d.h < 0) {
		*err = "cannot open the state file";
		return -1;
	}
	d.left = t->comp_size;
	static uint32_t cpu[CPU_STATE_WORDS];
	int bad = 0;
	if (dec32() != CPU_STATE_WORDS)
		bad = 1;
	if (!bad) {
		dec((uint8_t *)cpu, sizeof cpu);
		if (apply) {
			uint8_t *ram = ti.ram, *flash = ti.flash;
			dec((uint8_t *)&ti, sizeof ti);
			ti.ram = ram, ti.flash = flash;
		} else
			dec(SCRATCH, sizeof ti);
		dec(apply ? ti.ram : NULL, TI_RAM_SIZE);
	}
	uint32_t prev = 0, nb = 0;
	while (!bad && !d.err) {
		uint32_t b = dec32();
		if (b == 0xFFFFFFFFu)
			break;
		if (b >= NBLOCKS || (nb && b <= prev)) {
			bad = 1;
			break;
		}
		prev = b;
		nb++;
		dec(SCRATCH, BLOCK);
		if (apply) {
			uint32_t *f = (uint32_t *)(ti.flash + b * BLOCK);
			const uint32_t *x = (const uint32_t *)SCRATCH, *r = (const uint32_t *)(ROM_MEM + b * BLOCK);
			for (uint32_t i = 0; i < BLOCK / 4; i++)
				f[i] = r[i] ^ x[i];
		}
	}
	files_close(d.h);
	if (bad || d.err || d.raw != t->raw_size || d.s1 != t->sum1 || d.s2 != t->sum2 || nb != t->nblocks) {
		*err = d.err ? "the state file is cut short" : "the state file is damaged (bad checksum)";
		return -1;
	}
	if (apply) {
		cpu_state_load(cpu);
		/* keys were let go before the save; the CG50 starts with none held */
		memset(ti.keys, 0, sizeof ti.keys);
		ti.on_key = 0;
		ti.key_change = ti.on_change = 0;
		ti_sync_irq();
	}
	return 0;
}

int state_restore(const char *name, state_info_t *info, const char **err)
{
	int size = files_exists(name);
	if (size < 0)
		return 1;
	state_trailer_t t;
	if (size < (int)sizeof t + 8) {
		*err = "the state file is too short";
		return -1;
	}
	if (files_read_at(name, IOBUF, sizeof t, size - (int)sizeof t) < (int)sizeof t) {
		*err = "cannot read the state file";
		return -1;
	}
	memcpy(&t, IOBUF, sizeof t);
	if (memcmp(t.magic, "TI68KSV", 8)) {
		*err = "not a TI68k state file";
		return -1;
	}
	if (t.version != STATE_VERSION || t.layout != layout()) {
		*err = "it was saved by another version of TI68k";
		return -1;
	}
	if (t.model != (uint32_t)ti.model || t.rom_fp != rom_fp) {
		*err = "it was saved with another ROM";
		return -1;
	}
	if (((t.comp_size + 1) & ~1u) + sizeof t != (uint32_t)size) {
		*err = "the state file has the wrong size";
		return -1;
	}
	if (stream(name, &t, 0, err) < 0)
		return -1;
	if (stream(name, &t, 1, err) < 0)
		return -2;
	if (info) {
		info->file_size = (uint32_t)size;
		info->raw_size = t.raw_size;
		info->nblocks = t.nblocks;
	}
	return 0;
}
