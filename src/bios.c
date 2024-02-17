#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cgba/bios.h"
#include "cgba/cpu.h"
#include "cgba/memory.h"
#include "cpu/arm7tdmi.h"

int load_bios_file(gba_mem *mem, const char *fname)
{
    FILE *fptr = fopen(fname, "rb");
    if (!fptr)
    {
        perror("Could not open BIOS file");
        goto open_error;
    }

    size_t bios_size = sizeof mem->bios;
    size_t bytes_read = fread(mem->bios, 1, bios_size, fptr);

    if (bytes_read != bios_size && !feof(fptr))
    {
        fputs("Error occured while reading from BIOS file\n", stderr);
        goto load_error;
    }

    fclose(fptr);
    return 0;

load_error:
    fclose(fptr);
open_error:
    return -1;
}

/*
 * Perform a GBA BIOS system call invoked by a SWI instruction.
 * Treats all syscalls as if they took on cycle to complete.
 */
int gba_syscall(arm7tdmi *cpu)
{
    bool thumb = cpu->spsr[BANK_SVC] & T_BITMASK;
    int prefetch_offset = thumb ? 2 : 4;
    uint32_t swi_addr = cpu->banked_registers[BANK_SVC][BANK_R14] - prefetch_offset;

    bios_syscall callno;
    if (thumb)
        callno = read_halfword(cpu->mem, swi_addr) & 0xff;
    else
        callno = (read_word(cpu->mem, swi_addr) >> 16) & 0xff;

    switch (callno)
    {
        case SYSCALL_DIV:
        {
            int32_t n = cpu->registers[R0];
            int32_t d = cpu->registers[R1];
            int32_t quot = n / d;
            int32_t rem = n % d;
            cpu->registers[R0] = quot;
            cpu->registers[R1] = rem;
            cpu->registers[R3] = quot < 0 ? -quot : quot;
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
