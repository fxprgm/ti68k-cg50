/* CG50 keyboard -> TI keyboard matrix.
 *
 * Keys go down and up on the TI when they go down and up on the CG50, so held keys stay held
 * (games, auto-repeat), with two corrections in emulated time: a TI key stays down at least
 * MIN_HOLD ms (AMS scans the keyboard from a timer interrupt: a quick tap while the emulator
 * runs behind would be lost), and a key that follows a deferred release waits GAP ms.
 *
 * SHIFT (the TI's 2nd) is held back until its use is known: released alone = a 2nd tap; with a
 * key = 2nd, then the key GAP ms later; held alone HOLD_ALONE ms = a held 2nd; SHIFT+MENU = the
 * emulator's menu, SHIFT+VARS = the TI's shift key, SHIFT+ALPHA = letter lock, SHIFT+OPTN = HAND
 * (92+). MENU alone = the CG50's main menu (main.c saves the state first).
 *
 * ALPHA is a letter layer, as on the CG50: tapped, the next key types the letter printed in red
 * above it (A-Z, SPACE on ".", theta on "^" for the 92+); SHIFT+ALPHA locks it until ALPHA is
 * pressed again; SHIFT held with a letter key gives a capital (the TI's shift). A letter is sent
 * as the TI keys that type it: alpha + its key on the TI-89, the letter key on the TI-92 Plus.
 * On the TI-89, ALPHA held down together with a key (or alone for HOLD_ALONE ms) is the TI's own
 * alpha key instead (games, the TI-89's letter positions). */
#include "keys.h"
#include "stats.h"
#include "ti68k.h"

#include <gint/keyboard.h>
#include <string.h>

#define MIN_HOLD 50
#define GAP 25
#define HOLD_ALONE 300

#define K TI_KEY
#define ON TI_KEY_ON
#define NOKEY 0xFE   /* a TI key the model doesn't have */

static const keymap_t map89[] = {
	{ KEY_F1, K(5, 7), "F1", "F1" },          { KEY_F2, K(4, 7), "F2", "F2" },
	{ KEY_F3, K(3, 7), "F3", "F3" },          { KEY_F4, K(2, 7), "F4", "F4" },
	{ KEY_F5, K(1, 7), "F5", "F5" },          { KEY_F6, K(5, 6), "F6", "HOME" },
	{ KEY_SHIFT, K(0, 4), "SHIFT", "2nd" },   { KEY_OPTN, K(0, 6), "OPTN", "diamond" },
	{ KEY_VARS, K(5, 0), "VARS", "APPS" },    { KEY_EXIT, K(6, 0), "EXIT", "ESC" },
	{ KEY_ACON, ON, "AC/ON", "ON" },          { KEY_DEL, K(2, 6), "DEL", "<- (back)" },
	{ KEY_SQUARE, K(4, 6), "x^2", "MODE" },   { KEY_FD, K(3, 6), "S<>D", "CATALOG" },
	{ KEY_XOT, K(5, 5), "X,th,T", "X" },      { KEY_LOG, K(4, 5), "log", "Y" },
	{ KEY_LN, K(3, 5), "ln", "Z" },           { KEY_SIN, K(2, 5), "sin", "T" },
	{ KEY_COS, K(5, 4), "cos", "=" },         { KEY_TAN, K(5, 3), "tan", "| (with)" },
	{ KEY_FRAC, K(1, 6), "a b/c", "CLEAR" },  { KEY_POWER, K(1, 5), "^", "^" },
	{ KEY_LEFTP, K(4, 4), "(", "(" },         { KEY_RIGHTP, K(3, 4), ")", ")" },
	{ KEY_COMMA, K(2, 4), ",", "," },         { KEY_ARROW, K(5, 1), "->", "STO>" },
	{ KEY_EXP, K(5, 2), "x10^x", "EE" },      { KEY_NEG, K(2, 0), "(-)", "(-)" },
	{ KEY_0, K(4, 0), "0", "0" },             { KEY_1, K(4, 1), "1", "1" },
	{ KEY_2, K(3, 1), "2", "2" },             { KEY_3, K(2, 1), "3", "3" },
	{ KEY_4, K(4, 2), "4", "4" },             { KEY_5, K(3, 2), "5", "5" },
	{ KEY_6, K(2, 2), "6", "6" },             { KEY_7, K(4, 3), "7", "7" },
	{ KEY_8, K(3, 3), "8", "8" },             { KEY_9, K(2, 3), "9", "9" },
	{ KEY_DOT, K(3, 0), ".", "." },           { KEY_ADD, K(1, 1), "+", "+" },
	{ KEY_SUB, K(1, 2), "-", "-" },           { KEY_MUL, K(1, 3), "x", "*" },
	{ KEY_DIV, K(1, 4), "/", "/" },           { KEY_EXE, K(1, 0), "EXE", "ENTER" },
	{ KEY_UP, K(0, 0), "UP", "UP" },          { KEY_LEFT, K(0, 1), "LEFT", "LEFT" },
	{ KEY_DOWN, K(0, 2), "DOWN", "DOWN" },    { KEY_RIGHT, K(0, 3), "RIGHT", "RIGHT" },
};

static const keymap_t map92[] = {
	{ KEY_F1, K(6, 4), "F1", "F1" },          { KEY_F2, K(4, 4), "F2", "F2" },
	{ KEY_F3, K(2, 4), "F3", "F3" },          { KEY_F4, K(9, 4), "F4", "F4" },
	{ KEY_F5, K(7, 4), "F5", "F5" },          { KEY_F6, K(5, 4), "F6", "F6" },
	{ KEY_SHIFT, K(0, 0), "SHIFT", "2nd" },   { KEY_OPTN, K(0, 1), "OPTN", "diamond" },
	{ KEY_VARS, K(7, 6), "VARS", "APPS" },    { KEY_EXIT, K(8, 6), "EXIT", "ESC" },
	{ KEY_ACON, ON, "AC/ON", "ON" },          { KEY_DEL, K(8, 0), "DEL", "<- (back)" },
	{ KEY_SQUARE, K(8, 5), "x^2", "MODE" },   { KEY_FD, K(3, 4), "S<>D", "F7" },
	{ KEY_LOG, K(1, 4), "log", "F8" },        { KEY_XOT, K(2, 1), "X,th,T", "X" },
	{ KEY_LN, K(6, 5), "ln", "LN" },          { KEY_SIN, K(5, 5), "sin", "SIN" },
	{ KEY_COS, K(5, 6), "cos", "COS" },       { KEY_TAN, K(5, 7), "tan", "TAN" },
	{ KEY_FRAC, K(7, 5), "a b/c", "CLEAR" },  { KEY_POWER, K(6, 0), "^", "^" },
	{ KEY_LEFTP, K(4, 5), "(", "(" },         { KEY_RIGHTP, K(4, 6), ")", ")" },
	{ KEY_COMMA, K(4, 7), ",", "," },         { KEY_ARROW, K(3, 0), "->", "STO>" },
	{ KEY_EXP, K(7, 0), "x10^x", "=" },       { KEY_NEG, K(9, 7), "(-)", "(-)" },
	{ KEY_0, K(9, 5), "0", "0" },             { KEY_1, K(1, 5), "1", "1" },
	{ KEY_2, K(1, 6), "2", "2" },             { KEY_3, K(1, 7), "3", "3" },
	{ KEY_4, K(2, 5), "4", "4" },             { KEY_5, K(2, 6), "5", "5" },
	{ KEY_6, K(2, 7), "6", "6" },             { KEY_7, K(3, 5), "7", "7" },
	{ KEY_8, K(3, 6), "8", "8" },             { KEY_9, K(3, 7), "9", "9" },
	{ KEY_DOT, K(9, 6), ".", "." },           { KEY_ADD, K(8, 4), "+", "+" },
	{ KEY_SUB, K(9, 0), "-", "-" },           { KEY_MUL, K(7, 7), "x", "*" },
	{ KEY_DIV, K(5, 0), "/", "/" },           { KEY_EXE, K(9, 1), "EXE", "ENTER" },
	{ KEY_UP, K(0, 5), "UP", "UP" },          { KEY_LEFT, K(0, 4), "LEFT", "LEFT" },
	{ KEY_DOWN, K(0, 7), "DOWN", "DOWN" },    { KEY_RIGHT, K(0, 6), "RIGHT", "RIGHT" },
};

/* the CG50's red letters */
const letter_t letters[] = {
	{ KEY_XOT, 'a' }, { KEY_LOG, 'b' }, { KEY_LN, 'c' }, { KEY_SIN, 'd' }, { KEY_COS, 'e' },
	{ KEY_TAN, 'f' }, { KEY_FRAC, 'g' }, { KEY_FD, 'h' }, { KEY_LEFTP, 'i' }, { KEY_RIGHTP, 'j' },
	{ KEY_COMMA, 'k' }, { KEY_ARROW, 'l' }, { KEY_7, 'm' }, { KEY_8, 'n' }, { KEY_9, 'o' },
	{ KEY_4, 'p' }, { KEY_5, 'q' }, { KEY_6, 'r' }, { KEY_MUL, 's' }, { KEY_DIV, 't' },
	{ KEY_1, 'u' }, { KEY_2, 'v' }, { KEY_3, 'w' }, { KEY_ADD, 'x' }, { KEY_SUB, 'y' },
	{ KEY_0, 'z' }, { KEY_DOT, ' ' }, { KEY_POWER, '@' /* theta, 92+ */ },
};
const int letters_count = sizeof letters / sizeof letters[0];

/* a-z, then space, then theta: the TI key that types it (TI-89: with alpha) */
static const uint8_t letter89[28] = {
	K(5, 4), K(4, 4), K(3, 4), K(2, 4), K(1, 4), K(5, 3), K(4, 3), K(3, 3), K(2, 3), K(1, 3), /* a-j */
	K(5, 2), K(4, 2), K(3, 2), K(2, 2), K(1, 2), K(5, 1), K(4, 1), K(3, 1), K(2, 1), K(2, 5), /* k-t */
	K(1, 1), K(4, 0), K(3, 0), K(5, 5), K(4, 5), K(3, 5),                                     /* u-z */
	K(2, 0), 0xFE,                                                                             /* space, (no theta) */
};
static const uint8_t letter92[28] = {
	K(9, 2), K(5, 1), K(3, 1), K(2, 2), K(2, 3), K(3, 2), K(4, 2), K(5, 2), K(7, 3), K(6, 2),
	K(7, 2), K(8, 2), K(7, 1), K(6, 1), K(8, 3), K(6, 7), K(9, 3), K(3, 3), K(1, 2), K(4, 3),
	K(6, 3), K(4, 1), K(1, 3), K(2, 1), K(5, 3), K(1, 1),
	K(4, 0), K(8, 1),
};

const keymap_t *keymap;
int keymap_count;
static int model;
static uint8_t ti_2nd, ti_shift, ti_diamond, ti_alpha, ti_hand;
static const uint8_t *letter_ti;

void keys_init(int m)
{
	model = m;
	if (m == TI_MODEL_89) {
		keymap = map89, keymap_count = sizeof map89 / sizeof map89[0];
		ti_2nd = K(0, 4), ti_shift = K(0, 5), ti_diamond = K(0, 6), ti_alpha = K(0, 7), ti_hand = 0xFE;
		letter_ti = letter89;
	} else {
		keymap = map92, keymap_count = sizeof map92 / sizeof map92[0];
		ti_2nd = K(0, 0), ti_shift = K(0, 2), ti_diamond = K(0, 1), ti_alpha = 0xFE, ti_hand = K(0, 3);
		letter_ti = letter92;
	}
	(void)ti_diamond;
	keys_release_all();
}

/* ---- the queue of TI key changes, in emulated ms -------------------------------------------- */

#define QMAX 48
#define NKEYS 81                         /* 80 matrix keys + ON */
static struct { uint8_t key, down; uint32_t at; } q[QMAX];
static int qn;
static uint32_t down_at[NKEYS];
static uint8_t is_down[NKEYS];          /* as applied to the TI */
static uint32_t last_release;

static int slot(int key) { return key == ON ? 80 : key; }

static void apply(int key, int down)
{
	if (key == NOKEY)
		return;
	ti_key(key, down);
	is_down[slot(key)] = (uint8_t)down;
}

static void enqueue(int key, int down, uint32_t at)
{
	if (qn == QMAX) {                   /* full: apply the oldest now */
		apply(q[0].key, q[0].down);
		memmove(q, q + 1, sizeof q[0] * (QMAX - 1));
		qn--;
	}
	q[qn].key = (uint8_t)key, q[qn].down = (uint8_t)down, q[qn].at = at;
	qn++;
}

/* TI key down at `now` or after the last release + GAP; returns when it goes down */
static uint32_t ti_down(int key, uint32_t now)
{
	if (key == NOKEY)
		return now;
	uint32_t at = now;
	if ((int32_t)(last_release + GAP - at) > 0)
		at = last_release + GAP;
	down_at[slot(key)] = at;
	enqueue(key, 1, at);
	return at;
}

/* TI key up, no sooner than MIN_HOLD after it went down; returns when */
static uint32_t ti_up(int key, uint32_t now)
{
	if (key == NOKEY)
		return now;
	uint32_t at = down_at[slot(key)] + MIN_HOLD;
	if ((int32_t)(at - now) < 0)
		at = now;
	if ((int32_t)(at - last_release) > 0)
		last_release = at;
	enqueue(key, 0, at);
	return at;
}

/* ---- modifiers and the letter layer ------------------------------------------------------- */

enum { M_UP, M_PENDING, M_SENT, M_CONSUMED };
static int shift_st, alpha_st;
static uint32_t shift_at, alpha_at;
static int layer;                 /* 0 off, 1 one letter, 2 locked */
static int shift_tap;             /* SHIFT was just tapped alone (the TI got a 2nd tap) */
static int menu_held;             /* MENU went down as a menu request: ignore its release */
/* CG key -> what its down event sent, so the release matches */
static uint8_t sent[256];
enum { S_NONE, S_PLAIN, S_LETTER, S_SHIFTKEY, S_HAND };

int keys_layer(void) { return layer; }

void keys_tap_2nd(uint32_t now)
{
	ti_up(ti_2nd, ti_down(ti_2nd, now));
}

int keys_shift_armed(void) { return shift_st == M_PENDING || shift_st == M_SENT || shift_tap; }

int keys_cg_mods(void)
{
	return (shift_st == M_PENDING || shift_st == M_SENT ? 1 : 0) | (alpha_st != M_UP ? 2 : 0);
}

static const keymap_t *find(int key)
{
	for (int i = 0; i < keymap_count; i++)
		if (keymap[i].cg == key)
			return &keymap[i];
	return NULL;
}

static int letter_of(int key)
{
	for (int i = 0; i < letters_count; i++)
		if (letters[i].cg == key) {
			char c = letters[i].letter;
			int idx = c == ' ' ? 26 : c == '@' ? 27 : c - 'a';
			return letter_ti[idx] == 0xFE ? -1 : idx;
		}
	return -1;
}

/* type a letter as a complete tap: [modifier,] key */
static void type_letter(int idx, int upper, uint32_t now)
{
	int key = letter_ti[idx];
	int mod = upper ? ti_shift : model == TI_MODEL_89 ? ti_alpha : 0xFE;
	if (idx == 26 && model == TI_MODEL_89 && !upper)
		mod = ti_alpha;   /* alpha (-) = space */
	if (mod != 0xFE) {
		uint32_t t = ti_down(mod, now);
		t = ti_down(key, t + GAP);
		t = ti_up(key, t);
		ti_up(mod, t + GAP);
	} else {
		uint32_t t = ti_down(key, now);
		ti_up(key, t);
	}
}

/* SHIFT pending when another key goes down: the TI sees 2nd first */
static uint32_t shift_now(uint32_t now)
{
	if (shift_st == M_PENDING) {
		now = ti_down(ti_2nd, now) + GAP;
		shift_st = M_SENT;
	}
	return now;
}

int keys_event(int key, bool down, uint32_t now)
{
	ST->keyev++;
	if (key == KEY_SHIFT) {
		if (down) {
			shift_st = M_PENDING;
			shift_at = now;
		} else {
			if (shift_st == M_PENDING) {     /* a tap: 2nd down and up */
				ti_down(ti_2nd, now);
				shift_tap = 1;
			}
			if (shift_st == M_PENDING || shift_st == M_SENT)
				ti_up(ti_2nd, now);
			shift_st = M_UP;
		}
		return KEYS_NONE;
	}
	/* SHIFT+key where the key is one of the emulator's combos: SHIFT held (pending), or tapped
	   just before as CG50 users do (the TI then already got a 2nd tap: a second one cancels it,
	   AMS toggles 2nd) */
	int combo = 0;
	if (down) {
		combo = shift_st == M_PENDING || shift_tap;
		int is_letter = (layer || (alpha_st == M_PENDING && model != TI_MODEL_89)) && letter_of(key) >= 0;
		if (combo && shift_tap && (is_letter || key == KEY_ALPHA || key == KEY_VARS || (key == KEY_OPTN && ti_hand != 0xFE))) {
			uint32_t t = ti_down(ti_2nd, now);
			ti_up(ti_2nd, t);
			now = t + MIN_HOLD + GAP;
		}
		if (key != KEY_MENU)
			shift_tap = 0;
	}
	if (key == KEY_ALPHA) {
		if (down) {
			if (combo) {                     /* SHIFT+ALPHA: letter lock */
				if (shift_st == M_PENDING)
					shift_st = M_CONSUMED;
				layer = 2;
				alpha_st = M_CONSUMED;
				return KEYS_NONE;
			}
			alpha_st = M_PENDING;
			alpha_at = now;
		} else {
			if (alpha_st == M_PENDING)       /* a tap: the layer for one key, or off */
				layer = layer ? 0 : 1;
			if (alpha_st == M_SENT)
				ti_up(ti_alpha, now);
			alpha_st = M_UP;
		}
		return KEYS_NONE;
	}
	if (key == KEY_MENU) {
		if (down) {
			menu_held = 1;
			if (shift_st == M_PENDING) {
				shift_st = M_CONSUMED;
				return KEYS_MENU;
			}
			if (shift_st == M_SENT || shift_tap) {   /* the TI saw 2nd: main.c cancels it */
				shift_tap = 0;
				return KEYS_MENU_2ND;
			}
			return KEYS_OSMENU;
		}
		menu_held = 0;
		return KEYS_NONE;
	}

	if (!down) {
		int s = sent[key & 0xff];
		sent[key & 0xff] = S_NONE;
		if (s == S_PLAIN) {
			const keymap_t *m = find(key);
			if (m)
				ti_up(m->ti, now);
		} else if (s == S_SHIFTKEY)
			ti_up(ti_shift, now);
		else if (s == S_HAND)
			ti_up(ti_hand, now);
		return KEYS_NONE;
	}

	/* SHIFT combos */
	if (combo && key == KEY_VARS) {
		if (shift_st == M_PENDING)
			shift_st = M_CONSUMED;
		ti_down(ti_shift, now);
		sent[key] = S_SHIFTKEY;
		return KEYS_NONE;
	}
	if (combo && key == KEY_OPTN && ti_hand != 0xFE) {
		if (shift_st == M_PENDING)
			shift_st = M_CONSUMED;
		ti_down(ti_hand, now);
		sent[key] = S_HAND;
		return KEYS_NONE;
	}

	/* letters: the layer is on, or (TI-92 Plus) ALPHA is held */
	int idx = letter_of(key);
	int alpha_letter = alpha_st == M_PENDING && model != TI_MODEL_89;
	if (idx >= 0 && (layer || alpha_letter)) {
		int upper = combo || shift_st == M_CONSUMED;
		if (shift_st == M_PENDING)
			shift_st = M_CONSUMED;
		type_letter(idx, upper, now);
		sent[key] = S_LETTER;
		if (layer == 1)
			layer = 0;
		if (alpha_st == M_PENDING)
			alpha_st = M_CONSUMED;
		return KEYS_NONE;
	}
	if (layer == 1)
		layer = 0;                           /* a key without a letter ends a one-shot layer */

	const keymap_t *m = find(key);
	if (!m)
		return KEYS_NONE;
	now = shift_now(now);
	if (alpha_st == M_PENDING && model == TI_MODEL_89) {   /* ALPHA held + key: the TI's alpha */
		now = ti_down(ti_alpha, now) + GAP;
		alpha_st = M_SENT;
	}
	ti_down(m->ti, now);
	sent[key] = S_PLAIN;
	return KEYS_NONE;
}

void keys_process(uint32_t now)
{
	if (shift_st == M_PENDING && (int32_t)(now - shift_at) >= HOLD_ALONE) {
		ti_down(ti_2nd, now);                /* SHIFT held alone: a held 2nd */
		shift_st = M_SENT;
	}
	if (alpha_st == M_PENDING && model == TI_MODEL_89 && (int32_t)(now - alpha_at) >= HOLD_ALONE) {
		ti_down(ti_alpha, now);              /* ALPHA held alone: the TI's alpha, held */
		alpha_st = M_SENT;
	}
	int j = 0;
	for (int i = 0; i < qn; i++) {
		if ((int32_t)(q[i].at - now) <= 0)
			apply(q[i].key, q[i].down);
		else
			q[j++] = q[i];
	}
	qn = j;
	ST->layer = (uint32_t)layer;
}

void keys_release_all(void)
{
	qn = 0;
	for (int k = 0; k < NKEYS; k++)
		if (is_down[k]) {
			ti_key(k == 80 ? ON : k, 0);
			is_down[k] = 0;
		}
	memset(sent, 0, sizeof sent);
	/* a SHIFT or ALPHA still held belongs to the emulator now (its release does nothing) */
	shift_st = keydown(KEY_SHIFT) ? M_CONSUMED : M_UP;
	shift_tap = 0;
	alpha_st = keydown(KEY_ALPHA) ? M_CONSUMED : M_UP;
	menu_held = keydown(KEY_MENU);
	last_release = 0;
}
