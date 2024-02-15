#include <stdint.h>
#include <stdbool.h>
#include "cgba/gamepad.h"
#include "cgba/io.h"
#include "cgba/memory.h"
#include "cgba/ppu.h"

void write_io_byte(gba_mem *mem, uint32_t addr, uint8_t byte)
{
    bool msb = addr & 0x1; // addr = upper byte of 16-bit register
    switch (addr & ~0x1)
    {
        case DISPCNT:
            if (msb)
                mem->ppu->dispcnt = (mem->ppu->dispcnt & 0x00ff)
                                    | (byte << 8);
            else
                mem->ppu->dispcnt = (mem->ppu->dispcnt & 0xff08)
                                    | (byte & ~0x08);
            break;

        case DISPSTAT:
            if (msb)
                mem->ppu->dispstat = (mem->ppu->dispstat & 0x00ff)
                                     | (byte << 8);
            else
                // only bits 3-5 are writeable
                mem->ppu->dispstat = (mem->ppu->dispstat & 0xffc7)
                                     | (byte & ~0xc7);
            break;

        case VCOUNT: // read-only
            break;

        case KEYINPUT: // read-only
            break;
    }
}

uint8_t read_io_byte(gba_mem *mem, uint32_t addr)
{
    // TODO: implement open bus behavior
    uint8_t byte = 0xff;
    bool msb = addr & 0x1; // addr = upper byte of 16-bit register
    switch (addr & ~0x1)
    {
        case DISPCNT:
            if (msb)
                byte = mem->ppu->dispcnt >> 8;
            else
                byte = mem->ppu->dispcnt;
            break;

        case DISPSTAT:
            if (msb)
                byte = mem->ppu->dispstat >> 8;
            else
                byte = mem->ppu->dispstat;
            break;

        case VCOUNT:
            byte = msb ? 0 : mem->ppu->vcount;
            break;

        case KEYINPUT:
            if (msb)
                byte = mem->gamepad->state >> 8;
            else
                byte = mem->gamepad->state;
            break;
    }

    return byte;
}
