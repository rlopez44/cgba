#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "cgba/cpu.h"
#include "cgba/memory.h"
#include "arm7tdmi.h"

/* Reload the instruction pipeline after a pipeline flush */
void reload_pipeline(arm7tdmi *cpu)
{
    bool thumb = cpu->cpsr & T_BITMASK;
    if (thumb)
    {
        cpu->pipeline[0] = read_halfword(cpu, cpu->registers[R15] + 0);
        cpu->pipeline[1] = read_halfword(cpu, cpu->registers[R15] + 2);
        cpu->registers[R15] += 4;
    }
    else
    {
        cpu->pipeline[0] = read_word(cpu, cpu->registers[R15] + 0);
        cpu->pipeline[1] = read_word(cpu, cpu->registers[R15] + 4);
        cpu->registers[R15] += 8;
    }
}

static void decode_and_execute(arm7tdmi *cpu)
{
    if (cpu->cpsr & T_BITMASK)
    {
        // TODO: thumb support
        fprintf(stderr, "NotImplemented: Thumb instructions\n");
        exit(1);
    }
    else
    {
        decode_and_execute_arm(cpu);
    }
}

void run_cpu(arm7tdmi *cpu)
{
    decode_and_execute(cpu);
}

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

    // to simplify things, we'll also use a few more cycles
    // to fill up the instruction pipeline, rather than
    // deferring this to the CPU's run function
    reload_pipeline(cpu);
}

arm7tdmi *init_cpu(gba_mem *mem)
{
    arm7tdmi *cpu = malloc(sizeof(arm7tdmi));
    if (cpu == NULL)
        return NULL;

    cpu->pipeline[0] = 0;
    cpu->pipeline[1] = 0;
    cpu->cpsr = 0;
    for (int i = 0; i < ARM_NUM_REGISTERS; ++i)
        cpu->registers[i] = 0;

    for (int i = 0; i < ARM_NUM_BANKS; ++i)
    {
        cpu->spsr[i] = 0;
        for (int j = 0; j < ARM_NUM_BANKED_REGISTERS; ++j)
            cpu->banked_registers[i][j] = 0;
    }

    cpu->mem = mem;
    reset_cpu(cpu);
    return cpu;
}

void deinit_cpu(arm7tdmi *cpu)
{
    free(cpu);
}
