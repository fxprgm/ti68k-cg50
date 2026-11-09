#!/usr/bin/env python3
"""Fill the git-ignored app/gen/ for TI68k (run before `fxsdk build-cg`).

    python3 prepare.py                      -> TI68k.g3a: no ROM inside, the one to share
    python3 prepare.py --embed-rom ROMFILE  -> TI68Krom.g3a: your ROM inside (a personal build)

gen/musashi/: Musashi (third_party/Musashi) with the two TIbench patches (addin/prepare.py):
--nosave (no per-instruction register snapshot) and --fastmem (inline RAM/flash paths,
addin/src/ti_fastmem.h); its 64 K-entry tables live in DRAM (src/mem.h).

--embed-rom puts the ROM's non-empty 4 KB pages in gen/rom.bin + gen/rom.S: the add-in then
needs no .rom file on the calculator. It contains TI's code: keep it to yourself, never share
or commit it. Without the option any gen/rom.* is removed.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'addin'))
import prepare as base  # noqa: E402

GEN = os.path.join(HERE, 'gen')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--embed-rom', metavar='ROMFILE')
    a = ap.parse_args()
    base.gen_musashi(GEN, nosave=True, fastmem=True)
    if a.embed_rom:
        n, pages = base.gen_rom(GEN, a.embed_rom)
        print(f'gen/: Musashi --nosave --fastmem, ROM {os.path.basename(a.embed_rom)} embedded '
              f'({pages} of 512 pages, {n} bytes) -> TI68Krom.g3a: personal build, never share it')
    else:
        print('gen/: Musashi --nosave --fastmem, no ROM -> TI68k.g3a')


if __name__ == '__main__':
    main()
