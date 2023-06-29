#ifndef CGBA_MEMORY_H
#define CGBA_MEMORY_H

#include <stdint.h>

typedef struct gba_mem {
    // general internal memory
    uint8_t bios[0x4000];
    uint8_t ewram[0x40000];
    uint8_t iwram[0x8000];
    uint8_t io[0x400];

    // internal display memory
    uint8_t palette_ram[0x400];
    uint8_t vram[0x18000];
    uint8_t oam[0x400];

    // game pak
    uint8_t rom[0x2000000];
    uint8_t sram[0x10000];
} gba_mem;

typedef struct arm7tdmi arm7tdmi;

uint32_t read_word(arm7tdmi *cpu, uint32_t addr);
uint16_t read_halfword(arm7tdmi *cpu, uint32_t addr);
uint8_t read_byte(arm7tdmi *cpu, uint32_t addr);

void write_halfword(arm7tdmi *cpu, uint32_t addr, uint16_t val);

gba_mem *init_memory(const char *romfile);
void deinit_memory(gba_mem *mem);

#endif /* CGBA_MEMORY_H */
