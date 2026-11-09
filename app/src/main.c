/* TI68k: a TI-89 / TI-92 Plus emulator for the fx-CG50 (Musashi's 68000 + bench/ti68k.c).
 *
 * Holds no TI software: it loads the user's own ROM dump (any 2 MB TI-89 / TI-92 Plus flash
 * image, chosen once in a file browser and remembered in TI68k.cfg) from the storage memory, and
 * keeps the TI's state (RAM, archive, CPU) in a .sav file next to it between runs. SHIFT+MENU opens the emulator's menu; MENU goes to the CG50's
 * main menu (state saved first). Emulated time follows real time (a TMU); when the TI is idle
 * (AMS's low-power wait) the CG50 sleeps between 1 ms ticks, and when the TI is turned off the
 * add-in only waits for a key. The screen is redrawn where the TI's LCD changed (screen.c).
 *
 * Copyright (C) 2026; see notes/ti68k.md for the licence plan. Musashi: Copyright Karl
 * Stenerud, MIT licence (its notice is in its files, shown in About). */
#include <gint/cpu.h>
#include <gint/display.h>
#include <gint/drivers/r61524.h>
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "files.h"
#include "keys.h"
#include "m68k.h"
#include "mem.h"
#include "screen.h"
#include "state.h"
#include "stats.h"
#include "ti68k.h"

extern void m68ki_build_opcode_table(void);

#define VERSION "0.1"
#define ROM_SIZE TI_FLASH_SIZE

#define C_PANEL  C_RGB(4, 6, 10)
#define C_TITLE  C_RGB(31, 26, 8)
#define C_LABEL  C_RGB(20, 22, 26)
#define C_VALUE  C_WHITE
#define C_LIT    C_RGB(31, 20, 4)
#define C_DIM    C_RGB(9, 11, 15)
#define C_SEL    C_RGB(4, 10, 24)
#define C_FOOT   C_RGB(10, 10, 10)

void set_msg(const char *m)
{
	memset((char *)ST->msg, 0, sizeof ST->msg);
	strncpy((char *)ST->msg, m, sizeof ST->msg - 1);
}

/* ---- screen helpers ------------------------------------------------------------------------ */

static void push_all(void)
{
	r61524_display(gint_vram, 0, DHEIGHT, R61524_DMA_WAIT);
}

/* rows y0..y1 widened to whole groups of 4: r61524_display() moves 4 rows per DMA block, other
   heights leave the LCD's window short (the next frame lands shifted) or hang the DMA */
static void push_rows(int y0, int y1)
{
	y0 &= ~3;
	y1 |= 3;
	if (y1 > DHEIGHT - 1)
		y1 = DHEIGHT - 1;
	r61524_display(gint_vram, y0, y1 - y0 + 1, R61524_DMA_WAIT);
}

static void message(const char *title, const char *text, const char *footer)
{
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 22, C_PANEL);
	dtext(8, 6, C_TITLE, title);
	int y = 34;
	char line[80];
	for (const char *p = text; *p && y < DHEIGHT - 28; y += 15) {
		int n = 0;
		while (p[n] && p[n] != '\n' && n < 79)
			n++;
		memcpy(line, p, n);
		line[n] = 0;
		dtext(8, y, C_BLACK, line);
		p += n;
		if (*p == '\n')
			p++;
	}
	if (footer)
		dtext(8, DHEIGHT - 16, C_FOOT, footer);
	dupdate();
}

static int wait_key(void)
{
	clearevents();
	for (;;) {
		key_event_t ev = pollevent();
		if (ev.type == KEYEV_DOWN)
			return ev.key;
		if (ev.type == KEYEV_NONE)
			sleep();
	}
}

static void bar(int y, uint32_t done, uint32_t total)
{
	int w = 300, x = (DWIDTH - w) / 2;
	drect_border(x - 2, y - 2, x + w + 1, y + 13, C_WHITE, 1, C_BLACK);
	if (done && total)
		drect(x, y, x + (int)((uint64_t)w * (done > total ? total : done) / total) - 1, y + 11, C_RGB(6, 18, 6));
	dupdate();
}

/* ---- the ROM ------------------------------------------------------------------------------- */

typedef struct {
	char name[FILES_NAME_MAX];
	int model;
} rom_t;

static rom_t rom;                 /* the one running */
static char sav_name[FILES_NAME_MAX + 8];
static char ams[8] = "?";
static uint32_t mods_addr;        /* AMS's modifier flags in RAM, 0 = unknown version */

/* the model from a flash image's hardware parameter block (pointer at +0x104), 0 if none */
static int rom_model(const uint8_t *r, uint32_t avail)
{
	uint32_t off = (uint32_t)(r[0x106] << 8 | r[0x107]);
	if (off + 0x1a > avail)
		return 0;
	uint32_t id = (uint32_t)(r[off + 2] << 24 | r[off + 3] << 16 | r[off + 4] << 8 | r[off + 5]);
	return id == 3 ? TI_MODEL_89 : id == 1 ? TI_MODEL_92P : 0;
}

#ifndef TI68K_EMBED_ROM
/* ---- choosing the ROM file ----------------------------------------------------------------- */

#define CFG_NAME  "TI68k.cfg"     /* in the root: "TI68k ROM\n<the ROM's path>\n" */
#define CFG_MAGIC "TI68k ROM\n"
#define PICK_MAX  128
#define PICK_ROWS 9

static files_entry_t pick_ent[PICK_MAX];

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int name_cmp(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b))
		a++, b++;
	return lower(*a) - lower(*b);
}

/* a file name as shown in the list: without its folder */
static const char *leaf(const char *name)
{
	const char *p = strrchr(name, '\\');
	return p ? p + 1 : name;
}

/* what the user doesn't need to see: system folders, macOS's ._ files and .fseventsd */
static int hidden(const files_entry_t *e)
{
	const char *n = leaf(e->name);
	return n[0] == '.' || n[0] == '@' || !name_cmp(n, "System Volume Information") || !name_cmp(n, "LOST.DIR");
}

/* 0: the file is a TI-89 / TI-92 Plus flash image (r->model set); else an explanation in why */
static int check_rom(const char *name, rom_t *r, char *why, int max)
{
	int size = files_exists(name);
	if (size < 0) {
		snprintf(why, max, "%s can't be opened.", name);
		return -1;
	}
	if (size != ROM_SIZE) {
		snprintf(why, max, "%s is %d bytes.\nA TI-89 / TI-92 Plus ROM dump is 2 MB\n(2097152 bytes).",
		         leaf(name), size);
		return -1;
	}
	if (files_read_at(name, IOBUF, 0x10000, 0) < 0x10000) {
		snprintf(why, max, "Reading %s failed.", leaf(name));
		return -1;
	}
	int m = rom_model(IOBUF, 0x10000);
	if (!m) {
		snprintf(why, max, "%s is 2 MB but isn't a TI-89 or\nTI-92 Plus ROM dump.", leaf(name));
		return -1;
	}
	strcpy(r->name, name);
	r->model = m;
	return 0;
}

/* the ROM chosen last time (TI68k.cfg), if it is still there and still a ROM */
static int cfg_rom(rom_t *r)
{
	char buf[FILES_NAME_MAX + 16] = "";
	int n = files_read_at(CFG_NAME, buf, sizeof buf - 2, 0);
	if (n <= (int)strlen(CFG_MAGIC) || memcmp(buf, CFG_MAGIC, strlen(CFG_MAGIC)))
		return -1;
	buf[n] = 0;
	char *name = buf + strlen(CFG_MAGIC), *nl = strchr(name, '\n');
	if (!nl || nl == name || nl - name >= FILES_NAME_MAX)
		return -1;
	*nl = 0;
	char why[160];
	return check_rom(name, r, why, sizeof why);
}

static void cfg_save(const char *name)
{
	char buf[FILES_NAME_MAX + 16];
	int n = snprintf(buf, sizeof buf, CFG_MAGIC "%s\n", name);
	if (n & 1)
		buf[n++] = '\n';      /* BFile writes even sizes */
	int h = files_create(CFG_NAME);
	if (h < 0)
		return;
	files_write(h, buf, n);
	files_close(h);
}

/* the folder part of a name, with its '\' ("" for the root) */
static void folder_of(char *dir, const char *name)
{
	strcpy(dir, name);
	char *p = strrchr(dir, '\\');
	*(p ? p + 1 : dir) = 0;
}

/* a file browser over the storage memory: any file of any name can be chosen; it must be a 2 MB
   TI-89 / TI-92 Plus flash image. `current` (or NULL) is the ROM running now. 0 = chosen (*r),
   -1 = EXIT at the root */
static int pick_rom(rom_t *r, const rom_t *current)
{
	ST->phase = PH_CHOOSE;
	char dir[FILES_NAME_MAX] = "", came_from[FILES_NAME_MAX] = "";
	if (current)
		folder_of(dir, current->name);
	for (;;) {
		/* the folder's entries: ".." first (in a folder), then folders, then files, by name */
		int n = 0;
		if (*dir) {
			strcpy(pick_ent[0].name, "..");
			pick_ent[0].dir = 1;
			pick_ent[0].size = 0;
			n = 1;
		}
		int got = files_list(dir, pick_ent + n, PICK_MAX - n);
		for (int i = n, end = n + got; i < end; i++)
			if (!hidden(&pick_ent[i]))
				pick_ent[n++] = pick_ent[i];
		int first = *dir ? 1 : 0;
		for (int i = first + 1; i < n; i++) {
			files_entry_t e = pick_ent[i];
			int j = i;
			while (j > first && (pick_ent[j - 1].dir < e.dir
			       || (pick_ent[j - 1].dir == e.dir && name_cmp(leaf(pick_ent[j - 1].name), leaf(e.name)) > 0))) {
				pick_ent[j] = pick_ent[j - 1];
				j--;
			}
			pick_ent[j] = e;
		}
		/* the cursor: on the folder just left, else the first 2 MB file that isn't the running
		   ROM, else the top */
		int sel = 0, best = -1;
		for (int i = 0; i < n; i++) {
			if (*came_from && pick_ent[i].dir && !strcmp(pick_ent[i].name, came_from)) {
				best = i;
				break;
			}
			if (best < 0 && !pick_ent[i].dir && pick_ent[i].size == ROM_SIZE
			    && !(current && !strcmp(pick_ent[i].name, current->name)))
				best = i;
		}
		if (best >= 0)
			sel = best;
		*came_from = 0;

		for (int reload = 0; !reload;) {
			int top = sel < PICK_ROWS ? 0 : sel - PICK_ROWS + 1;
			set_msg(n ? leaf(pick_ent[sel].name) : "");
			dclear(C_WHITE);
			drect(0, 0, DWIDTH - 1, 22, C_PANEL);
			dtext(8, 6, C_TITLE, "TI68k: choose your ROM");
			dprint(8, 28, C_BLACK, "The ROM dump of your TI-89 / TI-92 Plus (2 MB).");
			dprint(8, 44, C_RGB(4, 8, 20), "Storage memory\\%s", dir);
			if (!n)
				dtext(16, 66, C_FOOT, "(no files here)");
			for (int i = top; i < n && i < top + PICK_ROWS; i++) {
				int y = 62 + 15 * (i - top);
				const files_entry_t *e = &pick_ent[i];
				if (i == sel)
					drect(4, y - 2, DWIDTH - 5, y + 12, C_SEL);
				int fg = i == sel ? C_WHITE : C_BLACK;
				if (e->dir) {
					dprint(12, y, fg, "%s\\", leaf(e->name));
					dtext(320, y, i == sel ? C_WHITE : C_FOOT, !strcmp(e->name, "..") ? "up" : "folder");
				} else {
					int rom2 = e->size == ROM_SIZE;
					dtext(12, y, fg, leaf(e->name));
					if (rom2)
						dtext(320, y, i == sel ? C_WHITE : C_RGB(2, 18, 4), "2 MB");
					else
						dprint(320, y, i == sel ? C_WHITE : C_FOOT, "%d KB", (int)((e->size + 1023) / 1024));
				}
			}
			if (top > 0)
				dtext(DWIDTH - 14, 62, C_FOOT, "^");
			if (top + PICK_ROWS < n)
				dtext(DWIDTH - 14, 62 + 15 * (PICK_ROWS - 1), C_FOOT, "v");
			dtext(8, DHEIGHT - 16, C_FOOT, *dir ? "EXE: choose / open    EXIT: back"
			                                   : current ? "EXE: choose / open    EXIT: cancel"
			                                             : "EXE: choose / open    EXIT: quit");
			dupdate();

			int k = wait_key();
			if (k == KEY_UP && n)
				sel = (sel + n - 1) % n;
			else if (k == KEY_DOWN && n)
				sel = (sel + 1) % n;
			else if (k == KEY_LEFT && n)
				sel = sel > PICK_ROWS ? sel - PICK_ROWS : 0;
			else if (k == KEY_RIGHT && n)
				sel = sel + PICK_ROWS < n ? sel + PICK_ROWS : n - 1;
			else if (k == KEY_EXIT || k == KEY_MENU) {
				if (!*dir)
					return -1;
				dir[strlen(dir) - 1] = 0;          /* up one folder */
				strcpy(came_from, dir);
				folder_of(dir, came_from);
				reload = 1;
			} else if (k == KEY_EXE && n) {
				const files_entry_t *e = &pick_ent[sel];
				if (!strcmp(e->name, "..")) {
					dir[strlen(dir) - 1] = 0;
					strcpy(came_from, dir);
					folder_of(dir, came_from);
					reload = 1;
				} else if (e->dir) {
					if (strlen(e->name) + 2 < FILES_NAME_MAX) {
						strcpy(dir, e->name);
						strcat(dir, "\\");
						reload = 1;
					}
				} else {
					char why[200];
					if (check_rom(e->name, r, why, sizeof why) == 0)
						return 0;
					set_msg("not a ROM");
					message("TI68k: not a ROM", why, "Press any key");
					wait_key();
				}
			}
		}
	}
}
#endif

static const char *model_name(int m) { return m == TI_MODEL_89 ? "TI-89" : "TI-92 Plus"; }

/* the AMS version ("2.09") from its "2.09, 03/27/2003" string, and what we know about it */
static void ams_info(void)
{
	const uint8_t *r = ROM_MEM;
	strcpy(ams, "?");
	for (uint32_t i = 0x12000; i < 0x1A0000; i++) {
		if (r[i + 1] != '.' || r[i + 4] != ',' || r[i + 5] != ' ' || r[i + 8] != '/' || r[i + 11] != '/')
			continue;
		if (r[i] < '1' || r[i] > '9' || r[i + 2] < '0' || r[i + 2] > '9' || r[i + 3] < '0' || r[i + 3] > '9')
			continue;
		memcpy(ams, r + i, 4);
		ams[4] = 0;
		break;
	}
	/* found by diffing the RAM around a 2nd / shift / diamond / alpha tap (bench/tiexp.c) */
	mods_addr = 0;
	if (!strcmp(ams, "2.09"))
		mods_addr = ti.model == TI_MODEL_89 ? 0x68b3 : 0x68af;
	ST->ams_known = mods_addr != 0;
}

static void fresh_machine(void)
{
	ti_init(ROM_MEM, ROM_SIZE, TI_RAM_MEM, TI_FLASH_MEM);
	ti_reset();
	keys_init(ti.model);
}

#ifdef TI68K_EMBED_ROM
extern const uint8_t ti_rom_pages[];

static int load_rom(void)
{
	ST->phase = PH_LOAD;
	message("TI68k", "Unpacking the built-in ROM...", NULL);
	const uint8_t *p = ti_rom_pages;
	if (memcmp(p, "TIRM", 4))
		return -1;
	uint32_t n = (uint32_t)(p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7]);
	const uint8_t *idx = p + 8, *data = idx + ((n * 2 + 3) & ~3u);
	memset(ROM_MEM, 0xff, ROM_SIZE);
	for (uint32_t i = 0; i < n; i++)
		memcpy(ROM_MEM + (uint32_t)(idx[2 * i] << 8 | idx[2 * i + 1]) * 4096, data + i * 4096, 4096);
	rom.model = rom_model(ROM_MEM, ROM_SIZE);
	strcpy(rom.name, rom.model == TI_MODEL_89 ? "TI89.rom" : "TI92P.rom");  /* names the .sav */
	return rom.model ? 0 : -1;
}
#else
static int load_rom(void)
{
	ST->phase = PH_LOAD;
	char text[120];
	snprintf(text, sizeof text, "Loading %s (%s)...", rom.name, model_name(rom.model));
	message("TI68k", text, NULL);
	uint32_t t0 = clock_ms();
	int h = files_open_read(rom.name);
	if (h < 0)
		return -1;
	for (uint32_t off = 0; off < ROM_SIZE; off += MEM_IOSIZE) {
		if (files_read(h, IOBUF, MEM_IOSIZE, (int)off) < (int)MEM_IOSIZE) {
			files_close(h);
			return -1;
		}
		memcpy(ROM_MEM + off, IOBUF, MEM_IOSIZE);
		bar(120, off + MEM_IOSIZE, ROM_SIZE);
	}
	files_close(h);
	ST->rom_ms = clock_ms() - t0;
	return rom_model(ROM_MEM, ROM_SIZE) == rom.model ? 0 : -1;
}
#endif

/* everything after the ROM bytes are in ROM_MEM */
static void rom_ready(void)
{
	state_rom_loaded();
	fresh_machine();
	ams_info();
	ST->model = (uint32_t)ti.model;
	/* next to the ROM: its name with .sav for its extension (a ROM named *.sav gets _.sav) */
	strcpy(sav_name, rom.name);
	char *slash = strrchr(sav_name, '\\'), *dot = strrchr(slash ? slash : sav_name, '.');
	if (!dot)
		dot = sav_name + strlen(sav_name);
	int is_sav = !strcmp(dot, ".sav") || !strcmp(dot, ".SAV") || !strcmp(dot, ".Sav");
	strcpy(is_sav ? dot + strlen(dot) : dot, is_sav ? "_.sav" : ".sav");
}

/* ---- state --------------------------------------------------------------------------------- */

static int save_state(void)
{
	ST->phase = PH_SAVE;
	keys_release_all();
	char text[100];
	snprintf(text, sizeof text, "Saving the %s's state to %s...", model_name(ti.model), sav_name);
	message("TI68k", text, NULL);
	uint32_t t0 = clock_ms();
	state_info_t info;
	const char *err = "";
	int rc = state_save(sav_name, (uint32_t)ST->emu_ms, &info, &err);
	ST->save_ms = clock_ms() - t0;
	if (rc < 0) {
		set_msg(err);
		snprintf(text, sizeof text, "%s.\nThe old state file (if any) is unchanged.", err);
		message("TI68k: the save failed", text, "Press any key");
		wait_key();
		return -1;
	}
	ST->state_bytes = info.file_size;
	ST->nblocks = info.nblocks;
	ST->saves++;
	return 0;
}

/* 0 restored or nothing to restore, -1 quit */
static int restore_state(void)
{
	ST->phase = PH_RESTORE;
	if (files_exists(sav_name) < 0)
		return 0;
	message("TI68k", "Restoring the saved state...", NULL);
	uint32_t t0 = clock_ms();
	state_info_t info;
	const char *err = "";
	int rc = state_restore(sav_name, &info, &err);
	ST->restore_ms = clock_ms() - t0;
	if (rc == 0) {
		ST->restored = 1;
		ST->state_bytes = info.file_size;
		ST->nblocks = info.nblocks;
		return 0;
	}
	if (rc == -2)
		fresh_machine();
	set_msg(err);
	ST->phase = PH_ASK;
	char text[300];
	snprintf(text, sizeof text,
	         "The saved state %s can't be used:\n%s.\n\n"
	         "EXE: start the %s fresh (a new boot);\n"
	         "     the old file is replaced at the next save.\n"
	         "EXIT: quit and keep the file as it is.", sav_name, err, model_name(ti.model));
	message("TI68k: saved state", text, NULL);
	for (;;) {
		int k = wait_key();
		if (k == KEY_EXE) {
			ST->restored = 2;
			return 0;
		}
		if (k == KEY_EXIT || k == KEY_MENU) {
			ST->restored = 3;
			return -1;
		}
	}
}

/* ---- leaving the add-in for a while (main menu, power off) -------------------------------- */

static uint32_t sum_words(uint32_t from, uint32_t to)
{
	const uint32_t *p = (const uint32_t *)from, *e = (const uint32_t *)to;
	uint32_t a = 0, b = 0;
	while (p < e) {
		a += *p++;
		b += a;
	}
	return a ^ (b << 1);
}

static uint32_t sum_all(void)
{
	return sum_words(MEM_FLASH, MEM_MUS_END) ^ (sum_words(MEM_ROM, MEM_END) << 3);
}

/* the state goes to the file first: if another add-in is started from the main menu, this one
   is gone without a word. Coming back, the DRAM is checked and, if anything moved it, everything
   is loaded again. Returns 0, or -1 if that reload failed (quit). */
static int away(int poweroff)
{
	ST->phase = PH_OSMENU;
	if (save_state() < 0)
		return 0;
	uint32_t sum = sum_all();
	if (poweroff)
		gint_poweroff(true);
	else
		gint_osmenu();
	clock_resync();
	ST->resumes++;
	keys_release_all();
	if (sum_all() == sum) {
		ST->dram_check = 1;
		return 0;
	}
	ST->dram_check = 2;
	m68ki_build_opcode_table();
	if (load_rom() < 0)
		return -1;
	rom_ready();
	return restore_state();
}

/* ---- the panel ----------------------------------------------------------------------------- */

static int show_speed;
static int panel_x;
static uint32_t panel_key = 0xFFFFFFFF;

static void diamond(int cx, int cy, int r, int color)
{
	for (int i = -r; i <= r; i++) {
		int w = r - (i < 0 ? -i : i);
		dline(cx - w, cy + i, cx + w, cy + i, color);
	}
}

/* modifier states for the panel: AMS's own flags when known, else the CG50 keys held */
static uint32_t mods_now(void)
{
	uint32_t m = 0;
	if (mods_addr)
		m = ti.ram[mods_addr];
	else {
		int cg = keys_cg_mods();
		m = (cg & 1 ? 1 : 0) | (cg & 2 ? 8 : 0);
	}
	ST->mods = m;
	return m;
}

static void indicator(int x, int y, int w, const char *label, int lit)
{
	drect(x, y, x + w - 1, y + 13, lit ? C_LIT : C_DIM);
	if (label)
		dtext_opt(x + w / 2, y + 2, lit ? C_BLACK : C_LABEL, C_NONE, DTEXT_CENTER, DTEXT_TOP, label, -1);
}

static void draw_panel(void)
{
	uint32_t m = mods_now();
	int layer = keys_layer();
	int x = panel_x, w = DWIDTH - panel_x;
	drect(x, 0, DWIDTH - 1, DHEIGHT - 1, C_PANEL);
	if (ti.model == TI_MODEL_89) {
		dtext(x + 6, 3, C_TITLE, "TI68k");
		dtext(x + 6, 17, C_LABEL, "TI-89");
		int y = 38, iw = w - 12;
		indicator(x + 6, y, iw, "2nd", m & 1);
		indicator(x + 6, y + 17, iw, NULL, m & 4);
		diamond(x + 6 + iw / 2, y + 17 + 6, 5, m & 4 ? C_BLACK : C_LABEL);
		indicator(x + 6, y + 34, iw, "shift", m & 2);
		indicator(x + 6, y + 51, iw, m & 0x30 ? (m & 0x10 ? "A-LOCK" : "a-lock") : "alpha", m & 0x38);
		indicator(x + 6, y + 68, iw, layer == 2 ? "ABC lock" : "ABC", layer);
		if (show_speed) {
			dtext(x + 6, 132, C_LABEL, "speed");
			dprint(x + 6, 144, C_VALUE, "%u%%", (unsigned)((ST->speed10 + 5) / 10));
			dtext(x + 6, 160, C_LABEL, "CPU");
			dprint(x + 6, 172, C_VALUE, "%u%%", (unsigned)((ST->load10 + 5) / 10));
		}
		dtext(x + 6, DHEIGHT - 28, C_LABEL, "SHIFT+");
		dtext(x + 6, DHEIGHT - 16, C_LABEL, "MENU");
	} else {
		/* 36 columns on the right, a strip under the screen */
		int y = 4, iw = w - 4;
		indicator(x + 2, y, iw, "2nd", m & 1);
		indicator(x + 2, y + 17, iw, NULL, m & 4);
		diamond(x + 2 + iw / 2, y + 17 + 6, 5, m & 4 ? C_BLACK : C_LABEL);
		indicator(x + 2, y + 34, iw, "shf", m & 2);
		indicator(x + 2, y + 51, iw, m & 0x30 ? (m & 0x10 ? "A" : "a") : "", m & 0x30);
		indicator(x + 2, y + 68, iw, layer == 2 ? "ABC" : "abc", layer);
		if (show_speed) {
			dtext(x + 2, 100, C_LABEL, "spd");
			dprint(x + 2, 112, C_VALUE, "%u", (unsigned)((ST->speed10 + 5) / 10));
			dtext(x + 2, 130, C_LABEL, "cpu");
			dprint(x + 2, 142, C_VALUE, "%u", (unsigned)((ST->load10 + 5) / 10));
		}
		drect(0, SCREEN92_Y + 192, x - 1, DHEIGHT - 1, C_PANEL);
		drect(0, 0, x - 1, SCREEN92_Y - 1, C_PANEL);
		dtext(4, 2, C_TITLE, "TI68k  TI-92 Plus");
		dtext(4, SCREEN92_Y + 194, C_LABEL, "SHIFT+MENU: menu   ALPHA: letters");
	}
	panel_key = m | (uint32_t)layer << 8 | (uint32_t)show_speed << 12 | ST->speed10 << 16;
}

/* the TI is off: a note over its screen */
static void draw_off(void)
{
	int x, y, w, h;
	screen_area(&x, &y, &w, &h);
	drect(x, y, x + w - 1, y + h - 1, screen_color(0));
	int cx = x + w / 2, cy = y + h / 2 - 30;
	dtext_opt(cx, cy, C_BLACK, C_NONE, DTEXT_CENTER, DTEXT_TOP, ti.model == TI_MODEL_89 ? "The TI-89 is off." : "The TI-92 Plus is off.", -1);
	dtext_opt(cx, cy + 20, C_BLACK, C_NONE, DTEXT_CENTER, DTEXT_TOP, "AC/ON: turn it on", -1);
	dtext_opt(cx, cy + 36, C_BLACK, C_NONE, DTEXT_CENTER, DTEXT_TOP, "SHIFT+AC/ON: turn the CG50 off", -1);
	dtext_opt(cx, cy + 52, C_BLACK, C_NONE, DTEXT_CENTER, DTEXT_TOP, "MENU: main menu", -1);
}

static void redraw_all(void)
{
	dclear(C_BLACK);
	screen_invalidate();
	int y0, y1;
	screen_update(&y0, &y1);
	draw_panel();
	push_all();
}

/* ---- help and about ------------------------------------------------------------------------ */

static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if ((*a | 32) != (*b | 32))
			return 0;
	return *a == *b;
}

static void help(void)
{
	ST->phase = PH_HELP;
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 18, C_PANEL);
	dprint(6, 3, C_TITLE, "Keys: CG50 -> %s", model_name(ti.model));
	int n = 0;
	for (int i = 0; i < keymap_count; i++) {
		const keymap_t *k = &keymap[i];
		if (same_name(k->cg_name, k->ti_name) || k->cg == KEY_MUL || k->cg == KEY_DIV)
			continue;
		int col = n / 10, y = 22 + 12 * (n % 10), x = 4 + 140 * col;
		dtext(x, y, C_BLUE, k->cg_name);
		dtext(x + 50, y, C_BLACK, k->ti_name);
		n++;
	}
	int y = 22 + 12 * 10 + 4;
	static const char *lines89[] = {
		"SHIFT+VARS: shift (TI)   SHIFT+AC/ON: OFF",
		"ALPHA, key: its red letter (SHIFT too: capital)",
		"SHIFT, ALPHA: letter lock; ALPHA ends it",
		"ALPHA held + key: the TI-89's own alpha key",
		"MENU: CG50 main menu (the state is saved first)",
	};
	static const char *lines92[] = {
		"SHIFT+VARS: shift   SHIFT+OPTN: HAND",
		"ALPHA, key: its red letter (^ = theta, . = space)",
		"SHIFT, ALPHA: letter lock; ALPHA ends it",
		"SHIFT+AC/ON: OFF    other keys: as labelled",
		"MENU: CG50 main menu (the state is saved first)",
	};
	for (int i = 0; i < 5; i++)
		dtext(4, y + 13 * i, C_BLACK, (ti.model == TI_MODEL_89 ? lines89 : lines92)[i]);
	dtext(4, DHEIGHT - 14, C_RED, "SHIFT+MENU: TI68k menu      any key: back");
	dupdate();
	wait_key();
}

static void about(void)
{
	char text[600];
	snprintf(text, sizeof text,
	         "TI68k " VERSION ": a TI-89 / TI-92 Plus emulator for the fx-CG50.\n"
	         "Running: %s, AMS %s, ROM %s\n"
	         "State file: %s\n\n"
	         "68000 core: Musashi, Copyright 1998-2002 Karl Stenerud,\n"
	         "MIT licence (github.com/kstenerud/Musashi).\n"
	         "TI68k contains no TI software: it runs the ROM dump of\n"
	         "your own calculator. TI-89, TI-92 Plus and AMS are\n"
	         "trademarks / software of Texas Instruments.",
	         model_name(ti.model), ams, rom.name, sav_name);
	message("About TI68k", text, "Press any key");
	wait_key();
}

/* ---- the menu ------------------------------------------------------------------------------ */

enum { M_RESUME, M_HELP, M_SPEED, M_SAVE, M_ROM, M_RESET, M_QUIT, M_QUIT_NOSAVE, M_ABOUT, M_COUNT };
enum { MENU_RESUME, MENU_QUIT, MENU_ROM };

/* the items shown: "Choose another ROM" only when the ROM comes from a file */
static int menu_ids[M_COUNT], menu_n;

static void menu_items(void)
{
	menu_n = 0;
	for (int i = 0; i < M_COUNT; i++) {
#ifdef TI68K_EMBED_ROM
		if (i == M_ROM)
			continue;
#endif
		menu_ids[menu_n++] = i;
	}
}

#ifndef TI68K_EMBED_ROM
static rom_t next_rom;        /* chosen in the menu, loaded once run() has returned */
#endif

/* the keys that opened the menu must go up before it reads keys (a MENU press seen twice
   would close it at once) */
static void wait_release(void)
{
	for (int i = 0; i < 3000 && keydown_any(KEY_SHIFT, KEY_MENU, 0); i++) {
		clearevents();
		sleep();
	}
	clearevents();
}

static int menu(void)
{
	ST->phase = PH_MENU;
	ST->menus++;
	keys_release_all();
	wait_release();
	uint32_t opened = clock_ms();
	int sel = 0;
	menu_items();
	for (;;) {
		screen_invalidate();
		int y0, y1;
		screen_update(&y0, &y1);
		draw_panel();
		int bx = 30, by = 20, bw = 250, bh = 18 * menu_n + 30;
		drect_border(bx, by, bx + bw, by + bh, C_WHITE, 2, C_PANEL);
		dtext(bx + 10, by + 6, C_PANEL, "TI68k menu");
		for (int i = 0; i < menu_n; i++) {
			char item[48];
			switch (menu_ids[i]) {
			case M_RESUME: strcpy(item, "Resume"); break;
			case M_HELP: strcpy(item, "Keys"); break;
			case M_SPEED: snprintf(item, sizeof item, "Speed display: %s", show_speed ? "on" : "off"); break;
			case M_SAVE: strcpy(item, "Save state now"); break;
			case M_ROM: strcpy(item, "Choose another ROM"); break;
			case M_RESET: strcpy(item, "Reset the TI (fresh boot)"); break;
			case M_QUIT: strcpy(item, "Quit (save state)"); break;
			case M_QUIT_NOSAVE: strcpy(item, "Quit without saving"); break;
			case M_ABOUT: strcpy(item, "About"); break;
			}
			int y = by + 24 + 18 * i;
			if (i == sel)
				drect(bx + 4, y - 2, bx + bw - 4, y + 13, C_SEL);
			dtext(bx + 12, y, i == sel ? C_WHITE : C_BLACK, item);
		}
		dupdate();
		int key = wait_key();
		ST->menu_keys = ST->menu_keys << 8 | (uint32_t)(key & 0xff);
		if (key == KEY_UP)
			sel = (sel + menu_n - 1) % menu_n;
		else if (key == KEY_DOWN)
			sel = (sel + 1) % menu_n;
		else if (key == KEY_EXIT || (key == KEY_MENU && clock_ms() - opened >= 400))
			return MENU_RESUME;   /* (a MENU "press" right away is the opening one bouncing) */
		else if (key == KEY_EXE) {
			switch (menu_ids[sel]) {
			case M_RESUME:
				return MENU_RESUME;
			case M_HELP:
				help();
				break;
			case M_SPEED:
				show_speed = !show_speed;
				break;
			case M_SAVE:
				save_state();
				break;
			case M_RESET:
				message("TI68k: reset the TI?",
				        "This boots the TI from the ROM, as after a reset:\n"
				        "its RAM and the archive are cleared. The state file\n"
				        "is replaced at the next save or quit.",
				        "EXE: reset    any other key: cancel");
				if (wait_key() == KEY_EXE) {
					fresh_machine();
					ST->restored = 0;
					return MENU_RESUME;
				}
				break;
			case M_QUIT:
				if (save_state() == 0)
					return MENU_QUIT;
				break;
			case M_QUIT_NOSAVE:
				return MENU_QUIT;
#ifndef TI68K_EMBED_ROM
			case M_ROM:
				/* the running TI's state is saved before the other ROM is loaded */
				if (pick_rom(&next_rom, &rom) == 0) {
					if (!strcmp(next_rom.name, rom.name))
						break;          /* the same file: nothing to do */
					if (save_state() == 0)
						return MENU_ROM;
				}
				break;
#endif
			case M_ABOUT:
				about();
				break;
			}
			ST->phase = PH_MENU;
		}
	}
}

/* SHIFT had been tapped, or held long enough, so AMS saw 2nd: a second 2nd cancels it (AMS
   toggles 2nd; with an AMS whose flags we don't know, send it anyway) */
static void cancel_2nd(uint32_t now)
{
	if (!mods_addr || (ti.ram[mods_addr] & 1)) {
		keys_tap_2nd(now);
	}
}

/* ---- the run loop -------------------------------------------------------------------------- */

#define MAX_LAG   200    /* ms the TI may fall behind before the debt is forgiven */
#define BURST     8      /* emulated ms between looks at the keyboard */
#define FRAME_MS  33
#define FRAME_SLOW 100
#define OFF_SLEEP_MS 300 /* after a key while the TI is off, run this long before sleeping again */
#define AUTO_OFF_MS (10 * 60 * 1000) /* the TI off and no key for 10 min: the CG50 goes off too */

static uint32_t emu_now(void)
{
	return (uint32_t)(ti.cycles / (ti.clock_hz / 1000));
}

static int ti_is_off(void)
{
	return ti.lowpower && !ti.lcd_on;
}

/* RUN_QUIT: quit the add-in; RUN_ROM: load next_rom */
enum { RUN_QUIT, RUN_ROM };

static int run(void)
{
	ST->phase = PH_RUN;
	panel_x = ti.model == TI_MODEL_89 ? 320 : 360;
	redraw_all();

	uint32_t real = clock_ms();
	uint32_t base = real - emu_now();      /* real ms at emulated ms 0 */
	uint32_t last_frame = real, last_sec = real, last_key = real;
	uint32_t sec_emu = emu_now(), sec_core = ST->core_us, start_emu = emu_now();
	int off_drawn = 0;
	uint32_t ms_per = ti.clock_hz / 1000;

	for (;;) {
		int req = KEYS_NONE, power = 0;
		key_event_t ev;
		while ((ev = pollevent()).type != KEYEV_NONE) {
			if (ev.type != KEYEV_DOWN && ev.type != KEYEV_UP)
				continue;
			last_key = clock_ms();
			if (ev.type == KEYEV_DOWN && ev.key == KEY_ACON && ti_is_off() && keys_shift_armed()) {
				power = 1;      /* the TI is off already: SHIFT+AC/ON turns the CG50 off */
				continue;
			}
			int r = keys_event(ev.key, ev.type == KEYEV_DOWN, emu_now());
			if (r != KEYS_NONE) {
				req = r;
				ST->last_req = (uint32_t)(r | ev.key << 8);
			}
		}
		if (power || req == KEYS_OSMENU || (ti_is_off() && clock_ms() - last_key > AUTO_OFF_MS)) {
			if (away(power || req != KEYS_OSMENU) < 0)
				return RUN_QUIT;
			ST->phase = PH_RUN;
			redraw_all();
			off_drawn = 0;
			last_key = clock_ms();
			base = clock_ms() - emu_now();
			continue;
		}
		if (req == KEYS_MENU || req == KEYS_MENU_2ND) {
			int m = menu();
			if (m == MENU_QUIT)
				return RUN_QUIT;
			if (m == MENU_ROM)
				return RUN_ROM;
			ST->phase = PH_RUN;
			if (req == KEYS_MENU_2ND)
				cancel_2nd(emu_now());
			redraw_all();
			off_drawn = 0;
			base = clock_ms() - emu_now();
			last_sec = clock_ms(), sec_emu = emu_now(), sec_core = ST->core_us;
			continue;
		}

		/* the TI is off and nothing is pending: wait for a key, the CG50 asleep */
		uint32_t now = clock_ms();
		if (ti_is_off() && now - last_key > OFF_SLEEP_MS) {
			if (!off_drawn) {
				ST->phase = PH_OFF;
				ST->ti_off = 1;
				draw_off();
				draw_panel();
				push_all();
				off_drawn = 1;
			}
			sleep();
			base = clock_ms() - emu_now();
			continue;
		}
		if (off_drawn) {
			ST->phase = PH_RUN;
			ST->ti_off = 0;
			off_drawn = 0;
			screen_invalidate();
		}

		int32_t behind = (int32_t)(now - base - emu_now());
		if (behind > MAX_LAG) {
			base += behind - MAX_LAG;
			behind = MAX_LAG;
		}
		if (behind > 0) {
			uint32_t t0 = clock_now();
			for (int i = 0; i < behind && i < BURST; i++) {
				keys_process(emu_now());
				ti_run(ms_per);
			}
			ST->core_us += clock_us(t0, clock_now());
			ST->emu_ms = emu_now() - start_emu;
			ST->instr = (uint32_t)ti.instr;
		}

		now = clock_ms();
		uint32_t interval = behind > 20 ? FRAME_SLOW : FRAME_MS;
		int frame_due = now - last_frame >= interval;
		int sec_due = now - last_sec >= 1000;
		if (frame_due) {
			int y0, y1;
			if (!ti_is_off() && ti.lcd_on && screen_update(&y0, &y1)) {
				push_rows(y0, y1);
				ST->frames++;
			}
			uint32_t m = mods_now();
			if ((m | (uint32_t)keys_layer() << 8 | (uint32_t)show_speed << 12) != (panel_key & 0xFFFF)) {
				draw_panel();
				push_all();
			}
			last_frame = now;
		}
		if (sec_due) {
			uint32_t dt = now - last_sec;
			ST->speed10 = (emu_now() - sec_emu) * 1000 / dt;
			ST->load10 = (ST->core_us - sec_core) / dt;
			ST->real_ms += dt;
			last_sec = now, sec_emu = emu_now(), sec_core = ST->core_us;
			if (show_speed) {
				draw_panel();
				push_all();
			}
		}
		if (behind <= 0 && !frame_due && !sec_due)
			sleep();
	}
}

/* ---- main ---------------------------------------------------------------------------------- */

int main(void)
{
	memset((void *)ST, 0, sizeof *ST);
	ST->magic = 0x5436384B; /* T68K */
	clock_init();
	screen_init();

#ifdef TI68K_EMBED_ROM
	if (load_rom() < 0) {
		ST->phase = PH_ERROR + 1;
		message("TI68k", "The built-in ROM is damaged.", "Press any key");
		wait_key();
		clock_quit();
		return 1;
	}
	rom_ready();
	if (restore_state() == 0)
		run();
#else
	/* the ROM chosen last time, else the file browser */
	ST->phase = PH_FIND;
	if (cfg_rom(&rom) < 0) {
		if (pick_rom(&rom, NULL) < 0) {
			clock_quit();
			return 0;
		}
		cfg_save(rom.name);
	}
	for (;;) {
		if (load_rom() < 0) {
			ST->phase = PH_ERROR + 2;
			set_msg("ROM load failed");
			message("TI68k", "Reading the ROM file failed.", "Press any key to quit");
			wait_key();
			clock_quit();
			return 0;
		}
		rom_ready();
		if (restore_state() < 0 || run() != RUN_ROM)
			break;
		rom = next_rom;           /* "Choose another ROM": the old state is saved */
		cfg_save(rom.name);
	}
#endif
	ST->phase = PH_QUIT;
	clock_quit();
	return 0;
}
