#ifndef CGBA_INTERRUPT_H
#define CGBA_INTERRUPT_H

#include <stdbool.h>
#include "cgba/cpu.h"

enum IRQ_FLAGS {
    IRQ_VBLANK = 1 << 0,
    IRQ_HBLANK = 1 << 1,
    IRQ_VCOUNT = 1 << 2,
    IRQ_TIMER0 = 1 << 3,
    IRQ_TIMER1 = 1 << 4,
    IRQ_TIMER2 = 1 << 5,
    IRQ_TIMER3 = 1 << 6,
    IRQ_SERIAL = 1 << 7,
    IRQ_DMA0   = 1 << 8,
    IRQ_DMA1   = 1 << 9,
    IRQ_DMA2   = 1 << 10,
    IRQ_DMA3   = 1 << 11,
    IRQ_KEYPAD = 1 << 12,
    IRQ_EXTERN = 1 << 13,
};

bool interrupt_pending(arm7tdmi *cpu);
void handle_interrupt(arm7tdmi *cpu);

#endif /* CGBA_INTERRUPT_H */
