# TI68k: a TI-89 / TI-92 Plus emulator add-in for the fx-CG50 (Musashi + bench/ti68k.c).
#   python3 prepare.py && fxsdk build-cg                      -> TI68k.g3a (no ROM inside)
#   python3 prepare.py --embed-rom ROMFILE && fxsdk build-cg  -> TI68Krom.g3a (personal build)
cmake_minimum_required(VERSION 3.15)
project(TI68k C ASM)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/gen)
set(MUS ${GEN}/musashi)
set(BENCH ${CMAKE_CURRENT_SOURCE_DIR}/../bench)
if(NOT EXISTS ${MUS}/m68kops.c)
  message(FATAL_ERROR "run python3 prepare.py first")
endif()

set(CORE_FILES ${MUS}/m68kcpu.c ${MUS}/m68kops.c)
set_source_files_properties(src/cpustate.c PROPERTIES COMPILE_OPTIONS "-w")
set(SOURCES src/main.c src/clock.c src/cpustate.c src/files.c src/keys.c src/screen.c src/state.c
  ${BENCH}/ti68k.c ${CORE_FILES})

set(G3A "TI68k.g3a")
if(EXISTS ${GEN}/rom.S)
  list(APPEND SOURCES gen/rom.S)
  set_source_files_properties(src/main.c PROPERTIES COMPILE_DEFINITIONS "TI68K_EMBED_ROM")
  set(G3A "TI68Krom.g3a")
endif()

add_executable(ti68k ${SOURCES})
set_source_files_properties(${CORE_FILES} PROPERTIES COMPILE_OPTIONS "-w")
# ../addin/src: ti_fastmem.h (the inline memory paths the generated m68kcpu.h includes)
target_include_directories(ti68k PRIVATE src ../addin/src ${BENCH} ${MUS})
target_compile_definitions(ti68k PRIVATE MUSASHI_CNF="ti_m68kconf.h" M68K_USE_64_BIT=0
  TI_LIGHT_HOOK TI_INLINE_HOOK)
target_compile_options(ti68k PRIVATE -O2 -g -std=gnu11 -Wall -Wno-unused-parameter
  -Wno-unused-function -Wno-sign-compare)
target_link_options(ti68k PRIVATE -Wl,-Map=ti68k.map)
target_link_libraries(ti68k Gint::Gint)

generate_g3a(TARGET ti68k OUTPUT ${G3A} NAME "TI68k"
  ICONS assets-cg/icon-uns.png assets-cg/icon-sel.png)
