#ifndef CGBA_BIOS_H
#define CGBA_BIOS_H

#include "cgba/cpu.h"

typedef enum bios_syscall {
    SYSCALL_DIV = 0x06, // signed 32-bit division
} bios_syscall;

int gba_syscall(arm7tdmi *cpu);

#endif /* CGBA_BIOS_H */
