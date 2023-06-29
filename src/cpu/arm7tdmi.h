#ifndef CGBA_ARM7TDMI_H
#define CGBA_ARM7TDMI_H

#include "cgba/cpu.h"

#define T_BITMASK (1 << 5)

void reload_pipeline(arm7tdmi *cpu);

void decode_and_execute_arm(arm7tdmi *cpu);

#endif /* CGBA_ARM7TDMI_H */
