#ifndef CGBA_MEMORY_H
#define CGBA_MEMORY_H

#include <stdint.h>

typedef struct gba_mem {
    // placeholder
    uint8_t mmap[1L << 32];
} gba_mem;

uint32_t read_word(gba_mem *mem, uint32_t addr);
uint16_t read_halfword(gba_mem *mem, uint32_t addr);

#endif /* CGBA_MEMORY_H */
