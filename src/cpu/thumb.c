#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "arm7tdmi.h"
#include "cgba/cpu.h"

int operate_with_immediate(arm7tdmi *cpu)
{
    uint16_t inst = cpu->pipeline[0];
    int operation = (inst >> 11) & 0x3;
    arm_register rd = (inst >> 8) & 0x7;
    uint8_t offset = inst & 0xff;

    bool write_result = operation != 0x1; // MOV, ADD, or SUB
    bool logical_op   = !operation;       // MOV

    prefetch(cpu);

    bool carry = false;
    bool overflow = false;
    uint32_t result;
    uint32_t op1 = cpu->registers[rd];
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
        cpu->registers[rd] = result;

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

int decode_and_execute_thumb(arm7tdmi *cpu)
{
    int num_clocks = 0;
    uint16_t inst = cpu->pipeline[0];

    // decoding is going to involve a lot of magic numbers
    // See references in `README.md` for encoding documentation
    if ((inst & 0xff00) == 0xdf00)      // software interrupt
        goto unimplemented;
    else if ((inst & 0xf800) == 0xe000) // unconditional branch
        goto unimplemented;
    else if ((inst & 0xf000) == 0xd000) // conditional branch
        goto unimplemented;
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
        goto unimplemented;
    else if ((inst & 0xe000) == 0x6000) // load/store w/immediate offset
        goto unimplemented;
    else if ((inst & 0xf200) == 0x5000) // load/store w/register offset
        goto unimplemented;
    else if ((inst & 0xf200) == 0x5200) // load/store sign-extended byte/halfword
        goto unimplemented;
    else if ((inst & 0xf800) == 0x4800) // PC relative load
        goto unimplemented;
    else if ((inst & 0xfc00) == 0x4400) // hi register operations/branch exchange
        goto unimplemented;
    else if ((inst & 0xfc00) == 0x4000) // ALU operations
        goto unimplemented;
    else if ((inst & 0xe000) == 0x2000) // move/compare/add/subtract immediate
        num_clocks = operate_with_immediate(cpu);
    else if ((inst & 0xf800) == 0x1800) // add/subtract
        goto unimplemented;
    else if ((inst & 0xe000) == 0x0000) // move shifted register
        goto unimplemented;
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
