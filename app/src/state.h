/* The TI's state in the storage memory, so that its RAM, archive and settings survive quitting.
 *
 * File (next to the ROM file, named after it: TI89.rom -> TI89.sav) = an RLE stream, padded
 * to an even length, then a 64-byte trailer (state_trailer_t). The stream, uncompressed:
 *   u32 CPU_STATE_WORDS, the 68000's registers and flags (cpustate.c)
 *   the machine (ti_t, ti68k.h; its two pointers are ignored on restore)
 *   the RAM, 256 KB
 *   for every 64 KB flash block that differs from the ROM file: u32 block number, then the
 *   block XOR the ROM's (mostly zeros: AMS rewrites whole blocks of which little changes);
 *   then u32 0xFFFFFFFF
 * RLE: a control byte c < 0x80 is followed by c+1 literal bytes; c >= 0x80 by one byte to
 * repeat c-0x80+3 times.
 * Written as <name>.tmp, then renamed over <name>.sav. Restore checks the trailer (version,
 * layout of this build, the ROM's fingerprint), then decodes the whole stream once to check its
 * sums before changing anything. */
#ifndef TI68K_STATE_H
#define TI68K_STATE_H

#include <stdint.h>

typedef struct {
	char magic[8];          /* "TI68KSV" */
	uint32_t version;       /* STATE_VERSION */
	uint32_t layout;        /* this build's struct layout (another build: refused) */
	uint32_t rom_fp;        /* fingerprint of the ROM the state belongs to */
	uint32_t model;         /* 89 or 92 */
	uint32_t raw_size;      /* uncompressed stream bytes */
	uint32_t comp_size;     /* compressed bytes (before the even padding) */
	uint32_t nblocks;       /* flash blocks stored */
	uint32_t sum1, sum2;    /* Fletcher-style sums of the uncompressed stream */
	uint32_t cg_time;       /* informative: the CG50's clock at the save (s since 2000) */
	uint32_t emu_ms;        /* informative: emulated ms in the saving session */
	uint32_t reserved[1];
} state_trailer_t;

#define STATE_VERSION 1

typedef struct {
	uint32_t file_size, raw_size, nblocks;
} state_info_t;

/* the ROM just loaded into ROM_MEM: computes its fingerprint */
void state_rom_loaded(void);

/* save to `name` (e.g. "TI89.sav"); 0 or < 0 with *err set */
int state_save(const char *name, uint32_t emu_ms, state_info_t *info, const char **err);

/* restore from `name` on top of a freshly reset machine. 1: no file; 0: restored;
   -1: refused before anything changed (*err says why); -2: failed half-way (reset the machine) */
int state_restore(const char *name, state_info_t *info, const char **err);

#endif
