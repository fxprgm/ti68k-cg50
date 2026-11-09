/* The 68000's state for the state file: Musashi's register file and internal flags, as plain
 * words (not its struct: that holds host pointers, the callbacks and cycle tables). */
#include "cpustate.h"
#include "m68kcpu.h"

#define FIELDS(X) \
	X(cpu_type) X(ppc) X(pc) X(vbr) X(sfc) X(dfc) X(ir) X(t1_flag) X(t0_flag) X(s_flag) X(m_flag) \
	X(x_flag) X(n_flag) X(not_z_flag) X(v_flag) X(c_flag) X(int_mask) X(int_level) X(stopped) \
	X(pref_addr) X(pref_data) X(instr_mode) X(run_mode)

int cpu_state_save(uint32_t *out)
{
	int n = 0;
	for (int i = 0; i < 16; i++)
		out[n++] = m68ki_cpu.dar[i];
	for (int i = 0; i < 7; i++)
		out[n++] = m68ki_cpu.sp[i];
#define SAVE(f) out[n++] = m68ki_cpu.f;
	FIELDS(SAVE)
	return n;
}

void cpu_state_load(const uint32_t *in)
{
	int n = 0;
	for (int i = 0; i < 16; i++)
		m68ki_cpu.dar[i] = in[n++];
	for (int i = 0; i < 7; i++)
		m68ki_cpu.sp[i] = in[n++];
#define LOAD(f) m68ki_cpu.f = in[n++];
	FIELDS(LOAD)
}
