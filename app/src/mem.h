/* TI68k's DRAM layout. Free DRAM while an add-in runs: physical 0x0C1E0000-0x0C7FFFFF, used
 * through the cached P1 view (gint's arenas end below it). The OS zero-fills part of it at every
 * add-in launch, so nothing survives a quit: the TI's state goes to a file (state.c).
 *
 *   0x8C1E0000  stats block (stats.h), read by the casio-cg50 probe test
 *   0x8C1F0000  file I/O buffer, 64 KB, only used through its uncached P2 view (BFile may move
 *               data with the DMA, which doesn't see the CPU cache)
 *   0x8C200000  the TI's flash, 2 MB
 *   0x8C400000  the TI's RAM, 256 KB
 *   0x8C440000  Musashi's opcode handler table (256 KB; prepare.py moves it here)
 *   0x8C480000  Musashi's cycle tables (5 x 64 KB)
 *   0x8C4D0000  LCD shadow (4 KB, screen.c's change detection)
 *   0x8C4E0000  scratch, 64 KB (state.c: one flash block XOR the ROM)
 *   0x8C500000  the ROM as loaded (2 MB): fresh boots, and the flash blocks a save leaves out
 *   0x8C700000  end
 */
#ifndef TI68K_MEM_H
#define TI68K_MEM_H

#include <stdint.h>

#define MEM_STATS   0x8C1E0000u
#define MEM_IOBUF   0x8C1F0000u
#define MEM_IOSIZE  0x00010000u
#define MEM_FLASH   0x8C200000u
#define MEM_RAM     0x8C400000u
#define MEM_MUSASHI 0x8C440000u
#define MEM_MUS_END 0x8C4D0000u
#define MEM_SHADOW  0x8C4D0000u
#define MEM_SCRATCH 0x8C4E0000u
#define MEM_ROM     0x8C500000u
#define MEM_END     0x8C700000u

#define IOBUF ((uint8_t *)(MEM_IOBUF | 0x20000000u))  /* P2: uncached */
#define TI_FLASH_MEM ((uint8_t *)MEM_FLASH)
#define TI_RAM_MEM   ((uint8_t *)MEM_RAM)
#define ROM_MEM      ((uint8_t *)MEM_ROM)

#endif
