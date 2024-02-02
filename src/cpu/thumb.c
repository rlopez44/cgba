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

    switch (op)
    {
        case 0x0: // ADD
            write_register(cpu, rd, op1 + op2);
            break;

        case 0x1: // CMP
        {
            uint32_t res = op1 - op2;
            bool carry = op1 >= op2; // set if no borrow
            // overflow into bit 31
            bool overflow = (((op1 ^ op2) & (op1 ^ res)) >> 31) & 1;
            cpu->cpsr = (cpu->cpsr & ~COND_FLAGS_MASK)
                        | (res & COND_N_BITMASK)
                        | (!res << COND_Z_SHIFT)
                        | (carry << COND_C_SHIFT)
                        | (overflow << COND_V_SHIFT);
            break;
        }

        case 0x2: // MOV
            write_register(cpu, rd, op2);
            break;

        case 0x3: // BX
            do_branch_and_exchange(cpu, read_register(cpu, rs));
            break;
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
        goto unimplemented;
    else if ((inst & 0xff00) == 0xb000) // add offset to SP
        goto unimplemented;
    else if ((inst & 0xf600) == 0xb400) // push/pop registers
        goto unimplemented;
    else if ((inst & 0xf000) == 0x8000) // load/store halfword
        goto unimplemented;
    else if ((inst & 0xf000) == 0x9000) // SP relative load/store
        goto unimplemented;
    else if ((inst & 0xf000) == 0xa000) // load address
        num_clocks = load_address(cpu);
    else if ((inst & 0xe000) == 0x6000) // load/store w/immediate offset
        goto unimplemented;
    else if ((inst & 0xf200) == 0x5000) // load/store w/register offset
        goto unimplemented;
    else if ((inst & 0xf200) == 0x5200) // load/store sign-extended byte/halfword
        goto unimplemented;
    else if ((inst & 0xf800) == 0x4800) // PC relative load
        num_clocks = pc_relative_load(cpu);
    else if ((inst & 0xfc00) == 0x4400) // hi register operations/branch exchange
        num_clocks = hi_register_op_or_bx(cpu);
    else if ((inst & 0xfc00) == 0x4000) // ALU operations
        goto unimplemented;
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
