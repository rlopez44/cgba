#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "arm7tdmi.h"
#include "cgba/cpu.h"
#include "cgba/memory.h"

#define COND_N_SHIFT 31
#define COND_Z_SHIFT 30
#define COND_C_SHIFT 29
#define COND_V_SHIFT 28

#define COND_N_BITMASK (1 << COND_N_SHIFT)
#define COND_Z_BITMASK (1 << COND_Z_SHIFT)
#define COND_C_BITMASK (1 << COND_C_SHIFT)
#define COND_V_BITMASK (1 << COND_V_SHIFT)

// decode ARM condition field
static bool check_cond(arm7tdmi *cpu)
{
    bool n_set = cpu->cpsr & COND_N_BITMASK;
    bool z_set = cpu->cpsr & COND_Z_BITMASK;
    bool c_set = cpu->cpsr & COND_C_BITMASK;
    bool v_set = cpu->cpsr & COND_V_BITMASK;

    bool result;
    switch ((cpu->pipeline[0] >> 28) & 0xf)
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

// prefetch a new instruction for the instruction pipeline
static void prefetch(arm7tdmi *cpu)
{
    cpu->pipeline[0] = cpu->pipeline[1];
    cpu->pipeline[1] = read_word(cpu->mem, cpu->registers[R15]);
    cpu->registers[R15] += 4;
}

static void restore_cpsr(arm7tdmi *cpu)
{
    arm_bankmode mode;
    switch (cpu->cpsr & 0x1f)
    {
        case 0x10: // user mode, should not get here
        case 0x1f: // system mode (= privileged user mode)
            mode = BANK_NONE;
            break;

        case 0x11: mode = BANK_FIQ; break;
        case 0x12: mode = BANK_IRQ; break;
        case 0x13: mode = BANK_SVC; break;
        case 0x17: mode = BANK_ABT; break;
        case 0x1b: mode = BANK_UND; break;

        default: // illegal mode, should not get here
            fprintf(stderr,
                    "Error: Illegal CPU mode encountered: %02x\n",
                    cpu->cpsr & 0x1f);
            exit(1);
    }

    if (mode != BANK_NONE)
        cpu->cpsr = cpu->spsr[mode];
}

static void bx(arm7tdmi *cpu)
{
    if (!check_cond(cpu))
        return;

    arm_register rn = cpu->pipeline[0] & 0xf;
    uint32_t addr = cpu->registers[rn];

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

// perform barrel shifter operation and return the shifter's carry out
static bool barrel_shift(arm7tdmi *cpu, uint32_t inst, uint32_t *result)
{
    bool shifter_carry = false;
    bool imm = inst & (1 << 25);
    bool shift_by_r = inst & (1 << 4); // shift by amount specified in a register

    uint8_t shift_amt;
    uint32_t op2;
    if (imm) // shift immediate value
    {
        op2 = inst & 0xff;
        shift_amt = 2 * ((inst >> 8) & 0xf); // shift by twice rotate field
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
        // shifting by register -> bottom byte of Rs specifies shift amount
        shift_amt = shift_by_r
                    ? cpu->registers[(inst >> 8) & 0xf] & 0xff
                    : (inst >> 7) & 0x1f;

        // fetching Rs uses up first cycle, so prefetching
        // occurs and Rd/Rm are read on the second cycle
        if (shift_by_r)
            prefetch(cpu);

        // calculate shift
        op2 = cpu->registers[inst & 0xf]; // Rm

        if (shift_by_r && !shift_amt) // Rs=0x0 -> no shift, C flag unaffected
        {
            shifter_carry = cpu->cpsr & COND_C_BITMASK;
        }
        else switch ((inst >> 5) & 0x2)
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

static void process_data(arm7tdmi *cpu)
{
    if (!check_cond(cpu))
        return;

    uint32_t inst = cpu->pipeline[0];
    bool set_conds = inst & (1 << 20);
    uint8_t opcode = (inst >> 21) & 0xf;

    arm_register rn = (inst >> 16) & 0xf;
    arm_register rd = (inst >> 12) & 0xf;

    bool logical_op = opcode < 0x2
                      || opcode == 0x8
                      || opcode == 0x9
                      || opcode >= 0xc;

    uint32_t result, op2;
    bool shifter_carry = barrel_shift(cpu, inst, &op2);

    switch (opcode)
    {
        case 0xd: // MOV
            result = op2;
            break;

        default: // temporary, unimplemented opcodes
            goto unimplemented;
    }

    // if we didn't shift by register, then we're still in the
    // first cycle at this point and prefetch has yet to occur
    if (!(inst & (1 << 4)))
        prefetch(cpu);

    // set flags if needed
    if (set_conds)
    {
        if (logical_op)
        {
            // flag order:  N  Z  C  V
            //        bit: 31 30 29 28
            cpu->cpsr = (cpu->cpsr & 0x1fffffff)
                        | (result & (1 << 31))
                        | (!result << 30)
                        | (shifter_carry << 29);
        }
        else
        {
            //TODO: arithmetic operations
        }
    }

    // apply result
    if (opcode <= 0x7 || opcode >= 0xc) // opcodes that write result
    {
        cpu->registers[rd] = result;

        if (rd == R15) // changing R15 -> pipeline flush
        {
            if (set_conds) // mode change
                restore_cpsr(cpu);

            reload_pipeline(cpu);
        }
    }
    else
    {
        // TODO: TST, TEQ, CMP, and CMN
    }

    return;

unimplemented:
    fprintf(stderr, "Unimplemented data processing instruction. Opcode: %X\n", opcode);
    exit(1);
}

static inline void panic_illegal_instruction(uint32_t inst)
{
    fprintf(stderr, "Error: Illegal instruction encountered: %08X\n", inst);
    exit(1);
}

void decode_and_execute_arm(arm7tdmi *cpu)
{
    // decoding is going to involve a lot of magic numbers
    // See references in `README.md` for encoding documentation
    uint32_t inst = cpu->pipeline[0];
    if ((inst & 0x0ffffff0) == 0x012fff10)      // branch and exchange
        bx(cpu);
    else if ((inst & 0x0e000000) == 0x08000000) // block data transfer
        goto unimplemented;
    else if ((inst & 0x0e000000) == 0x0a000000) // branch and branch with link
        goto unimplemented;
    else if ((inst & 0x0f000000) == 0x0f000000) // software interrupt
        goto unimplemented;
    else if ((inst & 0x0e000010) == 0x06000010) // undefined
        goto unimplemented;
    else if ((inst & 0x0c000000) == 0x04000000) // single data transfer
        goto unimplemented;
    else if ((inst & 0x0f800ff0) == 0x01000090) // single data swap
        goto unimplemented;
    else if ((inst & 0x0f0000f0) == 0x00000090) // multiply and multiply long
        goto unimplemented;
    else if ((inst & 0x0e400f90) == 0x00000090) // halfword data transfer register
        goto unimplemented;
    else if ((inst & 0x0e400090) == 0x00400090) // halfword data transfer immediate
        goto unimplemented;
    else if ((inst & 0x0fbf0000) == 0x010f0000) // PSR transfer MRS
        goto unimplemented;
    else if ((inst & 0x0db0f000) == 0x0120f000) // PSR transfer MSR
        goto unimplemented;
    else if ((inst & 0x0c000000) == 0x00000000) // data processing
        process_data(cpu);
    else
        panic_illegal_instruction(inst);

    return;

// temporary until all instructions are implemented
unimplemented:
    fprintf(stderr, "Error: Unimplemented instruction encountered: %08X\n", inst);
    exit(1);
}
