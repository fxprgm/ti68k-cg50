/* Musashi configuration for the TI-89 / TI-92 Plus bench (passed as -DMUSASHI_CNF). Plain 68000;
 * the interrupt acknowledge and the per-instruction hook go to ti68k.c. */
#ifndef TI_M68KCONF_H
#define TI_M68KCONF_H

#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2

#define M68K_COMPILE_FOR_MAME      M68K_OPT_OFF

#define M68K_EMULATE_010            M68K_OPT_OFF
#define M68K_EMULATE_EC020          M68K_OPT_OFF
#define M68K_EMULATE_020            M68K_OPT_OFF
#define M68K_EMULATE_030            M68K_OPT_OFF
#define M68K_EMULATE_040            M68K_OPT_OFF
#define M68K_SEPARATE_READS         M68K_OPT_OFF
#define M68K_SIMULATE_PD_WRITES     M68K_OPT_OFF

/* the ASIC's interrupts are held until the CPU takes them (ti68k.c clears the level) */
#define M68K_EMULATE_INT_ACK        M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A)    ti_int_ack(A)
int ti_int_ack(int level);

#define M68K_EMULATE_BKPT_ACK       M68K_OPT_OFF
#define M68K_EMULATE_TRACE          M68K_OPT_OFF
#define M68K_EMULATE_RESET          M68K_OPT_OFF
#define M68K_CMPILD_HAS_CALLBACK    M68K_OPT_OFF
#define M68K_RTE_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_TAS_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_ILLG_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_TRAP_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_EMULATE_FC             M68K_OPT_OFF
#define M68K_MONITOR_PC             M68K_OPT_OFF

/* instruction counter (and opcode / PC statistics in the bench) */
#if defined(TI_INLINE_HOOK)
/* the add-in's --fastmem build: ti.instr++ inline (ti_fastmem.h) */
#define M68K_INSTRUCTION_HOOK       M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) ti_inline_hook()
#elif !defined(TI_NO_HOOK)
#define M68K_INSTRUCTION_HOOK       M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) ti_instr_hook(pc)
void ti_instr_hook(unsigned int pc);
#else
#define M68K_INSTRUCTION_HOOK       M68K_OPT_OFF
#endif

#define M68K_EMULATE_PREFETCH       M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  M68K_OPT_OFF
#define M68K_LOG_ENABLE             M68K_OPT_OFF
#define M68K_LOG_1010_1111          M68K_OPT_OFF
#define M68K_LOG_FILEHANDLE         stderr
#define M68K_EMULATE_PMMU           M68K_OPT_OFF

#ifndef M68K_USE_64_BIT
#define M68K_USE_64_BIT             M68K_OPT_ON
#endif

#endif
