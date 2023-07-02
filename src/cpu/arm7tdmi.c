#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "cgba/cpu.h"
#include "cgba/log.h"
#include "cgba/memory.h"
#include "arm7tdmi.h"

/* Reload the instruction pipeline after a pipeline flush */
void reload_pipeline(arm7tdmi *cpu)
{
    bool thumb = cpu->cpsr & T_BITMASK;
    if (thumb)
    {
        cpu->pipeline[0] = read_halfword(cpu->mem, cpu->registers[R15] + 0);
        cpu->pipeline[1] = read_halfword(cpu->mem, cpu->registers[R15] + 2);
        cpu->registers[R15] += 4;
    }
    else
    {
        cpu->pipeline[0] = read_word(cpu->mem, cpu->registers[R15] + 0);
        cpu->pipeline[1] = read_word(cpu->mem, cpu->registers[R15] + 4);
        cpu->registers[R15] += 8;
    }
}

void skip_boot_screen(arm7tdmi *cpu)
{
    cpu->cpsr = (cpu->cpsr & CPU_MODE_MASK) | MODE_SYS;
    cpu->banked_registers[BANK_SVC][BANK_R13] = 0x03007fe0;
    cpu->banked_registers[BANK_IRQ][BANK_R13] = 0x03007fa0;
    cpu->registers[R13] = 0x03007f00;
    cpu->registers[R15] = 0x08000000;
    reload_pipeline(cpu);
}

static int decode_and_execute(arm7tdmi *cpu)
{
    int num_clocks;
    if (cpu->cpsr & T_BITMASK)
    {
        // TODO: thumb support
        fprintf(stderr, "NotImplemented: Thumb instructions\n");
        exit(1);
    }
    else
    {
        num_clocks = decode_and_execute_arm(cpu);
    }

    return num_clocks;
}

int run_cpu(arm7tdmi *cpu)
{
#ifdef DEBUG
    log_cpu_state(cpu, stdout);
#endif
    return decode_and_execute(cpu);
}

void reset_cpu(arm7tdmi *cpu)
{
    // NOTE: the value of PC and CPSR saved on
    // reset is not defined by the architecture
    cpu->banked_registers[BANK_SVC][BANK_R14] = cpu->registers[R14];
    cpu->spsr[BANK_SVC] = cpu->cpsr;
    cpu->cpsr = (cpu->cpsr & CPU_MODE_MASK) | MODE_SVC;
    cpu->cpsr = (cpu->cpsr & ~0xe0) | 0xc0;
    cpu->registers[R14] = 0x0;

    // to simplify things, we'll also use a few more cycles
    // to fill up the instruction pipeline, rather than
    // deferring this to the CPU's run function
    reload_pipeline(cpu);
}

arm7tdmi *init_cpu(void)
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

    return cpu;
}

void deinit_cpu(arm7tdmi *cpu)
{
    free(cpu);
}
