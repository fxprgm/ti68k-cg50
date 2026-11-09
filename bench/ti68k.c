/* ti68k: a minimal TI-89 / TI-92 Plus (HW2) machine around Musashi. See ti68k.h.
 *
 * Behaviour follows the documented hardware (ports at 0x600000 / 0x700000, the OSC2 timer
 * tick, auto-vector interrupts 1-7, the Sharp flash command set); where the docs are silent
 * it does what TiEmu does, since TiEmu is known to run AMS. Not cycle-exact: CPU cycles come
 * from Musashi's 68000 tables (no wait states). */
#include <string.h>

#include "m68k.h"
#include "ti68k.h"

ti_t ti;

/* ---- interrupts ---------------------------------------------------------------------------- */

static void update_irq(void)
{
	int level = 7;
	while (level > 0 && !(ti.pending & (1 << level)))
		level--;
	m68k_set_irq(level);
}

static void raise_int(int level)
{
	ti.pending |= 1 << level;
	update_irq();
}

/* Musashi's interrupt acknowledge: the level is held until the CPU takes it */
int ti_int_ack(int level)
{
	ti.pending &= ~(1 << level);
	ti.ints[level & 7]++;
	update_irq();
	return M68K_INT_ACK_AUTOVECTOR;
}

/* HW2: 0x70001F bits 2:1 = 10 masks the timer and keyboard interrupts */
static int hw2_ints_masked(void)
{
	return ti.hw >= 2 && (ti.io2[0x1f] & 4) && !(ti.io2[0x1f] & 2);
}

/* ---- per-instruction hook (counts) --------------------------------------------------------- */

#ifndef TI_LIGHT_HOOK
void (*ti_hist_hook)(uint32_t pc);

void ti_instr_hook(unsigned int pc)
{
	ti.instr++;
	if (pc < 0x200000)
		ti.instr_ram++;
	if (ti_hist_hook)
		ti_hist_hook(pc);
}
#else
/* the add-in: count only (a 64-bit increment, ~4 SH-4 instructions) */
void ti_instr_hook(unsigned int pc)
{
	(void)pc;
	ti.instr++;
}
#endif

/* ---- I/O ports 0x600000 -------------------------------------------------------------------- */

static uint8_t kbd_read_cols(void)
{
	uint16_t mask = (uint16_t)(ti.io[0x18] << 8 | ti.io[0x19]);
	uint8_t v = 0;
	for (int r = 0; r < 10; r++)
		if (!(mask & (1 << r)))
			v |= ti.keys[r];
	return (uint8_t)~v;
}

static uint8_t io_read(uint32_t a)
{
	a &= 31;
	uint8_t v = ti.io[a];
	ti.io_reads++;
	switch (a) {
	case 0x00: return (uint8_t)(v | 4);            /* bit 2: battery above the trip level */
	case 0x0d: ti.io[0x0d] = (uint8_t)((v & 0x80) | 0x40); return v; /* link status: read clears, TX empty */
	case 0x0f: ti.io[0x0d] &= ~0x20; return 0;     /* link receive buffer: nothing arrives */
	case 0x17: return ti.timer_value;
	case 0x1a: return (uint8_t)((v & ~2) | (ti.on_key ? 0 : 2)); /* bit 1: ON (0 = down) */
	case 0x1b: return kbd_read_cols();
	case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x1e: case 0x1f:
		return 0x14;
	default: return v;
	}
}

static void io_write(uint32_t a, uint8_t v)
{
	static const uint32_t timer_masks[4] = {0, 15, 127, 8191};
	a &= 31;
	ti.io_writes++;
	switch (a) {
	case 0x05:      /* turn off OSC1 (the CPU) until an interrupt in bits 0-4 (levels 1-5) or ON */
		ti.lowpower = 1;
		ti.lowpower_entries++;
		m68k_end_timeslice();
		break;
	case 0x0c:      /* link control; bits 6+5 reset the link */
		if ((v & 0x60) == 0x60)
			ti.io[0x0d] = 0x40;
		break;
	case 0x0f:      /* link transmit: no cable, the byte goes nowhere, TX stays empty */
		ti.io[0x0d] |= 0x40;
		break;
	case 0x10: if (ti.hw == 1) ti.lcd_addr = (uint32_t)(v << 8 | ti.io[0x11]) << 3; break;
	case 0x11: if (ti.hw == 1) ti.lcd_addr = (uint32_t)(ti.io[0x10] << 8 | v) << 3; break;
	case 0x15: ti.timer_mask = timer_masks[(v >> 4) & 3]; break;
	case 0x17: ti.timer_value = v; break;
	case 0x1c: ti.lcd_on = (v & 0x3c) != 0x3c; break;
	default: break;
	}
	ti.io[a] = v;
}

/* ---- I/O ports 0x700000 (HW2) -------------------------------------------------------------- */

static uint8_t io2_read(uint32_t a)
{
	ti.io_reads++;
	return ti.io2[a & 63];
}

static void io2_write(uint32_t a, uint8_t v)
{
	a &= 63;
	ti.io_writes++;
	switch (a) {
	case 0x17: ti.lcd_addr = 0x4c00 + 0x1000 * (v & 3); break;  /* LCD frame ("snoop range") */
	case 0x1d: ti.lcd_on = (v & 2) != 0; break;                  /* bit 1: screen enable */
	default: break;   /* 0x00-0x0F RAM execution protection, 0x12 flash protection: not enforced */
	}
	ti.io2[a] = v;
}

/* ---- flash (Sharp LH28F160S3 command set, as TI-89/92+ AMS uses it) ------------------------ */

static uint8_t flash_read8(uint32_t a)
{
	a &= TI_FLASH_SIZE - 1;
	if (ti.fl_cmd == 0x90) {        /* identifier codes */
		switch (a & 0xffff) {
		case 1: return 0x89;  /* manufacturer (Intel-compatible), low byte of word 0 */
		case 3: return 0xb5;  /* device */
		default: return 0;
		}
	}
	if (ti.fl_status)
		return 0xff;               /* status register: ready (TiEmu returns all ones) */
	return ti.flash[a];
}

static void flash_program(uint32_t a, uint8_t v)
{
	a &= TI_FLASH_SIZE - 1;
	ti.flash[a] &= v;                  /* programming only clears bits */
	ti.fl_dirty |= 1u << (a >> 16);
}

/* a command or (after 0x10/0x40) data; `width` 1 or 2 bytes */
static void flash_write(uint32_t a, uint32_t v, int width)
{
	if (ti.fl_cmd == 0x10 || ti.fl_cmd == 0x40) {
		if (width == 2) {
			flash_program(a, (uint8_t)(v >> 8));
			flash_program(a + 1, (uint8_t)v);
		} else
			flash_program(a, (uint8_t)v);
		ti.flash_writes++;
		ti.fl_cmd = 0x70;
		ti.fl_status = 1;
		return;
	}
	switch (v & 0xff) {
	case 0x10: case 0x40: ti.fl_cmd = (uint8_t)(v & 0xff); break;
	case 0x20: ti.fl_cmd = 0x20; break;
	case 0xd0:
		if (ti.fl_cmd == 0x20) {
			uint32_t b = (a & (TI_FLASH_SIZE - 1)) & ~0xffffu;
			memset(ti.flash + b, 0xff, 0x10000);
			ti.fl_dirty |= 1u << (b >> 16);
			ti.flash_erases++;
			ti.fl_cmd = 0x70;
			ti.fl_status = 1;
		}
		break;
	case 0x50: ti.fl_cmd = 0x50; break;               /* clear status */
	case 0x70: ti.fl_cmd = 0x70; ti.fl_status = 1; break;
	case 0x90: ti.fl_cmd = 0x90; break;
	case 0xff: ti.fl_cmd = 0xff; ti.fl_status = 0; break;
	default: break;
	}
}

/* ---- the bus (Musashi callbacks) ----------------------------------------------------------- */

unsigned int m68k_read_memory_8(unsigned int a)
{
	if (a < 0x200000) return ti.ram[a & (TI_RAM_SIZE - 1)];
	if (a < 0x600000) return flash_read8(a);
	if (a < 0x700000) return io_read(a);
	if (a < 0x800000 && ti.hw >= 2) return io2_read(a);
	return 0x14;
}

unsigned int m68k_read_memory_16(unsigned int a)
{
	if (a < 0x200000) {
		const uint8_t *p = ti.ram + (a & (TI_RAM_SIZE - 1));
		return (unsigned)(p[0] << 8 | p[1]);
	}
	if (a < 0x600000 && !ti.fl_status && ti.fl_cmd != 0x90) {
		const uint8_t *p = ti.flash + (a & (TI_FLASH_SIZE - 1));
		return (unsigned)(p[0] << 8 | p[1]);
	}
	return m68k_read_memory_8(a) << 8 | m68k_read_memory_8(a + 1);
}

unsigned int m68k_read_memory_32(unsigned int a)
{
	return m68k_read_memory_16(a) << 16 | m68k_read_memory_16(a + 2);
}

/* vector-table protection: writes below 0x120 raise level 7 while 0x600001 bit 2 is set */
static void ram_protect_check(uint32_t a)
{
	if ((a & (TI_RAM_SIZE - 1)) < 0x120 && (ti.io[1] & 4))
		raise_int(7);
}

void m68k_write_memory_8(unsigned int a, unsigned int v)
{
	if (a < 0x200000) {
		ram_protect_check(a);
		ti.ram[a & (TI_RAM_SIZE - 1)] = (uint8_t)v;
	} else if (a < 0x600000) flash_write(a, v, 1);
	else if (a < 0x700000) io_write(a, (uint8_t)v);
	else if (a < 0x800000 && ti.hw >= 2) io2_write(a, (uint8_t)v);
}

void m68k_write_memory_16(unsigned int a, unsigned int v)
{
	if (a < 0x200000) {
		ram_protect_check(a);
		uint8_t *p = ti.ram + (a & (TI_RAM_SIZE - 1));
		p[0] = (uint8_t)(v >> 8);
		p[1] = (uint8_t)v;
	} else if (a < 0x600000) flash_write(a, v & 0xffff, 2);
	else {
		m68k_write_memory_8(a, (v >> 8) & 0xff);
		m68k_write_memory_8(a + 1, v & 0xff);
	}
}

void m68k_write_memory_32(unsigned int a, unsigned int v)
{
	m68k_write_memory_16(a, v >> 16);
	m68k_write_memory_16(a + 2, v & 0xffff);
}

unsigned int m68k_read_disassembler_8(unsigned int a) { return m68k_read_memory_8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }

/* ---- the timer tick (OSC2/2^5: 16384 Hz on HW2) -------------------------------------------- */

static void tick(void)
{
	uint32_t t = ++ti.tick;
	int osc2 = ti.io[0x15] & 2;
	int masked = (ti.io[0x15] & 0x80) || hw2_ints_masked();

	/* the programmable rate generator 0x600017 counts up at the prescaler rate; level 5 when it
	   wraps to 0, then it reloads */
	if (osc2 && !(t & ti.timer_mask) && (ti.io[0x15] & 8)) {
		if (ti.timer_value == 0)
			ti.timer_value = ti.io[0x17];
		else if (++ti.timer_value == 0 && !masked)
			raise_int(5);
	}
	if (osc2 && !(t & 63) && !masked)                       /* level 1: 256 Hz */
		raise_int(1);
	if ((osc2 || ti.hw == 2) && !(t & 16383) && (ti.io[0x15] & 4) && !masked)
		raise_int(3);                                       /* level 3: 1 Hz clock (off in AMS) */
	if (ti.hw >= 2 && !(t % 192))
		ti.io2[0x1d] ^= 0x80;                               /* LCD frame toggle (~85 Hz) */

	/* level 4: link (error, activity, TX empty, RX full) when enabled in 0x60000C */
	if (!(ti.io[0x0c] & 0x40)) {
		uint8_t c = ti.io[0x0c], s = ti.io[0x0d];
		if (((c & 8) && (s & 0x80)) || ((c & 4) && (s & 8)) || ((c & 2) && (s & 0x40)) || ((c & 1) && (s & 0x20)))
			raise_int(4);
	}

	/* keyboard (128 Hz): level 2 when a key goes down, level 6 for ON */
	if (!(t & 127)) {
		if (ti.key_change) {
			if (!hw2_ints_masked())
				raise_int(2);
			ti.key_change = 0;
		}
		if (ti.on_change && ti.on_key) {
			raise_int(6);
			ti.on_change = 0;
		}
	}

	/* wake from low power: an interrupt the CPU asked for (0x600005 bits 0-4 = levels 1-5) or ON */
	if (ti.lowpower && ((ti.pending & (ti.io[5] << 1)) || (ti.pending & 0x40)))
		ti.lowpower = 0;
}

/* ---- public ---------------------------------------------------------------------------------- */

int ti_init(const uint8_t *rom, uint32_t size, uint8_t *ram, uint8_t *flash)
{
	if (size != TI_FLASH_SIZE)
		return -1;
	memset(&ti, 0, sizeof ti);
	ti.ram = ram;
	ti.flash = flash;
	if (flash != rom)
		memcpy(flash, rom, size);
	memset(ram, 0, TI_RAM_SIZE);
	rom = flash;

	/* hardware parameter block: pointer at flash + 0x104 (in the boot code) */
	uint32_t p = (uint32_t)(rom[0x104] << 24 | rom[0x105] << 16 | rom[0x106] << 8 | rom[0x107]);
	uint32_t off = p & 0xffff;
	if (off + 0x1a > size)
		return -2;
	uint32_t len = (uint32_t)(rom[off] << 8 | rom[off + 1]);
	uint32_t hwid = (uint32_t)(rom[off + 2] << 24 | rom[off + 3] << 16 | rom[off + 4] << 8 | rom[off + 5]);
	uint32_t gate = len >= 24 ? (uint32_t)(rom[off + 0x16] << 24 | rom[off + 0x17] << 16 | rom[off + 0x18] << 8 | rom[off + 0x19]) : 1;
	if (hwid == 3) ti.model = TI_MODEL_89;
	else if (hwid == 1) ti.model = TI_MODEL_92P;
	else return -3;
	ti.hw = gate >= 2 ? 2 : 1;
	ti.flash_base = ti.model == TI_MODEL_89 ? 0x200000 : 0x400000;
	ti.clock_hz = ti.hw == 1 ? 10000000 : 12000000;
	/* OSC2/2^5: 2^19/2^5 = 16384 Hz on HW2 (HW1's OSC2 is ~700 kHz but AMS sees it as ~10/7 s
	   per 2^14 ticks; TiEmu uses 427 cycles per tick there) */
	ti.cycles_per_tick = ti.hw == 1 ? 427 : 732;
	ti.lcd_addr = ti.hw >= 2 ? 0x4c00 : 0;
	ti.timer_mask = 15;
	ti.fl_cmd = 0xff;
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	return 0;
}

void ti_reset(void)
{
	memset(ti.io, 0, sizeof ti.io);
	memset(ti.io2, 0, sizeof ti.io2);
	if (ti.hw >= 2) {
		ti.io2[0x1d] = 2;
		ti.lcd_on = 1;
	}
	ti.pending = 0;
	ti.lowpower = 0;
	m68k_pulse_reset();
	/* Like TiEmu and MAME, start AMS from its own vector table (after the boot code): the
	   4-byte 0xCC filler that follows the OS header, then SSP and PC. */
	uint32_t vt = 0;
	for (uint32_t i = 0x12000; i + 18 < TI_FLASH_SIZE; i++) {
		const uint8_t *r = ti.flash + i;
		if (r[0] == 0x80 && r[1] == 0x7f && r[6] == 0xcc && r[7] == 0xcc && r[8] == 0xcc && r[9] == 0xcc) {
			vt = i + 10;
			break;
		}
	}
	const uint8_t *v = ti.flash + vt;
	uint32_t ssp = (uint32_t)(v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3]);
	uint32_t pc = (uint32_t)(v[4] << 24 | v[5] << 16 | v[6] << 8 | v[7]);
	m68k_set_reg(M68K_REG_SR, 0x2700);
	m68k_set_reg(M68K_REG_ISP, ssp);
	m68k_set_reg(M68K_REG_SP, ssp);
	m68k_set_reg(M68K_REG_PC, pc);
	update_irq();
}

void ti_run(uint32_t cycles)
{
	static int32_t avail;
	uint32_t ticks = (cycles + ti.cycles_per_tick - 1) / ti.cycles_per_tick;
	while (ticks--) {
		avail += (int32_t)ti.cycles_per_tick;
		while (avail > 0 && !ti.lowpower) {
			uint64_t before = ti.instr;
			int used = m68k_execute(avail);
			avail -= used;
			ti.cycles += (uint32_t)used;
			if (ti.instr != before)
				ti.busy_cycles += (uint32_t)used;
			else
				break;              /* STOP: wait for the next interrupt */
		}
		if (avail > 0) {            /* low power or STOP: the rest of this tick is idle */
			ti.cycles += (uint32_t)avail;
			avail = 0;
		}
		tick();
	}
}

void ti_sync_irq(void)
{
	update_irq();
}

void ti_key(int key, int down)
{
	if (key == TI_KEY_ON) {
		ti.on_key = (uint8_t)down;
		if (down)
			ti.on_change = 1;
		return;
	}
	uint8_t bit = (uint8_t)(1 << (key & 7));
	if (down) {
		ti.keys[key >> 3] |= bit;
		ti.key_change = 1;
	} else
		ti.keys[key >> 3] &= (uint8_t)~bit;
}

const uint8_t *ti_lcd(void) { return ti.ram + (ti.lcd_addr & (TI_RAM_SIZE - 1)); }
int ti_lcd_w(void) { return ti.model == TI_MODEL_89 ? 160 : 240; }
int ti_lcd_h(void) { return ti.model == TI_MODEL_89 ? 100 : 128; }
