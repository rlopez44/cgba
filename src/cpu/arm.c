#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "arm7tdmi.h"
#include "cgba/cpu.h"
#include "cgba/memory.h"

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

    barrel_shift_args shift_args = {
        .immediate = immediate,
        .shift_by_reg = shift_by_r,
    };

    if (immediate)
    {
        // shift by twice rotate field
        shift_args.shift_amt = 2 * ((inst >> 8) & 0xf);
        shift_args.shift_input = inst & 0xff;
    }
    else
    {
        // shifting by register -> bottom byte of Rs specifies shift amount
        shift_args.shift_amt = shift_by_r
                               ? read_register(cpu, (inst >> 8) & 0xf) & 0xff
                               : (inst >> 7) & 0x1f;

        // fetching Rs uses up first cycle, so prefetching
        // occurs and Rd/Rm are read on the second cycle
        if (shift_by_r)
            prefetch(cpu);

        shift_args.shift_input = read_register(cpu, inst & 0xf); // Rm
        shift_args.shift_opcode = (inst >> 5) & 0x3;
    }

    uint32_t op2;
    bool shifter_carry = barrel_shift(cpu, &shift_args, &op2);

    // operand1 is read after shifting is performed
    uint32_t op1 = read_register(cpu, rn);

    uint32_t result;
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
    block_transfer_args transfer_args = {
        .preindex          = inst & (1 << 24),
        .add               = inst & (1 << 23),
        .psr_or_force_user = inst & (1 << 22),
        .write_back        = inst & (1 << 21),
        .load              = inst & (1 << 20),
        .register_list     = inst & 0xffff,
        .rn                = (inst >> 16) & 0xf,
    };

    return do_block_transfer(cpu, &transfer_args);
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
    {
        offset = inst & 0x0fff;
    }
    else
    {
        // always shift register by immediate
        barrel_shift_args shift_args = {
            .immediate = false,
            .shift_by_reg = false,
            .shift_opcode = (inst >> 5) & 0x3,
            .shift_amt = (inst >> 7) & 0x1f,
            .shift_input = read_register(cpu, inst & 0xf),
        };
        barrel_shift(cpu, &shift_args, &offset);
    }

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
    {
        barrel_shift_args shift_args = {
            .immediate = true,
            .shift_amt = 2 * ((inst >> 8) & 0xf), // shift by twice rotate field
            .shift_input = inst & 0xff,
        };
        barrel_shift(cpu, &shift_args, &new_psr);
    }
    else
    {
        new_psr = read_register(cpu, inst & 0xf);
    }

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

        // Because we store in 64-bit var, we need to get
        // rid of upper 32 bits when doing 32-bit mul.
        if (!mul_long)
            result &= UINT32_MAX;

        if (!result)
            cpu->cpsr |= COND_Z_BITMASK;
        else if ((result >> shift_amt) & 1)
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
