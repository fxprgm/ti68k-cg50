#!/usr/bin/env python3
"""Fill the git-ignored addin/gen/ for TIbench (the measurement add-in):

  gen/musashi/   Musashi from third_party/Musashi (unchanged files) + its generated opcode
                 tables (m68kmake, built with the host compiler) + stubs for the FPU/PMMU code
                 a 68000 never runs (m68kfpu.c, m68kmmu.h: keeps softfloat out of the g3a)
  gen/rom.bin    the flash image's non-empty 4 KB pages: "TIRM", page count (u32 BE),
                 page numbers (u16 BE each, padded to 4 bytes), then the pages
  gen/rom.S      .incbin of rom.bin as `ti_rom_pages`

  python3 prepare.py --rom ROMFILE [--nosave]

--nosave drops Musashi's per-instruction copy of the 16 registers (REG_DA_SAVE, kept only for
bus-error recovery, which this machine never raises).
Never commit gen/ or the .g3a: they hold ROM bytes.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MUSASHI = os.path.join(ROOT, 'third_party', 'Musashi')
GEN = os.path.join(HERE, 'gen')

FPU_STUB = r'''/* TIbench: stub for Musashi's m68kfpu.c (68040 FPU). A 68000 has no FPU: F-line traps. */
void m68040_fpu_op0(void) { m68ki_exception_1111(); }
void m68040_fpu_op1(void) { m68ki_exception_1111(); }
'''
MMU_STUB = r'''/* TIbench: stub for Musashi's m68kmmu.h (68851 PMMU): F-line trap on a 68000. */
void m68881_mmu_ops(void) { m68ki_exception_1111(); }
'''


def patch_once(text, old, new, what):
    n = text.count(old)
    if n != 1:
        sys.exit(f'prepare.py: anchor for {what} found {n} times')
    return text.replace(old, new)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default='')
    ap.add_argument('--nosave', action='store_true')
    a = ap.parse_args()

    if os.path.isdir(GEN):
        shutil.rmtree(GEN)
    mus = os.path.join(GEN, 'musashi')
    os.makedirs(mus)
    for f in ['m68kcpu.c', 'm68kcpu.h', 'm68k.h', 'm68k_in.c', 'm68kmake.c']:
        shutil.copy(os.path.join(MUSASHI, f), mus)
    shutil.copytree(os.path.join(MUSASHI, 'softfloat'), os.path.join(mus, 'softfloat'))
    with open(os.path.join(mus, 'm68kfpu.c'), 'w') as f:
        f.write(FPU_STUB)
    with open(os.path.join(mus, 'm68kmmu.h'), 'w') as f:
        f.write(MMU_STUB)
    cpu = os.path.join(mus, 'm68kcpu.c')
    src = open(cpu).read()
    if a.nosave:
        src = patch_once(src, '''			for (i = 15; i >= 0; i--){
				REG_DA_SAVE[i] = REG_DA[i];
			}''', '''			(void)i; /* TIbench --nosave: no register snapshot */''', 'REG_DA_SAVE loop')
    open(cpu, 'w').write(src)
    # opcode tables
    mk = os.path.join(mus, 'm68kmake_host')
    subprocess.check_call(['cc', '-O2', '-w', '-o', mk, os.path.join(mus, 'm68kmake.c')])
    subprocess.check_call([mk, mus, os.path.join(mus, 'm68k_in.c')], stdout=subprocess.DEVNULL)
    os.remove(mk)
    # the two 64 K-entry tables (256 KB of handler pointers, 5 x 64 KB of cycle counts) don't fit
    # in gint's static RAM: put them in free DRAM (src/main.c's map: 0x8C440000, 0x8C480000)
    h = os.path.join(mus, 'm68kops.h')
    t = open(h).read()
    t = patch_once(t, 'extern void (*m68ki_instruction_jump_table[0x10000])(void); /* opcode handler jump table */',
                   '#define m68ki_instruction_jump_table ((void (**)(void))0x8C440000) /* TIbench: in DRAM */',
                   'jump table extern')
    t = patch_once(t, 'extern unsigned char m68ki_cycles[][0x10000];',
                   '#define m68ki_cycles ((unsigned char (*)[0x10000])0x8C480000) /* TIbench: in DRAM */',
                   'cycles extern')
    open(h, 'w').write(t)
    c = os.path.join(mus, 'm68kops.c')
    t = open(c).read()
    t = patch_once(t, 'void  (*m68ki_instruction_jump_table[0x10000])(void); /* opcode handler jump table */', '',
                   'jump table definition')
    t = patch_once(t, 'unsigned char m68ki_cycles[NUM_CPU_TYPES][0x10000]; /* Cycles used by CPU type */', '',
                   'cycles definition')
    open(c, 'w').write(t)

    # ROM pages
    rom = open(a.rom, 'rb').read()
    if len(rom) != 0x200000:
        sys.exit('prepare.py: expected a 2 MB flash image')
    pages = [i for i in range(0, len(rom) // 4096) if rom[i * 4096:(i + 1) * 4096] != b'\xff' * 4096]
    idx = b''.join(struct.pack('>H', p) for p in pages)
    if len(idx) % 4:
        idx += b'\0\0'
    blob = b'TIRM' + struct.pack('>I', len(pages)) + idx + b''.join(rom[p * 4096:(p + 1) * 4096] for p in pages)
    with open(os.path.join(GEN, 'rom.bin'), 'wb') as f:
        f.write(blob)
    with open(os.path.join(GEN, 'rom.S'), 'w') as f:
        f.write('\t.section .rodata\n\t.balign 4\n\t.global _ti_rom_pages\n_ti_rom_pages:\n'
                f'\t.incbin "{os.path.join(GEN, "rom.bin")}"\n')
    print(f'gen/: Musashi{" (--nosave)" if a.nosave else ""}, rom.bin {len(blob)} bytes '
          f'({len(pages)} of 512 pages) from {os.path.basename(a.rom)}')


if __name__ == '__main__':
    main()
