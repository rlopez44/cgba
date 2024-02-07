#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cgba/cpu.h"
#include "cgba/log.h"
#include "cgba/memory.h"
#include "arm7tdmi.h"

// For use by the LDM/STM instructions
int count_set_bits(uint32_t n)
{
    // Source: https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan
    int nset;
    for (nset = 0; n; ++nset)
        n &= n - 1; // clear the least significant bit set

    return nset;
}

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

/*
 * Calculate how many 8-bit multiplier array cycles
 * the MUL/MLA/MULL/MLAL instruction required.
 *
 * MUL/MLA/SMULL/SMLAL
 * --------------------
 * 1 if Rs[31:8] are all zero or all one
 * 2 if Rs[31:16] are all zero or all one
 * 3 if Rs[31:24] are all zero or all one
 * 4 otherwise
 *
 * UMULL/UMLAL
 * -----------
 * 1 if Rs[31:8] are all zero
 * 2 if Rs[31:16] are all zero
 * 3 if Rs[31:24] are all zero
 * 4 otherwise
 */
int get_multiply_array_cycles(uint32_t rs, bool mul_long, bool signed_)
{
    int num_clocks = 4;
    uint32_t work_val = rs;
    uint32_t ref_val = 0xffffffff;
    bool match_all_ones = !mul_long || signed_;
    for (int i = 1; i < 4; ++i)
    {
        work_val >>= 8;
        ref_val >>= 8;
        bool all_zeros = !work_val;
        bool all_ones  = work_val == ref_val;

        if (all_zeros || (match_all_ones && all_ones))
        {
            num_clocks = i;
            break;
        }
    }

    return num_clocks;
}

// Perform barrel shifter operation and return the shifter's carry out
bool barrel_shift(arm7tdmi *cpu, barrel_shift_args *args, uint32_t *result)
{
    bool shifter_carry = false;
    int shift_amt = args->shift_amt;
    uint32_t op2 = args->shift_input;
    if (args->immediate) // shift immediate value (ARM mode only)
    {
        if (shift_amt)
        {
            op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
            // we've already rotated, so bit 31 holds the carry out
            shifter_carry = (op2 >> 31) & 1;
        }
        else // zero shift -> carry flag unaffected
        {
            shifter_carry = cpu->cpsr & COND_C_BITMASK;
        }
    }
    else // shift register
    {
        // calculate shift
        if (args->shift_by_reg && !shift_amt) // Rs=0x0 -> no shift, C flag unaffected
        {
            shifter_carry = cpu->cpsr & COND_C_BITMASK;
        }
        else switch (args->shift_opcode)
        {
            case 0x0: // logical left
                if (shift_amt > 31) // possible if shifting by register
                {
                    shifter_carry = (shift_amt == 32) ? op2 & 1 : 0;
                    op2 = 0;
                }
                else if (shift_amt)
                {
                    shifter_carry = (op2 >> (32 - shift_amt)) & 1;
                    op2 <<= shift_amt;
                }
                else // LSL #0, Rm used directly w/no shift
                {
                    shifter_carry = cpu->cpsr & COND_C_BITMASK;
                }
                break;

            case 0x1: // logical right
                if (shift_amt > 32)
                {
                    shifter_carry = false;
                    op2 = 0;
                }
                else if (!shift_amt || shift_amt == 32)
                {
                    // shift amount of 0 encodes LSR #32
                    shifter_carry = (op2 >> 31) & 1;
                    op2 = 0;
                }
                else
                {
                    shifter_carry = (op2 >> (shift_amt - 1)) & 1;
                    op2 >>= shift_amt;
                }
                break;

            case 0x2: // arithmetic right
                if (!shift_amt || shift_amt > 31)
                {
                    // shift amount of 0 encodes ASR #32
                    if (op2 & (1 << 31))
                    {
                        shifter_carry = true;
                        op2 = 0xffffffff;
                    }
                    else
                    {
                        shifter_carry = false;
                        op2 = 0;
                    }
                }
                else
                {
                    bool negative = op2 & (1 << 31);
                    shifter_carry = (op2 >> (shift_amt - 1)) & 1;
                    op2 >>= shift_amt;

                    if (negative) // replace sifted-in zeroes with ones
                        op2 |= ~((1 << (31 - shift_amt)) - 1);
                }
                break;

            case 0x3: // rotate right
                if (!shift_amt) // ROR #0 encodes RRX
                {
                    shifter_carry = op2 & 1;
                    op2 = (op2 >> 1) | (((cpu->cpsr >> COND_C_SHIFT) & 1) << 31);
                }
                else
                {
                    // ROR by n >= 32 gives same result as ROR by n-32
                    shift_amt &= 0x1f;

                    // ROR by 0 (mod 32) -> Rm unaffected
                    if (shift_amt)
                        op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));

                    // Two cases: either ROR by 0 (mod 32) or ROR by
                    // nonzero amount (mod 32) and we've already rotated.
                    // In both cases, bit 31 contains the carry out bit.
                    shifter_carry = (op2 >> 31) & 1;
                }
                break;
        }
    }

    *result = op2;
    return shifter_carry;
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
