#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgba/interrupt.h"
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

#define TILES_PER_SCANLINE (FRAME_WIDTH / 8)

#define KB 1024

/* XBGR1555 */
#define WHITE 0xffff

#define PRAM_START 0x05000000
#define VRAM_START 0x06000000

enum PPU_BGNO {
    PPU_BG0,
    PPU_BG1,
    PPU_BG2,
    PPU_BG3,
};

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

static uint16_t get_bgcnt(gba_ppu *ppu, enum PPU_BGNO bgno)
{
    uint16_t bgcnt;
    switch (bgno)
    {
        case PPU_BG0: bgcnt = ppu->bg0cnt; break;
        case PPU_BG1: bgcnt = ppu->bg1cnt; break;
        case PPU_BG2: bgcnt = ppu->bg2cnt; break;
        case PPU_BG3: bgcnt = ppu->bg3cnt; break;
    }

    return bgcnt;
}

static void fetch_tile_map_entries(gba_ppu *ppu,
                                   enum PPU_BGNO bgno,
                                   uint16_t dest[static TILES_PER_SCANLINE])
{
    uint16_t bgcnt = get_bgcnt(ppu, bgno);
    int map_base_offset = (bgcnt >> 8) & 0x1f;
    uint32_t map_base_addr = VRAM_START + 2*KB*map_base_offset;

    uint32_t scanline_start = map_base_addr + 2*32*(ppu->vcount / 8);

    for (int i = 0; i < TILES_PER_SCANLINE; ++i)
    {
        uint32_t curr_addr = scanline_start + 2*i;
        dest[i] = read_halfword(ppu->mem, curr_addr);
    }
}

static void render_tile_data(gba_ppu *ppu,
                             enum PPU_BGNO bgno,
                             bool px_transparency[static FRAME_WIDTH],
                             uint16_t px_colors[static FRAME_WIDTH])
{
    // TODO: account for scrolling of the BG
    uint16_t bgcnt = get_bgcnt(ppu, bgno);
    if (bgcnt & (1 << 7))
    {
        fputs("8-bit color mode not implemented yet\n", stderr);
        exit(1);
    }
    else if (bgcnt & (1 << 6))
    {
        fputs("Mosaic effect not implemented yet\n", stderr);
        exit(1);
    }

    int tile_base_offset = (bgcnt >> 2) & 0x3;
    uint32_t tile_base_addr = VRAM_START + 16*KB*tile_base_offset;

    uint16_t tile_map_entries[TILES_PER_SCANLINE] = {0};
    fetch_tile_map_entries(ppu, bgno, tile_map_entries);

    for (int i = 0; i < TILES_PER_SCANLINE; ++i)
    {
        uint16_t tile_map_entry = tile_map_entries[i];
        int tileno = tile_map_entry & 0x3ff;
        int paletteno = (tile_map_entry >> 12) & 0xf;
        bool yflip = tile_map_entry & (1 << 11);
        bool xflip = tile_map_entry & (1 << 10);
        int yoffset = yflip ? 7 - ppu->vcount % 8 : ppu->vcount % 8;
        uint32_t line_addr = tile_base_addr + 32*tileno + 4*yoffset;
        uint32_t palette_offset = PRAM_START + 32*paletteno;

        for (int j = 0; j < 4; ++j)
        {
            uint32_t offset = xflip ? 3 - j : j;
            uint8_t palette_idx = read_byte(ppu->mem, line_addr + offset);
            uint8_t left_px_idx = palette_idx & 0xf;
            uint8_t right_px_idx = (palette_idx >> 4) & 0xf;

            if (xflip)
            {
                uint8_t tmp = left_px_idx;
                left_px_idx = right_px_idx;
                right_px_idx = tmp;
            }

            int px_base = 8*i + 2*j;
            if (px_transparency[px_base] && left_px_idx)
            {
                px_colors[px_base] = read_halfword(ppu->mem, palette_offset + 2*left_px_idx);
                px_transparency[px_base] = false;
            }

            if (px_transparency[px_base + 1] && right_px_idx)
            {
                px_colors[px_base + 1] = read_halfword(ppu->mem, palette_offset + 2*right_px_idx);
                px_transparency[px_base + 1] = false;
            }
        }
    }
}

static void render_background(gba_ppu *ppu,
                              enum PPU_BGNO bgno,
                              bool px_transparency[static FRAME_WIDTH],
                              uint16_t px_colors[static FRAME_WIDTH])
{
    int map_size = (get_bgcnt(ppu, bgno) >> 14) & 0x3;
    if (map_size)
    {
        fprintf(stderr, "Can only handle BG map size 0. Got: %d\n", map_size);
        exit(1);
    }

    render_tile_data(ppu, bgno, px_transparency, px_colors);
}

static void render_mode0_scanline(gba_ppu *ppu)
{
    bool bg3_enabled = ppu->dispcnt & (1 << 11);
    bool bg2_enabled = ppu->dispcnt & (1 << 10);
    bool bg1_enabled = ppu->dispcnt & (1 << 9);
    bool bg0_enabled = ppu->dispcnt & (1 << 8);

    bool any_bg = bg0_enabled || bg1_enabled || bg2_enabled || bg3_enabled;
    if (!any_bg)
    {
        uint32_t base_offset = FRAME_WIDTH * ppu->vcount;
        for (int i = 0; i < FRAME_WIDTH; ++i)
            ppu->frame_buffer[base_offset + i] = WHITE;
        return;
    }

    bool px_transparency[FRAME_WIDTH];
    memset(px_transparency, 1, sizeof px_transparency);

    uint16_t px_colors[FRAME_WIDTH];
    uint16_t backdrop = read_halfword(ppu->mem, PRAM_START);
    for (int i = 0; i < FRAME_WIDTH; ++i)
        px_colors[i] = backdrop;

    // Hardcoding the BG render order works for
    // Kirby - Nightmare in Dream Land because
    // the priority is fixed at BG0 > BG1 > BG2 > BG3.
    // TODO: actually take priority into account
    // instead of hardcoding draw order
    if (bg0_enabled)
        render_background(ppu, PPU_BG0, px_transparency, px_colors);

    if (bg1_enabled)
        render_background(ppu, PPU_BG1, px_transparency, px_colors);

    if (bg2_enabled)
        render_background(ppu, PPU_BG2, px_transparency, px_colors);

    if (bg3_enabled)
        render_background(ppu, PPU_BG3, px_transparency, px_colors);

    size_t framebuff_offset = FRAME_WIDTH * ppu->vcount;
    memcpy(ppu->frame_buffer + framebuff_offset, px_colors, sizeof px_colors);
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

        case 0x0:
            render_mode0_scanline(ppu);
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
        ppu->mem->irq_request |= IRQ_HBLANK;

    if (ppu->vcount < VBLANK_START)
        render_scanline(ppu);
}

static void enter_vblank(gba_ppu *ppu)
{
    ppu->dispstat |= 0x1; // VBlank flag
    if (ppu->dispstat & (1 << 3))
        ppu->mem->irq_request |= IRQ_VBLANK;
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
            ppu->mem->irq_request |= IRQ_VCOUNT;
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
