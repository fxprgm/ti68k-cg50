#!/bin/sh
# Build tibench on the Mac: Musashi (third_party/Musashi, untouched) with our configuration
# (bench/ti_m68kconf.h), its opcode tables generated into build/musashi, + bench/ti68k.c.
set -e
cd "$(dirname "$0")/.."
M=third_party/Musashi
G=build/musashi
mkdir -p $G build
if [ ! -f $G/m68kops.c ] || [ $M/m68k_in.c -nt $G/m68kops.c ]; then
	cc -O2 -w -o $G/m68kmake $M/m68kmake.c
	$G/m68kmake $G $M/m68k_in.c >/dev/null
fi
CFLAGS="-O2 -std=gnu11 -DMUSASHI_CNF=\"ti_m68kconf.h\" -Ibench -I$G -I$M"
cc $CFLAGS -w -c -o $G/m68kcpu.o $M/m68kcpu.c
cc $CFLAGS -w -c -o $G/m68kops.o $G/m68kops.c
cc $CFLAGS -w -c -o $G/m68kdasm.o $M/m68kdasm.c
cc $CFLAGS -w -c -o $G/softfloat.o $M/softfloat/softfloat.c
cc $CFLAGS -Wall -o build/tibench bench/tibench.c bench/png.c bench/scenario.c bench/ti68k.c $G/m68kcpu.o $G/m68kops.o $G/m68kdasm.o $G/softfloat.o -lm
echo "built build/tibench"
cc $CFLAGS -Wall -o build/tiexp bench/tiexp.c bench/png.c bench/scenario.c bench/ti68k.c $G/m68kcpu.o $G/m68kops.o $G/m68kdasm.o $G/softfloat.o -lm
echo "built build/tiexp"
