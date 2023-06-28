#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgba/memory.h"

// helper to abstract away memory map reads
static uint8_t byte_from_mmap(gba_mem *mem, uint32_t addr)
{
    uint8_t byte;
    if (addr <= 0x00003fff)
        byte = mem->bios[addr];
    else if (0x02000000 <= addr && addr <= 0x0203ffff)
        byte = mem->board_wram[addr - 0x02000000];
    else if (0x03000000 <= addr && addr <= 0x03007fff)
        byte = mem->chip_wram[addr - 0x03000000];
    else if (0x04000000 <= addr && addr <= 0x040003ff)
        byte = mem->io[addr - 0x04000000];
    else if (0x05000000 <= addr && addr <= 0x050003ff)
        byte = mem->palette_ram[addr - 0x05000000];
    else if (0x06000000 <= addr && addr <= 0x06017fff)
        byte = mem->vram[addr - 0x06000000];
    else if (0x07000000 <= addr && addr <= 0x070003ff)
        byte = mem->oam[addr - 0x07000000];
    else if (0x08000000 <= addr && addr <= 0x0dffffff)
        byte = mem->pak_rom[addr - 0x08000000];
    else if (0x0e000000 <= addr && addr <= 0x0e00ffff)
        byte = mem->pak_sram[addr - 0x0e000000];
    else // unmapped addresses
        byte = 0xff;

    return byte;
}

// helper to abstract away memory map writes
static void byte_to_mmap(gba_mem *mem, uint32_t addr, uint8_t byte)
{
    if (addr <= 0x00003fff)
        mem->bios[addr] = byte;
    else if (0x02000000 <= addr && addr <= 0x0203ffff)
        mem->board_wram[addr - 0x02000000] = byte;
    else if (0x03000000 <= addr && addr <= 0x03007fff)
        mem->chip_wram[addr - 0x03000000] = byte;
    else if (0x04000000 <= addr && addr <= 0x040003ff)
        mem->io[addr - 0x04000000] = byte;
    else if (0x05000000 <= addr && addr <= 0x050003ff)
        mem->palette_ram[addr - 0x05000000] = byte;
    else if (0x06000000 <= addr && addr <= 0x06017fff)
        mem->vram[addr - 0x06000000] = byte;
    else if (0x07000000 <= addr && addr <= 0x070003ff)
        mem->oam[addr - 0x07000000] = byte;
    else if (0x08000000 <= addr && addr <= 0x0dffffff)
        mem->pak_rom[addr - 0x08000000] = byte;
    else if (0x0e000000 <= addr && addr <= 0x0e00ffff)
        mem->pak_sram[addr - 0x0e000000] = byte;
}

uint32_t read_word(gba_mem *mem, uint32_t addr)
{
    uint32_t val = 0;

    // placeholder implementation
    val |= byte_from_mmap(mem, addr);
    val |= byte_from_mmap(mem, addr + 1) << 8;
    val |= byte_from_mmap(mem, addr + 2) << 16;
    val |= byte_from_mmap(mem, addr + 3) << 24;

    return val;
}

uint16_t read_halfword(gba_mem *mem, uint32_t addr)
{
    uint16_t val = 0;

    // placeholder implementation
    val |= byte_from_mmap(mem, addr);
    val |= byte_from_mmap(mem, addr + 1) << 8;

    return val;
}

uint8_t read_byte(gba_mem *mem, uint32_t addr)
{
    return byte_from_mmap(mem, addr);
}

void write_halfword(gba_mem *mem, uint32_t addr, uint16_t val)
{
    byte_to_mmap(mem, addr, val);
    byte_to_mmap(mem, addr + 1, val >> 8);
}

static size_t load_rom_or_die(gba_mem *mem, const char *romfile)
{
    FILE *fptr = fopen(romfile, "rb");
    if (fptr == NULL)
        goto open_error;

    size_t max_rom_size = sizeof mem->pak_rom;
    size_t bytes_read = fread(mem->pak_rom, 1, max_rom_size, fptr);

    if (bytes_read != max_rom_size && ferror(fptr))
        goto load_error;

    fclose(fptr);
    return bytes_read;

load_error:
    fclose(fptr);
open_error:
    perror("Error loading ROM");
    exit(1);
}

gba_mem *init_memory(const char *romfile)
{
    gba_mem *mem = malloc(sizeof(gba_mem));
    if (mem == NULL)
        return NULL;

    memset(mem, 0, sizeof(gba_mem));
    load_rom_or_die(mem, romfile);

    return mem;
}

void deinit_memory(gba_mem *mem)
{
    free(mem);
}
