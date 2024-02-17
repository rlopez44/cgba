#ifndef CGBA_GBA_H
#define CGBA_GBA_H

#include <stdbool.h>
#include <stdint.h>
#include "cgba/cpu.h"
#include "cgba/gamepad.h"
#include "cgba/memory.h"
#include "cgba/ppu.h"

typedef struct gba_system {
    arm7tdmi *cpu;
    gba_mem *mem;
    gba_ppu *ppu;
    gba_gamepad *gamepad;
    uint64_t clocks_emulated;
    bool skip_bios;
    bool running;
} gba_system;

void init_system_or_die(gba_system *gba, const char *romfile, const char *biosfile);
void deinit_system(gba_system *gba);
void run_system(gba_system *gba);

void report_rom_info(uint8_t *rom);

#endif /* CGBA_GBA_H */
