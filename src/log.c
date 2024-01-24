#include <stdio.h>
#include "cgba/log.h"
#include "cpu/arm7tdmi.h"

void log_cpu_state(arm7tdmi *cpu, FILE *fptr)
{
    if (fptr == NULL)
        fptr = stdout;

    // log format: * R0 R1 R2 ... R15 CPSR | <instruction pipeline>
    fprintf(fptr,
            "%08x %08x %08x %08x %08x %08x %08x %08x %08x "
            "%08x %08x %08x %08x %08x %08x %08x CPSR: %08x "
            "| %08x %08x\n",
            read_register(cpu, R0),
            read_register(cpu, R1),
            read_register(cpu, R2),
            read_register(cpu, R3),
            read_register(cpu, R4),
            read_register(cpu, R5),
            read_register(cpu, R6),
            read_register(cpu, R7),
            read_register(cpu, R8),
            read_register(cpu, R9),
            read_register(cpu, R10),
            read_register(cpu, R11),
            read_register(cpu, R12),
            read_register(cpu, R13),
            read_register(cpu, R14),
            read_register(cpu, R15),
            cpu->cpsr,
            cpu->pipeline[0],
            cpu->pipeline[1]);
}
