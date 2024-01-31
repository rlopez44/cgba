#include <stdbool.h>
#include <stdint.h>
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

// Decode ARM condition field or THUMB conditial branch condition
bool check_cond(arm7tdmi *cpu)
{
    uint32_t inst = cpu->pipeline[0];

    int shift = cpu->cpsr & T_BITMASK ? 8 : 28;
    int cond = (inst >> shift) & 0xf;

    bool n_set = cpu->cpsr & COND_N_BITMASK;
    bool z_set = cpu->cpsr & COND_Z_BITMASK;
    bool c_set = cpu->cpsr & COND_C_BITMASK;
    bool v_set = cpu->cpsr & COND_V_BITMASK;

    bool result;
    switch (cond)
    {
        case 0x0: result = z_set; break;
        case 0x1: result = !z_set; break;

        case 0x2: result = c_set; break;
        case 0x3: result = !c_set; break;

        case 0x4: result = n_set; break;
        case 0x5: result = !n_set; break;

        case 0x6: result = v_set; break;
        case 0x7: result = !v_set; break;

        case 0x8: result = c_set && !z_set; break;
        case 0x9: result = !c_set || z_set; break;

        case 0xa: result = n_set == v_set; break;
        case 0xb: result = n_set != v_set; break;

        case 0xc: result = !z_set && (n_set == v_set); break;
        case 0xd: result = z_set || (n_set != v_set); break;

        case 0xe: result = true; break;

        // 0b1111 is reserved and should not be used
        case 0xf: result = false; break;
    }

    return result;
}

void do_branch_and_exchange(arm7tdmi *cpu, uint32_t addr)
{
    if (addr & 1) // THUMB state
    {
        // halfword-align the instruction
        addr &= ~1;
        cpu->cpsr |= T_BITMASK;
    }
    else // ARM state
    {
        cpu->cpsr &= ~T_BITMASK;
    }

    cpu->registers[R15] = addr;

    // BX causes a pipeline flush and refill from [Rn]
    reload_pipeline(cpu);
}

arm_bankmode get_current_bankmode(arm7tdmi *cpu)
{
    arm_bankmode mode;
    switch (cpu->cpsr & CPU_MODE_MASK)
    {
        case MODE_USR:
        case MODE_SYS:
            mode = BANK_NONE;
            break;

        case MODE_FIQ: mode = BANK_FIQ; break;
        case MODE_IRQ: mode = BANK_IRQ; break;
        case MODE_SVC: mode = BANK_SVC; break;
        case MODE_ABT: mode = BANK_ABT; break;
        case MODE_UND: mode = BANK_UND; break;

        default: // illegal mode, should not get here
            fprintf(stderr,
                    "Error: Illegal CPU mode encountered: %02x\n",
                    cpu->cpsr & CPU_MODE_MASK);
            exit(1);
    }

    return mode;
}

static void validate_register_number_or_die(int regno)
{
    if (regno > R15 || regno < R0)
    {
        fprintf(stderr, "Illegal register number accessed: %d\n", regno);
        exit(1);
    }
}

static uint32_t *get_register(arm7tdmi *cpu, int regno)
{
    arm_bankmode mode = get_current_bankmode(cpu);

    bool not_banked = mode == BANK_NONE
                      || regno < R8
                      || (mode != BANK_FIQ && regno < R13)
                      || regno == R15;

    uint32_t *reg;
    if (not_banked)
        reg = cpu->registers + regno;
    else
        reg = cpu->banked_registers[mode] + regno - R8;

    return reg;
}

uint32_t read_register(arm7tdmi *cpu, int regno)
{
    validate_register_number_or_die(regno);
    return *get_register(cpu, regno);
}

void write_register(arm7tdmi *cpu, int regno, uint32_t value)
{
    validate_register_number_or_die(regno);
    uint32_t *reg = get_register(cpu, regno);
    *reg = value;
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
