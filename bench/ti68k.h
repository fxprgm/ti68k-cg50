/* ti68k: a minimal TI-89 / TI-92 Plus (HW2) machine around Musashi, for measuring AMS on the
 * desktop. Written for this project from the hardware documentation (TIGCC/GCC4TI port docs,
 * TiEmu's hardware notes and sources, MAME's ti89 driver): see notes/ti68k.md.
 *
 * Plain C99, no host dependencies: the same file is meant to build for the fx-CG50 later. */
#ifndef TI68K_H
#define TI68K_H

#include <stdint.h>

#define TI_RAM_SIZE   0x40000u    /* 256 KB, mirrored over 0x000000-0x1FFFFF */
#define TI_FLASH_SIZE 0x200000u   /* 2 MB Sharp LH28F160S3 */

enum { TI_MODEL_89 = 89, TI_MODEL_92P = 92 };

/* keys: row * 8 + bit, as read through port 0x60001B with row mask 0x600018 */
#define TI_KEY(row, bit) ((row) * 8 + (bit))
#define TI_KEY_ON 0xFF

typedef struct {
	int model;              /* TI_MODEL_* (from the ROM's hardware parameter block) */
	int hw;                 /* gate array: 1 or 2 */
	uint32_t clock_hz;      /* CPU clock: 10 MHz (HW1), 12 MHz (HW2) */
	uint32_t cycles_per_tick; /* CPU cycles per OSC2/2^5 timer tick (16384 Hz on HW2) */

	uint8_t *ram;           /* TI_RAM_SIZE */
	uint8_t *flash;         /* TI_FLASH_SIZE */
	uint32_t flash_base;    /* 0x200000 (89) or 0x400000 (92+) */

	uint8_t io[32];         /* 0x600000 (mirrored every 32 bytes) */
	uint8_t io2[64];        /* 0x700000 (HW2) */
	uint32_t lcd_addr;      /* LCD frame (HW2: 0x4C00 + 0x1000 * (0x700017 & 3)) */
	int lcd_on;

	/* timers and interrupts */
	uint32_t tick;          /* OSC2/2^5 ticks since reset */
	uint32_t timer_mask;    /* 0x600017 prescaler (0x600015 bits 4-5) */
	uint8_t timer_value;    /* 0x600017 */
	uint8_t pending;        /* pending auto-vector levels (bit n = level n) */
	uint8_t lowpower;       /* CPU stopped by a write to 0x600005 */

	/* keyboard */
	uint8_t keys[10];       /* pressed keys, bit set = down */
	uint8_t on_key;
	uint8_t key_change, on_change;

	/* flash write state machine */
	uint8_t fl_cmd;         /* last command: 0xFF read array, 0x10/0x40 program, 0x20 erase setup, 0x90 id, 0x70/0x50 status */
	uint8_t fl_status;      /* reads return the status register (after program/erase) */
	uint32_t fl_dirty;      /* bit per 64 KB block changed since load */

	/* statistics */
	uint64_t cycles;        /* emulated CPU clocks */
	uint64_t busy_cycles;   /* of which the CPU ran (not in low-power / STOP) */
	uint64_t instr;         /* instructions executed */
	uint64_t instr_ram;     /* ... with PC in RAM */
	uint32_t ints[8];       /* interrupts taken per level */
	uint32_t lowpower_entries;
	uint32_t flash_writes, flash_erases;
	uint32_t io_reads, io_writes; /* 0x600000/0x700000 accesses */
} ti_t;

extern ti_t ti;

/* Load a 2 MB flash image (boot + AMS + archive). Returns 0 or a negative error. */
int ti_init(const uint8_t *rom, uint32_t size);
void ti_reset(void);
/* Run about `cycles` CPU clocks (whole timer ticks). */
void ti_run(uint32_t cycles);
void ti_key(int key, int down);
/* 1-bit LCD frame: 30 bytes per row, 128 rows (the 89 shows the top-left 160x100) */
const uint8_t *ti_lcd(void);
int ti_lcd_w(void);
int ti_lcd_h(void);

/* hooks the bench may define (weak defaults in ti68k.c) */
void ti_on_instr(uint32_t pc);

#endif
