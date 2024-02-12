#ifndef CGBA_PPU_H
#define CGBA_PPU_H

#include <stdbool.h>
#include <stdint.h>
#include "cgba/memory.h"
#include "SDL_render.h"
#include "SDL_video.h"

#define FRAME_WIDTH 240
#define FRAME_HEIGHT 160

typedef struct gba_ppu {
    uint16_t dispcnt;
    uint16_t dispstat;
    uint8_t vcount;

    gba_mem *mem;

    uint16_t frame_buffer[FRAME_WIDTH*FRAME_HEIGHT]; // XBGR1555
    int scanline_clock;
    bool curr_frame_rendered;
    bool frame_presented_signal; // for processing SDL events once per frame

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *screen;
} gba_ppu;

gba_ppu *init_ppu(void);
void deinit_ppu(gba_ppu *ppu);

void init_screen_or_die(gba_ppu *ppu);
void run_ppu(gba_ppu *ppu, int num_clocks);


#endif /* CGBA_PPU_H */
