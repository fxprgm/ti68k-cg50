/* The stats block: big-endian words at physical 0x0C1E0000, read by the casio-cg50 probe test
 * (emu_go/ti68k_app_probe_test.go, untracked). Append-only: the probe uses word offsets. */
#ifndef TI68K_STATS_H
#define TI68K_STATS_H

#include <stdint.h>
#include "mem.h"

typedef struct {
	uint32_t magic;        /* 0  'T68K' */
	uint32_t phase;        /* 1  PH_* */
	uint32_t emu_ms;       /* 2  emulated ms since the add-in started */
	uint32_t real_ms;      /* 3  real ms in the run loop */
	uint32_t frames;       /* 4  screen updates pushed */
	uint32_t speed10;      /* 5  emulated / real time over the last second, 0.1 % */
	uint32_t load10;       /* 6  SH-4 time in the core over the last second, 0.1 % */
	uint32_t rom_ms;       /* 7  ROM load time */
	uint32_t restore_ms;   /* 8  state restore time */
	uint32_t save_ms;      /* 9  last state save time */
	uint32_t state_bytes;  /* 10 size of the last state file written / read */
	uint32_t restored;     /* 11 0 fresh boot, 1 restored, 2 refused (fresh boot), 3 kept file, quit */
	uint32_t nblocks;      /* 12 flash blocks in the last state file */
	uint32_t core_us;      /* 13 SH-4 time in the core, total */
	uint32_t model;        /* 14 89 or 92 */
	uint32_t ti_off;       /* 15 1 while the TI is turned off (the add-in sleeps) */
	uint32_t keyev;        /* 16 CG50 key events seen */
	uint32_t mods;         /* 17 AMS's modifier flags (2nd 1, shift 2, diamond 4, alpha 8, locks 0x10/0x20) */
	uint32_t layer;        /* 18 the letter layer: 0 off, 1 one letter, 2 locked */
	uint32_t resumes;      /* 19 returns from the CG50's main menu / power off */
	uint32_t dram_check;   /* 20 after the last return: 1 memory intact, 2 reloaded from the file */
	uint32_t instr;        /* 21 68000 instructions (low 32 bits) */
	uint32_t saves;        /* 22 state files written */
	uint32_t ams_known;    /* 23 1 if this AMS version's modifier address is known */
	char msg[64];          /* 24-39 last message (errors) */
	uint32_t menus;        /* 40 times the emulator menu opened */
	uint32_t last_req;     /* 41 the last request from keys_event (KEYS_*) | key << 8 */
	uint32_t menu_keys;    /* 42 the last 4 keys the menu read, newest in the low byte */
} stats_t;

enum {
	PH_START = 0, PH_FIND = 1, PH_LOAD = 2, PH_RESTORE = 3, PH_RUN = 4, PH_MENU = 5, PH_SAVE = 6,
	PH_QUIT = 7, PH_OFF = 8, PH_HELP = 9, PH_CHOOSE = 10, PH_ASK = 11, PH_OSMENU = 12,
	PH_ERROR = 0xE0, /* + 1 bad embedded ROM, + 2 ROM load failed, + 3 no ROM */
};

#define ST ((volatile stats_t *)MEM_STATS)

void set_msg(const char *m);

#endif
