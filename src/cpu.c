#include <stdlib.h>
#include "cgba/cpu.h"

/* Reset the CPU by performing the following:
 *
 * - store PC and CPSR into R14_svc and SPSR_svc
 * - in CPSR: force M[4:0] to 10011 (supervisor mode),
 *   set I, set F, and reset T (change CPU state to ARM)
 * - force PC to fetch from address 0x00
 */
static void reset_cpu(arm7tdmi *cpu)
{
    // NOTE: the value of PC and CPSR saved on
    // reset is not defined by the architecture
    cpu->banked_registers[BANK_SVC][BANK_R14] = cpu->registers[R14];
    cpu->spsr[BANK_SVC] = cpu->cpsr;
    cpu->cpsr = (cpu->cpsr & ~0xff) | 0xd3;
    cpu->registers[R14] = 0x0;
}

arm7tdmi *init_cpu(void)
{
    arm7tdmi *cpu = malloc(sizeof(arm7tdmi));
    reset_cpu(cpu);
    return cpu;
}

void deinit_cpu(arm7tdmi *cpu)
{
    free(cpu);
}
