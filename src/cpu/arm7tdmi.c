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
    cpu->cpsr = (cpu->cpsr & ~CPU_MODE_MASK) | MODE_SYS;
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
        num_clocks = decode_and_execute_thumb(cpu);
    else
        num_clocks = decode_and_execute_arm(cpu);

    return num_clocks;
}

// prefetch a new instruction for the instruction pipeline
void prefetch(arm7tdmi *cpu)
{
    bool thumb = cpu->cpsr & T_BITMASK;
    cpu->pipeline[0] = cpu->pipeline[1];

    uint32_t fetched;
    if (thumb)
        fetched = read_halfword(cpu->mem, cpu->registers[R15]);
    else
        fetched = read_word(cpu->mem, cpu->registers[R15]);

    cpu->pipeline[1] = fetched;

    cpu->registers[R15] += thumb ? 2 : 4;
}

void panic_illegal_instruction(arm7tdmi *cpu)
{
    const char *inst_type;
    int padlen;
    uint32_t addr;
    uint32_t inst = cpu->pipeline[0];
    if (cpu->cpsr & T_BITMASK)
    {
        inst &= 0xffff;
        padlen = 4;
        inst_type = "THUMB";
        addr = cpu->registers[R15] - 4;
    }
    else
    {
        padlen = 8;
        inst_type = "ARM";
        addr = cpu->registers[R15] - 8;
    }

    fprintf(stderr,
            "Error: Illegal %s instruction encountered: %0*X at address %08X\n",
            inst_type,
            padlen,
            inst,
            addr);
    exit(1);
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
    cpu->banked_registers[BANK_SVC][BANK_R14] = cpu->registers[R15];
    cpu->spsr[BANK_SVC] = cpu->cpsr;
    cpu->cpsr = (cpu->cpsr & ~CNTRL_BITS_MASK) | IRQ_DISABLE | FIQ_DISABLE | MODE_SVC;
    cpu->registers[R15] = 0x0;
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
