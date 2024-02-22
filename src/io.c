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

        case BG3CNT:
            if (msb)
                mem->ppu->bg3cnt = (mem->ppu->bg3cnt & 0x00ff)
                                   | byte << 8;
            else
                mem->ppu->bg3cnt = (mem->ppu->bg3cnt & 0xff00)
                                   | (byte & ~0x30u);
            break;

        case KEYINPUT: // read-only
            break;

        case IE:
            if (msb)
                // bits 14-15 are unused
                mem->irq_enable = (mem->irq_enable & 0xc0ff)
                                       | (byte & 0x3f) << 8;
            else
                mem->irq_enable = (mem->irq_enable & 0xff00) | byte;
            break;

        case IF:
            // interrupts are acknowledged by writing a 1 to
            // a given bit, in which case that bit is cleared
            if (msb)
                // bits 14-15 are unused
                mem->irq_request = mem->irq_request
                                        & ~((byte & 0x3f) << 8);
            else
                mem->irq_request = mem->irq_request & ~byte;
            break;

        case IME:
            if (!msb) // only bit 0 used
                mem->ime_flag = (mem->ime_flag & ~1u) | (byte & 1);
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

        case BG3CNT:
            if (msb)
                byte = mem->ppu->bg3cnt >> 8;
            else
                byte = mem->ppu->bg3cnt;
            break;

        case KEYINPUT:
            if (msb)
                byte = mem->gamepad->state >> 8;
            else
                byte = mem->gamepad->state;
            break;

        case IE:
            if (msb)
                byte = mem->irq_enable >> 8;
            else
                byte = mem->irq_enable;
            break;

        case IF:
            if (msb)
                byte = mem->irq_request >> 8;
            else
                byte = mem->irq_request;
            break;

        case IME:
            if (!msb) // only bit 0 used
                byte = mem->ime_flag;
            break;
    }

    return byte;
}
