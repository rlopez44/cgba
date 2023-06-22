#ifndef CGBA_CPU_H
#define CGBA_CPU_H

#include <stdint.h>
#include "cgba/memory.h"

#define ARM_NUM_BANKS 5
#define ARM_NUM_BANKED_REGISTERS 7
#define ARM_NUM_REGISTERS 16

typedef struct arm7tdmi {
    uint32_t pipeline[2];
    uint32_t registers[ARM_NUM_REGISTERS];
    uint32_t banked_registers[ARM_NUM_BANKS][ARM_NUM_BANKED_REGISTERS];

    // current and saved program status registers
    uint32_t cpsr;
    uint32_t spsr[ARM_NUM_BANKS];

    gba_mem *mem;
} arm7tdmi;


/* Create and initialize the ARM7TDMI CPU */
arm7tdmi *init_cpu(gba_mem *mem);

/* Free memory allocated for the CPU */
void deinit_cpu(arm7tdmi *cpu);

#endif /* CGBA_CPU_H */
