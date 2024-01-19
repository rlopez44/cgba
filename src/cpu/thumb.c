#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "arm7tdmi.h"
#include "cgba/cpu.h"

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
        goto unimplemented;
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
