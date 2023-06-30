#include <stdio.h>
#include "cgba/log.h"

void log_cpu_state(arm7tdmi *cpu, FILE *fptr)
{
    if (fptr == NULL)
        fptr = stdout;

    // log format: * R0 R1 R2 ... R15 CPSR | <instruction pipeline>
    fprintf(fptr,
            "%08x %08x %08x %08x %08x %08x %08x %08x %08x "
            "%08x %08x %08x %08x %08x %08x %08x CPSR: %08x "
            "| %08x %08x\n",
            cpu->registers[R0],
            cpu->registers[R1],
            cpu->registers[R2],
            cpu->registers[R3],
            cpu->registers[R4],
            cpu->registers[R5],
            cpu->registers[R6],
            cpu->registers[R7],
            cpu->registers[R8],
            cpu->registers[R9],
            cpu->registers[R10],
            cpu->registers[R11],
            cpu->registers[R12],
            cpu->registers[R13],
            cpu->registers[R14],
            cpu->registers[R15],
            cpu->cpsr,
            cpu->pipeline[0],
            cpu->pipeline[1]);
}
