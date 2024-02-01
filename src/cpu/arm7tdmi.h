#ifndef CGBA_ARM7TDMI_H
#define CGBA_ARM7TDMI_H

#include <stdbool.h>
#include <stdint.h>
#include "cgba/cpu.h"

#define T_BITMASK (1 << 5)

void reload_pipeline(arm7tdmi *cpu);

void prefetch(arm7tdmi *cpu);

arm_bankmode get_current_bankmode(arm7tdmi *cpu);

bool check_cond(arm7tdmi *cpu);

int decode_and_execute_arm(arm7tdmi *cpu);

int decode_and_execute_thumb(arm7tdmi *cpu);

bool barrel_shift(arm7tdmi *cpu, uint32_t inst, uint32_t *result, bool immediate);

void do_branch_and_exchange(arm7tdmi *cpu, uint32_t addr);

uint32_t read_register(arm7tdmi *cpu, int regno);

void write_register(arm7tdmi *cpu, int regno, uint32_t value);

void panic_illegal_instruction(arm7tdmi *cpu);

#endif /* CGBA_ARM7TDMI_H */
