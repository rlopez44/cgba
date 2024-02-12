#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgba/memory.h"
#include "cgba/ppu.h"
#include "SDL.h"

#define WINDOW_SCALE 3
#define CLOCKS_PER_DOT 4

#define HBLANK_START (CLOCKS_PER_DOT*240)
#define SCANLINE_END (CLOCKS_PER_DOT*308)

#define VBLANK_START  160 /* scanline start for vblank */
#define VBLANK_END    227 /* scanline end for vblank */
#define NUM_SCANLINES 228

/* XBGR1555 */
#define WHITE 0xffff

#define PRAM_START 0x05000000
#define VRAM_START 0x06000000

gba_ppu *init_ppu(void)
{
    gba_ppu *ppu = malloc(sizeof(gba_ppu));
    if (ppu == NULL)
        return NULL;

    ppu->dispcnt = 0x0080; // force blank -> all white lines drawn
    ppu->dispstat = 0;
    ppu->vcount = 0;
    ppu->scanline_clock = 0;
    ppu->curr_frame_rendered = false;

    // white screen on startup
    for (size_t i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; ++i)
        ppu->frame_buffer[i] = WHITE;

    return ppu;
}

void deinit_ppu(gba_ppu *ppu)
{
    if (ppu->screen != NULL)
        SDL_DestroyTexture(ppu->screen);

    if (ppu->renderer != NULL)
        SDL_DestroyRenderer(ppu->renderer);

    if (ppu->window != NULL)
        SDL_DestroyWindow(ppu->window);

    free(ppu);
}

void init_screen_or_die(gba_ppu *ppu)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        goto init_error;

    ppu->window = SDL_CreateWindow("CGBA -- A Game Boy Advance Emulator",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   WINDOW_SCALE * FRAME_WIDTH,
                                   WINDOW_SCALE * FRAME_HEIGHT,
                                   SDL_WINDOW_OPENGL);

    if (ppu->window == NULL)
        goto init_error;

    ppu->renderer = SDL_CreateRenderer(ppu->window, -1, 0);

    if (ppu->renderer == NULL)
        goto init_error;

    ppu->screen = SDL_CreateTexture(ppu->renderer,
                                    SDL_PIXELFORMAT_XBGR1555,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    FRAME_WIDTH,
                                    FRAME_HEIGHT);

    if (ppu->screen == NULL)
        goto init_error;

    void *pixels;
    int pitch;
    if (SDL_LockTexture(ppu->screen, NULL, &pixels, &pitch) < 0)
        goto init_error;

    memcpy(pixels, ppu->frame_buffer, FRAME_HEIGHT * pitch);
    SDL_UnlockTexture(ppu->screen);
    SDL_RenderClear(ppu->renderer);
    SDL_RenderCopy(ppu->renderer, ppu->screen, NULL, NULL);
    SDL_RenderPresent(ppu->renderer);

    return;

init_error:
    fprintf(stderr, "Failed to initialize screen: %s\n", SDL_GetError());
    deinit_ppu(ppu);
    exit(1);
}

static void render_frame(gba_ppu *ppu)
{
    void *pixels;
    int pitch;

    if (SDL_LockTexture(ppu->screen, NULL, &pixels, &pitch) < 0)
        goto frame_render_error;

    memcpy(pixels, ppu->frame_buffer, pitch * FRAME_HEIGHT);
    SDL_UnlockTexture(ppu->screen);
    SDL_RenderClear(ppu->renderer);
    SDL_RenderCopy(ppu->renderer, ppu->screen, NULL, NULL);
    SDL_RenderPresent(ppu->renderer);
    ppu->frame_presented_signal = true;

    return;

frame_render_error:
    fprintf(stderr, "Error rendering frame: %s\n", SDL_GetError());
    exit(1);
}

static void render_mode3_scanline(gba_ppu *ppu)
{
    uint32_t base_offset = FRAME_WIDTH * ppu->vcount;
    bool bg2_enabled = ppu->dispcnt & (1 << 10);

    if (bg2_enabled)
    {
        uint32_t addr;
        for (int i = 0; i < FRAME_WIDTH; ++i)
        {
            // XBGR1555 color format
            addr = VRAM_START + 2*(base_offset + i);
            ppu->frame_buffer[base_offset + i] = read_halfword(ppu->mem, addr);
        }
    }
    else
    {
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[base_offset + i] = WHITE;
    }
}

static void render_mode4_scanline(gba_ppu *ppu)
{
    uint32_t base_offset = FRAME_WIDTH * ppu->vcount;
    bool bg2_enabled = ppu->dispcnt & (1 << 10);
    bool frameno = ppu->dispcnt & (1 << 4);

    if (bg2_enabled)
    {
        uint32_t pixel_addr, palette_idx_addr;
        uint8_t palette_idx;
        for (int i = 0; i < FRAME_WIDTH; ++i)
        {
            palette_idx_addr = VRAM_START + frameno*0xa000 + base_offset + i;
            palette_idx = read_byte(ppu->mem, palette_idx_addr);
            pixel_addr = PRAM_START + 2*palette_idx;
            ppu->frame_buffer[base_offset + i] = read_halfword(ppu->mem, pixel_addr);
        }
    }
    else
    {
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[base_offset + i] = WHITE;
    }
}

static void render_scanline(gba_ppu *ppu)
{
    if (ppu->dispcnt & (1 << 7)) // forced blank
    {
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[FRAME_WIDTH * ppu->vcount + i] = WHITE;
    }
    else switch (ppu->dispcnt & 0x7) // PPU mode
    {
        case 0x3:
            render_mode3_scanline(ppu);
            break;

        case 0x4:
            render_mode4_scanline(ppu);
            break;

        default:
            fprintf(stderr,
                    "Error: Unimplemented BG mode: %d\n",
                    ppu->dispcnt & 0x7);
            exit(1);
    }
}

// Called on entering HBlank, including during VBlank scanlines
static void enter_hblank(gba_ppu *ppu)
{
    ppu->dispstat |= 0x2; // set HBlank flag

    if (ppu->dispstat & (1 << 4))
    {
        // TODO: HBlank IRQ
    }

    if (ppu->vcount < VBLANK_START)
        render_scanline(ppu);
}

static void enter_vblank(gba_ppu *ppu)
{
    ppu->dispstat |= 0x1; // VBlank flag
    if (ppu->dispstat & (1 << 3))
    {
        // TODO: VBlank IRQ
    }
    render_frame(ppu);
}

static void update_vcount(gba_ppu *ppu)
{
    ppu->scanline_clock = 0;
    ppu->dispstat &= ~0x2; // unset HBlank flag
    ppu->vcount = (ppu->vcount + 1) % NUM_SCANLINES;

    uint8_t lyc = ppu->dispstat >> 8;
    if (lyc == ppu->vcount)
    {
        ppu->dispstat |= 0x4; // V-Counter flag
        if (ppu->dispstat & (1 << 5))
        {
            // TODO: V-Counter IRQ
        }
    }
    else
    {
        ppu->dispstat &= ~0x4;
    }
}

void run_ppu(gba_ppu *ppu, int num_clocks)
{
    for (; num_clocks; --num_clocks)
    {
        ++ppu->scanline_clock;

        if (ppu->scanline_clock == HBLANK_START)
            enter_hblank(ppu);
        else if (ppu->scanline_clock == SCANLINE_END)
            update_vcount(ppu);

        // beginning of new scanline
        if (!ppu->scanline_clock)
        {
            // VBlank is scanlines 160..226 (not 227)
            if (ppu->vcount == VBLANK_START)
                enter_vblank(ppu);
            else if (ppu->vcount == VBLANK_END)
                ppu->dispstat &= ~0x1;
        }
    }
}
