#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "arm7tdmi.h"
#include "cgba/cpu.h"

#define COND_N_BITMASK (1 << 31)
#define COND_Z_BITMASK (1 << 30)
#define COND_C_BITMASK (1 << 29)
#define COND_V_BITMASK (1 << 28)

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
        goto unimplemented;
    else
        panic_illegal_instruction(inst);

    return;

// temporary until all instructions are implemented
unimplemented:
    fprintf(stderr, "Error: Unimplemented instruction encountered: %08X\n", inst);
    exit(1);
}
