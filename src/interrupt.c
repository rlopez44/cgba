#include <stdbool.h>
#include "cgba/cpu.h"
#include "cpu/arm7tdmi.h"

#define IRQ_VECTOR 0x18

bool interrupt_pending(arm7tdmi *cpu)
{
    bool interrupt_possible = cpu->irq_enable & cpu->irq_request & 0x3fff;
    bool cpsr_irq_disable = cpu->cpsr & IRQ_DISABLE;

    return cpu->ime_flag && !cpsr_irq_disable && interrupt_possible;
}


void handle_interrupt(arm7tdmi *cpu)
{
    uint32_t r15val = cpu->registers[R15];
    if (cpu->cpsr & T_BITMASK)
        cpu->banked_registers[BANK_IRQ][BANK_R14] = r15val;
    else
        cpu->banked_registers[BANK_IRQ][BANK_R14] = r15val - 4;

    cpu->spsr[BANK_IRQ] =cpu->cpsr;

    uint32_t mask = THUMB_ENABLE | CPU_MODE_MASK;
    cpu->cpsr = (cpu->cpsr & ~mask) | IRQ_DISABLE | MODE_IRQ;
    cpu->registers[R15] = IRQ_VECTOR;
    reload_pipeline(cpu);
}
