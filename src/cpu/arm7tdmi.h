#ifndef CGBA_ARM7TDMI_H
#define CGBA_ARM7TDMI_H

#include <stdint.h>
#include "cgba/cpu.h"

#define T_BITMASK (1 << 5)

void reload_pipeline(arm7tdmi *cpu);

void prefetch(arm7tdmi *cpu);

int decode_and_execute_arm(arm7tdmi *cpu);

int decode_and_execute_thumb(arm7tdmi *cpu);

void do_branch_and_exchange(arm7tdmi *cpu, uint32_t addr);

void panic_illegal_instruction(arm7tdmi *cpu);

#endif /* CGBA_ARM7TDMI_H */
