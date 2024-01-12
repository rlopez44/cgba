#ifndef CGBA_CPU_H
#define CGBA_CPU_H

#include <stdint.h>
#include "cgba/memory.h"

/* 4 times the GB CPU frequency */
#define GBA_CPU_FREQ 16777216

#define ARM_NUM_BANKS 5
#define ARM_NUM_BANKED_REGISTERS 7
#define ARM_NUM_REGISTERS 16

#define CPU_MODE_MASK   0x1fu
#define CNTRL_BITS_MASK 0xffu

#define COND_N_SHIFT 31
#define COND_Z_SHIFT 30
#define COND_C_SHIFT 29
#define COND_V_SHIFT 28

#define COND_N_BITMASK (1 << COND_N_SHIFT)
#define COND_Z_BITMASK (1 << COND_Z_SHIFT)
#define COND_C_BITMASK (1 << COND_C_SHIFT)
#define COND_V_BITMASK (1 << COND_V_SHIFT)

#define COND_FLAGS_MASK 0xf0000000

typedef enum arm_cpu_mode {
    MODE_USR = 0x10,
    MODE_FIQ = 0x11,
    MODE_IRQ = 0x12,
    MODE_SVC = 0x13,
    MODE_ABT = 0x17,
    MODE_UND = 0x1b,
    MODE_SYS = 0x1f, // privileged user mode
} arm_cpu_mode;

typedef enum arm_bankmode {
    BANK_FIQ,
    BANK_SVC,
    BANK_ABT,
    BANK_IRQ,
    BANK_UND,
    BANK_NONE, // user/system mode; no banked registers
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
    // bank R8 - R12 used only by FIQ
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
arm7tdmi *init_cpu(void);

/* Free memory allocated for the CPU */
void deinit_cpu(arm7tdmi *cpu);

/* Reset the CPU by performing the following:
 *
 * - store PC and CPSR into R14_svc and SPSR_svc
 * - in CPSR: force M[4:0] to 10011 (supervisor mode),
 *   set I, set F, and reset T (change CPU state to ARM)
 * - force PC to fetch from address 0x00
 */
void reset_cpu(arm7tdmi *cpu);

/* Run the CPU for one instruction
 * Returning number of cycles required
 */
int run_cpu(arm7tdmi *cpu);

/* Set the CPU state to what it would be when
 * the BIOS finishes running on boot up
 */
void skip_boot_screen(arm7tdmi *cpu);

#endif /* CGBA_CPU_H */
