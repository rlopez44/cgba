#ifndef CGBA_IO_H
#define CGBA_IO_H

#include <stdint.h>
#include "cgba/memory.h"

enum io_registers {
    DISPCNT   = 0x04000000,
    DISPSTAT  = 0x04000004,
    VCOUNT    = 0x04000006,

    KEYINPUT  = 0x04000130,
};

// Write a byte to the given address in the I/O register address range
void write_io_byte(gba_mem *mem, uint32_t addr, uint8_t byte);

// Read a byte from the given address in the I/O register address range
uint8_t read_io_byte(gba_mem *mem, uint32_t addr);

#endif /* CGBA_IO_H */
