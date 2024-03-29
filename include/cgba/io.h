#ifndef CGBA_IO_H
#define CGBA_IO_H

#include <stdint.h>
#include "cgba/memory.h"

enum io_registers {
    DISPCNT   = 0x04000000,
    DISPSTAT  = 0x04000004,
    VCOUNT    = 0x04000006,

    BG0CNT    = 0x04000008,
    BG1CNT    = 0x0400000a,
    BG2CNT    = 0x0400000c,
    BG3CNT    = 0x0400000e,

    KEYINPUT  = 0x04000130,

    IE        = 0x04000200,
    IF        = 0x04000202,
    IME       = 0x04000208,
};

// Write a byte to the given address in the I/O register address range
void write_io_byte(gba_mem *mem, uint32_t addr, uint8_t byte);

// Read a byte from the given address in the I/O register address range
uint8_t read_io_byte(gba_mem *mem, uint32_t addr);

#endif /* CGBA_IO_H */
