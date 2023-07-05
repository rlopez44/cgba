#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "cgba/cpu.h"
#include "cgba/memory.h"
#include "cgba/ppu.h"
#include "cgba/gba.h"
#include "SDL_events.h"

static void connect_compoments(gba_system *gba)
{
    // two-way connection between memory and the CPU/PPU
    gba->cpu->mem = gba->mem;
    gba->mem->cpu = gba->cpu;
    gba->mem->ppu = gba->ppu;
    gba->ppu->mem = gba->mem;
}

void init_system_or_die(gba_system *gba, const char *romfile)
{
    gba->skip_bios = true; // hardcoded until bios implemented
    gba->running = true;
    gba->clocks_emulated = 0;
    gba->mem = init_memory(romfile);
    if (gba->mem == NULL)
    {
        fputs("Failed to allocate GBA memory\n", stderr);
        exit(1);
    }

    gba->cpu = init_cpu();
    if (gba->cpu == NULL)
    {
        fputs("Failed to allocate ARM7TDMI\n", stderr);
        exit(1);
    }

    gba->ppu = init_ppu();
    if (gba->ppu == NULL)
    {
        fputs("Failed to allocate PPU\n", stderr);
        exit(1);
    }

    connect_compoments(gba);

    reset_cpu(gba->cpu);

    if (gba->skip_bios)
        skip_boot_screen(gba->cpu);

    init_screen_or_die(gba->ppu);
}

void deinit_system(gba_system *gba)
{
    deinit_memory(gba->mem);
    deinit_cpu(gba->cpu);
    deinit_ppu(gba->ppu);
}

void run_system(gba_system *gba)
{
    // emulate one second until we implement a basic PPU
    int num_clocks;
    SDL_Event event;
    while (gba->running)
    {
        num_clocks = run_cpu(gba->cpu);
        gba->clocks_emulated += num_clocks;
        run_ppu(gba->ppu, num_clocks);

        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT)
                gba->running = false;
    }
}

void report_rom_info(uint8_t *rom)
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
