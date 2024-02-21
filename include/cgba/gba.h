#ifndef CGBA_GBA_H
#define CGBA_GBA_H

#include <stdbool.h>
#include <stdint.h>
#include "cgba/cpu.h"
#include "cgba/gamepad.h"
#include "cgba/memory.h"
#include "cgba/ppu.h"

/* frame duration is 16.743 ms */
#define GBA_FRAME_DURATION_MS 17

typedef struct gba_system {
    arm7tdmi *cpu;
    gba_mem *mem;
    gba_ppu *ppu;
    gba_gamepad *gamepad;
    uint64_t clocks_emulated;
    uint64_t next_frame_time;
    bool skip_bios;
    bool running;
} gba_system;

void init_system_or_die(gba_system *gba, const char *romfile, const char *biosfile);
void deinit_system(gba_system *gba);
void run_system(gba_system *gba);

void report_rom_info(uint8_t *rom);

#endif /* CGBA_GBA_H */
