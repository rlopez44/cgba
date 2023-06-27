#include <stdio.h>
#include <stdlib.h>
#include "cgba/cpu.h"
#include "cgba/memory.h"

typedef struct gba_system {
    arm7tdmi *cpu;
    gba_mem *mem;
} gba_system;

static void init_system_or_die(gba_system *gba)
{
    gba->mem = init_memory();
    if (gba->mem == NULL)
    {
        fputs("Failed to allocate GBA memory\n", stderr);
        exit(1);
    }

    gba->cpu = init_cpu(gba->mem);
    if (gba->cpu == NULL)
    {
        fputs("Failed to allocate ARM7TDMI\n", stderr);
        exit(1);
    }

}

static void deinit_system(gba_system *gba)
{
    deinit_memory(gba->mem);
    deinit_cpu(gba->cpu);
}

int main(void)
{
    printf("CGBA: A Game Boy Advance Emulator\n"
           "---------------------------------\n");

    gba_system gba;
    init_system_or_die(&gba);

    puts("GBA system initialized. Exiting...\n");

    deinit_system(&gba);

    return 0;
}
