#ifndef CGBA_CPU_H
#define CGBA_CPU_H

#include <stdint.h>
#include "cgba/memory.h"

#define ARM_NUM_BANKS 5
#define ARM_NUM_BANKED_REGISTERS 7
#define ARM_NUM_REGISTERS 16

typedef enum arm_bankmode {
    BANK_FIQ,
    BANK_SVC,
    BANK_ABT,
    BANK_IRQ,
    BANK_UND,
    BANK_NONE, // user mode; no banked registers
} arm_bankmode;

typedef enum arm_register {
    R0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
} arm_register;

typedef enum arm_bank_register {
    BANK_R8,
    BANK_R9,
    BANK_R10,
    BANK_R11,
    BANK_R12,
    BANK_R13,
    BANK_R14,
} arm_bank_register;

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

/* Run the CPU for one instruction */
void run_cpu(arm7tdmi *cpu);

#endif /* CGBA_CPU_H */
