#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgba/bios.h"
#include "cgba/cpu.h"
#include "cgba/io.h"
#include "cgba/memory.h"

// helper to abstract away memory map reads
static uint8_t byte_from_mmap(gba_mem *mem, uint32_t addr)
{
    // TODO: implement proper open bus values
    uint8_t byte = 0xff;
    switch (addr >> 24)
    {
        case 0x00: // BIOS
            // can only read BIOS when PC is inside BIOS
            // recall: R15 = PC + 8 (BIOS access expected in ARM state)
            if (addr <= 0x3fff && mem->cpu->registers[R15] <= 0x3fff + 8)
                byte = mem->bios[addr & 0x3fff];
            else
                fprintf(stderr,
                        "Warning: attempted BIOS data access from outside BIOS: PC=%08X\n",
                        mem->cpu->registers[R15] - 8);
            break;

        case 0x02: // EWRAM
            byte = mem->ewram[addr & 0x3ffff];
            break;

        case 0x03: // IWRAM
            byte = mem->iwram[addr & 0x7fff];
            break;

        case 0x04: // I/O
            // TODO: implement undocumented port at
            // 0x04000800 repeated each 64k
            if ((addr & 0xffffff) <= 0x3ff)
                byte = read_io_byte(mem, addr);
            break;

        case 0x05: // palette RAM
            byte = mem->palette_ram[addr & 0x3ff];
            break;

        case 0x06: // VRAM
        {
            // 96k = (64k + 32k) mirrored every 128k
            // as 64k + 32k + 32k, with the 32k blocks
            // mirroring each other
            uint32_t offset = addr & 0x1ffff;
            if (offset > 0x17fff)
                offset -= 0x8000;
            byte = mem->vram[offset];
            break;
        }

        case 0x07: // OAM
            byte = mem->oam[addr & 0x3ff];
            break;

        case 0x08: // ROM
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
            // TODO: implement the 3 ROM wait states
            byte = mem->rom[addr & 0x1ffffff];
            break;

        case 0x0e: // SRAM
        case 0x0f:
            // TODO: implement distinction between 32k and 64k SRAM
            byte = mem->sram[0xffff];
            break;
    }

    return byte;
}

// helper to abstract away memory map writes
static void byte_to_mmap(gba_mem *mem, uint32_t addr, uint8_t byte)
{
    switch (addr >> 24)
    {
        case 0x00: // BIOS
            break;

        case 0x02: // EWRAM
            mem->ewram[addr & 0x3ffff] = byte;
            break;

        case 0x03: // IWRAM
            mem->iwram[addr & 0x7fff] = byte;
            break;

        case 0x04: // I/O
            // TODO: implement undocumented port at
            // 0x04000800 repeated each 64k
            // TODO: implement read-only protections
            if ((addr & 0xffffff) <= 0x3ff)
                write_io_byte(mem, addr, byte);
            break;

        case 0x05: // palette RAM
            mem->palette_ram[addr & 0x3ff] = byte;
            break;

        case 0x06: // VRAM
        {
            // 96k = (64k + 32k) mirrored every 128k
            // as 64k + 32k + 32k, with the 32k blocks
            // mirroring each other
            uint32_t offset = addr & 0x1ffff;
            if (offset > 0x17fff)
                offset -= 0x8000;
            mem->vram[offset] = byte;
            break;
        }

        case 0x07: // OAM
            mem->oam[addr & 0x3ff] = byte;
            break;

        case 0x08: // ROM
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
            break;

        case 0x0e: // SRAM
        case 0x0f:
            // TODO: implement distinction between 32k and 64k SRAM
            mem->sram[0xffff] = byte;
            break;
    }
}

uint32_t read_word(gba_mem *mem, uint32_t addr)
{
    addr &= ~0x3u; // force alignment
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
    addr &= ~0x1u; // force alignment
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

void write_word(gba_mem *mem, uint32_t addr, uint32_t val)
{
    addr &= ~0x3u; // force alignment
    byte_to_mmap(mem, addr, val);
    byte_to_mmap(mem, addr + 1, val >> 8);
    byte_to_mmap(mem, addr + 2, val >> 16);
    byte_to_mmap(mem, addr + 3, val >> 24);
}

void write_halfword(gba_mem *mem, uint32_t addr, uint16_t val)
{
    addr &= ~0x1u; // force alignment
    byte_to_mmap(mem, addr, val);
    byte_to_mmap(mem, addr + 1, val >> 8);
}

void write_byte(gba_mem *mem, uint32_t addr, uint8_t val)
{
    byte_to_mmap(mem, addr, val);
}

static size_t load_rom_or_die(gba_mem *mem, const char *romfile)
{
    FILE *fptr = fopen(romfile, "rb");
    if (fptr == NULL)
        goto open_error;

    size_t max_rom_size = sizeof mem->rom;
    size_t bytes_read = fread(mem->rom, 1, max_rom_size, fptr);

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

gba_mem *init_memory(const char *romfile, const char *biosfile)
{
    gba_mem *mem = malloc(sizeof(gba_mem));
    if (mem == NULL)
        return NULL;

    memset(mem, 0, sizeof(gba_mem));
    load_rom_or_die(mem, romfile);

    if (biosfile != NULL && load_bios_file(mem, biosfile))
        exit(1);

    mem->has_bios = biosfile != NULL;

    return mem;
}

void deinit_memory(gba_mem *mem)
{
    free(mem);
}
