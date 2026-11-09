/* CG50 keyboard -> TI-89 / TI-92 Plus keyboard matrix. See keys.c for the scheme. */
#ifndef TI68K_KEYS_H
#define TI68K_KEYS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint8_t cg;              /* gint key code */
	uint8_t ti;              /* TI_KEY(row, bit), or TI_KEY_ON */
	const char *cg_name, *ti_name;
} keymap_t;

/* the plain key table of the model running (keys_init) */
extern const keymap_t *keymap;
extern int keymap_count;

void keys_init(int model);

/* what keys_event() asks of the caller */
enum { KEYS_NONE, KEYS_MENU, KEYS_MENU_2ND, KEYS_OSMENU };

/* a CG50 key event at emulated time now (ms) */
int keys_event(int key, bool down, uint32_t now);
/* apply the queued TI key changes due at emulated time now */
void keys_process(uint32_t now);
/* release every TI key at once and forget the queue (before the menu, a save, ...) */
void keys_release_all(void);

/* the letter layer: 0 off, 1 the next key types its letter, 2 locked */
int keys_layer(void);
/* a 2nd tap for the TI (to cancel one it latched) */
void keys_tap_2nd(uint32_t now);
/* SHIFT is held, or was tapped just before (the next key is SHIFT+key) */
int keys_shift_armed(void);
/* CG50 modifiers held or pending, for the panel: bit 0 SHIFT, bit 1 ALPHA */
int keys_cg_mods(void);

/* the letters: CG50 key -> letter (the red labels), for the help screen */
typedef struct { uint8_t cg; char letter; } letter_t;
extern const letter_t letters[];
extern const int letters_count;

#endif
