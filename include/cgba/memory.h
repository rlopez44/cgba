#ifndef CGBA_MEMORY_H
#define CGBA_MEMORY_H

#include <stdint.h>

typedef struct gba_mem {
    // placeholder
    uint8_t mmap[1L << 32];
} gba_mem;

uint32_t read_word(gba_mem *mem, uint32_t addr);
uint16_t read_halfword(gba_mem *mem, uint32_t addr);
uint8_t read_byte(gba_mem *mem, uint32_t addr);

void write_halfword(gba_mem *mem, uint32_t addr, uint16_t val);

gba_mem *init_memory(void);
void deinit_memory(gba_mem *mem);

#endif /* CGBA_MEMORY_H */
