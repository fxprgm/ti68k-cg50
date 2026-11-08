/* The key scenario (see scenario.h). Plain C, no floating point, no host dependencies. */
#include <stddef.h>
#include <string.h>

#include "scenario.h"
#include "ti68k.h"

typedef struct { const char *name; int key; } keyname_t;

/* TI-89: rows of port 0x60001B (bit 7..0), TIGCC / TiEmu key matrix */
static const keyname_t keys89[] = {
	{"alpha", TI_KEY(0, 7)}, {"diamond", TI_KEY(0, 6)}, {"shift", TI_KEY(0, 5)}, {"2nd", TI_KEY(0, 4)},
	{"right", TI_KEY(0, 3)}, {"down", TI_KEY(0, 2)}, {"left", TI_KEY(0, 1)}, {"up", TI_KEY(0, 0)},
	{"f5", TI_KEY(1, 7)}, {"clear", TI_KEY(1, 6)}, {"^", TI_KEY(1, 5)}, {"/", TI_KEY(1, 4)},
	{"*", TI_KEY(1, 3)}, {"-", TI_KEY(1, 2)}, {"+", TI_KEY(1, 1)}, {"enter", TI_KEY(1, 0)},
	{"f4", TI_KEY(2, 7)}, {"bksp", TI_KEY(2, 6)}, {"t", TI_KEY(2, 5)}, {",", TI_KEY(2, 4)},
	{"9", TI_KEY(2, 3)}, {"6", TI_KEY(2, 2)}, {"3", TI_KEY(2, 1)}, {"neg", TI_KEY(2, 0)},
	{"f3", TI_KEY(3, 7)}, {"catalog", TI_KEY(3, 6)}, {"z", TI_KEY(3, 5)}, {")", TI_KEY(3, 4)},
	{"8", TI_KEY(3, 3)}, {"5", TI_KEY(3, 2)}, {"2", TI_KEY(3, 1)}, {".", TI_KEY(3, 0)},
	{"f2", TI_KEY(4, 7)}, {"mode", TI_KEY(4, 6)}, {"y", TI_KEY(4, 5)}, {"(", TI_KEY(4, 4)},
	{"7", TI_KEY(4, 3)}, {"4", TI_KEY(4, 2)}, {"1", TI_KEY(4, 1)}, {"0", TI_KEY(4, 0)},
	{"f1", TI_KEY(5, 7)}, {"home", TI_KEY(5, 6)}, {"x", TI_KEY(5, 5)}, {"=", TI_KEY(5, 4)},
	{"|", TI_KEY(5, 3)}, {"ee", TI_KEY(5, 2)}, {"sto", TI_KEY(5, 1)}, {"apps", TI_KEY(5, 0)},
	{"esc", TI_KEY(6, 0)}, {"on", TI_KEY_ON}, {NULL, 0}
};

/* TI-92 Plus */
static const keyname_t keys92[] = {
	{"down", TI_KEY(0, 7)}, {"right", TI_KEY(0, 6)}, {"up", TI_KEY(0, 5)}, {"left", TI_KEY(0, 4)},
	{"hand", TI_KEY(0, 3)}, {"shift", TI_KEY(0, 2)}, {"diamond", TI_KEY(0, 1)}, {"2nd", TI_KEY(0, 0)},
	{"3", TI_KEY(1, 7)}, {"2", TI_KEY(1, 6)}, {"1", TI_KEY(1, 5)}, {"f8", TI_KEY(1, 4)},
	{"w", TI_KEY(1, 3)}, {"s", TI_KEY(1, 2)}, {"z", TI_KEY(1, 1)},
	{"6", TI_KEY(2, 7)}, {"5", TI_KEY(2, 6)}, {"4", TI_KEY(2, 5)}, {"f3", TI_KEY(2, 4)},
	{"e", TI_KEY(2, 3)}, {"d", TI_KEY(2, 2)}, {"x", TI_KEY(2, 1)},
	{"9", TI_KEY(3, 7)}, {"8", TI_KEY(3, 6)}, {"7", TI_KEY(3, 5)}, {"f7", TI_KEY(3, 4)},
	{"r", TI_KEY(3, 3)}, {"f", TI_KEY(3, 2)}, {"c", TI_KEY(3, 1)}, {"sto", TI_KEY(3, 0)},
	{",", TI_KEY(4, 7)}, {")", TI_KEY(4, 6)}, {"(", TI_KEY(4, 5)}, {"f2", TI_KEY(4, 4)},
	{"t", TI_KEY(4, 3)}, {"g", TI_KEY(4, 2)}, {"v", TI_KEY(4, 1)}, {"space", TI_KEY(4, 0)},
	{"tan", TI_KEY(5, 7)}, {"cos", TI_KEY(5, 6)}, {"sin", TI_KEY(5, 5)}, {"f6", TI_KEY(5, 4)},
	{"y", TI_KEY(5, 3)}, {"h", TI_KEY(5, 2)}, {"b", TI_KEY(5, 1)}, {"/", TI_KEY(5, 0)},
	{"p", TI_KEY(6, 7)}, {"enter2", TI_KEY(6, 6)}, {"ln", TI_KEY(6, 5)}, {"f1", TI_KEY(6, 4)},
	{"u", TI_KEY(6, 3)}, {"j", TI_KEY(6, 2)}, {"n", TI_KEY(6, 1)}, {"^", TI_KEY(6, 0)},
	{"*", TI_KEY(7, 7)}, {"apps", TI_KEY(7, 6)}, {"clear", TI_KEY(7, 5)}, {"f5", TI_KEY(7, 4)},
	{"i", TI_KEY(7, 3)}, {"k", TI_KEY(7, 2)}, {"m", TI_KEY(7, 1)}, {"=", TI_KEY(7, 0)},
	{"esc", TI_KEY(8, 6)}, {"mode", TI_KEY(8, 5)}, {"+", TI_KEY(8, 4)},
	{"o", TI_KEY(8, 3)}, {"l", TI_KEY(8, 2)}, {"theta", TI_KEY(8, 1)}, {"bksp", TI_KEY(8, 0)},
	{"neg", TI_KEY(9, 7)}, {".", TI_KEY(9, 6)}, {"0", TI_KEY(9, 5)}, {"f4", TI_KEY(9, 4)},
	{"q", TI_KEY(9, 3)}, {"a", TI_KEY(9, 2)}, {"enter", TI_KEY(9, 1)}, {"-", TI_KEY(9, 0)},
	{"on", TI_KEY_ON}, {NULL, 0}
};

static const keyname_t *keytab(void) { return ti.model == TI_MODEL_89 ? keys89 : keys92; }

int scen_key_lookup(const char *name)
{
	for (const keyname_t *k = keytab(); k->name; k++)
		if (!strcmp(k->name, name)) return k->key;
	return -1;
}

int scen_step;

/* run until the CPU has been mostly idle (busy < 20 %) for 60 ms; give up after max_ms */
static void wait_idle(uint32_t max_ms)
{
	uint32_t waited = 0;
	int quiet = 0;
	while (waited < max_ms) {
		uint64_t c0 = ti.cycles, b0 = ti.busy_cycles;
		scen_run_ms(10);
		waited += 10;
		quiet = (ti.busy_cycles - b0) * 5 < ti.cycles - c0 ? quiet + 1 : 0;
		if (quiet >= 6) return;
	}
}

static void press(const char *name)
{
	int k = scen_key_lookup(name);
	if (k < 0) return;
	ti_key(k, 1);
	scen_run_ms(100);
	ti_key(k, 0);
	wait_idle(300000);
}

/* "a b c": press each key in turn */
static void keys(const char *seq)
{
	char name[16];
	while (*seq) {
		while (*seq == ' ') seq++;
		size_t n = 0;
		while (seq[n] && seq[n] != ' ') n++;
		if (n && n < sizeof name) {
			memcpy(name, seq, n);
			name[n] = 0;
			press(name);
		}
		seq += n;
	}
}

typedef struct {
	const char *name;
	const char *k89, *k92;   /* keys to press (TI-89, TI-92 Plus) */
	uint32_t run_ms;         /* then run this long */
	const char *shot;        /* screenshot name, or NULL */
} step_t;

static const step_t steps[] = {
	{"boot 0-1 s", NULL, NULL, 1000, "boot_1s"},
	{"boot 1-2 s", NULL, NULL, 1000, "boot_2s"},
	{"boot 2-3 s", NULL, NULL, 1000, "boot_3s"},
	{"boot 3-4 s", NULL, NULL, 1000, "boot_4s"},
	{"boot 4-5 s", NULL, NULL, 1000, "boot_5s"},
	{"boot 5-6 s", NULL, NULL, 1000, "boot_6s"},
	{"idle at the home screen (5 s)", NULL, NULL, 5000, "idle"},
	{"2+3 ENTER", "2 + 3 enter", "2 + 3 enter", 0, "add"},
	{"type 7 digits", "1 2 3 4 5 6 7", "1 2 3 4 5 6 7", 0, "digits"},
	{"CLEAR CLEAR", "clear clear", "clear clear", 0, NULL},
	{"type factor(x^3-1)", "f2 2 x ^ 3 - 1 )", "f2 2 x ^ 3 - 1 )", 0, NULL},
	{"ENTER factor(x^3-1)", "enter", "enter", 0, "factor"},
	{"type integral 1/(x^3+1) dx", "clear f3 2 1 / ( x ^ 3 + 1 ) , x )", "clear f3 2 1 / ( x ^ 3 + 1 ) , x )", 0, NULL},
	{"ENTER integral 1/(x^3+1) dx", "enter", "enter", 0, "integral"},
	{"type factor(2^64+1)", "clear f2 2 2 ^ 6 4 + 1 )", "clear f2 2 2 ^ 6 4 + 1 )", 0, NULL},
	{"ENTER factor(2^64+1)", "enter", "enter", 0, "factor64"},
	{"open Y= editor", "diamond f1", "diamond w", 0, "yequ"},
	{"type y1=sin(x)", "enter 2nd y x ) enter", "enter sin x ) enter", 0, NULL},
	{"GRAPH sin(x)", "diamond f3", "diamond r", 0, "graph"},
	{"ZoomStd (F2 6) redraw", "f2 6", "f2 6", 0, "zoomstd"},
	{"HOME, idle 5 s", "home", "diamond q", 5000, "end"},
};

int scenario_steps(void) { return (int)(sizeof steps / sizeof steps[0]); }

void scenario_run(void)
{
	for (scen_step = 0; scen_step < scenario_steps(); scen_step++) {
		const step_t *s = &steps[scen_step];
		const char *k = ti.model == TI_MODEL_89 ? s->k89 : s->k92;
		if (k) keys(k);
		if (s->run_ms) scen_run_ms(s->run_ms);
		scen_phase(scen_step, s->name, s->shot);
	}
}
