#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "arm7tdmi.h"
#include "cgba/bios.h"
#include "cgba/cpu.h"
#include "cgba/memory.h"

static int count_set_bits(uint32_t n)
{
    // Source: https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan
    int nset;
    for (nset = 0; n; ++nset)
        n &= n - 1; // clear the least significant bit set

    return nset;
}

static void restore_cpsr(arm7tdmi *cpu)
{
    arm_bankmode mode = get_current_bankmode(cpu);
    if (mode != BANK_NONE)
        cpu->cpsr = cpu->spsr[mode];
}

static void undefined_instruction_trap(arm7tdmi *cpu)
{
    fprintf(stderr,
            "Error: ARM undefined instruction trap encountered %08X at address %08X\n",
            cpu->pipeline[0],
            cpu->registers[R15] - 8);
    exit(1);
}

static int bx(arm7tdmi *cpu, uint32_t inst)
{
    int rn = inst & 0xf;
    uint32_t addr = read_register(cpu, rn);

    do_branch_and_exchange(cpu, addr);

    // 2S + 1N cycles
    return 3;
}

// perform barrel shifter operation and return the shifter's carry out
static bool barrel_shift(arm7tdmi *cpu, uint32_t inst, uint32_t *result, bool immediate)
{
    bool shifter_carry = false;
    uint8_t shift_amt;
    uint32_t op2;
    if (immediate) // shift immediate value
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
        bool shift_by_r = inst & (1 << 4); // shift by amount specified in a register

        // shifting by register -> bottom byte of Rs specifies shift amount
        shift_amt = shift_by_r
                    ? read_register(cpu, (inst >> 8) & 0xf) & 0xff
                    : (inst >> 7) & 0x1f;

        // fetching Rs uses up first cycle, so prefetching
        // occurs and Rd/Rm are read on the second cycle
        if (shift_by_r)
            prefetch(cpu);

        // calculate shift
        op2 = read_register(cpu, inst & 0xf); // Rm

        if (shift_by_r && !shift_amt) // Rs=0x0 -> no shift, C flag unaffected
        {
            shifter_carry = cpu->cpsr & COND_C_BITMASK;
        }
        else switch ((inst >> 5) & 0x3)
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

static int branch(arm7tdmi *cpu, uint32_t inst)
{
    // instruction contains signed 2's complement 24-bit offset
    uint32_t offset = inst & 0x00ffffff;

    if (offset & (1 << 23))
        offset |= 0xff000000;

    offset <<= 2;

    if (inst & (1 << 24)) // branch w/link
    {
        // point to instruction following the branch, R14[1:0] always cleared
        uint32_t old_pc = (cpu->registers[R15] - 4) & ~0x3;
        write_register(cpu, R14, old_pc);
    }

    cpu->registers[R15] += offset;
    reload_pipeline(cpu);

    // 2S + 1N cycles
    return 3;
}

static int process_data(arm7tdmi *cpu, uint32_t inst)
{
    int num_clocks;
    bool set_conds = inst & (1 << 20);
    uint8_t opcode = (inst >> 21) & 0xf;

    int rn = (inst >> 16) & 0xf;
    int rd = (inst >> 12) & 0xf;

    bool logical_op = opcode < 0x2
                      || opcode == 0x8
                      || opcode == 0x9
                      || opcode >= 0xc;

    bool immediate = inst & (1 << 25);

    // register specified shift
    bool shift_by_r = !immediate && (inst & (1 << 4));

    bool write_result = opcode <= 0x7 || opcode >= 0xc;
    bool carry_flag = cpu->cpsr & COND_C_BITMASK;

    uint32_t result, op2;
    bool shifter_carry = barrel_shift(cpu, inst, &op2, immediate);

    // operand1 is read after shifting is performed
    uint32_t op1 = read_register(cpu, rn);

    bool op_carry = false, op_overflow = false; // only used by arithmetic operations
    switch (opcode)
    {
        case 0x0: // AND
        case 0x8: // TST
            result = op1 & op2;
            break;

        case 0x1: // EOR
        case 0x9: // TEQ
            result = op1 ^ op2;
            break;

        case 0x2: // SUB
        case 0xa: // CMP
            result = op1 - op2;
            op_carry = op1 >= op2; // set if no borrow
            // overflow into bit 31
            op_overflow = (((op1 ^ op2) & (op1 ^ result)) >> 31) & 1;
            break;

        case 0x3: // RSB
            result = op2 - op1;
            op_carry = op2 >= op1;
            op_overflow = (((op2 ^ op1) & (op2 ^ result)) >> 31) & 1;
            break;

        case 0x4: // ADD
        case 0xb: // CMN
            result = op1 + op2;
            op_carry = op2 > UINT32_MAX - op1;
            op_overflow = ((~(op1 ^ op2) & (op1 ^ result)) >> 31) & 1;
            break;

        case 0x5: // ADC
            result = op1 + op2 + carry_flag;
            // if op1 + op2 doesn't overflow, then check if
            // adding the carry flag causes an overflow
            op_carry = op2 > UINT32_MAX - op1
                       || op1 + op2 > UINT32_MAX - carry_flag;
            op_overflow = ((~(op1 ^ op2) & (op1 ^ result)) >> 31) & 1;
            break;

        case 0x6: // SBC
            result = op1 - op2 + carry_flag - 1;
            // if op1 - op2 doesn't borrow, then check if
            // subtracting 1 - carry_flag also doesn't borrow
            op_carry = op1 >= op2 && op1 - op2 >= 1u - carry_flag;
            op_overflow = (((op1 ^ op2) & (op1 ^ result)) >> 31) & 1;
            break;

        case 0x7: // RSC
            result = op2 - op1 + carry_flag - 1;
            op_carry = op2 >= op1 && op2 - op1 >= 1u - carry_flag;
            op_overflow = (((op2 ^ op1) & (op2 ^ result)) >> 31) & 1;
            break;

        case 0xc: // ORR
            result = op1 | op2;
            break;

        case 0xd: // MOV
            result = op2;
            break;

        case 0xe: // BIC
            result = op1 & ~op2;
            break;

        case 0xf: // MVN
            result = ~op2;
            break;
    }

    // if we didn't shift register by register, then we're still in
    // the first cycle at this point and prefetch has yet to occur
    if (!shift_by_r)
        prefetch(cpu);

    // set flags if needed
    if (set_conds)
    {
        // flag order:  N  Z  C  V
        //        bit: 31 30 29 28
        if (logical_op)
            cpu->cpsr = (cpu->cpsr & 0x1fffffff)
                        | (result & (1 << 31))
                        | (!result << COND_Z_SHIFT)
                        | (shifter_carry << COND_C_SHIFT);
        else
            cpu->cpsr = (cpu->cpsr & 0x0fffffff)
                        | (result & (1 << 31))
                        | (!result << COND_Z_SHIFT)
                        | (op_carry << COND_C_SHIFT)
                        | (op_overflow << COND_V_SHIFT);
    }

    if (write_result)
        write_register(cpu, rd, result);

    // mode change and possible pipeline flush
    if (rd == R15)
    {
        // always true for TST, TEQ, CMP, and CMN
        if (set_conds)
            restore_cpsr(cpu);

        // TST, TEQ, CMP, and CMN don't flush the pipeline
        if (write_result)
            reload_pipeline(cpu);
    }

    if (rd == R15 && write_result && shift_by_r)
        num_clocks = 4; // 2S + 1N + 1I
    else if (rd == R15 && write_result)
        num_clocks = 3; // 2S + 1N
    else if (shift_by_r)
        num_clocks = 2; // 1S + 1I
    else
        num_clocks = 1; // 1S

    return num_clocks;
}

static int block_data_transfer(arm7tdmi *cpu, uint32_t inst)
{
    int num_clocks;
    bool preindex   = inst & (1 << 24);
    bool add        = inst & (1 << 23);
    bool sbit       = inst & (1 << 22);
    bool load       = inst & (1 << 20);

    int register_list = inst & 0xffff;

    bool effective_preincrement = (preindex && add) || (!preindex && !add);

    int rn = (inst >> 16) & 0xf;
    uint32_t base = read_register(cpu, rn);

    prefetch(cpu);

    uint32_t curr_addr = base;

    bool pc_trans;
    int num_transfers;

    if (register_list)
    {
        pc_trans = inst & (1 << 15);

        bool write_back = inst & (1 << 21);
        bool mode_change = sbit && pc_trans && load;
        bool user_bank_trans = sbit && !mode_change;

        bool base_in_rlist = register_list & (1 << rn);

        // we need to count the number of registers to be transferred because
        // LDM/STM start at the lowest address of the block and fill upward
        num_transfers = count_set_bits(register_list);

        uint32_t modified_base = base;
        if (add)
            modified_base += 4*num_transfers;
        else
            modified_base -= 4*num_transfers;

        if (!add)
            curr_addr -= 4*num_transfers;

        for (int i = 0; i < ARM_NUM_REGISTERS; ++i)
        {
            // bit i not set -> no transfer for register Ri
            // except for the empty register list edge case
            if (!(inst & (1 << i)))
                continue;

            if (effective_preincrement)
                curr_addr += 4;

            uint32_t transfer_data;
            if (load)
            {
                transfer_data = read_word(cpu->mem, curr_addr);
                if (user_bank_trans)
                    cpu->registers[i] = transfer_data;
                else
                    write_register(cpu, i, transfer_data);
            }
            else
            {
                // STM with base in register list:
                // first register in list -> original base stored
                // second or later in list -> modified base stored
                int mask = (1 << rn) - 1;
                bool first_in_rlist = !(register_list & mask);

                if (i == rn && first_in_rlist)
                    transfer_data = base;
                else if (i == rn)
                    transfer_data = modified_base;
                else if (user_bank_trans)
                    transfer_data = cpu->registers[i];
                else
                    transfer_data = read_register(cpu, i);

                write_word(cpu->mem, curr_addr, transfer_data);
            }

            if (!effective_preincrement)
                curr_addr += 4;
        }

        if (mode_change)
        {
            arm_bankmode mode = get_current_bankmode(cpu);
            if (mode == BANK_NONE)
            {
                fprintf(stderr, "Error: attempted LDM mode change in user mode\n");
                exit(1);
            }

            cpu->cpsr = cpu->spsr[mode];
        }

        if ((pc_trans || !num_transfers) && load)
            reload_pipeline(cpu);

        // LDM: write-back value is overwritten by transfer
        // when the base is included in the register list
        if (write_back && !(load && base_in_rlist))
            write_register(cpu, rn, modified_base);
    }
    else
    {
        // edge case: empty register list transfers R15 and writes
        // back to Rn with offset +/-0x40 for increment/decrement
        pc_trans = true;
        num_transfers = 1;

        if (!add)
            curr_addr -= 0x40;

        if (effective_preincrement)
            curr_addr += 4;

        if (load)
        {
            cpu->registers[R15] = read_word(cpu->mem, curr_addr);
            reload_pipeline(cpu);
        }
        else
        {
            write_word(cpu->mem, curr_addr, cpu->registers[R15]);
        }

        if (add)
            write_register(cpu, rn, base + 0x40);
        else
            write_register(cpu, rn, base - 0x40);
    }

    if (load && pc_trans)
        num_clocks = (num_transfers + 1) + 2 + 1; // (n+1)S + 2N + 1I
    else if (load)
        num_clocks = num_transfers + 1 + 1;       // nS + 1N + 1I
    else
        num_clocks = (num_transfers - 1) + 2;     // (n - 1)S + 2N

    return num_clocks;
}

static int single_data_transfer(arm7tdmi *cpu, uint32_t inst)
{
    int num_clocks;
    bool immediate = !(inst & (1 << 25));
    bool preindex = inst & (1 << 24);
    bool add_offset = inst & (1 << 23);
    bool byte_trans = inst & (1 << 22);
    bool write_back = inst & (1 << 21);
    bool load = inst & (1 << 20);

    int rn = (inst >> 16) & 0xf;
    int rd = (inst >> 12) & 0xf;

    uint32_t offset;
    if (immediate)
        offset = inst & 0x0fff;
    else
        barrel_shift(cpu, inst, &offset, false); // always register by immediate

    uint32_t transfer_addr = read_register(cpu, rn);

    if (preindex)
    {
        if (add_offset)
            transfer_addr += offset;
        else
            transfer_addr -= offset;
    }

    prefetch(cpu); // prefetch occurs before the load/store

    if (load)
    {
        if (byte_trans)
        {
            write_register(cpu, rd, read_byte(cpu->mem, transfer_addr));
        }
        else
        {
            // if address is not word-aligned, we need to rotate the
            // word-aligned data so the addressed byte is in bits 0-7 of Rd
            int boundary_offset = transfer_addr & 0x3;
            int rot_amt = 8 * boundary_offset;
            uint32_t word = read_word(cpu->mem, transfer_addr);

            uint32_t writeval;
            if (rot_amt)
                writeval = word >> rot_amt | word << (32 - rot_amt);
            else
                writeval = word;
            write_register(cpu, rd, writeval);
        }

        if (rd == R15)
            reload_pipeline(cpu);

        // R15: 2S + 2N + 1I, else 1S + 1N + 1I
        num_clocks = rd == R15 ? 5 : 3;
    }
    else
    {
        uint32_t writeval = read_register(cpu, rd);
        if (byte_trans)
            write_byte(cpu->mem, transfer_addr, writeval);
        else
            write_word(cpu->mem, transfer_addr, writeval);

        num_clocks = 2; // 2N cycles
    }

    // Note: LDR never writes back if the base
    // and destination registers are the same.
    if ((!load || rd != rn) && (write_back || !preindex))
    {
        uint32_t writeval = read_register(cpu, rn);
        if (add_offset)
            writeval += offset;
        else
            writeval -= offset;
        write_register(cpu, rn, writeval);
    }

    return num_clocks;
}

static int halfword_transfer(arm7tdmi *cpu, uint32_t inst, bool immediate)
{
    int num_clocks;
    bool preindex = inst & (1 << 24);
    bool add_offset = inst & (1 << 23);
    bool write_back = inst & (1 << 21);
    bool load = inst & (1 << 20);
    bool signed_ = inst & (1 << 6);
    bool halfword = inst & (1 << 5);

    int rn = (inst >> 16) & 0xf;
    int rd = (inst >> 12) & 0xf;

    uint32_t offset;
    if (immediate) // 8-bit immediate
        offset = ((inst >> 4) & 0xf0) | (inst & 0xf);
    else
        offset = read_register(cpu, inst & 0xf);

    uint32_t transfer_addr = read_register(cpu, rn);

    if (preindex)
    {
        if (add_offset)
            transfer_addr += offset;
        else
            transfer_addr -= offset;
    }

    prefetch(cpu); // prefetch occurs before the load/store

    if (load) // LDRH/LDRSH/LDRSB
    {
        uint32_t data;
        if (halfword)
        {
            data = read_halfword(cpu->mem, transfer_addr);

            // If the address is not halfword-aligned...
            // LDRH:  aligned data is rotated so the addressed
            //        byte is in bits 0-7 of Rd
            // LDRSH: the addressed byte is sign-extended
            bool unaligned = transfer_addr & 1;
            if (unaligned && signed_)
            {
                data = (data >> 8) & 0xff;
                if (data & 0x80)
                    data |= 0xffffff00;
            }
            else if (signed_)
            {
                if (data & 0x8000)
                    data |= 0xffff0000;
            }
            else if (unaligned)
            {
                data = data >> 8 | data << 24;
            }
        }
        else
        {
            data = read_byte(cpu->mem, transfer_addr);
            if (data & 0x80)
                data |= 0xffffff00;
        }

        write_register(cpu, rd, data);

        if (rd == R15)
            reload_pipeline(cpu);

        // R15: 2S + 2N + 1I, otherwise: 1S + 1N + 1I
        num_clocks = rd == R15 ? 5 : 3;
    }
    else // store: only one instruction: STRH (S=0, H=1)
    {
        num_clocks = 2; // 2N cycles
        write_halfword(cpu->mem, transfer_addr, read_register(cpu, rd));
    }

    // Note: LDRH/LDRSH never writes back if the base
    // and destination registers are the same.
    if ((!load || rd != rn) && (write_back || !preindex))
    {
        uint32_t writeval = read_register(cpu, rn);
        if (add_offset)
            writeval += offset;
        else
            writeval -= offset;
        write_register(cpu, rn, writeval);
    }

    return num_clocks;
}

// transfer data from a PSR to a register
static int mrs_transfer(arm7tdmi *cpu)
{
    uint32_t inst = cpu->pipeline[0];
    bool from_spsr = inst & (1 << 22);
    int rd = (inst >> 12) & 0xf;
    arm_bankmode bank_mode = get_current_bankmode(cpu);
    uint32_t src_psr = from_spsr ? cpu->spsr[bank_mode] : cpu->cpsr;

    if (bank_mode == BANK_NONE && from_spsr)
    {
        fprintf(stderr, "Error: PSR transfer from SPSR attempted in user mode\n");
        exit(1);
    }

    write_register(cpu, rd, src_psr);
    prefetch(cpu);

    // 1S cycles
    return 1;
}

// transfer data from a register or immediate value to a PSR
static int msr_transfer(arm7tdmi *cpu, uint32_t inst)
{
    bool to_spsr = inst & (1 << 22);
    bool set_cntrl_bits = inst & (1 << 16);
    bool set_flag_bits = inst & (1 << 19);
    bool immediate = inst & (1 << 25);
    arm_cpu_mode cpu_mode = cpu->cpsr & CPU_MODE_MASK;
    arm_bankmode bank_mode = get_current_bankmode(cpu);

    uint32_t *dest_psr = to_spsr ? cpu->spsr + bank_mode : &cpu->cpsr;

    if (to_spsr && (cpu_mode == MODE_USR || cpu_mode == MODE_SYS))
    {
        fprintf(stderr, "Error: PSR transfer to SPSR attempted in user mode\n");
        exit(1);
    }

    uint32_t new_psr;
    if (immediate)
        barrel_shift(cpu, inst, &new_psr, true);
    else
        new_psr = read_register(cpu, inst & 0xf);

    uint32_t write_mask = 0;
    // control bits are protected in unprivileged user mode
    if (set_cntrl_bits && cpu_mode != MODE_USR)
        write_mask |= CNTRL_BITS_MASK;

    if (set_flag_bits)
        write_mask |= COND_FLAGS_MASK;

    *dest_psr = (*dest_psr & ~write_mask) | (new_psr & write_mask);

    prefetch(cpu);

    // 1S cycles
    return 1;
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
static int get_multiply_array_cycles(uint32_t rs, bool mul_long, bool signed_)
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

static int multiply(arm7tdmi *cpu, uint32_t inst)
{
    bool mul_long   = (inst >> 23) & 1;
    bool signed_    = (inst >> 22) & 1; // always zero for MUL/MLA
    bool accumulate = (inst >> 21) & 1;
    bool set_conds  = (inst >> 20) & 1;
    int rd = (inst >> 16) & 0xf;
    int rn = (inst >> 12) & 0xf;
    int rs = (inst >> 8) & 0xf;
    int rm = inst & 0xf;

    prefetch(cpu);

    // store as 64-bit so we can do both
    // multiply and multiply long instructions
    uint64_t rs_value = read_register(cpu, rs);
    uint64_t rm_value = read_register(cpu, rm);

    if (signed_)
    {
        uint64_t extension = (uint64_t)UINT32_MAX << 32;
        if ((rs_value >> 31) & 1)
            rs_value |= extension;

        if ((rm_value >> 31) & 1)
            rm_value |= extension;
    }

    uint64_t result = rm_value * rs_value;

    if (accumulate && mul_long)
    {
        uint64_t rdhi_value = read_register(cpu, rd);
        uint64_t rdlo_value = read_register(cpu, rn);
        result += (rdhi_value << 32) | rdlo_value;
    }
    else if (accumulate)
    {
        result += read_register(cpu, rn);
    }

    if (mul_long)
    {
        write_register(cpu, rd, result >> 32);
        write_register(cpu, rn, result);
    }
    else
    {
        write_register(cpu, rd, result);
    }

    if (set_conds)
    {
        int shift_amt = mul_long ? 63 : 31;
        cpu->cpsr = cpu->cpsr & ~(COND_N_BITMASK | COND_Z_BITMASK);

        if (!result)
            cpu->cpsr |= COND_Z_BITMASK;
        else if (result >> shift_amt)
            cpu->cpsr |= COND_N_BITMASK;
    }

    int array_cycles = get_multiply_array_cycles(rs_value, mul_long, signed_);

    //  MUL: 1S + (array_cycles)I,      MLA: 1S + (array_cycles + 1)I
    // MULL: 1S + (array_cycles + 1)I, MLAL: 1S + (array_cycles + 2)I
    return 1 + array_cycles + accumulate + 1*mul_long;
}

static int single_data_swap(arm7tdmi *cpu)
{
    uint32_t inst = cpu->pipeline[0];
    bool byte = inst & (1 << 22);
    int rn = (inst >> 16) & 0xf;
    int rd = (inst >> 12) & 0xf;
    int rm = inst & 0xf;

    uint32_t addr = read_register(cpu, rn);
    uint32_t reg_value = read_register(cpu, rm);

    prefetch(cpu);

    uint32_t mem_value;
    if (byte)
    {
        mem_value = read_byte(cpu->mem, addr);
        write_byte(cpu->mem, addr, reg_value);
    }
    else
    {
        mem_value = read_word(cpu->mem, addr);

        // unaligned read -> addressed byte into bits 0-7
        int rot_amt = 8 * (addr & 0x3);
        if (rot_amt)
            mem_value = mem_value >> rot_amt | mem_value << (32 - rot_amt);

        write_word(cpu->mem, addr, reg_value);
    }

    write_register(cpu, rd, mem_value);

    // 1S + 2N + 1I
    return 4;
}

/*
 * Enter the software interrupt trap by doing the following:
 * - save address to instruction following SWI into R14_svc
 * - save the CPSR to SPSR_svc
 * - disable interrupts
 * - enter ARM state
 * - enter supervisor mode
 * - jump to SWI vector
 */
static int software_interrupt(arm7tdmi *cpu)
{
    // 2S + 1N
    int num_clocks = 3;
    cpu->banked_registers[BANK_SVC][BANK_R14] = cpu->registers[R15] - 4;
    cpu->spsr[BANK_SVC] = cpu->cpsr;
    cpu->cpsr = (cpu->cpsr & ~CNTRL_BITS_MASK) | IRQ_DISABLE | FIQ_DISABLE | MODE_SVC;

    cpu->registers[R15] = 0x08;
    reload_pipeline(cpu);

    // NOTE: since I don't support BIOS files yet I emulate syscalls in C
    num_clocks += gba_syscall(cpu);

    return num_clocks;
}

int decode_and_execute_arm(arm7tdmi *cpu)
{
    // all instructions can be conditionally executed
    if (!check_cond(cpu))
    {
        prefetch(cpu);
        return 1; // 1S
    }

    // decoding is going to involve a lot of magic numbers
    // See references in `README.md` for encoding documentation
    int num_clocks = 0;
    uint32_t inst = cpu->pipeline[0];
    if ((inst & 0x0ffffff0) == 0x012fff10)      // branch and exchange
        num_clocks = bx(cpu, inst);
    else if ((inst & 0x0e000000) == 0x08000000) // block data transfer
        num_clocks = block_data_transfer(cpu, inst);
    else if ((inst & 0x0e000000) == 0x0a000000) // branch and branch with link
        num_clocks = branch(cpu, inst);
    else if ((inst & 0x0f000000) == 0x0f000000) // software interrupt
        num_clocks = software_interrupt(cpu);
    else if ((inst & 0x0e000010) == 0x06000010) // undefined
        undefined_instruction_trap(cpu);
    else if ((inst & 0x0c000000) == 0x04000000) // single data transfer
        num_clocks = single_data_transfer(cpu, inst);
    else if ((inst & 0x0f800ff0) == 0x01000090) // single data swap
        num_clocks = single_data_swap(cpu);
    else if ((inst & 0x0f0000f0) == 0x00000090) // multiply and multiply long
        num_clocks = multiply(cpu, inst);
    else if ((inst & 0x0e400f90) == 0x00000090) // halfword data transfer register
        num_clocks = halfword_transfer(cpu, inst, false);
    else if ((inst & 0x0e400090) == 0x00400090) // halfword data transfer immediate
        num_clocks = halfword_transfer(cpu, inst, true);
    else if ((inst & 0x0fbf0000) == 0x010f0000) // PSR transfer MRS
        num_clocks = mrs_transfer(cpu);
    else if ((inst & 0x0db0f000) == 0x0120f000) // PSR transfer MSR
        num_clocks = msr_transfer(cpu, inst);
    else if ((inst & 0x0c000000) == 0x00000000) // data processing
        num_clocks = process_data(cpu, inst);
    else
        panic_illegal_instruction(cpu);

    return num_clocks;
}
