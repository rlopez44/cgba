#ifndef CGBA_ARM7TDMI_H
#define CGBA_ARM7TDMI_H

#include "cgba/cpu.h"

#define T_BITMASK (1 << 5)

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

void reload_pipeline(arm7tdmi *cpu);

/* ARM instructions */
void bx(arm7tdmi *cpu);

#endif /* CGBA_ARM7TDMI_H */
