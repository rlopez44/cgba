#ifndef CGBA_ARM7TDMI_H
#define CGBA_ARM7TDMI_H

#include "cgba/cpu.h"

#define T_BITMASK (1 << 5)

void reload_pipeline(arm7tdmi *cpu);

void prefetch(arm7tdmi *cpu);

int decode_and_execute_arm(arm7tdmi *cpu);

int decode_and_execute_thumb(arm7tdmi *cpu);

void panic_illegal_instruction(arm7tdmi *cpu);

#endif /* CGBA_ARM7TDMI_H */
