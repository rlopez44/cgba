#ifndef CGBA_ARM7TDMI_H
#define CGBA_ARM7TDMI_H

#include <stdbool.h>
#include <stdint.h>
#include "cgba/cpu.h"

typedef struct barrel_shift_args {
    uint32_t shift_input;
    int shift_amt;
    bool immediate;

    // only used when shifting a register
    bool shift_by_reg;
    int shift_opcode;
} barrel_shift_args;

#define T_BITMASK (1 << 5)

int count_set_bits(uint32_t n);

void reload_pipeline(arm7tdmi *cpu);

void prefetch(arm7tdmi *cpu);

arm_bankmode get_current_bankmode(arm7tdmi *cpu);

bool check_cond(arm7tdmi *cpu);

int decode_and_execute_arm(arm7tdmi *cpu);

int decode_and_execute_thumb(arm7tdmi *cpu);

int get_multiply_array_cycles(uint32_t rs, bool mul_long, bool signed_);

bool barrel_shift(arm7tdmi *cpu, barrel_shift_args *args, uint32_t *result);

void do_branch_and_exchange(arm7tdmi *cpu, uint32_t addr);

uint32_t read_register(arm7tdmi *cpu, int regno);

void write_register(arm7tdmi *cpu, int regno, uint32_t value);

void panic_illegal_instruction(arm7tdmi *cpu);

#endif /* CGBA_ARM7TDMI_H */
