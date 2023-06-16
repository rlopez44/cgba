#include <stdint.h>
#include "cgba/memory.h"

uint32_t read_word(gba_mem *mem, uint32_t addr)
{
    uint32_t val = 0;

    // placeholder implementation
    val |= mem->mmap[addr + 0];
    val |= mem->mmap[addr + 1] << 8;
    val |= mem->mmap[addr + 2] << 16;
    val |= mem->mmap[addr + 3] << 24;

    return val;
}

uint16_t read_halfword(gba_mem *mem, uint32_t addr)
{
    uint16_t val = 0;

    // placeholder implementation
    val |= mem->mmap[addr + 0];
    val |= mem->mmap[addr + 1] << 8;

    return val;
}
