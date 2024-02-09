#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "arm7tdmi.h"
#include "cgba/cpu.h"
#include "cgba/memory.h"

static int do_branch(arm7tdmi *cpu, uint32_t offset)
{
    cpu->registers[R15] += offset;
    reload_pipeline(cpu);

    // 2S + 1N cycles
    return 3;
}

static int unconditional_branch(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    // sign-extended 12-bit offset
    uint32_t offset = inst & 0x7ff;
    if (offset & (1 << 10))
        offset |= ~0x7ffu;

    offset <<= 1;

    return do_branch(cpu, offset);
}

static int conditional_branch(arm7tdmi *cpu)
{
    if (!check_cond(cpu))
    {
        prefetch(cpu);
        return 1; // 1S
    }

    uint16_t inst = cpu->pipeline[0];
    // sign-extended 9-bit offset
    uint32_t offset = inst & 0xff;
    if (offset & 0x80)
        offset |= 0xffffff00;

    offset <<= 1;

    return do_branch(cpu, offset);
}

static int long_branch_with_link(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    bool offset_low = (inst >> 11) & 1;

    uint32_t offset = inst & 0x7ff;
    uint32_t tmp;
    if (offset_low) // second instruction
    {
        offset <<= 1;
        tmp = read_register(cpu, R14) + offset;
        write_register(cpu, R14, (cpu->registers[R15] - 2) | 1);
        cpu->registers[R15] = tmp;
        reload_pipeline(cpu);
    }
    else // first instruction
    {
        if (offset & (1 << 10))
            offset |= ~0x7ffu;
        offset <<= 12;
        tmp = cpu->registers[R15] + offset;
        write_register(cpu, R14, tmp);
        prefetch(cpu);
    }

    // first instruction: 1S, second instruction: 2S + 1N
    return offset_low ? 3 : 1;
}

static int add_offset_to_sp(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    bool negative = (inst >> 7) & 1;
    uint32_t offset = (inst & 0x7f) << 2;

    uint32_t sp = read_register(cpu, R13);
    if (negative)
        sp -= offset;
    else
        sp += offset;

    write_register(cpu, R13, sp);
    prefetch(cpu);

    return 1; // 1S
}

static int operate_with_immediate(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    int operation = (inst >> 11) & 0x3;
    int rd = (inst >> 8) & 0x7;
    uint8_t offset = inst & 0xff;

    bool write_result = operation != 0x1; // MOV, ADD, or SUB
    bool logical_op   = !operation;       // MOV

    prefetch(cpu);

    bool carry = false;
    bool overflow = false;
    uint32_t result;
    uint32_t op1 = read_register(cpu, rd);
    switch (operation)
    {
        case 0x0: // MOV
            result = offset;
            break;

        case 0x1: // CMP
        case 0x3: // SUB
            result = op1 - offset;
            carry = op1 >= offset; // set if no borrow
            // overflow into bit 31
            overflow = (((op1 ^ offset) & (op1 ^ result)) >> 31) & 1;
            break;

        case 0x2: // ADD
            result = op1 + offset;
            carry = offset > UINT32_MAX - op1;
            overflow = ((~(op1 ^ offset) & (op1 ^ result)) >> 31) & 1;
            break;
    }

    if (write_result)
        write_register(cpu, rd, result);

    uint32_t mask;
    if (logical_op)
    {
        mask = ~(COND_N_BITMASK | COND_Z_BITMASK);
        cpu->cpsr = (cpu->cpsr & mask)
                    | (result & COND_N_BITMASK)
                    | (!result << COND_Z_SHIFT);
    }
    else
    {
        mask = ~COND_FLAGS_MASK;
        cpu->cpsr = (cpu->cpsr & mask)
                    | (result & COND_N_BITMASK)
                    | (!result << COND_Z_SHIFT)
                    | (carry << COND_C_SHIFT)
                    | (overflow << COND_V_SHIFT);
    }

    return 1; // 1S cycles
}

static int hi_register_op_or_bx(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    int op = (inst >> 8) & 0x3;
    bool h1 = (inst >> 7) & 1;
    bool h2 = (inst >> 6) & 1;

    // the H flags determine whether we use
    // a low (0-7) or hi (8-15) register
    int rd = (h1*0x8) | (inst & 0x7);
    int rs = (h2*0x8) | ((inst >> 3) & 0x7);

    bool write_result = op == 0x0 || op == 0x2; // ADD, MOV
    bool branch = op == 0x3; // BX

    uint32_t op1 = read_register(cpu, rd);
    uint32_t op2 = read_register(cpu, rs);

    uint32_t result;
    switch (op)
    {
        case 0x0: // ADD
            result = op1 + op2;
            break;

        case 0x1: // CMP
        {
            result = op1 - op2;
            bool carry = op1 >= op2; // set if no borrow
            // overflow into bit 31
            bool overflow = (((op1 ^ op2) & (op1 ^ result)) >> 31) & 1;
            cpu->cpsr = (cpu->cpsr & ~COND_FLAGS_MASK)
                        | (result & COND_N_BITMASK)
                        | (!result << COND_Z_SHIFT)
                        | (carry << COND_C_SHIFT)
                        | (overflow << COND_V_SHIFT);
            break;
        }

        case 0x2: // MOV
            result = op2;
            break;

        case 0x3: // BX
            do_branch_and_exchange(cpu, read_register(cpu, rs));
            break;
    }

    if (write_result)
    {
        // R15 must be halfword-aligned
        if (rd == R15)
            result &= ~1u;

        write_register(cpu, rd, result);
    }

    // branching has it's own pipeline reload logic
    if (!branch)
    {
        // CMP doesn't flush the pipeline
        if (rd == R15 && write_result)
            reload_pipeline(cpu);
        else
            prefetch(cpu);
    }

    int num_clocks;
    if ((rd == R15 && write_result) || branch)
        num_clocks = 3; // 2S + 1N
    else
        num_clocks = 1; // 1S

    return num_clocks;
}

static int push_pop_registers(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    bool link = inst & (1 << 8);
    bool load = inst & (1 << 11);
    int register_list = inst & 0xff;

    if (link && load) // POP PC
        register_list |= (1 << R15);
    else if (link)    // PUSH LR
        register_list |= (1 << R14);

    // instructions are one of STMDB R13! and LDMIA R13!
    bool preindex = !load;
    bool add = load;

    block_transfer_args transfer_args = {
        .preindex          = preindex,
        .add               = add,
        .load              = load,
        .psr_or_force_user = false,
        .write_back        = true,
        .register_list     = register_list,
        .rn                = R13,
    };

    return do_block_transfer(cpu, &transfer_args);
}

static int load_store_halfword(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    bool load = inst & (1 << 11);
    uint32_t offset = ((inst >> 6) & 0x1f) << 1;
    int rb = (inst >> 3) & 0x7;
    int rd = inst & 0x7;

    uint32_t transfer_addr = read_register(cpu, rb) + offset;

    prefetch(cpu);

    uint32_t data;
    if (load)
    {
        data = read_halfword(cpu->mem, transfer_addr);
        if (transfer_addr & 1)
            data = data >> 8 | data << 24;
        write_register(cpu, rd, data);
    }
    else
    {
        data = read_register(cpu, rd);
        write_halfword(cpu->mem, transfer_addr, data);
    }

    // LDR: 1S + 1N + 1I, STR: 2N
    return load ? 3 : 2;
}

static int sp_relative_load_store(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    bool load = inst & (1 << 11);
    int rd = (inst >> 8) & 0x7;
    uint32_t offset = (inst & 0xff) << 2;
    uint32_t transfer_addr = read_register(cpu, R13) + offset;

    prefetch(cpu);

    uint32_t data;
    if (load)
    {
        data = read_word(cpu->mem, transfer_addr);
        // unaligned load -> rotated data
        int rot_amt = 8 * (transfer_addr & 0x3);
        if (rot_amt)
            data = data >> rot_amt | data << (32 - rot_amt);
        write_register(cpu, rd, data);
    }
    else
    {
        data = read_register(cpu, rd);
        write_word(cpu->mem, transfer_addr, data);
    }

    // LDR: 1S + 1N + 1I, STR: 2N
    return load ? 3 : 2;
}

static int load_address(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    bool sp = (inst >> 11) & 1;
    int rd = (inst >> 8) & 0x7;
    uint16_t imm_val = (inst & 0xff) << 2;

    uint32_t source;
    if (sp)
        source = read_register(cpu, R13);
    else // bit 1 of PC is always read as 0
        source = cpu->registers[R15] & ~0x2u;

    write_register(cpu, rd, source + imm_val);

    prefetch(cpu);

    return 1; // 1S
}

static int load_store_with_offset(arm7tdmi *cpu, bool immediate)
{
    uint16_t inst = cpu->pipeline[0];
    bool load = inst & (1 << 11);

    bool byte_trans;
    if (immediate)
        byte_trans = inst & (1 << 12);
    else
        byte_trans = inst & (1 << 10);

    int rb = (inst >> 3) & 0x7;
    int rd = inst & 0x7;

    uint32_t base = read_register(cpu, rb);

    uint32_t offset;
    if (immediate)
    {
        offset = (inst >> 6) & 0x1f;
        if (!byte_trans)
            offset <<= 2;
    }
    else
    {
        int ro = (inst >> 6) & 0x7;
        offset = read_register(cpu, ro);
    }

    uint32_t transfer_addr = base + offset;

    prefetch(cpu);

    int num_clocks;
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
            if (rot_amt)
                word = word >> rot_amt | word << (32 - rot_amt);
            write_register(cpu, rd, word);
        }

        // 1S + 1N + 1I
        num_clocks = 3;
    }
    else
    {
        uint32_t data = read_register(cpu, rd);
        if (byte_trans)
            write_byte(cpu->mem, transfer_addr, data);
        else
            write_word(cpu->mem, transfer_addr, data);

        num_clocks = 2; // 2N cycles
    }

    return num_clocks;
}

static int load_store_sign_extended(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    int opcode = (inst >> 10) & 0x3;
    int ro = (inst >> 6) & 0x7;
    int rb = (inst >> 3) & 0x7;
    int rd = inst & 0x7;

    uint32_t base = read_register(cpu, rb);
    uint32_t offset = read_register(cpu, ro);
    uint32_t transfer_addr = base + offset;
    bool unaligned = transfer_addr & 1;

    prefetch(cpu);

    uint32_t data;
    switch (opcode)
    {
        case 0x0: // STRH
            write_halfword(cpu->mem, transfer_addr, read_register(cpu, rd));
            break;

        case 0x1: // LDSB
            data = read_byte(cpu->mem, transfer_addr);
            if (data & 0x80)
                data |= 0xffffff00;
            write_register(cpu, rd, data);
            break;

        case 0x2: // LDRH
            data = read_halfword(cpu->mem, transfer_addr);

            // If the address is not halfword-aligned, the
            // aligned data is rotated so the addressed
            // byte is in bits 0-7 of Rd
            if (unaligned)
                data = data >> 8 | data << 24;
            write_register(cpu, rd, data);
            break;

        case 0x3: // LDSH
            data = read_halfword(cpu->mem, transfer_addr);

            // If the address is not halfword-aligned,
            // the addressed byte is sign-extended
            if (unaligned)
            {
                data = (data >> 8) & 0xff;
                if (data & 0x80)
                    data |= 0xffffff00;
            }
            else
            {
                if (data & 0x8000)
                    data |= 0xffff0000;
            }
            write_register(cpu, rd, data);
            break;
    }

    // LDR: 1S + 1N + 1I, STR: 2N
    return opcode ? 3 : 2;
}

static int pc_relative_load(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    uint32_t imm = (inst & 0xff) << 2; // 10-bit immediate
    int rd = (inst >> 8) & 0x7;
    uint32_t base = cpu->registers[R15] & ~0x2u;
    uint32_t data = read_word(cpu->mem, base + imm);

    write_register(cpu, rd, data);

    int num_clocks;
    if (rd == R15)
    {
        reload_pipeline(cpu);
        // 2S + 2N + 1I
        num_clocks = 5;
    }
    else
    {
        prefetch(cpu);
        // 1S + 1N + 1I
        num_clocks = 3;
    }

    return num_clocks;
}

static int alu_operation(arm7tdmi *cpu)
{
    uint32_t inst = cpu->pipeline[0];
    int opcode = (inst >> 6) & 0xf;
    int rs = (inst >> 3) & 0x7;
    int rd = inst & 0x7;

    uint32_t op1 = read_register(cpu, rd);
    uint32_t op2 = read_register(cpu, rs);

    bool carry_flag = cpu->cpsr & COND_C_BITMASK;

    uint32_t result;
    // only used by arithmetic operations
    bool op_carry = false;
    bool op_overflow = false;

    switch (opcode)
    {
        case 0x0: // AND
        case 0x8: // TST
            result = op1 & op2;
            break;

        case 0x1: // EOR
            result = op1 ^ op2;
            break;

        case 0x2: // LSL
        case 0x3: // LSR
        case 0x4: // ASR
        case 0x7: // ROR
        {
            // 0 -> LSL, 1 -> LSR, 2 -> ASR, 3 -> ROR
            int shift_opcode = opcode == 0x7 ? 0x3 : opcode - 2;
            barrel_shift_args args = {
                .immediate = false,
                .shift_amt = op2 & 0xff,
                .shift_by_reg = true,
                .shift_input = op1,
                .shift_opcode = shift_opcode,
            };

            op_carry = barrel_shift(cpu, &args, &result);
            break;
        }

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

        case 0x9: // NEG
            result = -op2;
            op_carry = !op2;
            op_overflow = ((op2 & (op2 ^ result)) >> 31) & 1;
            break;

        case 0xa: // CMP
            result = op1 - op2;
            op_carry = op1 >= op2; // set if no borrow
            // overflow into bit 31
            op_overflow = (((op1 ^ op2) & (op1 ^ result)) >> 31) & 1;
            break;

        case 0xb: // CMN
            result = op1 + op2;
            op_carry = op2 > UINT32_MAX - op1;
            op_overflow = ((~(op1 ^ op2) & (op1 ^ result)) >> 31) & 1;
            break;

        case 0xc: // ORR
            result = op1 | op2;
            break;

        case 0xd: // MUL
            result = op1 * op2;
            break;

        case 0xe: // BIC
            result = op1 & ~op2;
            break;

        case 0xf: // MVN
            result = ~op2;
            break;
    }

    prefetch(cpu);

    bool write_result = !(opcode == 0x8
                          || opcode == 0xa
                          || opcode == 0xb);

    bool logical_op = opcode <= 0x4
                      || opcode == 0x7
                      || opcode == 0x8
                      || opcode >= 0xc;

    bool used_barrel_shift = opcode == 0x2
                             || opcode == 0x3
                             || opcode == 0x4
                             || opcode >= 0x7;

    if (write_result)
        write_register(cpu, rd, result);

    // all operations affect N and Z flags at a minimum
    uint32_t altered_flags = (result & COND_N_BITMASK)
                             | (!result << COND_Z_SHIFT);
    uint32_t mask = ~(COND_N_BITMASK | COND_Z_BITMASK);

    if (used_barrel_shift)
    {
        mask &= ~COND_C_BITMASK;
        altered_flags |= op_carry << COND_C_SHIFT;
    }

    if (!logical_op)
    {
        mask &= ~COND_V_BITMASK;
        altered_flags |= op_overflow << COND_V_SHIFT;
    }

    cpu->cpsr = (cpu->cpsr & mask) | altered_flags;

    int num_clocks = 1; // 1S
    if (opcode == 0xd) // +mI (MUL)
        num_clocks += get_multiply_array_cycles(op1, false, false);
    else if (used_barrel_shift) // +1I
        num_clocks += 1;

    return num_clocks;
}

static int add_subtract(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    bool immediate = inst & (1 << 10);
    bool sub = inst & (1 << 9);
    int offset_arg = (inst >> 6) & 0x7;
    int rs = (inst >> 3) & 0x7;
    int rd = inst & 0x7;

    uint32_t offset;
    if (immediate)
        offset = offset_arg;
    else
        offset = read_register(cpu, offset_arg);

    uint32_t source = read_register(cpu, rs);
    uint32_t result;
    if (sub)
        result = source - offset;
    else
        result = source + offset;

    prefetch(cpu);
    write_register(cpu, rd, result);

    bool carry = offset > UINT32_MAX - source;
    bool overflow = ((~(source ^ offset) & (source ^ result)) >> 31) & 1;

    cpu->cpsr = (cpu->cpsr & ~COND_FLAGS_MASK)
                | (result & COND_N_BITMASK)
                | (!result << COND_Z_SHIFT)
                | (carry << COND_C_SHIFT)
                | (overflow << COND_V_SHIFT);

    return 1; // 1S
}

static int move_shifted_register(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    uint32_t rdval;

    barrel_shift_args args = {
        .immediate = false,
        .shift_by_reg = false,
        .shift_amt = (inst >> 6) & 0x1f,
        .shift_opcode = (inst >> 11) & 0x3,
        .shift_input = read_register(cpu, (inst >> 3) & 0x7),
    };

    bool shifter_carry = barrel_shift(cpu, &args, &rdval);

    prefetch(cpu);
    write_register(cpu, inst & 0x7, rdval);

    const uint32_t mask = ~(COND_N_BITMASK | COND_Z_BITMASK | COND_C_BITMASK);
    cpu->cpsr = (cpu->cpsr & mask)
                | (rdval & COND_N_BITMASK)
                | (!rdval << COND_Z_SHIFT)
                | (shifter_carry << COND_C_SHIFT);

    return 1; // 1S
}

int decode_and_execute_thumb(arm7tdmi *cpu)
{
    int num_clocks = 0;
    uint16_t inst = cpu->pipeline[0];

    // decoding is going to involve a lot of magic numbers
    // See references in `README.md` for encoding documentation
    if ((inst & 0xff00) == 0xdf00)      // software interrupt
        goto unimplemented;
    else if ((inst & 0xf800) == 0xe000) // unconditional branch
        num_clocks = unconditional_branch(cpu);
    else if ((inst & 0xf000) == 0xd000) // conditional branch
        num_clocks = conditional_branch(cpu);
    else if ((inst & 0xf000) == 0xc000) // multiple load/store
        goto unimplemented;
    else if ((inst & 0xf000) == 0xf000) // long branch w/link
        num_clocks = long_branch_with_link(cpu);
    else if ((inst & 0xff00) == 0xb000) // add offset to SP
        num_clocks = add_offset_to_sp(cpu);
    else if ((inst & 0xf600) == 0xb400) // push/pop registers
        num_clocks = push_pop_registers(cpu);
    else if ((inst & 0xf000) == 0x8000) // load/store halfword
        num_clocks = load_store_halfword(cpu);
    else if ((inst & 0xf000) == 0x9000) // SP-relative load/store
        num_clocks = sp_relative_load_store(cpu);
    else if ((inst & 0xf000) == 0xa000) // load address
        num_clocks = load_address(cpu);
    else if ((inst & 0xe000) == 0x6000) // load/store w/immediate offset
        num_clocks = load_store_with_offset(cpu, true);
    else if ((inst & 0xf200) == 0x5000) // load/store w/register offset
        num_clocks = load_store_with_offset(cpu, false);
    else if ((inst & 0xf200) == 0x5200) // load/store sign-extended byte/halfword
        num_clocks = load_store_sign_extended(cpu);
    else if ((inst & 0xf800) == 0x4800) // PC relative load
        num_clocks = pc_relative_load(cpu);
    else if ((inst & 0xfc00) == 0x4400) // hi register operations/branch exchange
        num_clocks = hi_register_op_or_bx(cpu);
    else if ((inst & 0xfc00) == 0x4000) // ALU operations
        num_clocks = alu_operation(cpu);
    else if ((inst & 0xe000) == 0x2000) // move/compare/add/subtract immediate
        num_clocks = operate_with_immediate(cpu);
    else if ((inst & 0xf800) == 0x1800) // add/subtract
        num_clocks = add_subtract(cpu);
    else if ((inst & 0xe000) == 0x0000) // move shifted register
        num_clocks = move_shifted_register(cpu);
    else
        panic_illegal_instruction(cpu);

    return num_clocks;

// temporary until all instructions are implemented
unimplemented:
    fprintf(stderr,
            "Error: Unimplemented THUMB instruction encountered: %04X at address %08X\n",
            inst,
            cpu->registers[R15] - 4);
    exit(1);
}
