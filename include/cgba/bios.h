#ifndef CGBA_BIOS_H
#define CGBA_BIOS_H

#include "cgba/cpu.h"
#include "cgba/memory.h"

typedef enum bios_syscall {
    SYSCALL_DIV = 0x06, // signed 32-bit division
} bios_syscall;

int gba_syscall(arm7tdmi *cpu);

int load_bios_file(gba_mem *mem, const char *fname);

#endif /* CGBA_BIOS_H */
