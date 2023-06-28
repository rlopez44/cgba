#ifndef CGBA_MEMORY_H
#define CGBA_MEMORY_H

#include <stdint.h>

typedef struct gba_mem {
    // general internal memory
    uint8_t bios[0x4000];
    uint8_t board_wram[0x40000];
    uint8_t chip_wram[0x8000];
    uint8_t io[0x400];

    // internal display memory
    uint8_t palette_ram[0x400];
    uint8_t vram[0x18000];
    uint8_t oam[0x400];

    // game pak
    uint8_t pak_rom[0x6000000];
    uint8_t pak_sram[0x10000];
} gba_mem;

uint32_t read_word(gba_mem *mem, uint32_t addr);
uint16_t read_halfword(gba_mem *mem, uint32_t addr);
uint8_t read_byte(gba_mem *mem, uint32_t addr);

void write_halfword(gba_mem *mem, uint32_t addr, uint16_t val);

gba_mem *init_memory(const char *romfile);
void deinit_memory(gba_mem *mem);

#endif /* CGBA_MEMORY_H */
