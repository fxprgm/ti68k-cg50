/* TIbench --fastmem: inline RAM/flash fast paths for Musashi's memory accesses (prepare.py
 * includes this from the generated m68kcpu.h and points its six m68ki_*_fc wrappers here).
 * Everything else (I/O, flash commands) goes to ti68k.c's m68k_read/write_memory_*. */
#ifndef TI_FASTMEM_H
#define TI_FASTMEM_H
#include <stdint.h>
#include "ti68k.h"

#define ti_inline_hook() (ti.instr++)

static inline unsigned ti_rd16(unsigned a)
{
	if (a < 0x200000)
		return *(const uint16_t *)(ti.ram + (a & (TI_RAM_SIZE - 2)));
	if (a < 0x600000 && !ti.fl_status && ti.fl_cmd != 0x90)
		return *(const uint16_t *)(ti.flash + (a & (TI_FLASH_SIZE - 2)));
	return m68k_read_memory_16(a);
}

static inline unsigned ti_rd8(unsigned a)
{
	if (a < 0x200000)
		return ti.ram[a & (TI_RAM_SIZE - 1)];
	return m68k_read_memory_8(a);
}

static inline unsigned ti_rd32(unsigned a)
{
	if (a < 0x200000) {
		const uint16_t *p = (const uint16_t *)(ti.ram + (a & (TI_RAM_SIZE - 2)));
		return (unsigned)p[0] << 16 | p[1];   /* 0x3FFFE wraps: AMS never does that */
	}
	return ti_rd16(a) << 16 | ti_rd16(a + 2);
}

static inline void ti_wr8(unsigned a, unsigned v)
{
	if (a >= 0x120 && a < 0x40000) ti.ram[a] = (uint8_t)v;
	else m68k_write_memory_8(a, v);
}

static inline void ti_wr16(unsigned a, unsigned v)
{
	if (a >= 0x120 && a < 0x40000) *(uint16_t *)(ti.ram + (a & ~1u)) = (uint16_t)v;
	else m68k_write_memory_16(a, v);
}

static inline void ti_wr32(unsigned a, unsigned v)
{
	if (a >= 0x120 && a < 0x3FFFC) {
		uint16_t *p = (uint16_t *)(ti.ram + (a & ~1u));
		p[0] = (uint16_t)(v >> 16);
		p[1] = (uint16_t)v;
	} else
		m68k_write_memory_32(a, v);
}
#endif
