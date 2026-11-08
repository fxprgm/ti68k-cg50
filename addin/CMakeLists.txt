# TIbench: Musashi + bench/ti68k.c (TI-89 / TI-92 Plus) on the fx-CG50 (measurement build, local only:
# the .g3a embeds the ROM).   python3 prepare.py [--rom ...] [--nosave] && fxsdk build-cg
cmake_minimum_required(VERSION 3.15)
project(TIbench C ASM)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/gen)
set(MUS ${GEN}/musashi)
set(BENCH ${CMAKE_CURRENT_SOURCE_DIR}/../bench)
if(NOT EXISTS ${MUS}/m68kops.c OR NOT EXISTS ${GEN}/rom.S)
  message(FATAL_ERROR "run python3 prepare.py first")
endif()

set(CORE_FILES ${MUS}/m68kcpu.c ${MUS}/m68kops.c)
set(SOURCES src/main.c src/prof.c ${BENCH}/ti68k.c ${BENCH}/scenario.c gen/rom.S ${CORE_FILES})

add_executable(tibench ${SOURCES})
# prepare.py --fastmem: inline memory fast paths + an inline instruction counter
if(EXISTS ${GEN}/FASTMEM)
  target_compile_definitions(tibench PRIVATE TI_INLINE_HOOK)
endif()
set_source_files_properties(${CORE_FILES} PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(tibench PRIVATE src ${BENCH} ${MUS})
target_compile_definitions(tibench PRIVATE MUSASHI_CNF="ti_m68kconf.h" M68K_USE_64_BIT=0 TI_LIGHT_HOOK)
target_compile_options(tibench PRIVATE -O2 -g -std=gnu11 -Wall -Wno-unused-parameter
  -Wno-unused-function -Wno-sign-compare)
target_link_options(tibench PRIVATE -Wl,-Map=tibench.map)
target_link_libraries(tibench Gint::Gint)

generate_g3a(TARGET tibench OUTPUT "TIbench.g3a" NAME "TIbench")
