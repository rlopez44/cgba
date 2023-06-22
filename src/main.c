#include <stdio.h>
#include "cgba/cpu.h"
#include "cgba/memory.h"

int main(void)
{
    printf("CGBA: A Game Boy Advance Emulator\n"
           "---------------------------------\n");

    gba_mem *mem = init_memory();
    if (mem == NULL)
    {
        printf("Failed to allocate memory for ARM7TDMI\n");
        return 1;
    }

    arm7tdmi *cpu = init_cpu(mem);
    if (cpu == NULL)
    {
        printf("Failed to allocate ARM7TDMI\n");
        return 1;
    }

    printf("ARM7TDMI allocated using %zu bytes\n", sizeof(arm7tdmi));
    deinit_memory(cpu->mem);
    deinit_cpu(cpu);

    return 0;
}
