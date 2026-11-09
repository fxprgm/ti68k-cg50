#!/usr/bin/env python3
"""Fill the git-ignored addin/gen/ for TIbench (the measurement add-in):

  gen/musashi/   Musashi from third_party/Musashi (unchanged files) + its generated opcode
                 tables (m68kmake, built with the host compiler) + stubs for the FPU/PMMU code
                 a 68000 never runs (m68kfpu.c, m68kmmu.h: keeps softfloat out of the g3a)
  gen/rom.bin    the flash image's non-empty 4 KB pages: "TIRM", page count (u32 BE),
                 page numbers (u16 BE each, padded to 4 bytes), then the pages
  gen/rom.S      .incbin of rom.bin as `ti_rom_pages`

  python3 prepare.py --rom ROMFILE [--nosave]

--fastmem inlines the RAM/flash paths of every memory access (src/ti_fastmem.h) and the
instruction counter (build with -DTI_INLINE_HOOK, CMake does when gen/FASTMEM exists).
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
# The ROM dump of your own TI-89 / TI-92 Plus (a 2 MB flash image) that TIbench runs: fill in its
# path here, or pass --rom ROMFILE. TIbench embeds it, so the .g3a is for your calculator only.
ROM_PATH = ''

MUSASHI = os.path.join(ROOT, 'third_party', 'Musashi')
MUSASHI_URL = 'https://github.com/kstenerud/Musashi.git'
MUSASHI_COMMIT = '313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd'   # the version tested here
GEN = os.path.join(HERE, 'gen')


def fetch_musashi():
    """third_party/Musashi (MIT): cloned at the tested commit when it isn't there yet"""
    if os.path.isfile(os.path.join(MUSASHI, 'm68kmake.c')):
        return
    print(f'prepare.py: fetching Musashi {MUSASHI_COMMIT[:7]} into third_party/Musashi')
    os.makedirs(os.path.dirname(MUSASHI), exist_ok=True)
    subprocess.run(['git', 'clone', '-q', MUSASHI_URL, MUSASHI], check=True)
    subprocess.run(['git', '-C', MUSASHI, 'checkout', '-q', MUSASHI_COMMIT], check=True)

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


def gen_musashi(gen, nosave, fastmem):
    """gen/musashi: Musashi + generated tables (DRAM-resident), --nosave / --fastmem patches"""
    fetch_musashi()
    if os.path.isdir(gen):
        shutil.rmtree(gen)
    mus = os.path.join(gen, 'musashi')
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
    if nosave:
        src = patch_once(src, '''			for (i = 15; i >= 0; i--){
				REG_DA_SAVE[i] = REG_DA[i];
			}''', '''			(void)i; /* TIbench --nosave: no register snapshot */''', 'REG_DA_SAVE loop')
    open(cpu, 'w').write(src)
    if fastmem:
        hp = os.path.join(mus, 'm68kcpu.h')
        h = open(hp).read()
        for w in ('read', 'write'):
            for n in (8, 16, 32):
                call = f'm68k_{w}_memory_{n}(ADDRESS_68K(address)'
                new = f'ti_{"rd" if w == "read" else "wr"}{n}(ADDRESS_68K(address)'
                h = patch_once(h, call, new, call)
        h = patch_once(h, '#include "m68k.h"', '#include "m68k.h"\n#include "ti_fastmem.h" /* TIbench --fastmem */',
                       'm68k.h include')
        open(hp, 'w').write(h)
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
    if fastmem:
        open(os.path.join(gen, 'FASTMEM'), 'w').close()


def gen_rom(gen, rom_path):
    """gen/rom.bin + gen/rom.S (`ti_rom_pages`): the flash image's non-empty 4 KB pages.
    Returns (blob size, page count)."""
    rom = open(rom_path, 'rb').read()
    if len(rom) != 0x200000:
        sys.exit('prepare.py: expected a 2 MB flash image')
    pages = [i for i in range(0, len(rom) // 4096) if rom[i * 4096:(i + 1) * 4096] != b'\xff' * 4096]
    idx = b''.join(struct.pack('>H', p) for p in pages)
    if len(idx) % 4:
        idx += b'\0\0'
    blob = b'TIRM' + struct.pack('>I', len(pages)) + idx + b''.join(rom[p * 4096:(p + 1) * 4096] for p in pages)
    with open(os.path.join(gen, 'rom.bin'), 'wb') as f:
        f.write(blob)
    with open(os.path.join(gen, 'rom.S'), 'w') as f:
        f.write('\t.section .rodata\n\t.balign 4\n\t.global _ti_rom_pages\n_ti_rom_pages:\n'
                f'\t.incbin "{os.path.join(gen, "rom.bin")}"\n')
    return len(blob), len(pages)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default=ROM_PATH, help='your TI-89 / TI-92 Plus ROM dump (2 MB)')
    ap.add_argument('--nosave', action='store_true')
    ap.add_argument('--fastmem', action='store_true',
                    help='inline RAM/flash paths (src/ti_fastmem.h) + inline instruction counter')
    a = ap.parse_args()
    if not a.rom:
        sys.exit('prepare.py: no ROM set. TIbench needs the ROM dump of your own TI-89 / TI-92 Plus:\n'
                 '  python3 prepare.py --rom path/to/your.rom [--nosave] [--fastmem]\n'
                 'or fill in ROM_PATH at the top of addin/prepare.py.')
    if not os.path.isfile(a.rom):
        sys.exit(f'prepare.py: no ROM file at {a.rom}')
    gen_musashi(GEN, a.nosave, a.fastmem)
    n, pages = gen_rom(GEN, a.rom)
    print(f'gen/: Musashi{" --nosave" if a.nosave else ""}{" --fastmem" if a.fastmem else ""}, rom.bin {n} bytes '
          f'({pages} of 512 pages) from {os.path.basename(a.rom)}')


if __name__ == '__main__':
    main()
