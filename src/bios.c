#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cgba/bios.h"
#include "cgba/cpu.h"
#include "cgba/memory.h"
#include "cpu/arm7tdmi.h"

/*
 * Perform a GBA BIOS system call invoked by a SWI instruction.
 * Treats all syscalls as if they took on cycle to complete.
 */
int gba_syscall(arm7tdmi *cpu)
{
    uint32_t swi_addr = cpu->banked_registers[BANK_SVC][BANK_R14] - 4;
    bios_syscall callno = (read_word(cpu->mem, swi_addr) >> 16) & 0xff;

    switch (callno)
    {
        case SYSCALL_DIV:
        {
            uint32_t n = cpu->registers[R0];
            uint32_t d = cpu->registers[R1];
            uint32_t quot = (int32_t)n / (int32_t)d;
            uint32_t rem = (int32_t)n % (int32_t)d;
            cpu->registers[R0] = quot;
            cpu->registers[R1] = rem;
            cpu->registers[R3] = quot >> 31 ? ~(quot - 1) : quot;
            break;
        }

        default:
            fprintf(stderr, "Error: unimplemented syscall: %02X\n", callno);
            exit(1);
    }

    // MOVS PC, R14_svc to exit the SWI trap
    cpu->registers[R15] = cpu->banked_registers[BANK_SVC][BANK_R14];
    cpu->cpsr = cpu->spsr[BANK_SVC];
    reload_pipeline(cpu);

    return 1;
}
