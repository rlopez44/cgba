#include <stdio.h>
#include "cgba/cpu.h"

int main(void)
{
    printf("CGBA: A Game Boy Advance Emulator\n"
           "---------------------------------\n");

    arm7tdmi *cpu = init_cpu();
    if (cpu == NULL)
    {
        printf("Failed to allocate ARM7TDMI\n");
        return 1;
    }

    printf("ARM7TDMI allocated using %zu bytes\n", sizeof(arm7tdmi));
    deinit_cpu(cpu);

    return 0;
}
