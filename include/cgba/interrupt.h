#ifndef CGBA_INTERRUPT_H
#define CGBA_INTERRUPT_H

#include <stdbool.h>
#include "cgba/cpu.h"

bool interrupt_pending(arm7tdmi *cpu);
void handle_interrupt(arm7tdmi *cpu);

#endif /* CGBA_INTERRUPT_H */
