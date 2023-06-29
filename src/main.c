#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgba/cpu.h"
#include "cgba/memory.h"

typedef struct gba_system {
    arm7tdmi *cpu;
    gba_mem *mem;
} gba_system;

static void init_system_or_die(gba_system *gba, const char *romfile)
{
    gba->mem = init_memory(romfile);
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

static void report_rom_info(uint8_t *rom)
{
    char title[13] = {0};
    char game_and_maker_code[7] = {0};
    uint8_t version = rom[0xbc];

    memcpy(title, rom + 0xa0, sizeof title - 1);
    memcpy(game_and_maker_code, rom + 0xac, sizeof game_and_maker_code - 1);

    printf("Title: %s (%s, Rev.%02d)\n",
           title,
           game_and_maker_code,
           version);
}

static void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <romfile>\n", progname);
}

int main(int argc, const char **argv)
{
    gba_system gba;

    puts("CGBA: A Game Boy Advance Emulator\n"
         "---------------------------------");

    if (argc != 2)
    {
        usage(argv[0]);
        return 1;
    }

    printf("ROM file: %s\n", argv[1]);
    init_system_or_die(&gba, argv[1]);
    report_rom_info(gba.mem->rom);
    puts("GBA system initialized. Exiting...");
    deinit_system(&gba);

    return 0;
}
